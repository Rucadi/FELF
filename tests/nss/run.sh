#!/usr/bin/env bash
# NSS modules named by nsswitch.conf come from the host.  In a user and
# mount namespace with its own /etc (nsswitch.conf: "passwd: files
# felftest") and no nscd, the FELF looks up a user that only the host's
# libnss_felftest.so.2 knows.  Exits 77 (skipped) without namespaces.
#
#   run.sh UNSHARE FELF MODULE_DIR WORK
set -u
unshare=$1 felf=$2 moddir=$3 work=$4
etc=$work/nss-etc
mkdir -p "$etc"
cp /etc/passwd /etc/group "$etc/" 2>/dev/null
printf 'passwd: files felftest\ngroup: files\n' > "$etc/nsswitch.conf"
"$unshare" -Urm true 2>/dev/null || exit 77
exec "$unshare" -Urm bash -c '
    mount --bind "$1" /etc || exit 77
    if [ -e /var/run/nscd/socket ]; then
        mount --bind /dev/null /var/run/nscd/socket || exit 77
    fi
    LD_LIBRARY_PATH=$2 exec "$3" felf-nss-user' bash "$etc" "$moddir" "$felf"
