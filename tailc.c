/* tailc -- an indirect call in tail position through a pointer that must survive an intervening
 * call, which forces it into a CALLEE-SAVED register. That is the shape FunctorMem::operator()
 * has (its intervening call is the runtime helper for a bitwise AND), and if the epilogue restores
 * that register before the tail call, the jump goes to the caller's value instead. No C++ and no
 * member pointers here: if this breaks, the "member-pointer miscompile" was never about them. */
#include <stdio.h>

typedef int (*fp)(int);

int add1(int x) { return x + 1; }
int mul3(int x) { return x * 3; }

struct S { fp f; int tag; };

int helper(int x);                  /* defined in the other TU so it cannot be inlined away */

int call_it(struct S *s, int v)
{
    fp f = s->f;                    /* loaded before... */
    v = helper(v);                  /* ...a call, so f cannot stay in a caller-saved register */
    return f(v);                    /* ...and then a tail call through it */
}

struct S sa = { add1, 1 };
struct S sm = { mul3, 2 };

int main(void)
{
    int a = call_it(&sa, 10);
    int m = call_it(&sm, 10);

    printf("tailc: add1 -> %d (expect 11)\n", a);
    printf("tailc: mul3 -> %d (expect 30)\n", m);
    printf("tailc: %s\n", (a == 11 && m == 30) ? "ok" : "WRONG -- the tail call went somewhere else");
    return 0;
}
