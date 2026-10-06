# Opt-in dormant SI polling configuration

`MGX_BOOT_WII_SI_POLL_DORMANT` extends `MGX_BOOT_WII_VI_CLOCK_NTSC` with
one bounded, stored SI_POLL word. Select it explicitly with the host option
`--si-poll-dormant` or the native builder's `--boot-profile si-poll-dormant`.
The SI-only extension preserves the other device contracts. The subsequent
[timebase safety guard](timebase-guard.md) applies to all profiles and stops
clock-dependent startup earlier; old end-to-end frontiers are historical. The new profile inherits reference NTSC VI clock 1,
EUART capture and the shared 4096-event MMIO trace.

The named software-reference initial word is `0x01EC0000`: X=492, Y=0,
EN=0, VBCPY=0 and reserved bits zero. This is not a measured retail/apploader
handoff or a verified physical reset state. Only the Y byte (bits 8..15) may
change. X must stay exactly 492; even otherwise inactive configurations with a
different X are rejected. The stored word is returned on reads, including all
previously accepted Y changes and idempotent writes.

Only aligned 32-bit reads and writes of physical `0x0D006430` and explicit
uncached `0xCD006430` are supported. These addresses share one state. C/CC
and D80/CD80 are real reference mappings but are outside this subset; arbitrary
address masking, neighboring registers and raw pointers are not admitted.

Before any accepted access, validate the current word against the same fixed-X,
Y-only predicate and reject an asserted PI SI cause or pending SI bit (`0x8`).
For a write, additionally validate the complete input, including high callback
bits. Width/alignment, state, value and capacity must all pass before state,
counters or trace mutation. Invalid accesses leave devices, RAM, reservations
and prior trace entries unchanged. Accepted accesses record the guest's
original PC/address, bus word, width and direction. They do not enter the RAM
write journal or clear a reservation. Existing committed PI mask writes still
stop on pending IRQs; no interrupt delivery is invented.

The reference POLL MMIO handler is direct word storage. Its X field later
feeds VI-driven scheduling. EN=0 does not prevent every reference device
callback: `UpdateDevices()` still visits each channel. This diagnostic has no
SI endpoint, scheduler or device-clock advancement, and fixes X precisely to
avoid introducing a new scheduling change. Supporting Y configuration does
not prove there are no controllers or implement a running SI device.

All CSR/status/data/output/buffer/clock-count operations remain unsupported.
There is no transfer, DMA, timer, polling event, completion, synthetic no-device
response, VBlank copy, controller data or SI interrupt generation. Future SI
state must become part of the invariant before broadening this profile.

## Pinned source mapping

All links use Dolphin revision `0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8`:

- [SI offsets and initialization](https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/SI/SI.cpp#L261-L287): zero POLL, then X=492. The preset name identifies this software provenance.
- [POLL bit layout](https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/SI/SI.h#L136-L153): VBCPY `0xF`, EN `0xF0`, Y `0xFF00`, X `0x03FF0000`, reserved `0xFC000000`.
- [Word read/write handlers](https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/SI/SI.cpp#L361-L391): POLL is full-word direct storage; neighboring CSR has independent transfer and IRQ behavior. Restricting X/EN/copy/reserved state is this implementation's bounded policy, not the reference's general register semantics.
- [Physical maps](https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/Memmap.cpp#L65-L86) and [mirror mapping](https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/MMIO.h#L25-L77): supported D-window plus explicitly excluded real aliases.
- [VI polling schedule](https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/VideoInterface.cpp#L903-L983) and [SI updates/X accessor](https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/SI/SI.cpp#L507-L567): callback scheduling is not disabled merely by EN=0.
- [SI IRQ derivation](https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/SI/SI.cpp#L99-L132) and [PI SI source](https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/ProcessorInterface.h#L33): source `0x8` must not be silently cleared by this latch.

Shared CPU timebase behavior is outside this register contract. Successful
host/native parity establishes agreement of that current CPU/device model; it
does not validate a progressing clock or authentic timing.

Synthetic tests cover all Y values, explicit alias sharing, idempotence, exact
trace/counter data, every X choice, each forbidden bit, all 256 callback widths,
misalignment, adjacent accesses, every previous profile, malformed state,
PI cause/pending, raw pointers and shared/full log capacity. Existing template,
serial and pending-PI tests remain active. Private full-module host/native
measurements are separate from the source-only tests. No gameplay is claimed.
