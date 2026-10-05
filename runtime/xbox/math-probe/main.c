/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <hal/video.h>
#include <hal/debug.h>
#include <windows.h>
#include <math.h>
#include <fenv.h>
#include "mgx_math.h"
int main(void) {
    XVideoSetMode(640,480,32,REFRESH_DEFAULT);
    mgx_math_init();
    double r=fma(0x1.0000000000001p0,0x1.ffffffffffffep-1,-1.0);
    union{double d;uint64_t u;} result={r};
    debugPrint("ModernGekkoX math diagnostic - NOT a game boot\n");
    debugPrint("FMA cancellation test: %s\n",result.u==UINT64_C(0xb970000000000000)?"PASS":"FAIL");
    for(int i=0;i<4;++i){static const int modes[4]={0,0xc00,0x800,0x400};
        int status=fesetround(modes[i]);debugPrint("round %x -> %x (rc=%d)\n",modes[i],fegetround(),status);}
    while(1)Sleep(1000);
    return 0;
}
