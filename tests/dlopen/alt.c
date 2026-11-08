/* The host's libplugin.so: same SONAME, different value.  A FELF must use
 * the copy it has bundled, never this one. */
int plugin_value(void)
{
    return 666;
}
