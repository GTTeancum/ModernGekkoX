/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Freestanding 32-bit caller for the real cpu.c memory helper definitions.
   External callbacks remain cdecl; only the eight memory helpers opt in. */
#include "cpu/cpu.h"
static CPUState cpu;
static u8 ram[128],mem2[128];
static unsigned checks,observed,journaled,external_reads,external_writes;
static u32 expected_address,expected_width;static u64 expected_value;
static void quit(int code){__asm__ volatile("int $0x80"::"a"(1),"b"(code));__builtin_unreachable();}
#define CHECK(x) do {++checks;if(!(x))quit(1);}while(0)
static void check_write(CPUState *c,u32 address,u64 value,u8 width,void *user){
    CHECK(c==&cpu && user==&checks && address==expected_address);
    CHECK(width==expected_width && value==expected_value);++observed;
}
static void journal(u32 offset,u32 size,void *user){CHECK(user==&checks && offset<128 && size==expected_width);++journaled;}
static u64 read_external(CPUState *c,u32 address,u8 width){CHECK(c==&cpu && address==expected_address && width==expected_width);++external_reads;return expected_value;}
static void write_external(CPUState *c,u32 address,u64 value,u8 width){CHECK(c==&cpu && address==expected_address && width==expected_width && value==expected_value);++external_writes;}
static u64 read_width(u32 a,unsigned w){switch(w){case 1:return mem_read8(&cpu,a);case 2:return mem_read16(&cpu,a);case 4:return mem_read32(&cpu,a);default:return mem_read64(&cpu,a);}}
static void write_width(u32 a,unsigned w,u64 v){switch(w){case 1:mem_write8(&cpu,a,(u8)v);break;case 2:mem_write16(&cpu,a,(u16)v);break;case 4:mem_write32(&cpu,a,(u32)v);break;default:mem_write64(&cpu,a,v);break;}}
__attribute__((force_align_arg_pointer)) void _start(void){
    cpu.ram=ram;cpu.ram_size=sizeof(ram);cpu.mem2=mem2;cpu.mem2_size=sizeof(mem2);
    cpu.external_read=read_external;cpu.external_write=write_external;
    ppc_set_mem_write_check(check_write,&checks);ppc_set_mem_write_journal(journal,&checks);
    const u32 bases[]={0x80000000,0xc0000000,0x90000000,0xd0000000};
    u32 seed=0x76543210;
    for(unsigned bank=0;bank<4;++bank)for(unsigned i=0;i<256;++i)for(unsigned w=1;w<=8;w*=2){
        seed=seed*1664525u+1013904223u;u64 v=((u64)seed<<32)|(seed^0xfedcba98u);
        if(w<8)v&=(UINT64_C(1)<<(w*8))-1;
        const u32 offset=i%119,a=bases[bank]+offset;
        expected_address=a;expected_width=w;expected_value=v;
        cpu.reserve_valid=1;cpu.reserve_addr=a;
        unsigned n=observed;write_width(a,w,v);CHECK(observed==n+1 && !cpu.reserve_valid);
        CHECK(read_width(a,w)==v);
        const u8 *bytes=bank<2?ram:mem2;
        for(unsigned b=0;b<w;++b)CHECK(bytes[offset+b]==(u8)(v>>(8*(w-1-b))));
    }
    CHECK(observed==4096 && journaled==2048);
    for(unsigned w=1;w<=8;w*=2){
        expected_address=0xcc005000;expected_width=w;expected_value=UINT64_C(0xfedcba9876543210);
        if(w<8)expected_value&=(UINT64_C(1)<<(w*8))-1;
        unsigned n=observed;cpu.reserve_valid=1;cpu.reserve_addr=0x80000000;
        write_width(expected_address,w,expected_value);CHECK(cpu.reserve_valid && observed==n);
        CHECK(read_width(expected_address,w)==expected_value);
    }
    CHECK(external_reads==4 && external_writes==4);
    /* A wide straddle must take the callback without a partial RAM store. */
    expected_address=0x8000007c;expected_width=8;expected_value=UINT64_C(0x8877665544332211);
    u32 before=mem_read32(&cpu,expected_address);write_width(expected_address,8,expected_value);
    CHECK(mem_read32(&cpu,expected_address)==before);CHECK(read_width(expected_address,8)==expected_value);
    const char ok[]="PASS: actual i386 memory-helper ABI, 4096 RAM stores plus external and straddle checks\n";
    __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(ok),"d"(sizeof(ok)-1):"memory");quit(0);
}
