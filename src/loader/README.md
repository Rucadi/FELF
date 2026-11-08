# FELF loader pieces

- `felf_audit.c` — the freestanding `LD_AUDIT` module embedded in every
  FELF: strict resolution, redirection of bundled names, host lookup for
  the graphics stack.
- `felf_runtime.c` — the runtime shim (`LD_PRELOAD`): `/proc/self/exe`,
  the caller's environment for child processes.

The patched glibc dynamic linker these work with is in
[`../glibc`](../glibc/README.md).
