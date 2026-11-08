# The commands run with nothing but the rootfs on PATH, and the image's
# single layer holds only bin/: one file, the rest symlinks.
{
  runCommand,
  gnutar,
  gzip,
  gnugrep,
  coreutils,
  findutils,
  felfVersion,
  # examples/oci-coreutils-bash.nix: { rootfs, image }.
  ociCoreutilsBash,
}:
let
  inherit (ociCoreutilsBash) rootfs image;
in
runCommand "felf-oci-coreutils-bash-test-${felfVersion}" {
  nativeBuildInputs = [ gnutar gzip gnugrep coreutils findutils ];
} ''
  set -euo pipefail
  R=${rootfs}
  env -i PATH=$R/bin $R/bin/bash -c '
    set -e
    # No caller locale: the FELF names C.UTF-8 and carries its archive.
    [ "$LANG" = C.UTF-8 ]
    [ "$(printenv LANG)" = C.UTF-8 ]
    echo hello | tr a-z A-Z
    printf "b\na\n" | sort | tr "\n" " "; echo
    [ "$(basename /x/y/z)" = z ]
    seq 3 | wc -l
    ls -l '"$R"'/bin/ls
    btop --version | head -n 1
  ' > out
  cat out
  grep -qx HELLO out
  grep -qx "a b " out
  grep -qx 3 out
  grep -q "ls -> bash" out
  grep -q "^btop version: " out

  mkdir img && tar -xzf ${image} -C img 2>/dev/null \
    || tar -xf ${image} -C img
  layers=$(find img -name layer.tar)
  [ "$(echo "$layers" | wc -l)" -eq 1 ]
  tar -tvf $layers | tee listing
  if tar -tf $layers | grep -Ev '^\./?$|^\.?/?bin(/|$)' | grep -q .; then
    echo "layer has entries outside bin/" >&2; exit 1
  fi
  [ "$(grep -c '^-' listing)" -eq 1 ]
  grep -Eq ' (\./)?bin/bash$' listing
  cp out $out
''
