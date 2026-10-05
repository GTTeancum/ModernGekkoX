# Bounded Broadway HID4 profile

This is a fixed-configuration diagnostic contract, not a complete Broadway MMU.

DolRecomp's CPUState now explicitly selects Gekko (zero/default) or Broadway.
HID4/SPR 1011 reaches a host callback only in supervisor Broadway mode. User-mode
access raises the existing privilege exception first; absent callbacks and
Gekko/unknown variants remain illegal. cpu_reset preserves the selected variant.
CPUState has changed: rebuild all consumers. On x64 its size grows from 3536 to
3544 bytes, so the existing module size gate rejects the prior x64 layout. On the
nxdk i386 target the field occupies tail padding at offset 3476: both old and new
sizes are 3480. A size-only gate does NOT identify stale i386 modules. B006 uses
fresh whole-program builds with dependency hashes, not loadable old modules.
Explicit module ABI/revision gating is required before supporting mixed-version
i386 modules. LLVM-module integration is not validated here.

The Wii CPU profile presets HID4 to 0x83900000, from the pinned Dolphin boot
setup. It accepts a read or a write of that identical value only. Every single-bit
change is rejected before updating state, including SBE, ST0 and cache-related
bits. Readback and accepted-write counters are observable. A same-value write
cannot change BAT activation, translation configuration or cache policy; it does
not synthesize an invalidation completion event. The existing coherent/single-thread
memory abstraction and code-write guard stay in force.

The fixed physical/cached/uncached aliases remain those provided by existing
memory helpers. Tests exercise backed MEM1 and MEM2 aliases and reject unbacked
MEM2. SBE is not permission to allocate missing memory or bypass protection.
Dynamic BAT/page-table changes, physical cache timing, devices and an authentic
apploader handoff are not implemented. The strict profile grants no HID4 behavior.

Primary implementation references, Dolphin commit
0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8:
- Source/Core/Core/PowerPC/Gekko.h: UReg_HID4, especially SBE and ST0 fields.
- Source/Core/Core/Boot/Boot_BS2Emu.cpp: CBoot::SetupHID and SetupBAT.
- Source/Core/Core/PowerPC/MMU.cpp: extended BAT activation depends on Wii and SBE.
- Source/Core/Core/PowerPC/Interpreter/Interpreter_SystemRegisters.cpp: changed
  HID4 requires instruction/data BAT updates; privilege checks precede access.
- Source/Core/Core/PowerPC/PowerPC.cpp: Broadway PVR 0x00087102.

These references informed the contract; no Dolphin implementation was copied.
Forty synthetic runtime cases cover fixed readback, repeat writes, all 32 bit
changes, privilege failures, strict mode, invalid prior state, aliases and bounds.
