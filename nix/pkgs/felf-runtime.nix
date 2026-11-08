# The audit module and the runtime shim: glibc shared objects that felf
# embeds in every FELF.  Built apart from the static CLI (nix/pkgs/felf.nix).
{
  lib,
  stdenv,
  cmake,
  ninja,
  felfSource,
  felfVersion,
}:
stdenv.mkDerivation {
  pname = "felf-runtime";
  version = felfVersion;
  src = felfSource;
  nativeBuildInputs = [ cmake ninja ];
  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    "-DFELF_BUILD_CLI=OFF"
    "-DFELF_BUILD_BOOTSTRAP=OFF"
    "-DFELF_BUILD_AUDIT=ON"
    "-DFELF_BUILD_TESTS=OFF"
  ];
  installPhase = ''
    install -Dm644 libfelf_audit.so $out/lib/felf/libfelf_audit.so
    install -Dm644 libfelf_runtime.so $out/lib/felf/libfelf_runtime.so
  '';
  meta = {
    description = "FELF audit module and runtime shim";
    license = lib.licenses.mit;
    platforms = [ "x86_64-linux" "aarch64-linux" ];
  };
}
