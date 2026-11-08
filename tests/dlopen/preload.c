/* Loaded through LD_PRELOAD into the dynamic bootstrap (when the bootstrap
 * is not static).  A FELF must not let it reach the application. */
#include <stdio.h>

__attribute__((constructor)) static void evil(void)
{
    fprintf(stderr, "HOST PRELOAD RUN\n");
}
