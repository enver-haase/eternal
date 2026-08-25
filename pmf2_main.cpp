#include "pmf2.h"
#include <stdio.h>

int main(void)
{
    Target t;
    Functor<int, int> *a = make_add(&t);
    Functor<int, int> *m = make_mul(&t);

    printf("pmf2: direct           add(10)=%d mul(10)=%d (expect 11 30)\n", t.add(10), t.mul(10));
    fflush(stdout);
    printf("pmf2: cross-TU functor add(10)=");
    fflush(stdout);
    printf("%d (expect 11)\n", (*a)(10));
    fflush(stdout);
    printf("pmf2: cross-TU functor mul(10)=");
    fflush(stdout);
    printf("%d (expect 30)\n", (*m)(10));
    fflush(stdout);
    printf("pmf2: both cross-TU calls work\n");
    delete a;
    delete m;
    return 0;
}
