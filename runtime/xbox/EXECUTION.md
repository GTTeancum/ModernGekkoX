# Bounded generated-code diagnostic

This layer integrates the C-backend CPU state with the bounded DOL loader.
It is not a working Wii compatibility runtime or a claim of game boot.

Use the pinned CPU source from `GTTeancum/DolRecompX` commit
`40b87e284766b9edf4ffcabd4cd9271279514059`. The old template submodules
are not used. Compile `startup-probe/bridge.c` with
`MGX_GENERATED_HEADER` naming your privately generated header, and link its
unmodified C chunks, `cpu.c`, `mgx_dol.c`, `mgx_exec.c`, and `mgx_math.c`.
Host builds compile `cpu.c` with `-include mgx_math_redirect.h`; Xbox builds
resolve the required standard math names to `mgx_math.obj`. Keep all game-derived
code, executables, traces, and output objects out of this public repository.

## Exact scope

The entry state is deliberately identified as zero registers/low memory plus
loaded DOL sections and BSS, not an authentic Wii apploader environment.
MEM1 receives 24 MiB for this diagnostic; this is not a final memory plan.
The runner has a dispatch-quantum budget; that is NOT an instruction count.
Generated back-edge yield guards bound each quantum. The CLI must also be run
under a process timeout during development.

Unknown instructions, special registers, devices, cache callbacks, external
pointers, and writes to translated text stop immediately with diagnostics.
No unsupported operation is reported as successful. An omitted dispatch entry
returns failure. The CPU must be discarded after this one-shot run. Only one
CPU/thread may run at once. The global write journal and call-depth counter are
restored on exit. Executable MEM2 sections are refused because this pinned CPU
write journal does not cover them. This is not general SMC support.

`tools/make_trace_bridge.py` creates a smaller *trace-restricted diagnostic*.
It permits only the observed dispatcher entries and keeps the corresponding
unmodified chunks. Every other entry fails closed. It does not establish that
unobserved game code is unnecessary, and it must not replace the complete-game
build when claiming a playable port. Its generated bridge and selection file
are game-derived and remain private.

## Math boundaries

`mgx_math.c` supplies a correctness-first binary64 fused operation using exact
integer accumulation and one rounding, plus required rounding/CRT repairs.
The accumulator is intentionally not the final performance implementation.
Finite FMA results are checked against both unbounded Python integer arithmetic
and host libm, in all four rounding modes. Synthetic vectors also run in a
freestanding i386/Pentium III build under QEMU. That is not Xbox hardware testing.

These functions do not raise host floating-point exception flags or provide
errno compatibility. Guest exception and NaN bookkeeping remain in the pinned
DolRecomp helpers. Complete PPC floating-point equivalence is not established.
Only required double-precision CRT entry points are overridden, not the entire
SDK math library. No unchecked `x*y+z` replaces fused arithmetic.

## Source-only regression tests

```
python runtime/xbox/tests/test_loader.py -v
python runtime/xbox/tests/test_math.py --output math-validation.json
python runtime/xbox/tests/run_exec_tests.py --compiler-source path/to/DolRecompX --sanitize
python runtime/xbox/tests/test_trace_bridge.py -v
```

`startup-probe/main.c` opens its DOL argument on the host, or `D:\main.dol` on
Xbox. A completed diagnostic deliberately reports `game_booted:false`.
