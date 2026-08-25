/* pmf2 -- the ScummVM shape, split across translation units.
 *
 * A single-TU version of this (pmfstore) passes at -O2, presumably because the compiler can see
 * everything and fold it. ScummVM's real case has the functor CONSTRUCTED in one translation unit
 * and CALLED through an abstract base in another, with the templates instantiated in both -- which
 * is where the miscompile of the {ptr,adj} copy survives.
 */
#ifndef PMF2_H
#define PMF2_H

struct Target {
    virtual ~Target() {}
    int add(int x);
    int mul(int x);
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

/* Built in pmf2_lib.cpp, called from pmf2_main.cpp. */
Functor<int, int> *make_add(Target *t);
Functor<int, int> *make_mul(Target *t);

#endif
