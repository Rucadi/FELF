# `nix-shell` with no arguments enters the development shell.  default.nix
# is the package set (an attribute set), so nix-shell cannot pick a
# derivation from it without -A; shell.nix is the conventional entry point.
#
#   nix-shell                 # this shell
#   nix-shell -A shells.default
#   nix develop               # the flake's devShell
{ system ? builtins.currentSystem, pkgs ? null }:
(import ./default.nix (
  if pkgs == null then { inherit system; } else { inherit pkgs; }
)).shells.default
