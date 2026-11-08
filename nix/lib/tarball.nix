# Pack a FELF tree into a tarball that extracts into one directory.
#
#   $out/<name>-<system>.tar[.gz]   containing <name>/...
#
# With file = true, $out is the archive itself and the derivation is named
# like it (what `nix bundle` links into the working directory).
#
# The tree's relative symlinks are kept (a multi-program FELF is stored
# once).  Extraction is safe: the build fails if any entry is absolute or a
# symlink leads outside <name>/.  The archive is reproducible: sorted
# entries, fixed mtime, root:root, no atime/ctime, gzip without a name or
# timestamp.  Files are owner-writable so the extracted tree can be removed
# normally.
{ pkgs }:
let
  inherit (pkgs.stdenv.hostPlatform) system;
in
{
  # Top-level directory inside the tarball.
  name,
  # The tree (a bundle.nix output).
  tree,
  # Compress with gzip (.tar.gz).
  gzip ? false,
  # Make $out the archive file instead of a directory holding it.
  file ? false,
}:
let
  archive = "${name}-${system}.tar${if gzip then ".gz" else ""}";
in
pkgs.runCommand (if file then archive else "${name}-tarball") {
  nativeBuildInputs = [ pkgs.gnutar pkgs.gzip pkgs.coreutils pkgs.findutils ];
} ''
  set -euo pipefail
  mkdir stage
  cp -a ${tree} "stage/${name}"
  chmod -R u+w stage

  # Every symlink must stay inside the tree.
  top=$(realpath "stage/${name}")
  find "stage/${name}" -type l | while IFS= read -r link; do
    target=$(readlink "$link")
    case "$target" in
      /*) echo "tarball.nix: absolute symlink $link -> $target" >&2; exit 1 ;;
    esac
    resolved=$(realpath -m "$(dirname "$link")/$target")
    case "$resolved" in
      "$top"|"$top"/*) ;;
      *) echo "tarball.nix: $link -> $target leaves the tree" >&2; exit 1 ;;
    esac
  done

  ${if file then ''archive=$out'' else ''
    mkdir -p "$out"
    archive="$out/${archive}"
  ''}
  tar --sort=name --mtime=@1 --owner=0 --group=0 --numeric-owner \
    --format=posix \
    --pax-option=exthdr.name=%d/PaxHeaders/%f,delete=atime,delete=ctime \
    ${if gzip then ''--use-compress-program="gzip -9n"'' else ""} \
    -C stage -cf "$archive" "${name}"

  # No entry may be absolute or climb out of the directory.
  if tar -tf "$archive" | grep -Ev "^${name}(/|$)" | grep -q .; then
    echo "tarball.nix: entries outside ${name}/" >&2
    exit 1
  fi
''
