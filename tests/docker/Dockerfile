# A stock distribution plus FELFs, to check that they run there.
#
# The build context (prepared by run.sh) holds the packed files: hello.felf
# and box/, a bash + coreutils userland in one FELF with symlinks.  They are
# installed under /opt (owned by root) and run as an ordinary user, "felf".
#
# UID/GID default to 1000; run.sh passes the caller's so the user can write
# the mounted output directory.
ARG BASE=debian:12

FROM ${BASE}
ARG UID=1000
ARG GID=1000
# Ubuntu 24.04 ships an "ubuntu" user with UID 1000; free the UID first.
RUN existing=$(getent passwd "$UID" | cut -d: -f1) \
 && if [ -n "$existing" ]; then userdel -r "$existing" 2>/dev/null || userdel "$existing"; fi \
 && if ! getent group "$GID" >/dev/null; then groupadd -g "$GID" felf; fi \
 && useradd -m -u "$UID" -g "$GID" -s /bin/bash felf
COPY hello.felf /opt/hello.felf
COPY box /opt/box
USER felf
WORKDIR /home/felf
