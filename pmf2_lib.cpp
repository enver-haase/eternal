#include "pmf2.h"

int Target::add(int x) { return x + 1; }
int Target::mul(int x) { return x * 3; }

Functor<int, int> *make_add(Target *t) { return new FunctorMem<int, int, Target>(t, &Target::add); }
Functor<int, int> *make_mul(Target *t) { return new FunctorMem<int, int, Target>(t, &Target::mul); }
