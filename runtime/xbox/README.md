# Xbox runtime bring-up

This directory is the writable Xbox-specific runtime layer. The original project
template and its upstream `lib/ModernGekko` gitlink are preserved. These files do
not depend on that desktop runtime or assume ABI compatibility with it.

## Implemented

- Allocation-free DOL validation and loading into caller-owned MEM1/MEM2 banks.
- Physical, cached and uncached RAM aliases; bounded, overflow-safe accesses.
- Section overlap and executable entry validation.
- BSS cleared before sections are copied: initialized data inside the BSS
  envelope is preserved. I/O failure invalidates the plan; callers must discard
  partially loaded memory rather than execute it.
- Eighteen synthetic-fixture host tests, with no game files in CI.
- An nxdk loader diagnostic XBE target. It reads `D:\main.dol`, validates/loads
  it, reports the result, and stops. It does NOT execute any guest code.

Run the host tests with `python3 runtime/xbox/tests/test_loader.py -v`.
Build the diagnostic with `NXDK_DIR=/path/to/nxdk` and nxdk's `bin` on PATH:
`make -C runtime/xbox/loader-probe -j2`.
The CI SDK is pinned to XboxDev/nxdk commit
`14d5ee97e73347c973f1f57b68b79ec08c9e77f2`, with its recursive submodules.

## Not yet implemented

No game execution loop, graphics/audio/input backend, Wii services, streaming
allocator, or native-game XBE link. Hardware behavior is untested.
The diagnostic's 24 MiB MEM1 allocation is not a game memory-budget solution.

The separate DolRecompX target probe found its 32-bit MSVC-ABI fence used SSE2;
a locked-exchange repair is tested there. Another execution blocker is the pinned
nxdk-pdclib `platform/xbox/functions/math/fma.c`: its functions assert and return
zero, rather than implement fused multiply-add. Do not claim exact floating-point
semantics from a compile pass or replace FMA with an unchecked multiply/add.

Keep game binaries, assets and generated game code out of this public repository.
