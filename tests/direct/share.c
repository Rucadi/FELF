#include <signal.h>
#include <stdio.h>
#include <unistd.h>

/* Built twice, as two different programs (-DWHO=...) of one FELF: each
 * says it is ready and waits, so the suite can compare the two processes'
 * mappings of the libraries.  The alarm ends it if the suite does not. */
int main(void)
{
    alarm(60);
    printf("%s ready\n", WHO);
    fflush(stdout);
    pause();
    return 0;
}
