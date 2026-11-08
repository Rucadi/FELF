#!/usr/bin/env bash
# Start a FELF bundle in a stock distribution container on a display you can
# watch in a browser (Xvfb + x11vnc + noVNC).
#
# Usage:
#   tests/docker/view.sh [IMAGE] [COMMAND...]
#       IMAGE    default debian:12
#       COMMAND  default /opt/krita/bin/krita --nosplash
# Environment:
#   HELLO, KRITA  bundle trees; GCC_TAR the gcc tarball directory
#                 (defaults: nix build .#bundle-hello, .#bundle-krita,
#                 .#bundle-gcc-tarball)
#   PORT          host port for the web client (default 6080)
#   FELF_DEBUG    passed through to the bundle
#
# The bundles are copied into the image (never bind-mounted), as in run.sh.
# Unlike run.sh the container has a network, to publish the web client on
# 127.0.0.1 only.  Stop it with Ctrl-C.

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
IMAGE=${1:-debian:12}
[ $# -gt 0 ] && shift
[ $# -eq 0 ] && set -- /opt/krita/bin/krita --nosplash
PORT=${PORT:-6080}

build() {
    nix build "$ROOT#$1" --no-link --print-out-paths 2>/dev/null
}
HELLO=$(readlink -f "${HELLO:-$(build bundle-hello)}")
KRITA=$(readlink -f "${KRITA:-$(build bundle-krita)}")
GCC_TAR=$(readlink -f "${GCC_TAR:-$(build bundle-gcc-tarball)}")

CONTEXT=$(mktemp -d "${TMPDIR:-/tmp}/felf-docker-context.XXXXXX")
trap 'chmod -R u+w "$CONTEXT" 2>/dev/null; rm -rf "$CONTEXT"' EXIT
cp "$ROOT/tests/docker/Dockerfile" "$CONTEXT/"
# The paths above are already resolved; keep the trees' own relative
# symlinks (bin/kritarunner -> krita, libexec/.../cc1 -> .../bin/gcc).
cp -a "$HELLO/." "$CONTEXT/hello"
cp -a "$KRITA/." "$CONTEXT/krita"
cp "$GCC_TAR"/*.tar "$CONTEXT/gcc.tar"
chmod -R u+w "$CONTEXT"

name=$(printf '%s' "$IMAGE" | tr ':/' '--')
tag=felf-test:$name-view
echo "building $tag ..."
docker build -q --build-arg "BASE=$IMAGE" \
    --build-arg "UID=$(id -u)" --build-arg "GID=$(id -g)" \
    --target view -t "$tag" \
    "$CONTEXT" >/dev/null

tty=()
[ -t 0 ] && tty=(-it)
echo "open http://127.0.0.1:$PORT/vnc.html?autoconnect=1&resize=scale"
docker run --rm "${tty[@]}" --init \
    -p "127.0.0.1:$PORT:6080" \
    -e FELF_DEBUG="${FELF_DEBUG:-}" \
    "$tag" sh -c '
        Xvfb :1 -screen 0 1600x1000x24 -nolisten tcp &
        sleep 1
        x11vnc -display :1 -forever -shared -nopw -localhost -quiet \
            -rfbport 5900 >/dev/null 2>&1 &
        websockify --web /usr/share/novnc 6080 localhost:5900 \
            >/dev/null 2>&1 &
        DISPLAY=:1 openbox >/dev/null 2>&1 &
        DISPLAY=:1 exec "$@"' sh "$@"
