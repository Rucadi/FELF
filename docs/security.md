# Security

## What a FELF guarantees

- **Integrity, not authenticity.**  Every object has a SHA-256, and
  `felf verify` checks them all.  The file is not signed: anyone who can
  modify it can also update the hashes.  The bootstrap checks the layout at
  every start, not the hashes.
- **Bundled libraries win.**  The caller's `LD_*` variables (`LD_PRELOAD`,
  `LD_LIBRARY_PATH`, `LD_AUDIT`, ...) are removed before the bundled loader
  starts, so they cannot inject or replace libraries; the shim gives them
  back to child processes.  A bundled name always resolves to the bundled
  copy.
- **Strict mode** (the default) is a library-resolution policy: only bundled
  objects, files under the bundle root, and host-preferred libraries (the
  graphics stack) may be loaded.  It is not a sandbox: the program runs with
  the caller's privileges and can read and execute anything they can.

## What comes from the host

- **The graphics stack** (libglvnd, Mesa, Vulkan, DRM, VA-API, NVIDIA),
  looked up the way the host's loader would.  These libraries, and the
  dependencies they load, run inside the program.  `--no-default-host-libs`
  turns this off.
- **In permissive mode**, any name the FELF does not bundle.
- **The kernel, `/proc`, and files the program opens itself.**  FELF
  requires `/proc/self/fd`.

## Files and descriptors

- Nothing is extracted to disk: embedded objects become memfds, or are
  mapped read-only from the FELF file itself.
- The FELF's descriptors (998 and up) are close-on-exec once the program
  runs, so child processes do not inherit them.
- `/nix/store` paths inside every embedded object are rewritten,
  so they cannot resolve to a host's store.
