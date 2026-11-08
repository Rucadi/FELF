# Nix

Nix is how this repository builds and tests FELF. You don't need it to use
FELF.

`flake.nix` and `default.nix` (no flakes; nixpkgs pinned by `flake.lock`)
both expose `nix/default.nix`.

## Packages

| Attribute | What it is |
|---|---|
| `felf` | the static CLI, embedding the three below |
| `felf-bootstrap` | the static bootstrap |
| `felf-runtime` | the audit module and shim |
| `felf-glibc` | glibc with the patched `ld.so` (a full glibc build, once) |
| `felf-locales` | the embedded locale archive (C.UTF-8, en_US.UTF-8) |
| `oci-coreutils-bash` | the OCI image below |

```bash
nix build .#felf && ./result/bin/felf --version
```

## OCI image

`nix/examples/oci-coreutils-bash.nix` builds a single-layer image whose only
regular file is `/bin/bash`: one FELF holding bash, all of coreutils and
btop. Every other `/bin` entry is a symlink to it, made with
`FELF_INSTALL_LINKS`.

```bash
nix build .#oci-coreutils-bash
docker load < result
docker run --rm felf-coreutils-bash -c 'ls -l /bin | head; echo hi | sha256sum'
```

The layer is built with `allowedReferences = [ ]`, so the build fails if it
would carry any store path.

## Development shell

`nix develop` (or `nix-shell`) provides `felf_build` and `felf_test`; see
[building.md](building.md).

## Checks

`nix flake check` builds:

| Check | Verifies |
|---|---|
| `felf-tests` | the CTest suite, and that `felf` is static |
| `felf-reproducibility` | two packs are identical |
| `oci-coreutils-bash-test` | the image runs with only its own `/bin`, one FELF plus symlinks |
