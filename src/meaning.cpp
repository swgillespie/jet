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
#include "meaning.h"
#include "contract.h"
#include "gc.h"

Trampoline QuotedMeaning::Eval(Sexp *act) {
  CONTRACT {
    FORBID_GC;
    PRECONDITION(act->IsActivation());
  }

  UNUSED_PARAMETER(act);
  return Trampoline(quoted);
}

Trampoline ReferenceMeaning::Eval(Sexp *act) {
  CONTRACT {
    FORBID_GC;
    PRECONDITION(act->IsActivation());
  }

  return Trampoline(act->activation->Get(up_index, right_index));
}

Trampoline DefinitionMeaning::Eval(Sexp *act) {
  CONTRACT { PRECONDITION(act->IsActivation()); }

  GC_HELPER_FRAME;
  GC_PROTECT(act);
  GC_PROTECTED_LOCAL(value);
  value = Evaluate(binding_value, act);
  act->activation->Set(up_index, right_index, value);
  return Trampoline(GcHeap::AllocateEmpty());
}

Trampoline SetMeaning::Eval(Sexp *act) {
  CONTRACT { PRECONDITION(act->IsActivation()); }

  GC_HELPER_FRAME;
  GC_PROTECT(act);
  GC_PROTECTED_LOCAL(value);

  value = Evaluate(binding_value, act);
  act->activation->Set(up_index, right_index, value);
  return Trampoline(GcHeap::AllocateEmpty());
}

Trampoline ConditionalMeaning::Eval(Sexp *act) {
  CONTRACT { PRECONDITION(act->IsActivation()); }

  GC_HELPER_FRAME;
  GC_PROTECT(act);
  GC_PROTECTED_LOCAL(cond);

  cond = Evaluate(condition, act);
  if (cond->IsTruthy()) {
    return Trampoline(act, true_branch);
  } else {
    return Trampoline(act, false_branch);
  }
}

Trampoline SequenceMeaning::Eval(Sexp *act) {
  CONTRACT { PRECONDITION(act->IsActivation()); }

  GC_HELPER_FRAME;
  GC_PROTECT(act);

  for (auto &entry : body) {
    Evaluate(entry, act);
  }

  return Trampoline(act, final_form);
}

Trampoline LambdaMeaning::Eval(Sexp *act) {
  CONTRACT { PRECONDITION(act->IsActivation()); }

  GC_HELPER_FRAME;
  GC_PROTECT(act);

  return GcHeap::AllocateFunction(this, act);
}

// Reverse an s-expression in place, classic interview question style.
static void ReverseSexp(Sexp **head) {
  CONTRACT { FORBID_GC; }

  Sexp *prev = GcHeap::AllocateEmpty();
  Sexp *cursor = *head;
  Sexp *next = GcHeap::AllocateEmpty();
  while (!cursor->IsEmpty()) {
    assert(cursor->IsCons());
    next = cursor->cons.cdr;
    cursor->cons.cdr = prev;
    prev = cursor;
    cursor = next;
  }

  *head = prev;
}

Trampoline InvocationMeaning::Eval(Sexp *act) {
  CONTRACT { PRECONDITION(act->IsActivation()); }

  GC_HELPER_FRAME;
  GC_PROTECT(act);
  GC_PROTECTED_LOCAL(child_act);
  GC_PROTECTED_LOCAL(called_expr);
  GC_PROTECTED_LOCAL(eval_arg)

  called_expr = Evaluate(base, act);
  if (!called_expr->IsFunction() && !called_expr->IsNativeFunction() &&
      !called_expr->IsMacro()) {
    throw JetRuntimeException("called a non-callable value");
  }

  if (called_expr->IsFunction() || called_expr->IsMacro()) {
    if (called_expr->function.func_meaning->IsVariadic()) {
      // if this is a variadic function, all we need to do is
      // ensure we called this with at least the number of required args.
      if (arguments.size() < called_expr->function.func_meaning->Arity()) {
        throw JetRuntimeException("arity mismatch");
      }
    } else {
      // otherwise, we need an exact match.
      if (arguments.size() != called_expr->function.func_meaning->Arity()) {
        throw JetRuntimeException("arity mismatch");
      }
    }

    // we have to
    //   1) evaluate the args (if this isn't a macro),
    //   2) create a new activation,
    //   3) place the arguments into the new activation.
    // if this function is variadic, we need all "rest" arguments
    // to be bound to the final arg (arity + 1)
    size_t right_index = 0;
    child_act = GcHeap::AllocateActivation(called_expr->function.activation);
    auto it = arguments.begin();
    for (size_t i = 0; i < called_expr->function.func_meaning->Arity();
         it++, i++) {
      eval_arg = Evaluate(*it, act);
      child_act->activation->Set(0, right_index++, eval_arg);
    }

    // if there are still arguments left, this function is variadic
    // and we have more work to do.
    if (it != arguments.end()) {
      assert(called_expr->function.func_meaning->IsVariadic());
      GC_PROTECTED_LOCAL(args_list);
      args_list = GcHeap::AllocateEmpty();
      for (; it != arguments.end(); it++) {
        eval_arg = Evaluate(*it, act);

        // we're building up this list in reverse, because it's
        // much more efficient to append to the front of linked
        // lists than to the back.
        args_list = GcHeap::AllocateCons(eval_arg, args_list);
      }

      // now, we have to reverse the list we just made.
      // this is the first time I've ever had to do this algorithm
      // outside of a job interview. nice!
      ReverseSexp(&args_list);
      child_act->activation->Set(0, right_index, args_list);
    }

    if (arguments.size() == 0 &&
        called_expr->function.func_meaning->IsVariadic()) {
      // we have to give the called function an empty list if it's not called
      // with any arguments.
      child_act->activation->Set(0, 0, GcHeap::AllocateEmpty());
    }

    // tail call the function
    return Trampoline(child_act, called_expr->function.func_meaning->Body());
  }

  // this is a native function call.
  // arity check, first up.
  if (arguments.size() != called_expr->native_function.arity) {
    throw JetRuntimeException("arity mismatch");
  }

  // second, eval all our arguments and store them in a vector.
  GC_PROTECTED_LOCAL_VECTOR(args);
  for (auto &argument : arguments) {
    eval_arg = Evaluate(argument, act);
    args.push_back(eval_arg);
  }

  // we can skip activation creation since native functions
  // don't use activations.

  // invoke our native function, passing the vector's data pointer
  // as an argument.
  GC_PROTECTED_LOCAL(ret);
  ret = (*called_expr->native_function.func)(args.data());

  // we can't tail call native functions.
  return Trampoline(ret);
}

Sexp *Evaluate(Sexp *meaning, Sexp *act) {
  GC_HELPER_FRAME;

  Trampoline result(act, meaning);

  // important - the activation and value fields in Trampoline
  // have the same memory location, so only one GC_PROTECT
  // is required.
  GC_PROTECT(result.value);
  GC_PROTECT(result.meaning);

  while (result.IsThunk()) {
    assert(result.activation->IsActivation());
    assert(result.meaning->IsMeaning());
    result = result.meaning->meaning->Eval(result.activation);
  }

  return result.value;
}