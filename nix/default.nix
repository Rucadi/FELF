# The FELF package set for one nixpkgs (one system).
#
#   import ./nix { inherit pkgs; }
#
# returns { packages, checks, bundle, tarball, bundler, shell }.  flake.nix
# exposes it per system; ../default.nix exposes it without flakes.
#
#   pkgs/      the tools: felf, felf-bootstrap, felf-runtime, felf-glibc
#   lib/       bundle.nix (package a derivation as a FELF), tarball.nix
#   bundles/   bundles built with lib/bundle.nix: GCC, Krita
#   examples/  the single-FELF OCI image
#   tests/     checks
{ pkgs }:
let
  inherit (pkgs) lib;

  scope = lib.makeScope pkgs.newScope (self: {
    felfVersion = "0.1.0";
    felfSource = self.callPackage ./source.nix { };

    # The glibc loader: its file name, and where distributions keep it.
    loaderName = baseNameOf pkgs.stdenv.cc.bintools.dynamicLinker;
    fhsLoader =
      (if pkgs.stdenv.hostPlatform.isx86_64 then "/lib64/" else "/lib/")
      + self.loaderName;

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

    # Package a derivation as a FELF (tree by default); see lib/bundle.nix.
    bundle = import ./lib/bundle.nix {
      inherit pkgs;
      inherit (self) felf;
    };
    # A FELF tree as a reproducible tarball; see lib/tarball.nix.
    tarball = import ./lib/tarball.nix { inherit pkgs; };
    # `nix bundle --bundler`: a package as a FELF tree in a .tar.gz.
    bundler = import ./bundler.nix { inherit (self) bundle tarball; };

    bundle-hello = self.bundle { package = pkgs.hello; };
    # A relocatable GCC toolchain: one FELF for the driver, cc1, as, ld...
    bundle-gcc = import ./bundles/gcc.nix {
      inherit pkgs;
      inherit (self) bundle;
      dynamicLinker = self.fhsLoader;
    };
    # bundle-gcc as gcc-<version>/ in a tarball, symlinks preserved.
    bundle-gcc-tarball = self.tarball {
      name = "gcc-${pkgs.gcc.cc.version}";
      tree = self.bundle-gcc;
    };
    bundle-krita = import ./bundles/krita.nix {
      inherit pkgs;
      inherit (self) bundle;
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
    bundler-test = self.callPackage ./tests/bundler.nix { };
    inherit (self.callPackage ./tests/bundles.nix { })
      bundle-hello-test bundle-gcc-test bundle-gcc-tarball-test;

    shell = self.callPackage ./shell.nix { };
  });

  packages = lib.getAttrs [
    "felf" "felf-bootstrap" "felf-runtime" "felf-glibc" "felf-locales"
    "felf-tests" "felf-reproducibility"
    "bundle-hello" "bundle-hello-test"
    "bundle-gcc" "bundle-gcc-test"
    "bundle-gcc-tarball" "bundle-gcc-tarball-test"
    "bundle-krita"
    "oci-coreutils-bash" "oci-coreutils-bash-test"
    "bundler-test"
  ] scope;
in
{
  packages = packages // { default = packages.felf; };
  # What `nix flake check` builds.  Krita and the plain tarball are left out
  # (large; their tests cover the same paths).
  checks = lib.getAttrs [
    "felf" "felf-bootstrap" "felf-tests" "felf-reproducibility"
    "felf-glibc" "bundle-hello" "bundle-hello-test" "bundle-gcc-test"
    "bundle-gcc-tarball-test" "oci-coreutils-bash-test" "bundler-test"
  ] scope;
  inherit (scope) bundle tarball bundler shell;
}
