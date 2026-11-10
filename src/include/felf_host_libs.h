#ifndef FELF_HOST_LIBS_H
#define FELF_HOST_LIBS_H

/*
 * Host-preferred libraries.
 *
 * The graphics stack (GL/EGL/GLX dispatch, vendor drivers, Vulkan, DRM/GBM,
 * VA-API/VDPAU, CUDA) is tied to the host's kernel driver and its vendor
 * configuration, so it must come from the host.  At runtime a name that
 * matches one of these patterns is looked up in the host library
 * directories first; the embedded copy, if the packer found one, is only a
 * fallback for hosts that keep these libraries outside the standard
 * directories (NixOS keeps libglvnd in the store and only the vendor
 * drivers in /run/opengl-driver/lib).
 *
 * NSS modules are host-preferred for the same reason: /etc/nsswitch.conf
 * names the host's sources of users, groups and hosts (systemd, sss,
 * ldap, mdns...), and glibc dlopens libnss_<source>.so.2 for each.  The
 * host's module runs against the bundled glibc; one that cannot load is
 * UNAVAIL to NSS, which moves on to the next source.  files and dns are
 * built into libc.
 *
 * A pattern is an exact file name or a prefix followed by a single
 * trailing '*'.  Patterns match the basename of a requested name.
 *
 * This header is used by the freestanding audit module: no libc.
 */

static const char *const felf_default_host_libs[] = {
    /* libglvnd dispatch and vendor libraries */
    "libGL.so.1",
    "libGLX.so.0",
    "libGLdispatch.so.0",
    "libOpenGL.so.0",
    "libEGL.so.1",
    "libGLESv1_CM.so.1",
    "libGLESv2.so.2",
    "libGLX_*",
    "libEGL_*",
    "libGLESv1_CM_*",
    "libGLESv2_*",
    /* Mesa */
    "libgallium-*",
    "libgbm.so.1",
    "libdrm.so.2",
    "libdrm_*",
    /* Vulkan loader and ICDs */
    "libvulkan.so.1",
    "libvulkan_*",
    /* NVIDIA / CUDA */
    "libnvidia-*",
    "libcuda.so*",
    "libnvcuvid.so*",
    "libnvoptix.so*",
    /* Video acceleration */
    "libva.so.2",
    "libva-drm.so.2",
    "libva-x11.so.2",
    "libva-wayland.so.2",
    "libvdpau.so.1",
    "libvdpau_*",
    /* NSS modules named by /etc/nsswitch.conf */
    "libnss_*",
};

#define FELF_DEFAULT_HOST_LIB_COUNT \
    (sizeof(felf_default_host_libs) / sizeof(felf_default_host_libs[0]))

static inline int felf_host_pattern_match(const char *pattern,
                                          const char *name)
{
    while (*pattern != '\0') {
        if (pattern[0] == '*' && pattern[1] == '\0')
            return 1;
        if (*pattern != *name)
            return 0;
        pattern++;
        name++;
    }
    return *name == '\0';
}

/* Nonzero when NAME matches one of the newline-separated patterns in LIST
 * (the meta value "host_libs"). */
static inline int felf_host_list_match(const char *list, const char *name)
{
    while (*list != '\0') {
        const char *end = list;
        while (*end != '\0' && *end != '\n')
            end++;
        const char *p = list, *n = name;
        while (p != end && !(*p == '*' && p + 1 == end) && *p == *n) {
            p++;
            n++;
        }
        /* The whole pattern matched the whole name, or up to a '*'. */
        if (end != list && ((p == end && *n == '\0') ||
                            (p + 1 == end && *p == '*')))
            return 1;
        list = *end != '\0' ? end + 1 : end;
    }
    return 0;
}

#endif
