# Fail-closed timebase access

All bounded startup profiles now stop on the first architecturally legal
PowerPC timebase read or write. No profile has a sourced initial timebase or a
guest clock/event model. Reading the zero-initialized storage previously let
startup consume an invented frozen clock. Stopping earlier is a correctness
repair, not a new regression to bypass.

The generic DolRecomp CPU hook `PPCTimebaseAccess` is an optional access policy,
not a clock provider. `ppc_mftb` invokes it for TBR 268/269, after rejecting every
other encoding with the original illegal-instruction path. `ppc_mtspr` invokes
it for legal write aliases 284/285 only after privilege checks. The callback
receives the exact register, read/write direction, zero for a read or the write
value, and original CIA. A returning callback permits existing storage behavior;
a rejecting callback must not return. A null callback preserves standalone CPU
behavior. No opcode signature, original guest instruction or generated call
needs changing. MFSPR 268/269 is a legal user-readable alias (Gekko Table 2-52) and uses the
same policy; it is not routed through the generic SPR provider. The prior CPU
helper incorrectly rejected those legal aliases; that inconsistency is repaired.

The mgx callback sets diagnostic PC to CIA and stops with
`unimplemented-timebase-read` or `unimplemented-timebase-write`. The stop address
is the TBR/SPR number, width is 4, and write value is retained. It creates no guest
exception and changes no GPR, timebase, reservation, RAM, device register,
accepted-access counter or journal. It is installed before dispatch for every
profile, including strict. No late-read exception, title PC bypass, fake tick,
function skip, successful transfer, or controller absence response is supplied.

`cpu_reset` preserves this optional policy like the other CPU hooks. The runner
saves/restores all CPU callback pointers and their user context together, plus
the existing global memory policies, on normal and nonlocal exits. The temporary
execution context is never left installed after the run. This is still a bounded
one-shot model; reset/reload CPU and memory for a fresh startup measurement.

## ABI and rebuild requirement

The hook is appended after `runtime_cpu`; all earlier offsets are unchanged.
On the verified x86-64 host CPUState grows 3544 -> 3552 bytes, with the new hook
at 3544. On the nxdk i386 target it grows 3480 -> 3488 bytes, hook at 3480.
All generated chunks, bridge, runtime, driver, compiler and tests must be rebuilt.
The existing module state-size check rejects the immediately preceding layouts.
Do not mix historical objects or snapshot ABI declarations with this build.
LLVM uses `offsetof`/`sizeof`, rather than a second hand-maintained CPU layout.
Its separate timebase emitter bypasses the CPU helper policy; LLVM modules are
not guarded or supported by this C-backend checkpoint. Enabling that backend
requires a separate policy integration and validation. The existing C emitter's MFSPR 268/269 alias routing is retained; synthetic
emitted execution tests prove both legal encodings reach the policy while
reserved MFTB encodings and privileged writes retain exception precedence.

## Historical evidence and scope

Earlier host/native and O0/Oz parity is still evidence of agreement with the
same historical model. It does not establish authentic elapsed time or a real
Wii boot handoff. Keep the historical reports, binaries and MEM1 unchanged.
A read inside a generated local jump can have an exact instruction PC that
differs from its dispatch trace entry. Current measurements must establish the
new earlier stop rather than assert old SI/device frontiers. Title-specific
addresses and measurements belong only in private validation reports.

A future clock requires justified guest-cycle accounting, a sourced starting
clock/boot handoff, correct read/write/rollover behavior and event ordering.
One increment per dispatch is not such a model. The dormant SI_POLL and stored
VI configuration tests remain useful isolated contracts; an earlier guarded
startup no longer claims to have reached those later interfaces.

## Primary legality reference

IBM Gekko User's Manual v1.2, Table 2-52 (printed page 2-62 / PDF page 116)
and note 2 (printed page 2-63 / PDF page 117) document user-readable MFSPR
268/269 and supervisor MTSPR 284/285. The note explicitly permits both MFTB
and MFSPR reads. The earlier research's claim that MFSPR268/269 is illegal was
inferred from a buggy helper and is superseded by this primary evidence.
Source: https://doc.kodewerx.org/documents/gekko_user_manual.pdf
