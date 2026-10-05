/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MGX_MATH_H
#define MGX_MATH_H
#include <stdint.h>
/* Values use PPC FPSCR RN order. This reference path does not raise host FP
   exception flags. Guest exception bookkeeping remains in DolRecomp's helpers. */
enum { MGX_RN, MGX_RZ, MGX_RP, MGX_RM };
uint64_t mgx_fma_bits(uint64_t x, uint64_t y, uint64_t z, unsigned rounding);
int mgx_math_setround(int mode);
int mgx_math_getround(void);
void mgx_math_init(void);
double mgx_math_fma(double x, double y, double z);
double mgx_math_floor(double x);
double mgx_math_ceil(double x);
double mgx_math_trunc(double x);
double mgx_math_ldexp(double x, int exponent);
double mgx_math_fmod(double x, double y);
#endif
