# Byte-immutable translated text: scalar store contract

The fixed Wii CPU diagnostic permits a scalar store overlapping translated
MEM1 text only when **every byte of the entire 1/2/4/8-byte store equals RAM**.
The strict profile rejects even an identical text store. No template range is
reclassified as data and no changed instruction is accepted.

The new optional DolRecomp `PPCMemWriteCheck` runs after the direct RAM helper
has resolved the whole access, and before reservation invalidation, the legacy
journal callback, or the write. Its address is the original guest effective
address and its value contains the low `size*8` bits to be written big-endian.
An aborting policy does not return. The existing journal signature and CPUState
layout are unchanged. This API is for this scalar C runtime, not LLVM modules.

External memory providers own their policy. The Xbox diagnostic's external
write callback validates the complete access and applies exactly the same
check before writing. Thus physical/cached/uncached MEM1 aliases cannot evade
protection; MEM2 is not confused with MEM1. Executable MEM2 remains refused.
Unmapped access, invalid external widths and a differing byte anywhere in a
text-overlapping store stop before RAM changes. A store touching text plus data
is conservatively rejected if *any* byte differs, including the data portion.

An accepted same-byte store is **performed**, not optimized away. Reservation
invalidation covers every overlapping 32-byte line in the fixed RAM alias model
including physical aliases and a wide scalar store crossing a line boundary.
This is not a new general MMU mapping model. Individual scalar transactions
are checked; multi-store instructions are not promised instruction-wide rollback.

The one-shot runner temporarily owns both process-global observer hooks and
restores them on all dispatched exits. It disables the legacy value-less
journal during its run, replacing its text check with the value-aware policy.
Serialized, single-threaded use and discarding CPU callbacks after a run remain
required. General DMA, writable host pointers and concurrent stores are absent.

Keeping bytes unchanged preserves the current AOT/text relation in the existing
coherent-cache abstraction. It does not implement real cache timing, dirty-line
loss, dynamically copied code, or general self-modifying code. A changed template
must not execute through stale translated labels; that problem remains separate.

Tests include 213 runtime store cases (3,124 checks), 38 compiler policy cases
(596 checks), legacy suites and sanitizer runs. The host probe optionally exports
MEM1 with `--dump-mem1 path`; exclusive creation prevents overwriting an existing
input or evidence file. Seven synthetic CLI cases exercise bytes, strict mode,
invalid flags and existing-file/symlink protection. Dumps are private diagnostics,
not game boot evidence, and must never be published with game data.
