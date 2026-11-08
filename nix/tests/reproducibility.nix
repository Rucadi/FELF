# Two packs of the same input are byte-for-byte identical.
{
  runCommand,
  stdenv,
  gcc,
  binutils,
  coreutils,
  diffutils,
  felfSource,
  felfVersion,
  felf,
}:
runCommand "felf-reproducibility-${felfVersion}" {
  nativeBuildInputs = [ gcc binutils coreutils diffutils ];
} ''
  export CC=${stdenv.cc}/bin/cc
  $CC -fPIE -pie ${felfSource}/tests/hello/hello.c -o hello
  for name in first second; do
    ${felf}/bin/felf pack hello \
      -o $name.felf
  done
  cmp first.felf second.felf
  mkdir -p $out
  touch $out/ok
''
