# FELF documentation

A FELF ("fat ELF") is one executable file that carries a program together
with everything it needs to run: its shared libraries, its own glibc and
dynamic linker, and the small runtime pieces that make the embedded copies
win over whatever the host has.  It runs on any Linux of the same
architecture, without installation and without extracting files to disk.

| Document | Contents |
|---|---|
| [architecture.md](architecture.md) | The components and how a FELF is built and started |
| [format.md](format.md) | File layout, footer, embedded database, object storage |
| [packing.md](packing.md) | `felf pack`: options, dependency resolution, checks |
| [runtime.md](runtime.md) | Bootstrap, loader handoff, audit module, runtime shim, direct mapping |
| [nix.md](nix.md) | The Nix packages, `bundle.nix`, `nix bundle`, tarballs, the OCI example |
| [security.md](security.md) | What FELF guarantees and what it does not |

The glibc patch FELF's loader is built with is described in
[`../loader/README.md`](../loader/README.md).
