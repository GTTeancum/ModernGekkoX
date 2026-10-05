/* SPDX-License-Identifier: GPL-3.0-or-later
 * Freestanding Linux i386 diagnostic: the same Pentium III math core, no libc.
 * Executes on the build host, NOT Xbox hardware. */
#include "mgx_math.h"
#include <stddef.h>
void *memset(void *dst,int c,size_t n){unsigned char *p=dst;while(n--)*p++=(unsigned char)c;return dst;}
extern const uint64_t mgx_vectors_start[],mgx_vectors_end[];
static int equal(uint64_t a,uint64_t b){
    const uint64_t inf=UINT64_C(0x7ff0000000000000),frac=UINT64_C(0x000fffffffffffff);
    return a==b || ((a&inf)==inf&&(a&frac)&&(b&inf)==inf&&(b&frac));
}
static int run(void){
    static const int modes[4]={0,0xc00,0x800,0x400};
    mgx_math_init();
    unsigned short original;
    __asm__ volatile("fnstcw %0":"=m"(original));
    for (unsigned i=0;i<8;++i){
        unsigned csr;unsigned short cw;int mode=modes[i%4];
        if(mgx_math_setround(mode)||mgx_math_getround()!=mode)return 1;
        __asm__ volatile("fnstcw %0":"=m"(cw));
        __asm__ volatile("stmxcsr %0":"=m"(csr));
        if((cw&~0xc00u)!=(original&~0xc00u)||(csr&0x6000u)!=(unsigned)mode<<3)return 2;
        if(mgx_math_setround(1)!=-1||mgx_math_getround()!=mode)return 3;
        volatile double one=1.0,half=0x1p-53;
        volatile double sum=one+half;
        union {double d;uint64_t u;} v={sum};
        if(v.u!=(i%4==2?UINT64_C(0x3ff0000000000001):UINT64_C(0x3ff0000000000000)))return 4;
    }
    for(const uint64_t *p=mgx_vectors_start;p<mgx_vectors_end;p+=5){
        if(!equal(mgx_fma_bits(p[0],p[1],p[2],(unsigned)p[3]),p[4]))return 5;
        union {double d;uint64_t u;} a,b,c,r;a.u=p[0];b.u=p[1];c.u=p[2];
        mgx_math_setround(modes[p[3]]);r.d=mgx_math_fma(a.d,b.d,c.d);
        if(!equal(r.u,p[4]))return 6;
    }
    return 0;
}
void mgx_test_entry(void){
    int result=run();
    static const char yes[]="PASS: freestanding Pentium III math vectors and rounding controls\n";
    static const char no[]="FAIL: freestanding Pentium III math validation\n";
    const char *text=result?no:yes;unsigned length=result?sizeof(no)-1:sizeof(yes)-1;
    __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(text),"d"(length):"memory");
    __asm__ volatile("int $0x80"::"a"(1),"b"(result):"memory");
    __builtin_unreachable();
}

__attribute__((naked,noreturn)) void _start(void){
    __asm__ volatile("andl $-16, %esp\ncall mgx_test_entry");
}
