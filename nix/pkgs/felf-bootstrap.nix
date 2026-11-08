# The statically linked bootstrap at the front of every FELF.
{
  lib,
  pkgsStatic,
  cmake,
  ninja,
  felfSource,
  felfVersion,
}:
pkgsStatic.stdenv.mkDerivation {
  pname = "felf-bootstrap";
  version = felfVersion;
  src = felfSource;
  nativeBuildInputs = [ cmake ninja ];
  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    "-DFELF_BUILD_CLI=OFF"
    "-DFELF_BUILD_AUDIT=OFF"
    "-DFELF_BUILD_TESTS=OFF"
    "-DFELF_STATIC_BOOTSTRAP=ON"
  ];
  installPhase = ''
    mkdir -p $out/bin
    cp felf-bootstrap $out/bin/felf-bootstrap
  '';
  dontStrip = true;
  meta = {
    description = "Statically linked FELF bootstrap";
    license = lib.licenses.mit;
    platforms = [ "x86_64-linux" "aarch64-linux" ];
  };
}
