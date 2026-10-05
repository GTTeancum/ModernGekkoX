# Explicit absent-SP1 EXI probe

`MGX_BOOT_WII_EXI_PROBE` extends the existing status-only EXI preset.
`--exi-only` preserves the old diagnostic boundary. This is a bounded software
reference, not a captured Wii boot state or cycle-accurate EXI controller.

## Endpoint and transfer contract

Channel 0, chip-select value 4 (status bit `0x200`), is explicitly configured as
**None**. It is not a catch-all for unsupported devices. The IPL/RTC/SRAM endpoint
(chip-select value 2), multi-select configurations, and active card/network
operations remain rejected before mutation. Existing no-card presence states and
insertion-event latches are retained.

The new endpoint accepts aligned 32-bit status, control, and immediate-data
register accesses in the Wii physical/uncached windows. The data register stores
the written word. An immediate read or write shifts 1–4 bytes, as encoded by
TLEN. For the explicit None endpoint, outgoing bytes are discarded and incoming
bytes are zero, matching the cited base-device implementation. Read/write duplex
mode, DMA, unknown bits, other endpoints, and wrong widths remain unsupported.
A write without TSTART stores control only and does not report a transfer.

A supported TSTART operation completes synchronously in this reference model:
it clears TSTART, latches TCINT, increments transfer/byte counters, and updates
the modeled PI EXI source. TCINT survives a zero write and is cleared by W1C.
If that committed operation produces an enabled PI interrupt, execution stops
with the committed state and event intact. No interrupt handler is invented.
The diagnostic still rejects copied-vector dispatch. Existing/inconsistent live
state and trace-capacity failures do not silently change registers.

## DI configuration dependency

The profile also exposes only the read-only Wii DI configuration register at
`0x0D006024` / `0xCD006024`. Its stored value is the pinned reference preset 1
(boot-ROM descrambler disabled). This is **not** disc presence, successful disc
I/O, or an IOS response. Other DI registers, writes, and unsupported widths fail.
No reset, disc command, DMA, or streaming behavior is supplied by this addition.

## Primary references

Dolphin revision `0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8`:

- `Source/Core/Core/HW/EXI/EXI_Device.cpp`: `EXIDeviceType::None` constructs
  `IEXIDevice`; `ImmRead` starts each byte at zero and the base `TransferByte`
  leaves it unchanged. `ImmWrite` shifts the most-significant byte first.
- `Source/Core/Core/HW/EXI/EXI_Channel.cpp`: immediate length/direction,
  synchronous completion for devices without delayed completion, TSTART clear,
  TCINT latching, status W1C, and chip-select handling.
- `Source/Core/Core/HW/EXI/EXI_Channel.h`: status/control bit positions.
- `Source/Core/Core/HW/DVD/DVDInterface.cpp`: `Init` sets CONFIG=1;
  `RegisterMMIO` exposes the configuration register read-only.

Repository: https://github.com/dolphin-emu/dolphin/tree/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW

Tests use synthetic inputs only. This does not provide the IPL, SRAM contents,
calendar/time service, save storage, network adapter, or Xbox hardware evidence.
