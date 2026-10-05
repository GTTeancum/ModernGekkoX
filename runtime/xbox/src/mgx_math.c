/* SPDX-License-Identifier: GPL-3.0-or-later
 * Correctness-first binary64 support for the pinned Xbox CRT.
 * The fused operation uses an exact fixed-point integer accumulator, not x*y+z.
 * Finite binary64 products occupy exponents [-2148, 2047]. 132 32-bit limbs
 * cover that entire range, including carry. No SSE2, FMA ISA, or int128 needed.
 * This is a reference baseline, NOT the eventual performance implementation.
 */
#include "mgx_math.h"
#include <float.h>
#if DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024
#error binary64 double required
#endif
#define N 132
#define FRAC UINT64_C(0x000fffffffffffff)
#define SIGN UINT64_C(0x8000000000000000)
#define INF UINT64_C(0x7ff0000000000000)
#define QUIET UINT64_C(0x0008000000000000)
#define QNAN (INF | QUIET)
typedef union { double d; uint64_t u; } bits64;
typedef struct { uint64_t m; int e; unsigned sign; } number;
static number decode(uint64_t u) {
    number a;
    unsigned e = (unsigned)(u >> 52) & 2047u;
    a.m = u & FRAC;
    if (e) a.m |= UINT64_C(1) << 52;
    a.e = e ? (int)e - 1075 : -1074;
    a.sign = (unsigned)(u >> 63);
    return a;
}
static int is_nan(uint64_t u) { return (u & INF) == INF && (u & FRAC); }
static void put(uint32_t *a, const uint32_t *v, unsigned count, unsigned bit) {
    unsigned k = bit / 32, s = bit % 32;
    for (unsigned i = 0; i < count; ++i) {
        if (k + i < N) a[k+i] |= v[i] << s;
        if (s && k + i + 1 < N) a[k+i+1] |= v[i] >> (32-s);
    }
}
static int compare(const uint32_t *a, const uint32_t *b) {
    for (int i = N-1; i >= 0; --i)
        if (a[i] != b[i]) return a[i] > b[i] ? 1 : -1;
    return 0;
}
static uint64_t overflow(unsigned sign, unsigned rm) {
    int infinite = rm == MGX_RN || (rm == MGX_RP && !sign) || (rm == MGX_RM && sign);
    return ((uint64_t)sign << 63) | (infinite ? INF : INF-1);
}
static unsigned bit_at(const uint32_t *a, unsigned bit) {
    return bit < N*32 ? (a[bit/32] >> (bit%32)) & 1u : 0;
}
static int any_below(const uint32_t *a, unsigned bit) {
    unsigned words = bit/32, rem = bit%32;
    for (unsigned i = 0; i < words && i < N; ++i) if (a[i]) return 1;
    return rem && words < N && (a[words] & ((UINT32_C(1)<<rem)-1));
}
static uint64_t rounded(const uint32_t *a, unsigned sign, unsigned rm) {
    int h = N*32-1;
    while (h >= 0 && !bit_at(a,(unsigned)h)) --h;
    if (h < 0) return (uint64_t)sign << 63;
    unsigned shift = h > 1126 ? (unsigned)h-52 : 1074;
    uint64_t m = 0;
    for (unsigned j = 0; j < 53; ++j) m |= (uint64_t)bit_at(a,shift+j) << j;
    unsigned half = bit_at(a,shift-1);
    int low = any_below(a,shift-1), discarded = half || low;
    int increment = rm == MGX_RN ? half && (low || (m&1)) :
                    rm == MGX_RP ? discarded && !sign :
                    rm == MGX_RM ? discarded && sign : 0;
    m += (unsigned)increment;
    if (m == (UINT64_C(1)<<53)) { m >>= 1; ++shift; }
    int exponent = (int)shift-2148+52;
    if (exponent > 1023) return overflow(sign,rm);
    uint64_t result = m;
    if (m >= (UINT64_C(1)<<52)) result = ((uint64_t)(exponent+1023)<<52) | (m&FRAC);
    return ((uint64_t)sign<<63) | result;
}
uint64_t mgx_fma_bits(uint64_t x, uint64_t y, uint64_t z, unsigned rm) {
    /* Quiet NaN propagation; PPC-specific payload order/invalid handling is
       performed by ppc_fma after it detects our NaN result. */
    rm &= 3;
    if (is_nan(x)) return x | QUIET;
    if (is_nan(y)) return y | QUIET;
    if (is_nan(z)) return z | QUIET;
    unsigned sign = (unsigned)((x^y)>>63), zs = (unsigned)(z>>63);
    uint64_t ax=x&~SIGN, ay=y&~SIGN, az=z&~SIGN;
    if (ax==INF || ay==INF) {
        if (!ax || !ay) return QNAN;
        if (az==INF && sign!=zs) return QNAN;
        return ((uint64_t)sign<<63)|INF;
    }
    if (az==INF) return z;
    if (!ax || !ay) {
        if (az) return z;
        return (uint64_t)(sign==zs ? sign : rm==MGX_RM)<<63;
    }
    number a=decode(x), b=decode(y), c=decode(z);
    uint32_t p[N]={0}, q[N]={0}, product[4]={0};
    uint32_t av[2]={(uint32_t)a.m,(uint32_t)(a.m>>32)};
    uint32_t bv[2]={(uint32_t)b.m,(uint32_t)(b.m>>32)};
    for (unsigned i=0;i<2;++i) {
        uint64_t carry=0;
        for (unsigned j=0;j<2;++j) {
            uint64_t t=(uint64_t)av[i]*bv[j]+product[i+j]+carry;
            product[i+j]=(uint32_t)t; carry=t>>32;
        }
        product[i+2]=(uint32_t)carry;
    }
    put(p,product,4,(unsigned)(a.e+b.e+2148));
    uint32_t cv[2]={(uint32_t)c.m,(uint32_t)(c.m>>32)};
    put(q,cv,2,(unsigned)(c.e+2148));
    uint32_t *large=p,*small=q;
    if (sign==zs) {
        uint64_t carry=0;
        for (unsigned i=0;i<N;++i) {
            uint64_t t=(uint64_t)p[i]+q[i]+carry;
            p[i]=(uint32_t)t; carry=t>>32;
        }
    } else {
        int order=compare(p,q);
        if (!order) return (uint64_t)(rm==MGX_RM)<<63;
        if (order<0) {large=q;small=p;sign=zs;}
        uint64_t borrow=0;
        for (unsigned i=0;i<N;++i) {
            uint64_t sub=(uint64_t)small[i]+borrow;
            uint32_t old=large[i]; large[i]=old-(uint32_t)sub;
            borrow=(uint64_t)old<sub;
        }
    }
    return rounded(large,sign,rm);
}
/* Correct x87 constraints, 16-bit control-word access, SSE1 MXCSR support.
   Preserve precision/exception masks when changing only rounding direction. */
