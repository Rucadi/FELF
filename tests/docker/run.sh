#!/usr/bin/env bash
# Run FELFs inside stock distribution containers: the portability check.
#
# The FELFs are packed here and copied into the images (never
# bind-mounted); the containers have no /nix, no network, and run as an
# unprivileged user.  Only $OUT is mounted, to collect logs.
#
# Usage:
#   tests/docker/run.sh [IMAGE...]   default: debian:12 ubuntu:22.04 ubuntu:24.04
# Environment:
#   FELF       the felf CLI            (default: nix build .#felf)
#   HELLO      a program to pack       (default: nixpkgs' hello)
#   BASH, COREUTILS  for the userland  (default: nixpkgs' bash, coreutils)
#   OUT        output directory for logs (default: mktemp)

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
IMAGES=("$@")
[ ${#IMAGES[@]} -eq 0 ] && IMAGES=(debian:12 ubuntu:22.04 ubuntu:24.04)
OUT=${OUT:-$(mktemp -d "${TMPDIR:-/tmp}/felf-docker.XXXXXX")}
mkdir -p "$OUT"

build() {
    nix build --inputs-from "$ROOT" "$1" --no-link --print-out-paths 2>/dev/null
}
FELF=${FELF:-$(build "$ROOT#felf")/bin/felf}
HELLO=${HELLO:-$(build nixpkgs#hello)/bin/hello}
BASH_BIN=${BASH:-$(build nixpkgs#bashInteractive)/bin/bash}
COREUTILS=${COREUTILS:-$(build nixpkgs#coreutils)}

# Build context: the Dockerfile and the packed files.
CONTEXT=$(mktemp -d "${TMPDIR:-/tmp}/felf-docker-context.XXXXXX")
trap 'rm -rf "$CONTEXT"' EXIT
cp "$ROOT/tests/docker/Dockerfile" "$CONTEXT/"
"$FELF" pack "$HELLO" -o "$CONTEXT/hello.felf"
names=$(ls "$COREUTILS/bin" | grep -vx coreutils | paste -sd,)
mkdir -p "$CONTEXT/box/bin"
"$FELF" pack bash="$BASH_BIN" --program coreutils="$COREUTILS/bin/coreutils" \
    --alias "coreutils=$names" -o "$CONTEXT/box/bin/bash"
FELF_INSTALL_LINKS="$CONTEXT/box/bin" "$CONTEXT/box/bin/bash"

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

run_in() {
    local image=$1
    shift
    docker run --rm --network none -v "$OUT:/out" "$image" sh -c "$*"
}

for image in "${IMAGES[@]}"; do
    name=$(printf '%s' "$image" | tr ':/' '--')
    tag=felf-test:$name
    echo "== $image"
    if ! docker build -q --build-arg "BASE=$image" \
            --build-arg "UID=$(id -u)" --build-arg "GID=$(id -g)" \
            -t "$tag" "$CONTEXT" >"$OUT/$name-build" 2>&1; then
        fail "$image: build image"
        tail -n 20 "$OUT/$name-build" | sed 's/^/    /' >&2
        continue
    fi

    run_in "$tag" 'test ! -e /nix && echo no-nix' >"$OUT/$name-nonix" 2>&1 || true
    check_contains "$image: no /nix in the container" "$OUT/$name-nonix" "no-nix"

    run_in "$tag" '/opt/hello.felf' >"$OUT/$name-hello" 2>&1 || true
    check_contains "$image: hello" "$OUT/$name-hello" "Hello, world!"

    # The userland alone: an empty environment and only box/bin on PATH.
    run_in "$tag" "env -i PATH=/opt/box/bin bash -c 'seq 5 | sort -r | head -2 | paste -sd,'" \
        >"$OUT/$name-box" 2>&1 || true
    check_contains "$image: bash + coreutils userland" "$OUT/$name-box" "^5,4$"
done

echo
echo "logs: $OUT"
printf '%d passed, %d failed\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
