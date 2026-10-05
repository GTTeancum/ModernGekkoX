/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MGX_MATH_REDIRECT_H
#define MGX_MATH_REDIRECT_H
#include <math.h>
#include <fenv.h>
#include "mgx_math.h"
#define fma mgx_math_fma
#define floor mgx_math_floor
#define ceil mgx_math_ceil
#define trunc mgx_math_trunc
#define ldexp mgx_math_ldexp
#define fmod mgx_math_fmod
#define fesetround mgx_math_setround
#define fegetround mgx_math_getround
#endif
