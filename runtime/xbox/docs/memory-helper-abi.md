# Experimental memory-helper calling convention

The native builder defaults to cdecl. --memory-abi fastcall opts into
DOLRECOMP_X86_FASTCALL=1 for every generated chunk, the CPU helper implementation
and runtime consumers. Only the eight memory load/store helpers change ABI;
SDK APIs and callback typedefs keep their existing calling conventions.

The builder records the chosen ABI and includes the definition in exact cache
keys. A cdecl cache cannot donate objects to a fastcall link. All sources and
header dependencies are checked again before linking. Do not alter recorded
hashes to reuse stale objects. ELF objects require a build-level ABI gate;
PE/COFF additionally decorates the helper names, rejecting mixed links.

Tests/test_memory_abi.py compiles real cpu.c and a separate i386 caller under
both conventions. Tests exercise every scalar width, big-endian bytes, MEM1
and MEM2 aliases, callbacks, reservations and wide boundary straddles. --qemu
uses a Pentium III CPU profile and Linux system calls, not the Xbox kernel.
Additional matching/mixed PE links and the non-i386 opt-in error are checked.
The native builder's synthetic integration test checks both ABIs and cache
isolation. These are not whole-game or hardware-execution tests.

An image-size reduction must be measured from completed full-code links. It
neither proves a frame-time gain nor solves the total 64 MB budget: kernel,
SDK, diagnostics, graphics, audio, MEM2 and assets need additional memory.
