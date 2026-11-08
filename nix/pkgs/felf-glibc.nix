# The glibc every FELF carries: nixpkgs' glibc libraries, with its dynamic
# linker built with src/glibc/patches/0001-felf-loader.patch, which maps the
# uncompressed libraries of a FELF straight from the FELF file.  felf embeds
# this directory (nix/pkgs/felf.nix) and bundles it with every program in
# place of the program's own glibc.  Only ld-linux comes from the patched
# build: the patch changes nothing libc.so.6 shares with the loader.
{
  runCommand,
  findutils,
  glibc,
  loaderName,
}:
let
  patched = glibc.overrideAttrs (old: {
    patches = (old.patches or [ ]) ++ [ ../../src/glibc/patches/0001-felf-loader.patch ];
    doCheck = false;
  });
in
runCommand "felf-glibc-${glibc.version}" {
  nativeBuildInputs = [ findutils ];
  passthru = { inherit patched; inherit (glibc) version; };
} ''
  mkdir -p $out/lib
  find -L ${glibc}/lib -maxdepth 1 -type f -name 'lib*.so.*' \
    -exec install -m644 {} $out/lib/ \;
  install -m755 ${patched}/lib/${loaderName} $out/lib/${loaderName}
''
