/* K=9 r=1/3 Viterbi decoder for x86 SSE
 * Copyright Aug 2006, Phil Karn, KA9Q
 * May be used under the terms of the GNU Lesser General Public License (LGPL)
 */
#include <xmmintrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <limits.h>
#include "fec.h"

typedef union { unsigned long w[8]; unsigned char c[32];} decision_t;
typedef union { signed short s[256]; __m64 v[64];} metric_t;

static union branchtab39 { unsigned short s[128]; __m64 v[32];} Branchtab39[3];
static int Init = 0;

/* State info for instance of Viterbi decoder */
struct v39 {
  metric_t metrics1; /* path metric buffer 1 */
  metric_t metrics2; /* path metric buffer 2 */
  void *dp;          /* Pointer to current decision */
  metric_t *old_metrics,*new_metrics; /* Pointers to path metrics, swapped on every bit */
  void *decisions;   /* Beginning of decisions for block */
};

/* Initialize Viterbi decoder for start of new frame */
int init_viterbi39_sse(void *p,int starting_state){
  struct v39 *vp = p;
  int i;

  if(p == NULL)
    return -1;
  for(i=0;i<256;i++)
    vp->metrics1.s[i] = (SHRT_MIN+1000);

  vp->old_metrics = &vp->metrics1;
  vp->new_metrics = &vp->metrics2;
  vp->dp = vp->decisions;
  vp->old_metrics->s[starting_state & 255] = SHRT_MIN; /* Bias known start state */
  return 0;
}

/* Create a new instance of a Viterbi decoder */
void *create_viterbi39_sse(int len){
  struct v39 *vp;

  if(!Init){
    int polys[3] = { V39POLYA, V39POLYB, V39POLYC };

    set_viterbi39_polynomial_sse(polys);
  }
  if((vp = (struct v39 *)malloc(sizeof(struct v39))) == NULL){
    return NULL;
  }
  if((vp->decisions = malloc((len+8)*sizeof(decision_t))) == NULL){
    free(vp);
    return NULL;
  }
  init_viterbi39_sse(vp,0);
  return vp;
}

void set_viterbi39_polynomial_sse(int polys[3]){
  int state;

  for(state=0;state < 128;state++){
    Branchtab39[0].s[state] = (polys[0] < 0) ^ parity((2*state) & polys[0]) ? 255:0;
    Branchtab39[1].s[state] = (polys[1] < 0) ^ parity((2*state) & polys[1]) ? 255:0;
    Branchtab39[2].s[state] = (polys[2] < 0) ^ parity((2*state) & polys[2]) ? 255:0;
  }
  Init++;
}

/* Viterbi chainback */
int chainback_viterbi39_sse(
      void *p,
      unsigned char *data, /* Decoded output data */
      unsigned int nbits, /* Number of data bits */
      unsigned int endstate){ /* Terminal encoder state */
  struct v39 *vp = p;
  decision_t *d;
  int path_metric;

  if(p == NULL)
    return -1;
  d = (decision_t *)vp->decisions;  
  endstate %= 256;

  path_metric = vp->old_metrics->s[endstate];

  /* The store into data[] only needs to be done every 8 bits.
   * But this avoids a conditional branch, and the writes will
   * combine in the cache anyway
   */
  d += 8; /* Look past tail */
  while(nbits-- != 0){
    int k;

    /*    k = (d[nbits].w[endstate/32] >> (endstate%32)) & 1;*/
    k = (d[nbits].c[endstate/8] >> (endstate%8)) & 1;
    endstate = (k << 7) | (endstate >> 1);
    data[nbits>>3] = endstate;
  }
  return path_metric - SHRT_MIN;
}

/* Delete instance of a Viterbi decoder */
void delete_viterbi39_sse(void *p){
  struct v39 *vp = p;

  if(vp != NULL){
    free(vp->decisions);
    free(vp);
  }
}


