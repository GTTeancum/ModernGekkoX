#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare a private, complete C module with one parametric AOT li instruction.

Inputs are hash-locked by a private JSON specification. Original generated files
are never edited. A link token defined ONLY by the instrumented chunk binds the
runtime profile to the rewritten internal instruction label. This is not a JIT,
interpreter, general SMC solution, or relocated-handler execution mechanism.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import tempfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def dol_bytes(dol, address, size):
    if len(dol) < 256 or size <= 0 or address < 0 or address + size > 1 << 32:
        raise ValueError("invalid DOL/range")
    offsets = struct.unpack_from('>18I', dol, 0)
    addresses = struct.unpack_from('>18I', dol, 0x48)
    sizes = struct.unpack_from('>18I', dol, 0x90)
    candidates = []
    for i, (off, start, length) in enumerate(zip(offsets, addresses, sizes)):
        if length and start <= address and address + size <= start + length:
            pos = off + address - start
            if off < 256 or off + length > len(dol) or i >= 7:
                raise ValueError("template/writer must be file-backed text")
            candidates.append(dol[pos:pos + size])
    if len(candidates) != 1:
        raise ValueError("ambiguous or unbacked DOL text range")
    return candidates[0]


def prepare(generated, dol_path, spec_path, output):
    generated, output = Path(generated).resolve(), Path(output).resolve()
    if output.exists() or output == generated or generated in output.parents or output in generated.parents:
        raise ValueError("output must be a new directory outside the input tree")
    spec = json.loads(Path(spec_path).read_text())
    required = {'dol_sha256', 'template_sha256', 'address', 'size', 'patch_offset',
                'max_immediate', 'writer_pc', 'source', 'source_sha256'}
    if set(spec) != required:
        raise ValueError("unexpected template specification fields")
    for key in ['address', 'size', 'patch_offset', 'max_immediate']:
        if type(spec[key]) is not int:
            raise ValueError("integer field required: " + key)
    address, size, offset = (spec[k] for k in ['address', 'size', 'patch_offset'])
    writers = spec['writer_pc']
    if (address < 0x80000000 or address + size > 0x81800000 or address & 3 or
        size < 4 or size > 4096 or size & 3 or offset < 0 or offset & 3 or offset > size - 4 or
        not 0 <= spec['max_immediate'] <= 32767 or not isinstance(writers, list) or len(writers) != 2 or
        any(type(w) is not int or w & 3 or address <= w < address + size for w in writers)):
        raise ValueError("unsupported template geometry/writers")
    dol = Path(dol_path).read_bytes()
    if digest(dol) != spec['dol_sha256']:
        raise ValueError("DOL hash mismatch")
    original = dol_bytes(dol, address, size)
    if digest(original) != spec['template_sha256']:
        raise ValueError("template hash mismatch")
    opcode = struct.unpack_from('>I', original, offset)[0]
    if opcode & 0xfc1fffff != 0x38000000:
        raise ValueError("only addi rD,0,0 is supported")
    writer_words = [struct.unpack('>I', dol_bytes(dol, w, 4))[0] for w in writers]
    if any(w >> 26 != 36 for w in writer_words):
        raise ValueError("writer must be stw")
    rel = Path(spec['source'])
    if rel.is_absolute() or '..' in rel.parts or rel.parts[:1] != ('chunks',) or rel.suffix != '.c':
        raise ValueError("source must be a relative chunk C path")
    source = generated / rel
    if source.is_symlink() or not source.is_file():
        raise ValueError("missing regular generated source")
    text_bytes = source.read_bytes()
    if digest(text_bytes) != spec['source_sha256']:
        raise ValueError("generated source hash mismatch")
    text = text_bytes.decode('utf-8')
    pc, rd = address + offset, (opcode >> 21) & 31
    label = f'label_{pc:08X}:'
    pattern = (rf'{label}\n    ctx->pc = 0x{pc:08X}u;\n'
               rf'    // {pc:08X}: li\s+r{rd}, 0\n'
               rf'    ctx->gpr\[{rd}\] = \(u32\)\(s32\)\(0\);')
    matches = list(re.finditer(pattern, text))
    if len(matches) != 1 or text.count(label) != 1 or 'mgx_exec_template_li' in text:
        raise ValueError("unsupported/duplicate generated instruction shape")
    replacement = (f'{label}\n    ctx->pc = 0x{pc:08X}u;\n'
                   f'    // Parametric AOT operand; validated private template profile.\n'
                   f'    ctx->gpr[{rd}] = mgx_exec_template_li(ctx, 0x{pc:08X}u);')
    text = re.sub(pattern, lambda _: replacement, text)
    token = f'mgx_template_instrumented_{pc:08x}_{digest(text_bytes)[:16]}'
    include = re.search(r'^#include [^\n]+\n', text, re.M)
    if not include:
        raise ValueError("missing generated include")
    declarations = (f'extern uint32_t mgx_exec_template_li(CPUState *, uint32_t);\n'
                    f'const uint32_t {token} = 0x{opcode:08x}u;\n')
    text = text[:include.end()] + declarations + text[include.end():]
    array = ','.join(f'0x{v:02x}' for v in original)
    header = (f'/* Private generated profile. Requires the instrumented chunk. */\n'
              f'#include "mgx_exec.h"\nextern const uint32_t {token};\n'
              f'static const uint8_t mgx_template_original[] = {{{array}}};\n'
              f'static const mgx_code_template mgx_private_template = {{\n'
              f'  0x{address:08x}u,{size}u,{offset}u,{spec["max_immediate"]}u,mgx_template_original,\n'
              f'  {{0x{writers[0]:08x}u,0x{writers[1]:08x}u}},\n'
              f'  {{0x{writer_words[0]:08x}u,0x{writer_words[1]:08x}u}},&{token}\n}};\n'
              f'#define MGX_TEMPLATE_PROFILE (&mgx_private_template)\n')
    # Input only contains generator output. Refuse links rather than silently
    # copying files outside its tree or importing extra backing data.
    files = sorted(p for p in generated.rglob('*') if p.is_file())
    if any(p.is_symlink() for p in generated.rglob('*')):
        raise ValueError("symlink in generated tree")
    if (generated/'profile').exists():
        raise ValueError("input already has a profile directory")
    hashes = {str(p.relative_to(generated)): digest(p.read_bytes()) for p in files}
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.template-', dir=output.parent) as tmp:
        stage = Path(tmp)/'module'
        shutil.copytree(generated, stage)
        (stage/rel).write_text(text)
        (stage/'profile').mkdir()
        (stage/'profile/template.h').write_text(header)
        report = dict(spec, instrumented_instruction=f'0x{pc:08x}', register=rd,
                      token=token, original_files=len(files), original_file_hashes=hashes,
                      changed_source_sha256=digest(text.encode()), copied_handler_execution=False)
        (stage/'profile/manifest.json').write_text(json.dumps(report, indent=2)+'\n')
        (stage/'profile/input-spec.json').write_text(json.dumps(spec, indent=2)+'\n')
        if output.exists():
            raise ValueError("output appeared during preparation")
        stage.rename(output)
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    for name in ['generated', 'dol', 'spec', 'output']:
        p.add_argument('--'+name, type=Path, required=True)
    a = p.parse_args()
    try:
        r = prepare(a.generated, a.dol, a.spec, a.output)
    except (OSError, ValueError, KeyError, TypeError, struct.error) as e:
        p.exit(1, str(e)+'\n')
    print(json.dumps({k:r[k] for k in ['instrumented_instruction','register','original_files','copied_handler_execution']}))

if __name__ == '__main__':
    main()
