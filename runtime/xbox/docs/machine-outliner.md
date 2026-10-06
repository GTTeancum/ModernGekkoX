# Experimental machine outlining

Both complete-module builders accept `--machine-outliner`. It is disabled by
default. With this option, every compilation uses LLVM's
`-mllvm -enable-machine-outliner=always`, which replaces repeated native
instruction sequences with calls to compiler-generated helper functions.
The generated C files, guest entry points, dispatch tables, guest loop budget,
and unsupported-operation policy are unchanged. No generated chunks are pruned.

This is an experimental backend option, verified with Clang/LLVM 19.1.7. Other
compiler versions need their own validation. It can shrink an image at the cost
of additional native calls; a smaller image is not a speed improvement or proof
that a game fits in memory. Always retain the image-plus-MEM1 lower-bound audit
and measure the running target's actual available RAM.

Example (private generated module and profile paths are caller supplied):

```sh
python3 runtime/xbox/tools/build_xbox_probe.py \
  --sdk /path/to/nxdk --compiler-source /path/to/compiler \
  --generated /path/to/generated --output /path/to/native-outline \
  --optimization z --memory-abi fastcall --machine-outliner

python3 runtime/xbox/tools/build_host_probe.py \
  --compiler-source /path/to/compiler --generated /path/to/generated \
  --output /path/to/host-outline --optimization z --machine-outliner
```

Add the same `--template-profile` used by the corresponding baseline when
applicable. Keep separate output directories for baseline and experimental
builds. The `machine_outliner` report field records the option; exact compiler
arguments are included in dependency/cache keys. Switching outlining on or off
invalidates cached objects, while an unchanged build can reuse verified objects.
The SDK ABI and selected memory-helper ABI do not change.

Before adopting an outlined image for a title, compare the complete generated
chunk count and entry-symbol coverage, bounded host diagnostic reports and RAM
snapshots, and target-native execution. Include mathematical rounding/FMA and
memory-helper callback/straddle checks on the target, since host-only checks
cannot validate its i386 calling convention. A diagnostic reaching its existing
unsupported-operation frontier is not a game frame or a gameplay result.
