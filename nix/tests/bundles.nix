# Tests of the bundles built with lib/bundle.nix.
{
  pkgs,
  felfVersion,
  loaderName,
  fhsLoader,
  bundle-hello,
  bundle-gcc,
  bundle-gcc-tarball,
}:
let
  inherit (pkgs) runCommand;
  gccVersion = pkgs.gcc.cc.version;
in
{
  bundle-hello-test = runCommand "felf-bundle-hello-test-${felfVersion}" {
    nativeBuildInputs = [ pkgs.gnugrep ];
  } ''
    FELF_DEBUG=1 ${bundle-hello}/bin/hello > $out 2> debug
    grep -q "Hello" $out
    # Its libraries come straight from the FELF file, not from memfds.
    grep -q "libraries are mapped from the FELF file" debug
    grep -q "bundled libc.so.6 -> felf-map-1:" debug
  '';

  # The bundled compiler builds C, C++ and LTO programs using only its own
  # tree (env -i: no Nix compiler wrapper on PATH).  The sandbox has no
  # /lib64, so the results run through the sysroot's loader.
  bundle-gcc-test = runCommand "felf-bundle-gcc-test-${felfVersion}" {
    nativeBuildInputs = [ pkgs.gnugrep pkgs.binutils ];
  } ''
    G=${bundle-gcc}
    run() { env -i HOME=$PWD PATH=$G/bin "$@"; }
    printf '#include <math.h>\n#include <stdio.h>\nint main(void){printf("hello C %%.3f\\n", sqrt(2.0));}\n' > hello.c
    printf '#include <iostream>\nint main(){std::cout<<"hello C++"<<std::endl;}\n' > hello.cpp
    run gcc -O2 hello.c -o hello_c -lm
    run g++ -O2 -std=c++20 hello.cpp -o hello_cpp
    run gcc -O2 -flto hello.c -o hello_lto -lm
    readelf -l hello_c | grep -q "interpreter: ${fhsLoader}"
    for b in hello_c hello_cpp hello_lto; do
      $G/sysroot/lib/${loaderName} --library-path $G/sysroot/lib ./$b
    done > $out
    grep -q "hello C 1.414" $out
    grep -q "hello C++" $out
  '';

  # Extract the tarball like a user would and build with it.
  bundle-gcc-tarball-test = runCommand
    "felf-bundle-gcc-tarball-test-${felfVersion}" {
      nativeBuildInputs = [ pkgs.gnutar pkgs.gnugrep pkgs.binutils ];
    } ''
      set -euo pipefail
      v=${gccVersion}
      mkdir x && cd x
      tar -xf ${bundle-gcc-tarball}/gcc-$v-${pkgs.stdenv.hostPlatform.system}.tar
      G=$PWD/gcc-$v
      [ -L $G/bin/g++ ] && [ -L $G/libexec/gcc/*/$v/cc1 ]
      # The FELF is stored once; everything else links to it.
      [ $(find $G -type f -name gcc | wc -l) -eq 1 ]
      run() { env -i HOME=$PWD PATH=$G/bin "$@"; }
      printf '#include <stdio.h>\nint main(void){puts("tar C");}\n' > c.c
      printf '#include <iostream>\nint main(){std::cout<<"tar C++\\n";}\n' > c.cpp
      run gcc -O2 c.c -o c
      run g++ -O2 c.cpp -o cpp
      for b in c cpp; do
        $G/sysroot/lib/${loaderName} --library-path $G/sysroot/lib ./$b
      done > out
      grep -q "tar C$" out
      grep -q "tar C++" out
      rm -rf $G
      cp out $out
    '';
}
