# The integration suite in tests/ (CTest) against the installed tools.
{
  runCommand,
  cmake,
  ninja,
  gcc,
  binutils,
  gnugrep,
  gnused,
  findutils,
  coreutils,
  util-linux,
  pkgsStatic,
  felfSource,
  felfVersion,
  felf,
}:
runCommand "felf-tests-${felfVersion}" {
  nativeBuildInputs = [
    cmake ninja gcc binutils gnugrep gnused findutils coreutils util-linux
  ];
} ''
  cmake -S ${felfSource} -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DFELF_BUILD_CLI=OFF \
    -DFELF_BUILD_BOOTSTRAP=OFF \
    -DFELF_BUILD_AUDIT=OFF \
    -DFELF_TEST_CLI=${felf}/bin/felf \
    -DFELF_TEST_STATIC_BOOTSTRAP=ON \
    -DFELF_TEST_STATIC_CLI=ON \
    -DFELF_TEST_BUSYBOX=${pkgsStatic.busybox}/bin/busybox
  cmake --build build
  ctest --test-dir build --output-on-failure --parallel "$(nproc)"
  mkdir -p $out
  touch $out/ok
''