int update_viterbi39_blk_sse(void *p,unsigned char *syms,int nbits){
  struct v39 *vp = p;
  decision_t *d;
  int path_metric = 0;

  if(p == NULL)
    return -1;
  d = (decision_t *)vp->dp;
  while(nbits--){
    __m64 sym0v,sym1v,sym2v;
    void *tmp;
    int i;

    /* Splat the 0th symbol across sym0v, the 1st symbol across sym1v, etc */
    sym0v = _mm_set1_pi16(syms[0]);
    sym1v = _mm_set1_pi16(syms[1]);
    sym2v = _mm_set1_pi16(syms[2]);
    syms += 3;

    for(i=0;i<32;i++){
      __m64 decision0,decision1,metric,m_metric,m0,m1,m2,m3,survivor0,survivor1;

      /* Form branch metrics
       * Because Branchtab takes on values 0 and 255, and the values of sym?v are offset binary in the range 0-255,
       * the XOR operations constitute conditional negation.
       * metric and m_metric (-metric) are in the range 0-765
       */
      m0 = _mm_add_pi16(_mm_xor_si64(Branchtab39[0].v[i],sym0v),_mm_xor_si64(Branchtab39[1].v[i],sym1v));
      metric = _mm_add_pi16(_mm_xor_si64(Branchtab39[2].v[i],sym2v),m0);
      m_metric = _mm_sub_pi16(_mm_set1_pi16(765),metric);
    
      /* Add branch metrics to path metrics */
      m0 = _mm_adds_pi16(vp->old_metrics->v[i],metric);
      m3 = _mm_adds_pi16(vp->old_metrics->v[32+i],metric);
      m1 = _mm_adds_pi16(vp->old_metrics->v[32+i],m_metric);
      m2 = _mm_adds_pi16(vp->old_metrics->v[i],m_metric);
    
      /* Compare and select */
      survivor0 = _mm_min_pi16(m0,m1);
      survivor1 = _mm_min_pi16(m2,m3);
      decision0 = _mm_cmpeq_pi16(survivor0,m1);
      decision1 = _mm_cmpeq_pi16(survivor1,m3);
 
      /* Pack decisions into 8 bits and store */
      d->c[i] = _mm_movemask_pi8(_mm_unpacklo_pi8(_mm_packs_pi16(decision0,_mm_setzero_si64()),_mm_packs_pi16(decision1,_mm_setzero_si64())));

      /* Store surviving metrics */
      vp->new_metrics->v[2*i] = _mm_unpacklo_pi16(survivor0,survivor1);
      vp->new_metrics->v[2*i+1] = _mm_unpackhi_pi16(survivor0,survivor1);
    }
    /* See if we need to renormalize
     * Max metric spread for this code with 0-255 branch metrics is 12750
     */
    if(vp->new_metrics->s[0] >= SHRT_MAX-5000){
      int i,adjust;
      __m64 adjustv;
      union { __m64 v; signed short w[4]; } t;

      /* Find smallest metric and set adjustv to bring it down to SHRT_MIN */
      adjustv = vp->new_metrics->v[0];
      for(i=1;i<64;i++)
	adjustv = _mm_min_pi16(adjustv,vp->new_metrics->v[i]);

      adjustv = _mm_min_pi16(adjustv,_mm_srli_si64(adjustv,32));
      adjustv = _mm_min_pi16(adjustv,_mm_srli_si64(adjustv,16));    
      t.v = adjustv;
      adjust = t.w[0] - SHRT_MIN;
      path_metric += adjust;
      adjustv = _mm_set1_pi16(adjust);
      
      for(i=0;i<64;i++)
	vp->new_metrics->v[i] = _mm_sub_pi16(vp->new_metrics->v[i],adjustv);
    }
    d++;
    /* Swap pointers to old and new metrics */
    tmp = vp->old_metrics;
    vp->old_metrics = vp->new_metrics;
    vp->new_metrics = tmp;
  }
  vp->dp = d;
  _mm_empty();
  return path_metric;
}
