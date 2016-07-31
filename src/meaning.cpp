#include "meaning.h"
#include "gc.h"
#include "contract.h"

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
  CONTRACT {
    PRECONDITION(act->IsActivation());
  }

  GC_HELPER_FRAME;
  GC_PROTECT(act);
  GC_PROTECTED_LOCAL(value);
  value = Evaluate(binding_value, act);
  act->activation->Set(up_index, right_index, value);
  return Trampoline(GcHeap::AllocateEmpty());
}


Trampoline SetMeaning::Eval(Sexp *act) {
  CONTRACT {
    PRECONDITION(act->IsActivation());
  }

  GC_HELPER_FRAME;
  GC_PROTECT(act);
  GC_PROTECTED_LOCAL(value);

  value = Evaluate(binding_value, act);
  act->activation->Set(up_index, right_index, value);
  return Trampoline(GcHeap::AllocateEmpty());
}


Trampoline ConditionalMeaning::Eval(Sexp *act) {
  CONTRACT {
    PRECONDITION(act->IsActivation());
  }

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
  CONTRACT {
    PRECONDITION(act->IsActivation());
  }

  GC_HELPER_FRAME;
  GC_PROTECT(act);

  for (auto& entry : body) {
    Evaluate(entry, act);
  }

  return Trampoline(act, final_form);
}


Trampoline LambdaMeaning::Eval(Sexp *act) {
  CONTRACT {
    PRECONDITION(act->IsActivation());
  }

  GC_HELPER_FRAME;
  GC_PROTECT(act);

  return GcHeap::AllocateFunction(this, act);
}


Trampoline InvocationMeaning::Eval(Sexp *act) {
  CONTRACT {
    PRECONDITION(act->IsActivation());
  }

  GC_HELPER_FRAME;
  GC_PROTECT(act);
  GC_PROTECTED_LOCAL(child_act);
  GC_PROTECTED_LOCAL(called_expr);
  GC_PROTECTED_LOCAL(eval_arg)

  called_expr = Evaluate(base, act);
  if (!called_expr->IsFunction() && !called_expr->IsNativeFunction()) {
    throw JetRuntimeException("called a non-callable value");
  }

  if (called_expr->IsFunction()) {
    if (arguments.size() != called_expr->function.func_meaning->Arity()) {
      throw JetRuntimeException("arity mismatch");
    }

    // we have to 1) evaluate the args, 2) create a new activation,
    // 3) place the arguments into the new activation
    size_t right_index = 0;
    child_act = GcHeap::AllocateActivation(called_expr->function.activation->activation);
    for (auto& argument : arguments) {
      eval_arg = Evaluate(argument, act);
      child_act->activation->Set(0, right_index++, eval_arg);
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
  for (auto& argument : arguments) {
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

Sexp* Evaluate(Sexp* meaning, Sexp* act) {
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