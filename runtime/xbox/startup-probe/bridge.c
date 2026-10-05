/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MGX_GENERATED_HEADER
#error Supply MGX_GENERATED_HEADER for your privately generated game module
#endif
#include MGX_GENERATED_HEADER
int mgx_generated_dispatch(CPUState *cpu,uint32_t address){return dolrecomp_call(cpu,address);}
