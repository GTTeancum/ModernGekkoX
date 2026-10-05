# Inactive performance-monitor diagnostic profile (B005)

This extends the existing CPU-only profile; it is not a Wii boot handoff or
a cycle-accurate performance monitor.

## Evidence

The primary reference is Dolphin `PowerPC::UpdatePerformanceMonitor` at commit
`0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8`, in
`Source/Core/Core/PowerPC/PowerPC.cpp` (blob
`564d74d1f61705fe9c7fd5b7a7090c149292d21f`), where event selector zero
does not increment any of PMC1..4. `MMCRUpdated` disables its performance-monitor
path when MMCR0 and MMCR1 are both zero. `ResetRegisters` clears SPR storage.
This is reference implementation evidence for a deliberately restricted model,
not proof of full Broadway hardware fidelity.

Reference:
https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/PowerPC/PowerPC.cpp

## Contract

- MMCR0 (952) and MMCR1 (956) accept only zero. Every nonzero value, including
  event selectors, interrupt controls, freeze bits and reserved bits, stops
  before committing the write. A future active mode needs event accounting.
- PMC1 (953), PMC2 (954), PMC3 (957) and PMC4 (958) preserve their written
  32-bit values. They do not increment because neither control can select an
  event. Writes to a control register do not reset stored counters.
- Supervisor access uses the existing CPU helpers and their privilege checks.
  User aliases and SIA remain unsupported; user-mode alias access is not fixed.
- State is reset at the start of the existing one-shot execution profile.
  The strict profile retains its old unsupported-register stops.
- Report counters record accepted host callbacks, not guest instruction/cycle
  counts. Real-game initialization and traces remain private.
- The diagnostic reports SRR0/SRR1/MSR on exceptions so the originating
  instruction is not confused with the exception-vector address.

## Validation

25 synthetic inactive-PMU cases exercise all four counter values/readback,
control resets, inactive persistence across dispatches, rejected configuration
writes with preserved state, six privileged read/write paths, strict-profile
behavior, and unsupported aliases. These add 226 checks to the prior 235.

No instruction or game check is skipped; no active counters or interrupts are
reported as implemented. Authentic apploader/low-memory/FST handoff and HID4
semantics remain separate work.
