# Opt-in VI clock snapshot

`MGX_BOOT_WII_VI_CLOCK_NTSC` extends the existing bounded EUART profile with a
named software-reference NTSC VI clock snapshot of 1 (54 MHz).
`MGX_BOOT_WII_VI_CLOCK_27MHZ` explicitly selects snapshot 0 (27 MHz). The host
options are `--vi-clock-ntsc` and `--vi-clock-27mhz`; no option still selects
exactly the prior EUART profile. Older profiles do not gain any VI access.
The native builder selects these with `--boot-profile vi-clock-ntsc` or
`--boot-profile vi-clock-27mhz`; omitted/`serial` retains its default. The
choice is an explicit, content-keyed main-source compile definition, rather
than a change to generated code or the SDK.

These are diagnostic configuration choices, not measured retail/apploader
handoff states, universal reset values, video modes, or evidence of a frame.
The 27 MHz selection alone does not establish PAL or any particular TV mode.

Only aligned 16-bit reads of physical `0x0C00206C` and uncached `0xCC00206C`
are supported. The returned halfword is stored snapshot state; accesses retain
the caller's PC and original address in the common 4096-event MMIO log.
Capacity is checked before read counters or trace state change. RAM and its
reservation remain unchanged. Values outside 0/1 are rejected as invalid
snapshot configuration, a diagnostic restriction rather than a claim about
hardware reserved bits.

The real/reference register is writable. This bounded implementation rejects
all writes with `unsupported-vi-clock-write`, since the consequences of clock
changes are not modeled. It also rejects byte/wide/unaligned accesses, adjacent
registers (including DTV status), unverified aliases, and raw-pointer access.
A paired 32-bit read would require DTV semantics and is deliberately unsupported.
There is no VI timing, scanout, rendering, interrupt generation, SI polling,
controller, or generalized register-storage implementation here.

## Primary-source provenance

Pinned Dolphin revision `0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8`:

- [VI register definitions](https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/VideoInterface.h#L82): clock offset `0x6C`, separate DTV status `0x6E`; the `u16` clock field at line 436 documents 0/1 for 27/54 MHz.
- [Physical map](https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/Memmap.cpp#L65-L86): VI base `0x0C002000`; Wii does not move it to `0x0D002000`.
- [Reference boot preset](https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/VideoInterface.cpp#L159-L168): `m_clock = DiscIO::IsNTSC(region)`.
- [Halfword read/write](https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/VideoInterface.cpp#L265-L283): stored value and `UpdateParameters()` on writes.
- [Wider reference accesses](https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/VideoInterface.cpp#L419-L431): the reference also implements bytes and paired halfwords; those are outside this subset.
- [Clock use](https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/VideoInterface.cpp#L760-L767): bit 0 selects the clock for sample/half-line calculations. This does not establish physical reserved-bit behavior.

The source-only aggregate tests cover both configurations/aliases, all callback
widths, neighboring registers, invalid snapshot bits, exact event data,
capacity failure before mutation, original profile boundaries, and unchanged
RAM/reservation state. The complete serial and template suites also run under
both new profiles. Full game-derived host/native evidence remains private and
is separate from synthetic tests; no gameplay or authentic boot is claimed.
