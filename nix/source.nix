# The sources the FELF derivations are built from.  Only these paths are
# copied to the store, so editing documentation or Nix files does not
# rebuild felf.
{ lib }:
lib.fileset.toSource {
  root = ../.;
  fileset = lib.fileset.unions [
    ../CMakeLists.txt
    ../src
    ../tests
    ../third_party
  ];
}
