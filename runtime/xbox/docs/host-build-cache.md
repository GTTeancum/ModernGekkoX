# Complete host diagnostic build cache

`tools/build_host_probe.py` builds every generated C chunk plus the runtime.
It is a developer-side diagnostic build tool, not a separate PC port target.
Supply `--compiler-source`, `--generated`, and `--output`; no game files are
embedded in this tool or sent to CI.

For each translation unit, reuse requires the same compiler binary hash/version,
exact compiler argv, working directory, source path, every compiler-reported
`-MD` dependency hash (including system headers), and object hash. Missing or
changed input rebuilds; a failed compile removes both object and cache record.
The previous linked diagnostic is removed at build entry, and successful linking
uses a temporary output renamed to `startup-probe`. No old checkpoint objects
are silently imported. All chunks remain part of the link.

The first build is intentionally clean. Later builds reuse only validated local
records. Compiler, flags, source, header, object corruption and absent/malformed
records are tested with synthetic C, including paths containing spaces.

Use `--optimization 0` for the diagnostic baseline. Other supported compiler
optimization levels are experiments; matching one startup report is not proof
of full-game equivalence or a hardware performance measurement. Builds only
report compilation/linking, never infer execution or game boot.
