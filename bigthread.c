/* bigthread -- ScummVM cannot create SDL's audio thread ("Couldn't create audio thread!") while
 * /sdlaudio, doing the same call, can. The obvious difference is size: ScummVM is a 57 MB static
 * binary that has already allocated a lot by then, and linuxthreads places thread stacks at fixed
 * addresses rather than wherever mmap likes.
 *
 * So test the two size effects separately: a large .bss, and a large heap before the create.
 */
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

static char big_bss[48u << 20];          /* 48 MB, like a large static binary's image */

static void *worker(void *a) { (void)a; return NULL; }

static void try_create(const char *what)
{
    pthread_t t;
    int rc = pthread_create(&t, NULL, worker, NULL);
    printf("bigthread: pthread_create %-22s rc=%d (%s)\n", what, rc, rc ? strerror(rc) : "ok");
    fflush(stdout);
    if (rc == 0) pthread_join(t, NULL);
}

int main(void)
{
    printf("bigthread: .bss array at %p (%u MB)\n", (void *)big_bss, 48u);
    fflush(stdout);
    try_create("with big .bss");

    memset(big_bss, 1, sizeof big_bss);   /* touch it: demand paging makes it real */
    try_create("after touching .bss");

    void *p = malloc(40u << 20);
    printf("bigthread: malloc(40MB) -> %p\n", p); fflush(stdout);
    if (p) memset(p, 2, 40u << 20);
    try_create("after 40 MB heap");

    printf("bigthread: done\n"); fflush(stdout);
    return 0;
}
