/* pmfstore -- is a pointer-to-member-function still intact after being STORED in an object?
 *
 * ScummVM dies with an instruction fetch at a tiny address, reached from
 * Common::Functor1Mem<...>::operator(), which does (_t->*_func)(v1) with _func held in a class
 * field. Marking that call optnone did not help, and the VM's PC trace shows the jump landing in
 * heap/bss -- so the value in _func is already wrong before the call. That points at the copy of
 * the {ptr, adj} pair into the member, not at the call lowering.
 *
 * This mirrors the shape exactly: an abstract functor, a templated implementation holding a pointer
 * to member, constructed through a base-class pointer and called virtually.
 */
#include <stdio.h>

struct Target {
    int add(int x)  { return x + 1; }
    int mul(int x)  { return x * 3; }
    virtual ~Target() {}
};

template<class Res, class Arg>
struct Functor {
    virtual ~Functor() {}
    virtual Res operator()(Arg) const = 0;
};

template<class Res, class Arg, class T>
struct FunctorMem : public Functor<Res, Arg> {
    typedef Res (T::*FuncType)(Arg);

    FunctorMem(T *t, const FuncType &func) : _t(t), _func(func) {}

    Res operator()(Arg v) const override { return (_t->*_func)(v); }

private:
    mutable T *_t;
    const FuncType _func;
};

static Functor<int, int> *make(Target *t, int (Target::*f)(int))
{
    return new FunctorMem<int, int, Target>(t, f);
}

int main(void)
{
    Target t;
    Functor<int, int> *a = make(&t, &Target::add);
    Functor<int, int> *m = make(&t, &Target::mul);

    printf("pmfstore: direct        add(10)=%d mul(10)=%d (expect 11 30)\n",
           t.add(10), t.mul(10));
    fflush(stdout);
    printf("pmfstore: through store add(10)=%d (expect 11)\n", (*a)(10));
    fflush(stdout);
    printf("pmfstore: through store mul(10)=%d (expect 30)\n", (*m)(10));
    fflush(stdout);
    printf("pmfstore: both forms work\n");
    delete a;
    delete m;
    return 0;
}
