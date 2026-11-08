# An OCI image whose single layer holds exactly one executable: a FELF
# containing bash, all of GNU coreutils, btop, their shared glibc, and every
# library they need.  Every other command is a symlink to it:
#
#   /bin/bash             the FELF (the default program)
#   /bin/sh  -> bash      (no program named "sh": runs the default, bash)
#   /bin/ls  -> bash      runs the embedded coreutils as "ls"
#   /bin/cat -> bash      ...one symlink per coreutils command
#   /bin/btop -> bash     runs the embedded btop
#
# The FELF selects the embedded program by the basename of argv[0], like
# busybox.  The image has no /nix/store: the FELF derivation is built with
# allowedReferences = [ ], so the layer cannot pull in a store closure.
#
#   nix build .#oci-coreutils-bash
#   docker load < result            # or: podman load < result
#   docker run --rm -it felf-coreutils-bash:latest
#   docker run --rm felf-coreutils-bash:latest -c 'ls -l /bin | head'
{
  pkgs,
  felf,
}:
let
  bash = pkgs.bashInteractive;
  coreutils = pkgs.coreutils;
  btop = pkgs.btop;

  # The rootfs of the layer: bin/bash (the FELF) and symlinks.
  rootfs = pkgs.runCommand "felf-coreutils-bash-rootfs" {
    nativeBuildInputs = [ felf ];
    # The FELF embeds its libraries; nothing in the store may be referenced,
    # or the image would carry the store closure next to it.
    allowedReferences = [ ];
  } ''
    set -euo pipefail
    # nixpkgs' coreutils is one multicall binary; bin/ls, bin/cat... are
    # symlinks to it.  It is embedded once, and --alias gives it the other
    # names: each runs it with that name as argv[0].
    names=()
    for p in ${coreutils}/bin/*; do
      name=$(basename "$p")
      [ "$name" = coreutils ] || names+=("$name")
    done
    aliases=$(IFS=,; echo "''${names[*]}")

    mkdir -p "$out/bin"
    felf pack bash=${bash}/bin/bash \
      --program coreutils=${coreutils}/bin/coreutils \
      --alias "coreutils=$aliases" \
      --program btop=${btop}/bin/btop \
      --lazy \
      -o "$out/bin/bash"
    felf verify "$out/bin/bash"

    # One symlink per program and alias, made by the FELF itself.
    FELF_INSTALL_LINKS="$out/bin" "$out/bin/bash"
    ln -s bash "$out/bin/sh"
    cd "$out/bin"
    echo "felf: one file, $(stat -c %s bash) bytes, $(find . -type l | wc -l) symlinks"
  '';
in
{
  inherit rootfs;

  # A docker-archive with one layer (docker load / podman load).
  image = pkgs.dockerTools.buildImage {
    name = "felf-coreutils-bash";
    tag = "latest";
    copyToRoot = rootfs;
    config = {
      Entrypoint = [ "/bin/bash" ];
      Env = [ "PATH=/bin" ];
    };
  };
}
