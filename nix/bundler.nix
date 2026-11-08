# The `nix bundle` bundler:
#
#   nix bundle --bundler .#felf nixpkgs#coreutils
#
# turns the installable's package into a FELF tree (lib/bundle.nix, every
# dynamically linked executable in bin/ bundled into one FELF, the rest of
# the package kept as runtime data) and leaves it as
# ./<name>-<system>.tar.gz, which extracts into <name>/.
#
# The main program is the package's meta.mainProgram (else its pname).
{
  bundle,
  tarball,
}:
drv:
tarball {
  inherit (drv) name;
  tree = bundle {
    package = drv;
    allPrograms = true;
  };
  gzip = true;
  file = true;
}
