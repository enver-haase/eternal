/* pmftest -- is a pointer-to-VIRTUAL-member-function callable on this target?
 *
 * ScummVM dies in Common::XMLParser::parseActiveKey with an instruction fetch at 0x2f8, a tiny
 * even address. In the Itanium C++ ABI a pointer to member function is a pair {ptr, adj}, and for
 * a VIRTUAL member ptr holds vtable_offset + 1 -- an odd number the caller must test for and
 * resolve through the vtable. Jumping to 0x2f8 is what happens if that test is missing or wrong:
 * the offset gets used as a code address. ScummVM's XML parser dispatches through exactly such
 * pointers, so this twenty-line program decides whether the fault is ScummVM's or the toolchain's.
 */
#include <stdio.h>

struct Base {
    virtual ~Base() {}
    virtual int v(int x) { return x + 1; }
    int nv(int x) { return x + 100; }
};
struct Derived : Base {
    int v(int x) override { return x + 2; }
};

typedef int (Base::*PMF)(int);

int main()
{
    Derived d;
    Base   *b  = &d;
    PMF     pv = &Base::v;      /* virtual     -> ptr = vtable offset + 1 */
    PMF     pn = &Base::nv;     /* non-virtual -> ptr = the function      */

    printf("direct virtual call : %d (expect 12)\n", b->v(10));
    fflush(stdout);
    printf("pmf, non-virtual    : %d (expect 110)\n", (b->*pn)(10));
    fflush(stdout);
    printf("pmf, virtual        : ");
    fflush(stdout);
    printf("%d (expect 12)\n", (b->*pv)(10));
    fflush(stdout);
    printf("all four forms work\n");
    return 0;
}