int mgx_math_getround(void) {
    unsigned short cw;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    return cw & 0xc00;
}
int mgx_math_setround(int mode) {
    if (mode!=0 && mode!=0x400 && mode!=0x800 && mode!=0xc00) return -1;
    unsigned short cw; unsigned csr;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    cw=(unsigned short)((cw & ~0xc00u)|(unsigned)mode);
    __asm__ volatile("fldcw %0" :: "m"(cw) : "memory");
    __asm__ volatile("stmxcsr %0" : "=m"(csr));
    csr=(csr & ~0x6000u)|((unsigned)mode<<3);
    __asm__ volatile("ldmxcsr %0" :: "m"(csr) : "memory");
    return 0;
}
void mgx_math_init(void) {
    unsigned short cw; unsigned csr;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    cw=(unsigned short)((cw & ~0xf00u)|0x23fu); /* 53-bit, nearest, masked */
    __asm__ volatile("fldcw %0" :: "m"(cw) : "memory");
    __asm__ volatile("stmxcsr %0" : "=m"(csr));
    csr=(csr & ~0xe040u)|0x1f80u; /* nearest, no FTZ/DAZ, masked */
    __asm__ volatile("ldmxcsr %0" :: "m"(csr) : "memory");
}
static unsigned mode(void) {
    static const unsigned map[4]={MGX_RN,MGX_RM,MGX_RP,MGX_RZ};
    return map[(unsigned)mgx_math_getround()>>10];
}
double mgx_math_fma(double x,double y,double z) {
    bits64 a={x},b={y},c={z},r;
    r.u=mgx_fma_bits(a.u,b.u,c.u,mode());return r.d;
}
static double integral(double x,unsigned rm) {
    bits64 a={x};unsigned sign=(unsigned)(a.u>>63);int e=(int)((a.u>>52)&2047)-1023;
    if (e>=52 || !(a.u&~SIGN)) return x;
    if (e<0) {
        a.u &= SIGN;
        if ((rm==MGX_RP&&!sign)||(rm==MGX_RM&&sign)) a.u|=UINT64_C(0x3ff0000000000000);
        return a.d;
    }
    uint64_t mask=(UINT64_C(1)<<(52-e))-1;
    if (!(a.u&mask)) return x;
    a.u &= ~mask;
    if ((rm==MGX_RP&&!sign)||(rm==MGX_RM&&sign)) a.u+=mask+1;
    return a.d;
}
double mgx_math_floor(double x){return integral(x,MGX_RM);}
double mgx_math_ceil(double x){return integral(x,MGX_RP);}
double mgx_math_trunc(double x){return integral(x,MGX_RZ);}
double mgx_math_ldexp(double x,int exponent) {
    bits64 a={x},r;number n=decode(a.u);unsigned rm=mode();
    if ((a.u&INF)==INF || !(a.u&~SIGN)) return x;
    int64_t e=(int64_t)n.e+exponent;
    unsigned top=0;for(uint64_t t=n.m;t>>=1;) ++top;
    if(e+(int)top>1023){r.u=overflow(n.sign,rm);return r.d;}
    if(e+(int)top< -1075){
        r.u=((uint64_t)n.sign<<63)|((rm==MGX_RP&&!n.sign)||(rm==MGX_RM&&n.sign));return r.d;
    }
    uint32_t p[N]={0},v[2]={(uint32_t)n.m,(uint32_t)(n.m>>32)};
    put(p,v,2,(unsigned)(e+2148));r.u=rounded(p,n.sign,rm);return r.d;
}
double mgx_math_fmod(double x,double y) {
    bits64 a={x},b={y},r;uint64_t ax=a.u&~SIGN,ay=b.u&~SIGN;
    if(is_nan(a.u)){a.u|=QUIET;return a.d;}
    if(is_nan(b.u)){b.u|=QUIET;return b.d;}
    if(ax==INF||!ay){r.u=QNAN;return r.d;}
    if(ax<ay||!ax)return x;
    if(ax==ay){r.u=a.u&SIGN;return r.d;}
    number n=decode(a.u),d=decode(b.u);
    while(n.m<(UINT64_C(1)<<52)){n.m<<=1;--n.e;}
    while(d.m<(UINT64_C(1)<<52)){d.m<<=1;--d.e;}
    while(n.e>d.e){
        if(n.m>=d.m)n.m-=d.m;
        n.m<<=1;--n.e;
    }
    if(n.m>=d.m)n.m-=d.m;
    if(!n.m){r.u=a.u&SIGN;return r.d;}
    while(n.m<(UINT64_C(1)<<52)){n.m<<=1;--n.e;}
    r.u=n.e>=-1074?((uint64_t)(n.e+1075)<<52)|(n.m&FRAC):n.m>>(-1074-n.e);
    r.u|=a.u&SIGN;return r.d;
}
/* Override only the required pinned-nxdk entry points. On host, keep libc's
   symbols intact for independent comparison. These definitions prevent the
   stub/unsafe CRT objects being selected by the final Xbox link. */
#ifdef NXDK
double fma(double x,double y,double z){return mgx_math_fma(x,y,z);}
double floor(double x){return mgx_math_floor(x);}
double ceil(double x){return mgx_math_ceil(x);}
double trunc(double x){return mgx_math_trunc(x);}
double ldexp(double x,int e){return mgx_math_ldexp(x,e);}
double fmod(double x,double y){return mgx_math_fmod(x,y);}
int fegetround(void){return mgx_math_getround();}
int fesetround(int m){return mgx_math_setround(m);}
#endif
