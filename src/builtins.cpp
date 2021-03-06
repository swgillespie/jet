// Copyright (c) 2016 Sean Gillespie
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// afurnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
#include "builtins.h"
#include "analysis.h"
#include "contract.h"
#include "gc.h"
#include "interner.h"
#include "reader.h"

// Many thanks to "tinlyx" (http://stackoverflow.com/a/27826081) for
// inspiring this template hack.
template <typename T>
struct FunctionTrait : public FunctionTrait<decltype(&T::operator())> {};

template <typename ReturnType, typename... Args>
struct FunctionTrait<ReturnType (*)(Args...)> {
  typedef std::function<ReturnType(Args...)> f_type;
};

template <typename L>
static typename FunctionTrait<L>::f_type MakeFunction(L l) {
  return (typename FunctionTrait<L>::f_type)(l);
}

// Loads a single builtin function into the given activation.
template <typename F>
void LoadSingleBuiltin(Sexp *activation, const char *name, F func) {
  CONTRACT { PRECONDITION(activation->IsActivation()); }

  GC_HELPER_FRAME;
  GC_PROTECT(activation);
  GC_PROTECTED_LOCAL(alloced_func);

  auto stdfnc = MakeFunction(func);
  alloced_func = GcHeap::AllocateNativeFunction(stdfnc);
  size_t up, right;
  std::tie(up, right) =
      g_the_environment->DefineGlobal(SymbolInterner::InternSymbol(name));
  activation->activation->Set(up, right, alloced_func);
}

Sexp *Builtin_Add(Sexp *fst, Sexp *snd) {
  GC_HELPER_FRAME;
  GC_PROTECT(fst);
  GC_PROTECT(snd);

  if (!fst->IsFixnum()) {
    throw JetRuntimeException("type error: not a fixnum");
  }

  if (!snd->IsFixnum()) {
    throw JetRuntimeException("type error: not a fixnum");
  }

  return GcHeap::AllocateFixnum(fst->fixnum_value + snd->fixnum_value);
}

Sexp *Builtin_Sub(Sexp *fst, Sexp *snd) {
  GC_HELPER_FRAME;
  GC_PROTECT(fst);
  GC_PROTECT(snd);

  if (!fst->IsFixnum()) {
    throw JetRuntimeException("type error: not a fixnum");
  }

  if (!snd->IsFixnum()) {
    throw JetRuntimeException("type error: not a fixnum");
  }

  return GcHeap::AllocateFixnum(fst->fixnum_value - snd->fixnum_value);
}

Sexp *Builtin_Mul(Sexp *fst, Sexp *snd) {
  GC_HELPER_FRAME;
  GC_PROTECT(fst);
  GC_PROTECT(snd);

  if (!fst->IsFixnum()) {
    throw JetRuntimeException("type error: not a fixnum");
  }

  if (!snd->IsFixnum()) {
    throw JetRuntimeException("type error: not a fixnum");
  }

  return GcHeap::AllocateFixnum(fst->fixnum_value * snd->fixnum_value);
}

Sexp *Builtin_Div(Sexp *fst, Sexp *snd) {
  GC_HELPER_FRAME;
  GC_PROTECT(fst);
  GC_PROTECT(snd);

  if (!fst->IsFixnum()) {
    throw JetRuntimeException("type error: not a fixnum");
  }

  if (!snd->IsFixnum()) {
    throw JetRuntimeException("type error: not a fixnum");
  }

  if (snd->fixnum_value == 0) {
    throw JetRuntimeException("divided by zero");
  }

  return GcHeap::AllocateFixnum(fst->fixnum_value / snd->fixnum_value);
}

Sexp *Builtin_Car(Sexp *cons) {
  GC_HELPER_FRAME;
  GC_PROTECT(cons);

  if (!cons->IsCons()) {
    throw JetRuntimeException("type error: not a pair");
  }

  return cons->Car();
}

Sexp *Builtin_Cdr(Sexp *cons) {
  GC_HELPER_FRAME;
  GC_PROTECT(cons);

  if (!cons->IsCons()) {
    throw JetRuntimeException("type error: not a pair");
  }

  return cons->Cdr();
}

Sexp *Builtin_Cons(Sexp *fst, Sexp *snd) {
  GC_HELPER_FRAME;
  GC_PROTECT(fst);
  GC_PROTECT(snd);

  return GcHeap::AllocateCons(fst, snd);
}

Sexp *Builtin_Read() { return Read(std::cin); }

Sexp *Builtin_Eval(Sexp *form) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);
  GC_PROTECTED_LOCAL(analyzed);
  GC_PROTECTED_LOCAL(act);

  // TODO(segilles) this is a kludge due to the fact
  // that eval introduces a new activation. Not sure
  // if this is necessary.
  g_the_environment->EnterScope();
  analyzed = Analyze(form);
  g_the_environment->ExitScope();

  // this creates a new child activation. Not sure if that's
  // right, but it works.
  act = GcHeap::AllocateActivation(g_global_activation);
  return Evaluate(analyzed, act);
}

