# Reference boot interrupt registers

`MGX_BOOT_WII_IRQ` explicitly extends `MGX_BOOT_WII_CPU`. It is a bounded
initialization model, not complete Wii device emulation, an authentic apploader
handoff, working interrupts, or evidence of Xbox execution. The startup probe
uses it by default. `--cpu-only` retains the earlier CPU-only boundary;
`--strict` retains the original strict boundary. The flags are mutually exclusive.

## Source and scope

Primary implementation references, pinned to Dolphin commit
`0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8`:

- `Source/Core/Core/HW/ProcessorInterface.cpp` and `.h`: PI register offsets,
  initial mask/cause, direct mask read/write, write-one-to-clear cause,
  big-endian halfword reads, and `(cause & mask)` pending logic.
- `Source/Core/Core/HW/WII_IPC.cpp`: PPC IRQ flags/masks, the initial Broadway
  mask, and the observed special mask-write reset behavior. No IPC request,
  reply, acknowledgement, GPIO, reset-device operation, IOS update or event
  scheduling is implemented here.
- `Source/Core/Core/HW/MemoryInterface.cpp`: 16-bit MI mask register at 0x1c,
  initialized to zero. Active memory protection/fault generation is absent.

Canonical reference URLs:
https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/ProcessorInterface.cpp
https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/ProcessorInterface.h
https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/WII_IPC.cpp
https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/MemoryInterface.cpp

The initial PI cause is **0x10100**, not zero: reset button unpressed and a
latched VI condition, from the reference implementation's initial state.
The PI mask is zero; PPC flags are zero and its mask is 0x40000000; MI mask
is zero. These are explicit reference presets, not measured retail boot state.
Device sources do not run or change these conditions during this one-shot probe.

## Accepted and rejected operations

PI cause/mask at physical 0x0c003000/4 permit 32-bit reads and aligned 16-bit
reads in big-endian order. Writes must be aligned 32-bit and use only the low
15 implemented IRQ bits. Cause writes clear selected latched bits. Reset-button
state is not a maskable IRQ and cannot be changed by these writes; attempts to
use that bit or reserved high bits are rejected rather than guessed.

A valid PI write commits its state and records the access. If it creates a
pending PI interrupt, the diagnostic immediately stops with
`unsupported-pending-pi-interrupt`. It preserves the cause/mask/pending values;
it does **not** clear the interrupt, fabricate handler execution, set a guest
exception, or execute following guest instructions. This is deliberately earlier
than physical interrupt delivery, even when MSR.EE is clear. Delivery, source
reassertion and timing require their own implementation.

At 0x0d000034, only writing the existing idle mask **0x40000000** is supported.
At 0x0d000030, zero or Broadway-bit acknowledgement is supported **only while
flags are zero and the initial mask is intact**. With no accepted IPC request
and no live state, the reference reset/IOS-update paths cannot have a pending
transaction to consume. Other masks, live flags, IRQ register reads and
non-word accesses stop. This is not a general Hollywood interrupt-controller
implementation or permission to ignore an active IPC reset.

At 0x0c00401c, only 16-bit reads and zero-to-zero mask writes are supported.
Nonzero masks or a live mask stop. MI fault status, protection registers,
32-bit paired-register accesses and active faults are not modeled.

Only these physical addresses and their explicit 0xcc/0xcd uncached windows
are recognized. No arbitrary top-bit aliasing, register-window RAM mapping,
external native pointer access or adjacent device-register fallback is allowed.
Unsupported MMIO stops before state changes. Existing text/template protection
continues to apply to RAM, unchanged.

## Evidence and limits

Successful accesses carry PC/address/value/width/direction in a bounded 64-entry
trace. Trace exhaustion stops **before** another access mutates a register; it
never silently truncates. These access counts are not instruction or timing
measurements. Tests cover widths, alignment, aliases, mask readback, W1C,
preserved pending causes, committed-interrupt stops, idle IPC restrictions,
MI restrictions, trace overflow, original profiles, and code protection.

No CPUState layout change, guest-code omission, generic SMC, copied-vector
binding, renderer, audio, input, or hardware execution is provided by this work.
