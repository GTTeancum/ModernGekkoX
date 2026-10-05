# Bounded EXI initialization

`MGX_BOOT_WII_EXI` extends the stopped-audio reference profile. This is an
explicit no-external-cards configuration, not a captured retail boot state.
The old `--audio-only`, `--irq-only`, `--cpu-only` and `--strict` probes remain.

Initial channel status words are 0x800, 0x880 and 0. The insertion-event latches
on channels 0/1 remain set until acknowledged, even though no card is present.
Channel 1 starts with device-0 select set. Transfer controls start idle at zero.

Only aligned 32-bit accesses to Wii EXI status (offsets 0, 20, 40) and transfer
control (offsets 12, 32, 52) are recognized, at physical 0x0D006800 and uncached
0xCD006800. Other addresses, widths and raw-pointer requests fail.

Status writes preserve the insertion latch except for W1C acknowledgement.
IRQ masks, clock selection and device-0 CS are stored. Device 0 is explicitly
absent; no transfer or attached-card behavior is fabricated. Channel-0 ROMDIS
is stored, but boot-ROM access is still unsupported. Selecting other devices,
writing presence/reserved bits, live transfer flags, and nonzero transfer
control writes stop before mutation. There is no DMA or immediate transfer.

Latched EXTINT and its mask drive the EXI source in PI. A valid write producing
an enabled PI interrupt commits the state/event and stops before another guest
instruction. It does not pretend to deliver an interrupt. Live EXIINT/TCINT,
nonzero transfer control or an inconsistent PI source stop explicitly. All
accesses retain the bounded MMIO history and existing RAM/text guards.

Primary software references, pinned at Dolphin
0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8:
- https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/EXI/EXI_Channel.cpp
- https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/EXI/EXI_Channel.h
- https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/EXI/EXI_Device.cpp

This subset is not complete hardware fidelity. Copied interrupt vectors still
lack execution bindings. No save, network, controller or game-boot success is
implied by a successful register access.
