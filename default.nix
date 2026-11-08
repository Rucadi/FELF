# FELF without flakes.  nixpkgs is the revision pinned in flake.lock, so
# this builds the same derivations as the flake.
#
#   nix-build -A felf
#   nix-build -A oci-coreutils-bash
#   nix-build -A checks.felf-tests
#   nix-build -E '(import ./. { }).bundler (import <nixpkgs> { }).hello'
#   nix-shell                  # shell.nix selects the development shell
#   nix-shell -A shells.default
#
# Every package of the flake is a top-level attribute; checks, the bundle
# and tarball helpers, and the shell are under their own names.
{
  system ? builtins.currentSystem,
  pkgs ? import (
    let
      locked = (builtins.fromJSON (builtins.readFile ./flake.lock))
        .nodes.nixpkgs.locked;
    in
    fetchTarball {
      url = "https://github.com/${locked.owner}/${locked.repo}/archive/${locked.rev}.tar.gz";
      sha256 = locked.narHash;
    }
  ) { inherit system; },
}:
let
  felf = import ./nix { inherit pkgs; };
in
felf.packages // {
  inherit (felf) checks bundle tarball bundler;
  # Not a top-level derivation: plain nix-build must not try to build it.
  shells.default = felf.shell;
}
