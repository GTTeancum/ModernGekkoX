#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Deterministic bitwise math tests. Original inputs; no game data.
FMA checked against two independent oracles: exact Python integers and libm.
Optionally exports binary vectors for freestanding Pentium III execution.
"""
import argparse, ctypes as C, ctypes.util, json, math, random, struct, subprocess, tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
SIGN=1<<63; FRAC=(1<<52)-1; INF=0x7ff0000000000000
RN=[0,0xc00,0x800,0x400]
def dbl(x): return struct.unpack('<d',struct.pack('<Q',x))[0]
def bits(x): return struct.unpack('<Q',struct.pack('<d',x))[0]
def nan(u): return u&INF==INF and bool(u&FRAC)
def same(a,b): return a==b or (nan(a) and nan(b))
def oracle(a,b,c,rm):
    """Unbounded, dynamically aligned dyadic arithmetic; finite inputs only."""
    def dec(u):
        e=(u>>52)&2047; m=u&FRAC
        if e: m|=1<<52
        return (-m if u&SIGN else m), (e-1075 if e else -1074)
    am,ae=dec(a);bm,be=dec(b);cm,ce=dec(c)
    pe=ae+be; exponent=min(pe,ce)
    total=(am*bm<<(pe-exponent))+(cm<<(ce-exponent))
    if not total:
        ps=((a^b)>>63);zs=c>>63
        sign=ps if not am*bm and not cm and ps==zs else int(rm==3)
        return sign<<63
    sign=int(total<0); total=abs(total)
    top=total.bit_length()-1+exponent
    unit=max(top-52,-1074)
    cut=unit-exponent
    if cut>0: q,r=divmod(total,1<<cut)
    else: q,r=total<<-cut,0
    inc=False
    if r:
        if rm==0: inc=2*r>(1<<cut) or (2*r==(1<<cut) and q&1)
        if rm==2: inc=not sign
        if rm==3: inc=bool(sign)
    q+=int(inc)
    if q>=1<<53: q>>=1;unit+=1
    if unit+52>1023:
        return sign<<63 | (INF if rm==0 or (rm==2 and not sign) or (rm==3 and sign) else INF-1)
    if q<1<<52: return sign<<63 | q
    return sign<<63 | ((unit+1075)<<52) | (q&FRAC)
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--vectors',type=Path);ap.add_argument('--output',type=Path)
    ap.add_argument('--count',type=int,default=12000);args=ap.parse_args()
    rng=random.Random(0xB003)
    with tempfile.TemporaryDirectory() as d:
        so=Path(d)/'math.so'
        cmd=['clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-fno-fast-math','-ffp-contract=off','-frounding-math','-shared','-fPIC','-I'+str(ROOT/'include'),str(ROOT/'src/mgx_math.c'),'-o',str(so)]
        subprocess.run(cmd,check=True)
        lib=C.CDLL(str(so));ref=C.CDLL(ctypes.util.find_library('m'))
        f=lib.mgx_fma_bits;f.argtypes=[C.c_uint64]*3+[C.c_uint];f.restype=C.c_uint64
        fm=ref.fma;fm.argtypes=[C.c_double]*3;fm.restype=C.c_double
        lib.mgx_math_setround.argtypes=[C.c_int];lib.mgx_math_setround.restype=C.c_int
        lib.mgx_math_getround.restype=C.c_int; ref.fesetround.argtypes=[C.c_int]
        special=[0,SIGN,1,SIGN|1,FRAC,(1<<52), (1<<52)+1, bits(1.),bits(-1.),bits(0.5),bits(1.+2**-52),bits(1.-2**-53),INF-1,(INF-1)|SIGN,bits(2**-500),bits(2**500)]
        triples=[(a,b,c) for a in special for b in special for c in special]
        triples += [tuple(rng.getrandbits(64) for _ in range(3)) for _ in range(args.count)]
        # Deliberate near-cancellation, ties, overflow cancellation, tiny terms.
        for _ in range(2000):
            a=bits(rng.uniform(.5,2));b=bits(rng.uniform(.5,2));c=bits(-dbl(a)*dbl(b))
            triples.append((a,b,c))
        triples += [(INF,0,0),(0,INF,INF),(INF,bits(1.),INF|SIGN),(INF,bits(-1.),INF|SIGN), (0x7ff0000000000001,bits(1.),bits(2.))]
        vector_rows=[];fma_cases=python_cases=0
        try:
            lib.mgx_math_init()
            for rm,host in enumerate(RN):
                assert lib.mgx_math_setround(host)==0
                assert lib.mgx_math_getround()==host
                assert ref.fegetround()==host
                assert lib.mgx_math_setround(123)==-1 and lib.mgx_math_getround()==host
                for index,(a,b,c) in enumerate(triples):
                    got=f(a,b,c,rm);expect=bits(fm(dbl(a),dbl(b),dbl(c)))
                    assert same(got,expect),(rm,index,hex(a),hex(b),hex(c),hex(got),hex(expect),'libm')
                    if all(u&INF != INF for u in (a,b,c)):
                        exact=oracle(a,b,c,rm);python_cases+=1
                        assert got==exact,(rm,index,hex(got),hex(exact),'integer')
                    if index<4096 or index%17==0: vector_rows.append((a,b,c,rm,expect))
                    fma_cases+=1
            unary=0;ldexp_cases=0;mod_cases=0
            for name in ['floor','ceil','trunc']:
                own=getattr(lib,'mgx_math_'+name);std=getattr(ref,name)
                for fun in [own,std]:fun.argtypes=[C.c_double];fun.restype=C.c_double
                for u in special+[rng.getrandbits(64) for _ in range(15000)]:
                    assert same(bits(own(dbl(u))),bits(std(dbl(u)))),(name,hex(u));unary+=1
            own=lib.mgx_math_ldexp;std=ref.ldexp
            for fun in [own,std]:fun.argtypes=[C.c_double,C.c_int];fun.restype=C.c_double
            for rm,host in enumerate(RN):
                lib.mgx_math_setround(host)
                for _ in range(7000):
                    u=rng.getrandbits(64);e=rng.choice([rng.randint(-4096,4096),2147483647,-2147483648])
                    assert same(bits(own(dbl(u),e)),bits(std(dbl(u),e))),('ldexp',rm,hex(u),e);ldexp_cases+=1
            own=lib.mgx_math_fmod;std=ref.fmod
            for fun in [own,std]:fun.argtypes=[C.c_double]*2;fun.restype=C.c_double
            for _ in range(20000):
                a=rng.getrandbits(64);b=rng.getrandbits(64)
                assert same(bits(own(dbl(a),dbl(b))),bits(std(dbl(a),dbl(b)))),('fmod',hex(a),hex(b));mod_cases+=1
        finally: ref.fesetround(0)
        if args.vectors:
            args.vectors.parent.mkdir(parents=True,exist_ok=True)
            args.vectors.write_bytes(b''.join(struct.pack('<5Q',*r) for r in vector_rows))
        report={'fma_libm_cases':fma_cases,'fma_python_integer_cases':python_cases,'unary_cases':unary,'ldexp_cases':ldexp_cases,'fmod_cases':mod_cases,'pentium3_vector_count':len(vector_rows),'rounding_modes':4,'all_passed':True,'host_exception_flags_validated':False,'hardware_tested':False}
        text=json.dumps(report,indent=2);print(text)
        if args.output:args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(text+'\n')
if __name__=='__main__':main()
