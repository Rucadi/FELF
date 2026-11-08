#!/usr/bin/env bash
# Run FELF bundles inside stock distribution containers.
#
# This is the portability check.  The nix build outputs are copied
# (dereferenced) into the images with COPY, never bind-mounted; the
# containers have no /nix, no network, and run as the image's default
# unprivileged user (felf, created in the Dockerfile).
# Each base image yields two images (tests/docker/Dockerfile):
#
#   - bare (nothing installed): the bundle must be self-contained;
#   - gui (Xvfb + the distribution's Mesa): the GUI must start and the
#     graphics stack must come from the distribution, not from the bundle.
#
# Only $OUT is mounted, to collect logs and screenshots.
#
# Usage:
#   tests/docker/run.sh [IMAGE...]        default: debian:12 ubuntu:22.04
#                                         ubuntu:24.04
# Environment:
#   HELLO   hello bundle tree    (default: nix build .#bundle-hello)
#   KRITA   Krita bundle tree    (default: nix build .#bundle-krita)
#   GCC_TAR GCC tarball dir      (default: nix build .#bundle-gcc-tarball)
#   OUT     output directory for logs and screenshots (default: mktemp)
#   GUI_SECONDS  how long Krita must stay up (default: 45)

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
IMAGES=("$@")
[ ${#IMAGES[@]} -eq 0 ] && IMAGES=(debian:12 ubuntu:22.04 ubuntu:24.04)
GUI_SECONDS=${GUI_SECONDS:-45}
OUT=${OUT:-$(mktemp -d "${TMPDIR:-/tmp}/felf-docker.XXXXXX")}
mkdir -p "$OUT"

build() {
    nix build "$ROOT#$1" --no-link --print-out-paths 2>/dev/null
}
HELLO=$(readlink -f "${HELLO:-$(build bundle-hello)}")
KRITA=$(readlink -f "${KRITA:-$(build bundle-krita)}")
GCC_TAR=$(readlink -f "${GCC_TAR:-$(build bundle-gcc-tarball)}")

# Build context: the Dockerfile plus writable copies of the bundles,
# exactly what a user would unpack.
CONTEXT=$(mktemp -d "${TMPDIR:-/tmp}/felf-docker-context.XXXXXX")
trap 'chmod -R u+w "$CONTEXT" 2>/dev/null; rm -rf "$CONTEXT"' EXIT
cp "$ROOT/tests/docker/Dockerfile" "$CONTEXT/"
# The paths above are already resolved; keep the trees' own relative
# symlinks (bin/kritarunner -> krita, libexec/.../cc1 -> .../bin/gcc).
cp -a "$HELLO/." "$CONTEXT/hello"
cp -a "$KRITA/." "$CONTEXT/krita"
cp "$GCC_TAR"/*.tar "$CONTEXT/gcc.tar"
chmod -R u+w "$CONTEXT"
# Absolute symlinks could point into /nix; relative ones stay in the tree.
if find "$CONTEXT" -type l -lname '/*' | grep -q .; then
    echo "felf-docker: absolute symlinks in the build context:" >&2
    find "$CONTEXT" -type l -lname '/*' | head >&2
    exit 1
fi

PASS=0
FAIL=0
pass() { printf 'PASS: %s\n' "$1"; PASS=$((PASS + 1)); }
fail() { printf 'FAIL: %s\n' "$1" >&2; FAIL=$((FAIL + 1)); }

check_contains() {
    local name=$1 file=$2 needle=$3
    if grep -Eq -- "$needle" "$file"; then
        pass "$name"
    else
        fail "$name (missing: $needle)"
        grep -v '^felf\[' "$file" | head -n 20 | sed 's/^/    /' >&2 || true
    fi
}

check_not_contains() {
    local name=$1 file=$2 needle=$3
    if [ -f "$file" ] && ! grep -Eq -- "$needle" "$file"; then
        pass "$name"
    else
        fail "$name (unexpected: $needle)"
        grep -E -- "$needle" "$file" 2>/dev/null | head -n 5 | sed 's/^/    /' >&2 || true
    fi
}

# Run a command in IMAGE as its default user, felf (bundles copied in at
# /opt).  Only $OUT is mounted.
run_in() {
    local image=$1
    shift
    docker run --rm --network none -v "$OUT:/out" "$image" sh -c "$*"
}

# Sources for the compiler checks.
mkdir -p "$OUT/gcc-src"
cat >"$OUT/gcc-src/hello.c" <<'EOF'
#include <math.h>
#include <stdio.h>
int main(void) { printf("hello C %.3f\n", sqrt(2.0)); return 0; }
EOF
cat >"$OUT/gcc-src/hello.cpp" <<'EOF'
#include <iostream>
#include <thread>
#include <vector>
int main() {
    std::vector<int> v{1, 2, 3};
    int s = 0;
    std::thread t([&] { for (int x : v) s += x; });
    t.join();
    std::cout << "hello C++ " << s << std::endl;
}
EOF

for image in "${IMAGES[@]}"; do
    name=$(printf '%s' "$image" | tr ':/' '--')
    bare=felf-test:$name-bare
    gui=felf-test:$name-gui
    echo "== $image"
    for target in bare gui; do
        if ! docker build -q --build-arg "BASE=$image" \
                --build-arg "UID=$(id -u)" --build-arg "GID=$(id -g)" \
                --target "$target" \
                -t "felf-test:$name-$target" "$CONTEXT" \
                >"$OUT/$name-build-$target" 2>&1; then
            fail "$image: build $target image"
            tail -n 20 "$OUT/$name-build-$target" | sed 's/^/    /' >&2
            continue 2
        fi
    done

    run_in "$bare" 'test ! -e /nix && echo no-nix' >"$OUT/$name-nonix" 2>&1 || true
    check_contains "$image: no /nix in the container" "$OUT/$name-nonix" "no-nix"

    run_in "$bare" '/opt/hello/bin/hello' >"$OUT/$name-hello" 2>&1 || true
    check_contains "$image: bare hello" "$OUT/$name-hello" "Hello, world!"

    # Krita creates its QApplication even for --version, and the bare image
    # has no X server; stock Krita aborts there too.  Use the offscreen
    # platform, which needs none.
    run_in "$bare" 'QT_QPA_PLATFORM=offscreen /opt/krita/bin/krita --version' \
        >"$OUT/$name-krita-version" 2>&1 || true
    check_contains "$image: bare krita --version" "$OUT/$name-krita-version" \
        "^krita [0-9]"

    # The GCC bundle compiles and links on a system with no compiler; the
    # programs it builds run on the distribution's own glibc.
    run_in "$bare" '
        export PATH=/opt/gcc/bin:$PATH
        cd /tmp
        gcc --version | head -n 1
        gcc -O2 /out/gcc-src/hello.c -o hello_c -lm && ./hello_c
        g++ -O2 -std=c++20 /out/gcc-src/hello.cpp -o hello_cpp && ./hello_cpp
        gcc -O2 -flto /out/gcc-src/hello.c -o hello_lto -lm && ./hello_lto | sed "s/^/lto /"
        ' >"$OUT/$name-gcc" 2>&1 || true
    check_contains "$image: bare gcc runs" "$OUT/$name-gcc" "^gcc \(GCC\)"
    check_contains "$image: C program built and run" "$OUT/$name-gcc" \
        "^hello C 1\.414"
    check_contains "$image: C++ program built and run" "$OUT/$name-gcc" \
        "^hello C\+\+ 6"
    check_contains "$image: LTO build and run" "$OUT/$name-gcc" \
        "^lto hello C 1\.414"

    # krita_version is a symlink to the same FELF; the name selects it.
    run_in "$bare" '/opt/krita/bin/krita_version' \
        >"$OUT/$name-krita-version-link" 2>&1 || true
    check_contains "$image: bare krita_version symlink" \
        "$OUT/$name-krita-version-link" "^[0-9]+\.[0-9]+"

    run_in "$gui" '
        FELF_DEBUG=1 xvfb-run -a -s "-screen 0 1600x1000x24" sh -c "
            /opt/krita/bin/krita --nosplash > /out/'"$name"'-krita-gui.log 2>&1 &
            pid=\$!
            sleep '"$GUI_SECONDS"'
            if kill -0 \$pid 2>/dev/null; then
                echo ALIVE
                xwd -root -silent > /out/'"$name"'-krita.xwd
                kill \$pid
                ls \$HOME/.local/share/krita > /out/'"$name"'-krita-data
            else
                wait \$pid; echo DIED rc=\$?
            fi"' >"$OUT/$name-krita-gui" 2>&1 || true
    check_contains "$image: Krita GUI stays up for ${GUI_SECONDS}s" \
        "$OUT/$name-krita-gui" "^ALIVE"
    check_contains "$image: GLX vendor library from the distribution" \
        "$OUT/$name-krita-gui.log" \
        "felf\[audit\]: host libGLX_mesa\.so\.0 -> /(usr/)?lib/x86_64-linux-gnu/"
    check_contains "$image: libglvnd from the distribution" \
        "$OUT/$name-krita-gui.log" \
        "felf\[audit\]: host libGLX\.so\.0 -> /(usr/)?lib/x86_64-linux-gnu/"
    check_contains "$image: default resource bundles installed" \
        "$OUT/$name-krita-data" "Krita_4_Default_Resources\.bundle"
    check_not_contains "$image: no UTF-8 locale warning" \
        "$OUT/$name-krita-gui.log" "UTF-8 locale"
    if [ -f "$OUT/$name-krita.xwd" ] && command -v magick >/dev/null; then
        magick "xwd:$OUT/$name-krita.xwd" -resize 50% "$OUT/$name-krita.png" \
            2>/dev/null || true
    fi
done

echo
echo "logs and screenshots: $OUT"
printf '%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
