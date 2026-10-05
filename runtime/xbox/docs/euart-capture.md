# Bounded EUART capture profile

MGX_BOOT_WII_SERIAL extends the absent-SP1 profile; --probe-only preserves the
old default. The serial profile admits channel 0, chip-select 2, but NOT general
IPL/ROM/SRAM/RTC access. Its three commands are 0xb0000000 (EUART configuration
write), 0xb0000100 (FIFO output write), and 0x30000100 (FIFO queue-length read).
The controller shifts one to four bytes per immediate transfer, high byte
first. The first four written bytes form a command. A new device selection
resets the parser; writing the same select does not. Unknown commands, wrong
directions, other endpoints and DMA/duplex transfers stop before mutation.

Configuration bytes are recorded, not converted into invented register values.
Every non-NUL output byte is captured, including carriage return. NUL is padding
as in the reference. The host report serial_output_hex preserves the captured
bytes without JSON escaping or host encoding ambiguity. No kernel console or
peripheral is pretended to be present. Native code uses the same bounded sink.
FIFO queue reads return zero only because the synchronous sink has retained all
accepted output, and only while at least a 16-byte burst still fits. Capture
capacity is 2048 bytes. Overflow stops before the current transfer mutates
parser/output/control state; it does not truncate or overwrite old output.

The model is not a UART timing implementation, SRAM image, ROM provider, clock,
network adapter or saved-game service. Completion is synchronous. TCINT/W1C and
PI interrupt propagation retain the existing contract: a supported operation
that creates enabled PI pending state commits and logs it, then stops before
another guest instruction. No handler delivery is fabricated.

The serial profile has a bounded 4096-event MMIO history; old profiles retain
their 64-event limit. Dispatcher-history storage is 16384 entries. These buffers
are diagnostics and add allocation costs beyond image+MEM1. Full buffers stop
rather than silently allowing further unlogged device operations.

Primary reference: Dolphin 0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8,
Source/Core/Core/HW/EXI/EXI_DeviceIPL.cpp (SetCS, TransferByte/EUART,
UartFifoAccess), EXI_DeviceIPL.h, and EXI_Channel.cpp.
https://github.com/dolphin-emu/dolphin/blob/0961ec1d87a51b7ede57cc4df7629b7f92d0d9d8/Source/Core/Core/HW/EXI/EXI_DeviceIPL.cpp
The reference itself documents uncertain hardware details. This bounded
software behavior is not proof of original retail hardware state or timing.
