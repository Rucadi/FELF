# The nix bundle bundler on coreutils: every command of the multicall binary
# runs from the extracted tarball, and bin/ holds one FELF plus symlinks.
{
  runCommand,
  coreutils,
  gnutar,
  gzip,
  gnugrep,
  findutils,
  felfVersion,
  bundler,
}:
let
  archive = bundler coreutils;
in
runCommand "felf-bundler-test-${felfVersion}" {
  nativeBuildInputs = [ gnutar gzip gnugrep findutils ];
} ''
  set -euo pipefail
  tar -xzf ${archive}
  T=$PWD/${coreutils.name}
  [ "$(find $T/bin -type f | wc -l)" -eq 1 ]
  [ -L $T/bin/ls ] && [ -f $T/bin/coreutils ]
  env -i $T/bin/echo hello | env -i $T/bin/tr a-z A-Z > out
  env -i $T/bin/seq 3 | env -i $T/bin/sort -r | env -i $T/bin/head -1 >> out
  env -i $T/bin/ls $T/bin | env -i $T/bin/wc -l >> out
  cat out
  grep -qx HELLO out
  grep -qx 3 out
  [ "$(sed -n 3p out)" -gt 100 ]
  cp out $out
''
