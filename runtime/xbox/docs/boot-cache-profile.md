# B004 CPU-only boot/cache diagnostic profile

This is a bounded, one-shot diagnostic contract, not a general Wii machine or a
completed console boot environment. The strict profile remains available.

## Presets and references

The optional Wii CPU profile seeds MSR=0x2032, HID0=0x0011c664, and HID2=0xe0000000
from Dolphin CBoot::SetupMSR/SetupHID. L2CR starts zero, consistent with Dolphin
ResetRegisters. These are reference implementation presets, not a recovered
apploader handoff for any game. GPRs, low memory, BATs, FST placement, IOS,
interrupts, devices and disc services are not initialized by this profile.

Primary sources inspected:
- Dolphin Source/Core/Core/Boot/Boot_BS2Emu.cpp, blob
  a44796dd4f7819648011289d2a53afc232a0bd7a (SetupMSR/SetupHID).
- Dolphin Source/Core/Core/PowerPC/PowerPC.cpp, blob
  564d74d1f61705fe9c7fd5b7a7090c149292d21f (ResetRegisters).
- Dolphin Source/Core/Core/PowerPC/Interpreter/Interpreter_SystemRegisters.cpp
  (HID0 ICFI clears on write; HID0 DCFI is not similarly cleared).
- NXP MPC750 User Manual, MPC750UM, sections 3.1.5 and 9.1.4-9.1.5:
  https://www.nxp.com/docs/en/reference-manual/MPC750UM.pdf
  This is a related CPU reference, not proof of all Broadway-specific behavior.

## Supported abstraction

Execution is single-threaded with coherent RAM, no dirty data-cache copy, no
pending DMA/device operations, and immutable translated text. The CPU write
journal stops modifications to translated MEM1 text before writing. Executable
MEM2 remains rejected. No host cache-flush instruction is used to pretend to
simulate Wii hardware.

HID0 reads return actual profile state. Writes may change ICE/DCE or request
ICFI. ICFI performs an abstract invalidation epoch and reads back clear, as in
Dolphin's Gekko implementation. Other transitions, including DCFI changes and
cache locks, stop without modifying the register. DCFI's initial preset is
retained; loss of dirty lines is deliberately not implemented.

L2CR supports enable (0x80000000) and invalidate (0x00200000), with read-only
L2IP bit zero because there is no queued operation in this empty/coherent
abstract cache. A rising invalidation request completes synchronously and is
counted; the request remains until cleared by software. Invalidate plus enable
is rejected, as are SRAM/timing/test/configuration bits. This is not physical
invalidation timing and must not be used to claim cycle accuracy.

DCBST/DCBF/ICBI validate a complete mapped 32-byte line and order prior host
stores. They preserve RAM bytes. Ordinary-RAM DCBI is rejected because dirty
line discard is not modeled. DCBI of an already invalid locked-cache line is
accepted; a live line is rejected without destroying its data/tag. Unknown
SPRs, unknown instructions, unsupported memory and external control accesses
still stop. The dedicated CPU helpers perform their existing privilege checks.

The generator retains runtime boundaries, but charges their instructions now
that the callback returns into generated C. Those are existing scheduling
weights, not a claim of cycle-exact cache timing.

## Verification

17 synthetic profile cases cover 89 assertions in addition to the existing
23 execution cases (146 assertions). Generated-code tests independently verify
callback routing, effective addresses, readback, and privilege failure before
destination writes. Trace selection rejects explicitly truncated traces.
The narrower trace binary remains a diagnostic, never a whole-game optimizer.
