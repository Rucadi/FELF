# Test images for running FELF bundles on a stock distribution.
#
# The build context (prepared by run.sh/view.sh) holds copies of the nix
# build outputs (hello, krita trees and the gcc tarball): plain files and
# relative symlinks inside each tree, nothing under /nix.  The bundles are
# installed system-wide under /opt (owned by root) and run as an ordinary
# user, "felf".
#
#   bare  the stock image plus the bundles: they must be self-contained;
#   gui   adds only what a desktop user would have: an X server (Xvfb, for
#         headless runs) and the distribution's own Mesa/libglvnd;
#   view  gui plus a window manager (openbox), a VNC server and the noVNC
#         web client (view.sh).
#
# UID/GID default to 1000; the scripts pass the caller's so the user can
# write the mounted output directory.
ARG BASE=debian:12

FROM ${BASE} AS base
ARG UID=1000
ARG GID=1000
# Ubuntu 24.04 ships an "ubuntu" user with UID 1000; free the UID first.
RUN existing=$(getent passwd "$UID" | cut -d: -f1) \
 && if [ -n "$existing" ]; then userdel -r "$existing" 2>/dev/null || userdel "$existing"; fi \
 && if ! getent group "$GID" >/dev/null; then groupadd -g "$GID" felf; fi \
 && useradd -m -u "$UID" -g "$GID" -s /bin/bash felf \
 && install -d -m 700 -o "$UID" -g "$GID" "/run/user/$UID"
ENV HOME=/home/felf \
    XDG_RUNTIME_DIR=/run/user/${UID}

FROM base AS bare
COPY hello /opt/hello
COPY krita /opt/krita
# GCC arrives as its release tarball; /opt/gcc is a symlink to the
# extracted gcc-<version>/ (the tree is also reached through a link).
ADD gcc.tar /opt/
RUN ln -s "$(cd /opt && ls -d gcc-*)" /opt/gcc
USER felf
WORKDIR /home/felf

FROM base AS gui
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        xvfb xauth x11-apps \
        libgl1 libegl1 libgl1-mesa-dri libglx-mesa0 libgbm1 \
        fontconfig fonts-dejavu-core \
 && rm -rf /var/lib/apt/lists/*
COPY --from=bare /opt /opt
USER felf
WORKDIR /home/felf

FROM base AS view
RUN apt-get update \
 && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        xvfb xauth x11-apps x11vnc novnc websockify openbox \
        libgl1 libegl1 libgl1-mesa-dri libglx-mesa0 libgbm1 \
        fontconfig fonts-dejavu-core \
 && rm -rf /var/lib/apt/lists/*
COPY --from=bare /opt /opt
USER felf
WORKDIR /home/felf