Sexp *Builtin_Print(Sexp *form) {
  CONTRACT { FORBID_GC; }

  // print strings without quotes.
  if (form->IsString()) {
    std::cout << form->string_value;
  } else {
    form->Dump(std::cout);
  }

  std::cout.flush();
  return GcHeap::AllocateEmpty();
}

Sexp *Builtin_Println(Sexp *form) {
  CONTRACT { FORBID_GC; }

  // print strings without quotes.
  if (form->IsString()) {
    std::cout << form->string_value;
  } else {
    form->Dump(std::cout);
  }

  std::cout << std::endl;
  return GcHeap::AllocateEmpty();
}

Sexp *Builtin_Error(Sexp *form) {
  CONTRACT { FORBID_GC; }

  if (!form->IsString()) {
    throw JetRuntimeException("error called with non-string value");
  }

  throw JetRuntimeException(form->string_value);
}

Sexp *Builtin_EofObject_P(Sexp *form) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);

  return GcHeap::AllocateBool(form->IsEof());
}

Sexp *Builtin_EmptyP(Sexp *form) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);

  return GcHeap::AllocateBool(form->IsEmpty());
}

Sexp *Builtin_Not(Sexp *form) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);

  return GcHeap::AllocateBool(!form->IsTruthy());
}

Sexp *Builtin_PairP(Sexp *form) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);

  return GcHeap::AllocateBool(form->IsCons());
}

Sexp *Builtin_EqP(Sexp *first, Sexp *second) {
  GC_HELPER_FRAME;
  GC_PROTECT(first);
  GC_PROTECT(second);

  return GcHeap::AllocateBool(first == second);
}

bool EqualityHelper(Sexp *first, Sexp *second) {
  CONTRACT { FORBID_GC; }

  if (first->IsCons() && second->IsCons()) {
    return EqualityHelper(first->Car(), second->Car())
        && EqualityHelper(first->Cdr(), second->Cdr());
  }

  if (!first->IsCons() && !second->IsCons()) {
    if (first->IsFixnum() && second->IsFixnum()) {
      return first->fixnum_value == second->fixnum_value;
    }

    if (first->IsSymbol() && second->IsSymbol()) {
      return first->symbol_value == second->symbol_value;
    }

    if (first->IsString() && second->IsString()) {
      return strcmp(first->string_value, second->string_value) == 0;
    }

    if (first->IsBool() && second->IsBool()) {
      return first->bool_value == second->bool_value;
    }

    // (eq? first second) implies (equal? first second)
    if (first == second) {
      return true;
    }
  }

  return false;
}

Sexp *Builtin_EqualP(Sexp *first, Sexp *second) {
  GC_HELPER_FRAME;
  GC_PROTECT(first);
  GC_PROTECT(second);

  return GcHeap::AllocateBool(EqualityHelper(first, second));
}

Sexp *Builtin_SetCar(Sexp *cons, Sexp *car) {
  CONTRACT { FORBID_GC; }

  if (!cons->IsCons()) {
    throw JetRuntimeException("type error: not a cons");
  }

  GC_WRITE_BARRIER(cons, car);
  cons->cons.car = car;

  return GcHeap::AllocateEmpty();
}

Sexp *Builtin_SetCdr(Sexp *cons, Sexp *cdr) {
  CONTRACT { FORBID_GC; }

  if (!cons->IsCons()) {
    throw JetRuntimeException("type error: not a cons");
  }

  GC_WRITE_BARRIER(cons, cdr);
  cons->cons.cdr = cdr;

  return GcHeap::AllocateEmpty();
}

void LoadBuiltins(Sexp *activation) {
  CONTRACT { PRECONDITION(activation->IsActivation()); }

  GC_HELPER_FRAME;
  GC_PROTECT(activation);

  LoadSingleBuiltin(activation, "-primitive-add", Builtin_Add);
  LoadSingleBuiltin(activation, "-primitive-sub", Builtin_Sub);
  LoadSingleBuiltin(activation, "-primitive-mul", Builtin_Mul);
  LoadSingleBuiltin(activation, "-primitive-div", Builtin_Div);
  LoadSingleBuiltin(activation, "car", Builtin_Car);
  LoadSingleBuiltin(activation, "cdr", Builtin_Cdr);
  LoadSingleBuiltin(activation, "cons", Builtin_Cons);
  LoadSingleBuiltin(activation, "read", Builtin_Read);
  LoadSingleBuiltin(activation, "eval", Builtin_Eval);
  LoadSingleBuiltin(activation, "print", Builtin_Print);
  LoadSingleBuiltin(activation, "println", Builtin_Println);
  LoadSingleBuiltin(activation, "error", Builtin_Error);
  LoadSingleBuiltin(activation, "eof-object?", Builtin_EofObject_P);
  LoadSingleBuiltin(activation, "empty?", Builtin_EmptyP);
  LoadSingleBuiltin(activation, "not", Builtin_Not);
  LoadSingleBuiltin(activation, "pair?", Builtin_PairP);
  LoadSingleBuiltin(activation, "eq?", Builtin_EqP);
  LoadSingleBuiltin(activation, "equal?", Builtin_EqualP);
  LoadSingleBuiltin(activation, "set-car!", Builtin_SetCar);
  LoadSingleBuiltin(activation, "set-cdr!", Builtin_SetCdr);
}
