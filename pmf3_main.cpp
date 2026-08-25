/* pmf3 -- pmf2 said make_add() returns a functor that calls mul. The factories' ASSEMBLY is
 * correct: make_add stores @_ZN6Target3addEi and make_mul stores @_ZN6Target3mulEi, each through
 * the same instruction sequence. So the question moves past codegen to what the LINKED words
 * actually contain -- print them.
 *
 * A FunctorMem is { vtable, _t, _func.ptr, _func.adj }, sixteen bytes, and the two objects should
 * differ in exactly one word. If they do not, the member-function addresses collided somewhere
 * between the assembler and the linker, and no amount of staring at the C++ will show it.
 */
#include "pmf2.h"
#include <stdio.h>
#include <string.h>

static void dump(const char *what, const Functor<int, int> *f)
{
    unsigned w[4];
    memcpy(w, (const void *)f, sizeof w);
    printf("pmf3: %-8s object = %08x %08x %08x %08x\n", what, w[0], w[1], w[2], w[3]);
}

int main()
{
    Target t;
    int (Target::*padd)(int) = &Target::add;
    int (Target::*pmul)(int) = &Target::mul;
    unsigned a[2], m[2];

    memcpy(a, &padd, sizeof a);
    memcpy(m, &pmul, sizeof m);
    printf("pmf3: &Target::add = %08x %08x\n", a[0], a[1]);
    printf("pmf3: &Target::mul = %08x %08x\n", m[0], m[1]);
    printf("pmf3: direct add(10)=%d mul(10)=%d (expect 11 and 30)\n",
           (t.*padd)(10), (t.*pmul)(10));

    Functor<int, int> *fa = make_add(&t);
    Functor<int, int> *fm = make_mul(&t);
    dump("make_add", fa);
    dump("make_mul", fm);
    printf("pmf3: through the functor: add(10)=%d mul(10)=%d (expect 11 and 30)\n",
           (*fa)(10), (*fm)(10));
    return 0;
}
