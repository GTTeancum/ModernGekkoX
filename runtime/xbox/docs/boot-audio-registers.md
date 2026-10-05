# Stopped audio control registers

`MGX_BOOT_WII_AUDIO` extends `MGX_BOOT_WII_IRQ` with two control registers. The
startup probe selects it by default and reports `reference-wii-audio-idle`.
`--irq-only`, `--cpu-only`, and `--strict` preserve older execution profiles.
This is an explicit reference preset, not a captured retail/apploader state.

## Exact supported contract

- DSP control: aligned 16-bit accesses at physical `0x0c00500a` or uncached
  `0xcc00500a`. The initial word is `0x0804`: Halt and Init are set. This is
  the combined DSP-manager/DSPHLE reference state, NOT merely the manager's
  `0x0004` field and NOT evidence that an audio program has run.
- The three interrupt masks (`0x0150`) are stored and read back. Write-one-to-
  clear bits (`0x00a8`) acknowledge only already-clear status, and never become
  stored status bits. Reset, assert-interrupt, unhalt, Init changes, InitCode,
  DMA activity and reserved/high bits stop before state or log mutation.
- AI control: aligned 32-bit accesses at physical `0x0d006c00` or uncached
  `0xcd006c00`. The initial value `0x42` represents stopped playback with
  unchanged AIS48/AID32 rate selectors, as in the reference initializer.
  AIINTMSK (`0x4`) and AIINTVLD (`0x10`) are stored. AIINT (`0x8`) is an
  acknowledgment of an already-clear flag. Playback enable, rate changes,
  sample-counter reset and other bits are unsupported.

The reader returns current stored state; it is not an unconditional status
constant. A current DSP status outside Halt/Init plus masks, AI status outside
its fixed rates plus the two supported controls, or an already-asserted PI
DSP/AI source causes `unsupported-active-audio-state`. Those flags are never
silently cleared to continue. No source-assertion API is implemented here.

Both registers share the existing bounded MMIO event log. Capacity is checked
before state changes. Accepted reads/writes log their original guest address,
PC, width, direction and bus value, with separate DSP/AI counters. Accepted
MMIO stores do not masquerade as RAM stores or invalidate RAM reservations.
Adjacent registers, alternate aliases and raw host-pointer access remain
unsupported. The existing text-write, template and pending-PI-interrupt stops
are retained by the new profile.

There is no DSP microcode execution, reset handshake, mailbox communication,
ARAM/audio DMA, sample clock, audio rendering, interrupt delivery or timing
fidelity. The source state can be expanded only together with its downstream
behavior; accepting an active control write is not a substitute for that work.

## Primary behavior references

Pinned Dolphin revision: `0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8`.

- DSP bit layout and mask:
  https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/DSP.h
- Combined register read, mask storage and W1C flags in RegisterMMIO:
  https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/DSP.cpp
- Initialize and control transitions, including why active resets/Init changes
  cannot be treated as plain register storage:
  https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/DSPHLE/DSPHLE.cpp
- AI bit layout, Init, and RegisterMMIO:
  https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/AudioInterface.h
  https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/AudioInterface.cpp

These references describe software models with stated approximations; this
bounded implementation does not claim complete or measured hardware fidelity.
