# Direct Xbox integration with nxdk

The new `lib/nxdk` submodule pins XboxDev/nxdk at
14d5ee97e73347c973f1f57b68b79ec08c9e77f2, the SDK revision already used for
our diagnostic XBEs. Existing DolRecomp and ModernGekko gitlinks are unchanged.
Only initialize this submodule for Xbox-only development:

    git submodule update --init --recursive lib/nxdk

SDK source is a dependency, not a replacement for Wii platform translation.
Its kernel interfaces, graphics/input/audio libraries and examples are the
Xbox-side foundation. No SDK source edits or permissive Wii status stubs are
introduced here.

## Reusable build command

`tools/build_xbox_probe.py` replaces checkpoint-specific build scripting for
new builds. It accepts an SDK checkout, compiler checkout, generated module,
optional template profile and output directory. Every supplied chunk is linked.
The SDK libraries and cxbe must have been built; CI builds them with the
existing loader/math probes. A restored prebuilt library set must match its
recorded SDK source revision.

    python runtime/xbox/tools/build_xbox_probe.py \
      --sdk lib/nxdk --compiler-source /path/to/DolRecompX \
      --generated /private/generated \
      --template-profile /private/generated/profile/template.h \
      --output /private/xbox-build --optimization z --jobs 4

The SDK wrapper/critical header hashes are checked against `nxdk.lock.json`.
These are compatibility checks, not authentication of every SDK file. A reuse
report is accepted only with matching compiler identity, exact compile flags,
source and all recorded header dependencies and object hashes. Old reports
without compiler identity are not imported. Repeated builds may use
`--reuse-report /private/xbox-build/build-report.json` with another output.
Link inputs are rechecked and hashed. The PE timestamp is fixed for repeatable
PE comparisons; cxbe may still stamp the XBE with its generation time.

## Native RAM measurement

`mgx_nxdk_memory.h` calls nxdk's Xbox kernel `MmQueryStatistics`. Before guest
allocation, the target probe reports physical total/available/image pages and
checks the lower bound for MEM1 plus CPU/diagnostic records. Query failures
are reported as unknown, never interpreted as a zero-RAM measurement.

This is not a complete allocation guarantee: fragmentation, rendering, MEM2
and other costs are additional. It does not reduce code size, page out code,
or make an over-budget image fit 64 MB. No Xbox hardware measurements are
claimed merely because the native call compiles and links.

Synthetic tests cover the query glue, cache rejection and two real nxdk links
with complete object reuse and identical PE bytes. Those XBEs are not executed
by the test. Actual hardware execution remains a separate milestone.

## Static image memory audit

`tools/audit_pe_memory.py main.exe` validates an I386 PE32 image and reports its
mapped sections plus an explicit image+MEM1 lower bound. The Xbox builder embeds
this audit in `build-report.json`. The default comparison is against 64 MiB;
this is a planning baseline, not detected user hardware. It does not subtract
allocations already made by the running kernel, inspect fragmentation, or
account for additional MEM2, asset, GPU, heap and stack use. `fits_hardware` stays
unknown even if the lower bound is below the baseline. Actual memory reporting
continues to require executing the native MmQueryStatistics path on an Xbox.
