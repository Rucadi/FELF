# syntax=docker/dockerfile:1
#
# FELF built without Nix, on Ubuntu: plain CMake, which downloads, patches
# and builds FELF's glibc itself (src/glibc/README.md), then the CTest
# suite.  From the repository root:
#
#   docker build -f docker/ubuntu.Dockerfile -t felf:ubuntu .
#   docker run --rm felf:ubuntu felf --version
#
# The build directory is a BuildKit cache, so the glibc build (the slow
# part) is reused by later image builds.  --build-arg FELF_TESTS=0 skips
# the tests.
FROM ubuntu:26.04

ARG DEBIAN_FRONTEND=noninteractive
# gcc >= 15 (#embed), CMake, Ninja; for glibc: make, gawk, bison, python3,
# patch, Linux headers, CA certificates for the download; musl-tools for
# the static bootstrap; g++, busybox-static and util-linux for the tests.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        gcc g++ libc6-dev linux-libc-dev binutils \
        cmake ninja-build make gawk bison python3 patch xz-utils \
        ca-certificates musl-tools busybox-static util-linux file \
 && rm -rf /var/lib/apt/lists/*

COPY . /src

ARG FELF_TESTS=1
RUN --mount=type=cache,target=/build <<'EOF'
set -eu
# The bootstrap at the front of every FELF is static and built with musl,
# apart from the rest, as nix/pkgs/felf-bootstrap.nix does.
cmake -S /src -B /build/bootstrap -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER=musl-gcc \
    -DFELF_BUILD_CLI=OFF -DFELF_BUILD_AUDIT=OFF -DFELF_BUILD_TESTS=OFF \
    -DFELF_STATIC_BOOTSTRAP=ON
cmake --build /build/bootstrap

# Everything else.  FELF_GLIBC_DIR is unset: CMake builds FELF's glibc.
cmake -S /src -B /build/felf -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DFELF_BOOTSTRAP_FILE=/build/bootstrap/felf-bootstrap \
    -DFELF_TEST_BUSYBOX=/bin/busybox
cmake --build /build/felf
if [ "$FELF_TESTS" = 1 ]; then
    ctest --test-dir /build/felf --output-on-failure -j "$(nproc)"
fi
install -Dm755 /build/felf/felf /usr/local/bin/felf
EOF

CMD ["felf", "--help"]
