# FELF documentation

A FELF is one executable file holding a program, its libraries, its own
glibc and a small runtime. It runs on any Linux of the same architecture,
without installing or extracting anything.

| Start here | |
|---|---|
| [architecture.md](architecture.md) | the pieces, and how a FELF is packed and run |
| [packing.md](packing.md) | `felf pack`: options, what gets embedded, compression |
| [runtime.md](runtime.md) | start-up, library lookup, the shim, environment variables |

| Reference | |
|---|---|
| [format.md](format.md) | file layout, footer, the SQLite index |
| [security.md](security.md) | what FELF guarantees, and what it doesn't |
| [cmake.md](cmake.md) | packing from a CMake project |
| [nix.md](nix.md) | Nix packages, the OCI example, checks |
| [building.md](building.md) | building FELF and running its tests |

The loader patch is described in [src/glibc/README.md](../src/glibc/README.md).
