# The FELF package set for one nixpkgs (one system).
#
#   import ./nix { inherit pkgs; }
#
# returns { packages, checks, shell }.  flake.nix exposes it per system;
# ../default.nix exposes it without flakes.
#
#   pkgs/      the tools: felf, felf-bootstrap, felf-runtime, felf-glibc
#   examples/  the single-FELF OCI image
#   tests/     checks
{ pkgs }:
let
  inherit (pkgs) lib;

  scope = lib.makeScope pkgs.newScope (self: {
    felfVersion = "0.3.0";
    felfSource = self.callPackage ./source.nix { };

    # The glibc loader's file name.
    loaderName = baseNameOf pkgs.stdenv.cc.bintools.dynamicLinker;

    felf-bootstrap = self.callPackage ./pkgs/felf-bootstrap.nix { };
    felf = self.callPackage ./pkgs/felf.nix { };
    # The audit module and shim, and the glibc every FELF carries.
    felf-runtime = self.callPackage ./pkgs/felf-runtime.nix { };
    felf-glibc = self.callPackage ./pkgs/felf-glibc.nix { };
    # The locale archive every FELF carries.  C.UTF-8 is the locale programs
    # fall back to; en_US.UTF-8 is what a caller commonly names.
    felf-locales = pkgs.glibcLocales.override {
      allLocales = false;
      locales = [ "C.UTF-8/UTF-8" "en_US.UTF-8/UTF-8" ];
    };

    # An OCI image whose only executable is one FELF holding bash and
    # coreutils; every command is a symlink to it.
    ociCoreutilsBash = import ./examples/oci-coreutils-bash.nix {
      inherit pkgs;
      inherit (self) felf;
    };
    oci-coreutils-bash = self.ociCoreutilsBash.image;

    felf-tests = self.callPackage ./tests/felf.nix { };
    felf-reproducibility = self.callPackage ./tests/reproducibility.nix { };
    oci-coreutils-bash-test =
      self.callPackage ./tests/oci-coreutils-bash.nix { };

    shell = self.callPackage ./shell.nix { };
  });

  packages = lib.getAttrs [
    "felf" "felf-bootstrap" "felf-runtime" "felf-glibc" "felf-locales"
    "felf-tests" "felf-reproducibility"
    "oci-coreutils-bash" "oci-coreutils-bash-test"
  ] scope;
in
{
  packages = packages // { default = packages.felf; };
  # What `nix flake check` builds.
  checks = lib.getAttrs [
    "felf" "felf-bootstrap" "felf-tests" "felf-reproducibility"
    "felf-glibc" "oci-coreutils-bash-test"
  ] scope;
  inherit (scope) shell;
}
