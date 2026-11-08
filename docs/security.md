# Security

FELF controls **which libraries a program loads**. It is not a sandbox: the
program runs with the caller's privileges and can read and run anything
they can.

```mermaid
flowchart LR
    subgraph felf[from the FELF]
        P[program] --- L[bundled libraries, glibc]
    end
    subgraph host[from the host]
        K["kernel, /proc"]
        G[graphics stack]
        X[files the program opens]
        U["unbundled names<br/>(permissive only)"]
    end
    P --> K & G & X
    P -.-> U
```

## Guarantees

- **Bundled libraries win.** The caller's `LD_PRELOAD`, `LD_LIBRARY_PATH`,
  `LD_AUDIT` and other `LD_*` are removed before the bundled loader starts;
  the shim gives them back to child processes. A bundled name always
  resolves to the bundled copy.
- **Strict mode** (default): only bundled objects, files under the bundle
  root and the host graphics stack may load.
- **Nothing on disk.** Objects become memfds or are mapped read-only from
  the FELF itself.
- **Nothing leaks to children.** FELF's descriptors (998 and up) are
  close-on-exec once the program runs.
- **No store paths.** `/nix/store` paths in embedded objects are scrubbed,
  so they can't resolve to a host's store.

## Limits

- **Integrity, not authenticity.** Every object has a SHA-256 that
  `felf verify` checks, but the file isn't signed: whoever can modify it
  can update the hashes. The bootstrap checks the layout at every start,
  not the hashes.
- **Host code runs inside the program**: the graphics stack and whatever it
  loads (`--no-default-host-libs` turns this off), and in permissive mode
  any name the FELF doesn't bundle.
- **`/proc/self/fd` is required.**
