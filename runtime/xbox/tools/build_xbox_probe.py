#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build every supplied generated chunk using pinned nxdk. No game is bundled.

Reuse is accepted only from this builder with identical compiler identity,
exact flags, source, header dependencies and object bytes. Legacy reports
without compiler identity are not imported.
"""
import argparse,concurrent.futures,hashlib,json,os,shlex,shutil,struct,subprocess
from pathlib import Path
from build_host_probe import sha,dependency_paths
from audit_pe_memory import audit as audit_memory

def reusable(item,source,identity,old_identity):
    try:
        if old_identity!=identity:return False
        deps=item['dependencies'];original=Path(item['source'])
        return (str(original) in deps and original==source and
                sha(original)==sha(source)==deps[str(original)] and
                bool(deps) and all(sha(f)==h for f,h in deps.items()) and
                sha(item['object'])==item['object_sha256'])
    except (KeyError,TypeError,ValueError,OSError):return False

def pe_image_size(path):
    b=Path(path).read_bytes()
    if len(b)<64 or b[:2]!=b'MZ':raise ValueError('invalid DOS header')
    p=struct.unpack_from('<I',b,60)[0]
    if p+84>len(b) or b[p:p+4]!=b'PE\0\0' or struct.unpack_from('<H',b,p+4)[0]!=0x14c:
        raise ValueError('expected I386 PE image')
    return struct.unpack_from('<I',b,p+24+56)[0]

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sdk',type=Path,required=True)
    ap.add_argument('--compiler-source',type=Path,required=True)
    ap.add_argument('--generated',type=Path,required=True)
    ap.add_argument('--template-profile',type=Path)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--optimization',choices=['1','z'],default='1')
    ap.add_argument('--reuse-report',type=Path)
    ap.add_argument('--memory-abi',choices=['cdecl','fastcall'],default='cdecl',
                    help='experimental 32-bit memory-helper convention; SDK ABI unchanged')
    ap.add_argument('--jobs',type=int,default=2)
    a=ap.parse_args()
    if not 1<=a.jobs<=8:ap.error("--jobs must be in 1..8")
    rt=Path(__file__).resolve().parents[1];sdk=a.sdk.resolve();src=a.compiler_source.resolve()/'src'
    gen=a.generated.resolve();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
    lock=json.loads((rt/'nxdk.lock.json').read_text())
    for f,h in lock['required_file_sha256'].items():
        if sha(sdk/f)!=h:raise ValueError('nxdk pin mismatch: '+f)
    cc=shutil.which('clang');ld=shutil.which('lld');cxbe=sdk/'tools/cxbe/cxbe'
    if not cc or not ld or not cxbe.is_file():raise ValueError('clang, lld and built nxdk cxbe required')
    identity={'clang':sha(Path(cc).resolve()),'version':subprocess.check_output([cc,'--version'],text=True),
              'nxdk_revision':lock['revision'],'cc_wrapper':sha(sdk/'bin/nxdk-cc')}
    env=dict(os.environ,NXDK_DIR=str(sdk),PATH=str(sdk/'bin')+os.pathsep+os.environ['PATH'])
    headers=sorted(gen.glob('*.h'));chunks=sorted((gen/'chunks').glob('*.c'))
    if len(headers)!=1 or not chunks:raise ValueError('one module header and nonempty chunks required')
    module=headers[0].stem
    sources=chunks+[gen/(module+'.c'),src/'cpu/cpu.c',rt/'src/mgx_dol.c',rt/'src/mgx_math.c',
                    rt/'src/mgx_exec.c',rt/'startup-probe/main.c',rt/'startup-probe/bridge.c']
    if any(not f.is_file() for f in sources):raise ValueError('missing input source')
    old={}
    if a.reuse_report:
        old=json.loads(a.reuse_report.read_text())
        if old['optimization']!=a.optimization:raise ValueError('cache optimization mismatch')
    flags=[str(sdk/'bin/nxdk-cc'),'-std=gnu11','-O'+a.optimization,'-fno-fast-math',
           '-ffp-contract=off','-frounding-math','-I'+str(src),'-I'+str(rt/'include'),'-I'+str(gen)]
    if a.memory_abi=='fastcall':flags+=['-DDOLRECOMP_X86_FASTCALL=1']
    records=[];commands=[];all_old=old.get('objects',[])
    def compile_one(pair):
        index,f=pair
        extra=[]
        if f==src/'cpu/cpu.c':extra=['-include',str(rt/'include/mgx_math_redirect.h')]
        if f.name=='bridge.c':extra=['-DMGX_GENERATED_HEADER="'+module+'.h"']
        if f==rt/'startup-probe/main.c' and a.template_profile:
            extra=['-DMGX_TEMPLATE_PROFILE_HEADER="'+str(a.template_profile.resolve())+'"']
        key=flags+extra
        found=None
        for item in all_old:
            if item.get('compile_key')!=key:continue
            if reusable(item,f,identity,old.get('compiler_identity')):found=item;break
        if found:
            return dict(found,reused=True)
        obj=out/(f'{index:04d}-'+f.stem+'.obj');dep=obj.with_suffix('.d');obj.unlink(missing_ok=True)
        cmd=key+['-MD','-MF',str(dep),'-c',str(f),'-o',str(obj)];commands.append(cmd)
        r=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=180)
        obj.with_suffix('.log').write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError('compile failed: '+str(f)+'\n'+r.stderr)
        deps=dependency_paths(dep.read_text(),Path.cwd())
        if str(f) not in deps:raise ValueError('missing source dependency')
        return {'source':str(f),'object':str(obj),'object_sha256':sha(obj),
                        'dependencies':{p:sha(p) for p in deps},'compile_key':key,
                        'reused':False}
    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        records=list(pool.map(compile_one,enumerate(sources)))
    libs=sorted((sdk/'lib').glob('*.lib'))+[sdk/'lib/xboxkrnl/libxboxkrnl.lib']
    if not libs or any(not f.is_file() for f in libs):raise ValueError('missing built SDK libraries')
    objects=[Path(r['object']) for r in records]
    for r in records:
        if sha(r['object'])!=r['object_sha256'] or any(sha(f)!=h for f,h in r['dependencies'].items()):
            raise ValueError('input changed before link')
    exe=out/'main.exe';xbe=out/'default.xbe';exe.unlink(missing_ok=True);xbe.unlink(missing_ok=True)
    # Deterministic PE timestamp improves repeat-build comparison.
    cmd=[str(sdk/'bin/nxdk-link'),'-include:_automount_d_drive','-timestamp:0',
         '-out:'+str(exe),'-map:'+str(out/'startup.map'),*map(str,objects),*map(str,libs)]
    commands.append(cmd);r=subprocess.run(cmd,env=env,capture_output=True,text=True,timeout=120)
    (out/'link.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr)
    cmd=[str(cxbe),'-OUT:'+str(xbe),'-TITLE:Recompiled startup diagnostic',str(exe)]
    commands.append(cmd);r=subprocess.run(cmd,capture_output=True,text=True,timeout=60)
    (out/'cxbe.log').write_text(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError(r.stderr)
    result={'target':'i386-pc-win32/pentium3','optimization':a.optimization,'memory_abi':a.memory_abi,'compiler_identity':identity,
            'nxdk_revision':lock['revision'],'generated_chunks':len(chunks),'all_generated_chunks_linked':True,
            'objects_linked':len(records),'reused_verified_objects':sum(r['reused'] for r in records),
            'compiled_objects':sum(not r['reused'] for r in records),
            'memory_audit':audit_memory(exe),'pe_size_of_image':pe_image_size(exe),'xbe_size':xbe.stat().st_size,'xbe_sha256':sha(xbe),
            'xbox_hardware_tested':False,'game_booted':False,'commands':commands,'objects':records,
            'link_input_hashes':{str(f):sha(f) for f in objects+libs},'cxbe_sha256':sha(cxbe),
            'linker_sha256':sha(Path(ld).resolve()),'link_wrapper_sha256':sha(sdk/'bin/nxdk-link')}
    (out/'build-report.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('objects','commands','link_input_hashes','compiler_identity')},indent=2))
if __name__=='__main__':main()
