# Parametric instruction templates (bounded C diagnostic)

This opt-in profile supports one `addi rD,0,imm` instruction inside one loaded
MEM1 text template. It is not general self-modifying code or a CPU interpreter.

`prepare_template.py` consumes a PRIVATE specification and original DOL. It
checks the DOL, template and generated-chunk SHA-256 hashes, and validates a
single expected internal instruction label. It copies the complete generated
module, replacing only that instruction's constant operand with
`mgx_exec_template_li`. Entry through the dispatch switch, an internal branch,
or ordinary fallthrough therefore reaches the same memory-aware operand.
A token symbol defined only in the instrumented chunk is referenced by the
private profile. Linking that profile against the original unmodified chunk
fails, instead of silently accepting writes while executing the old constant.

The runtime validates the initial complete template and both writer instruction
words before execution. A differing store must be an aligned 32-bit write to
exactly the registered word, from one of the two registered writer PCs, with
unchanged opcode/RA/destination fields and an immediate in 0..max (max<=32767).
The rest of the complete template must remain original. All other text changes
retain the existing hard stop. Actual accepted stores preserve RAM writes and
reservation invalidation. The helper validates the template on execution and
returns the current immediate; the generated instruction assigns its original
fixed destination register. No CPUState layout or compiler ABI change is made.

The supplied diagnostic uses the existing coherent, serialized, CPU-only RAM
abstraction. It does not model physical instruction-cache timing. The config,
original bytes and token have to outlive the one-shot run; they are trusted
native inputs, not arbitrary guest pointers. Concurrent execution, DMA and
arbitrary writable host pointers remain outside the contract.

Copying the template elsewhere does NOT register copied bytes for execution.
The normal complete-code dispatcher must still reject unbound vector addresses.
This change permits verified construction and original-site parametric execution,
not working interrupts, relocated-handler execution or authentic boot state.
A future copied-code binding needs its own byte checks and validated semantics.

Tests include rejected configurations/stores, cached/uncached/physical aliases,
all admitted immediate values, template restoration, and sanitizer-enabled
compilation of a synthetic generated chunk entered by three control-flow paths.
The original stale chunk is also deliberately linked and must fail on its token.
Game binaries, specifications, prepared C and snapshots must stay private.
