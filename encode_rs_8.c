/* Reed-Solomon encoder
 * Copyright 2004, Phil Karn, KA9Q
 * May be used under the terms of the GNU Lesser General Public License (LGPL)
 */
#include <string.h>
#include "fixed.h"
#ifdef __VEC__
#include <sys/sysctl.h>
#endif


static enum {UNKNOWN=0,MMX,SSE,SSE2,ALTIVEC,PORT} cpu_mode;

static void encode_rs_8_c(data_t *data, data_t *parity,int pad);

void encode_rs_8(data_t *data, data_t *parity,int pad){
  if(cpu_mode == UNKNOWN){
#ifdef __i386__
    int f;
    /* Figure out what kind of CPU we have */
    f = cpu_features();
    if(f & (1<<26)){ /* SSE2 is present */
      cpu_mode = SSE2;
    } else if(f & (1<<25)){ /* SSE is present */
      cpu_mode = SSE;
    } else if(f & (1<<23)){ /* MMX is present */
      cpu_mode = MMX;
    } else { /* No SIMD at all */
      cpu_mode = PORT;
    }
#else
    cpu_mode = PORT;
#endif
  }
  switch(cpu_mode){
#if __i386__
  case MMX:
  case SSE:
  case SSE2:
#endif
  default:
    encode_rs_8_c(data,parity,pad);
    return;
  }
}

/* Portable C version */
static void encode_rs_8_c(data_t *data, data_t *parity,int pad){

#include "encode_rs.h"

}
