/* A host-preferred library.  The copy embedded at pack time returns 1; the
 * "host" copy (-DFELF_HOST_COPY) returns 2 plus a value from a host-only
 * vendor library, which tests that dependencies of host objects may come
 * from the host in strict mode. */
#ifdef FELF_HOST_COPY
int felf_vendor_value(void);

int felf_host_value(void)
{
    return 2 + felf_vendor_value();
}
#else
int felf_host_value(void)
{
    return 1;
}
#endif
