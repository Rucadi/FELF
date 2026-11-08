# The felf CLI (packer, inspector, verifier), linked statically against
# musl.  It embeds everything a FELF carries: the static bootstrap, the
# audit module and runtime shim (felf-runtime.nix), FELF's glibc
# (felf-glibc.nix) and the locale archive every FELF always carries.  The
# integration suite runs in nix/tests/felf.nix.
{
  lib,
  pkgsStatic,
  cmake,
  ninja,
  felfSource,
  felfVersion,
  felf-bootstrap,
  felf-runtime,
  felf-glibc,
  felf-locales,
}:
pkgsStatic.stdenv.mkDerivation {
  pname = "felf";
  version = felfVersion;
  src = felfSource;
  nativeBuildInputs = [ cmake ninja ];
  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    "-DFELF_BUILD_BOOTSTRAP=OFF"
    "-DFELF_BUILD_AUDIT=OFF"
    "-DFELF_BUILD_TESTS=OFF"
    "-DFELF_STATIC_CLI=ON"
    "-DFELF_BOOTSTRAP_FILE=${felf-bootstrap}/bin/felf-bootstrap"
    "-DFELF_AUDIT_FILE=${felf-runtime}/lib/felf/libfelf_audit.so"
    "-DFELF_SHIM_FILE=${felf-runtime}/lib/felf/libfelf_runtime.so"
    "-DFELF_GLIBC_DIR=${felf-glibc}/lib"
    "-DFELF_GLIBC_VERSION=${felf-glibc.version}"
    "-DFELF_LOCALE_ARCHIVE_FILE=${felf-locales}/lib/locale/locale-archive"
  ];
  meta = {
    description = "FELF packer, inspector, and verification CLI";
    license = lib.licenses.mit;
    mainProgram = "felf";
    platforms = [ "x86_64-linux" "aarch64-linux" ];
  };
}
