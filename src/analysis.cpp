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
#include "analysis.h"
#include "contract.h"
#include "gc.h"
#include "interner.h"
#include "options.h"

#include <iostream>

using namespace std::literals;

Environment *g_the_environment;

std::tuple<size_t, size_t> Environment::Get(size_t symbol) {
  // starting at the most recent lexical environment, search upwards
  // until we find what we're looking for.
  size_t up_index = 0;
  for (auto it = slot_map.rbegin(); it != slot_map.rend(); it++) {
    auto entry = it->find(symbol);
    if (entry != it->end()) {
      // we found it. the second entry of this map is the right_index
      return std::make_tuple(up_index, std::get<1>(entry->second));
    }

    up_index++;
  }

  if (g_options.emit_warnings) {
    std::cerr << "warning: possibly unbound symbol: "
              << SymbolInterner::GetSymbol(symbol) << std::endl;
  }

  return DefineGlobal(symbol);
}

void Environment::Define(size_t symbol) {
  std::unordered_map<size_t, std::tuple<bool, size_t>> &env = slot_map.back();
  size_t idx = env.size();
  env[symbol] = std::make_tuple(false, idx);
}

std::tuple<size_t, size_t> Environment::DefineGlobal(size_t symbol) {
  std::unordered_map<size_t, std::tuple<bool, size_t>> &env = slot_map.front();
  // if this symbol has already been defined (by either a duplicate define
  // or by a bind-after-reference), return the definition that already exists.
  if (env.find(symbol) != env.end()) {
    auto tup = env[symbol];
    return std::make_tuple(slot_map.size() - 1, std::get<1>(tup));
  }

  // otherwise, we'll define it here.
  size_t idx = env.size();
  env[symbol] = std::make_tuple(false, idx);
  return std::make_tuple(slot_map.size() - 1, idx);
}

bool Environment::IsMacro(size_t symbol) {
  for (auto it = slot_map.rbegin(); it != slot_map.rend(); it++) {
    auto entry = it->find(symbol);
    if (entry != it->end()) {
      return std::get<0>(entry->second);
    }
  }

  // this can happen if we're referencing a unbound identifier.
  // we'll emit an error later. just return false.
  return false;
}

void Environment::SetMacro(size_t symbol) {
  for (auto it = slot_map.rbegin(); it != slot_map.rend(); it++) {
    auto entry = it->find(symbol);
    if (entry != it->end()) {
      std::get<0>(entry->second) = true;
    }
  }

  UNREACHABLE();
}

void Environment::EnterScope() { slot_map.emplace_back(); }

void Environment::ExitScope() { slot_map.pop_back(); }

void Environment::Dump() {
  size_t index = 0;
  for (auto it = slot_map.rbegin(); it != slot_map.rend(); it++) {
    std::cout << "frame: " << index << std::endl;
    for (auto it2 = it->begin(); it2 != it->end(); it2++) {
      std::cout << "  offset: " << std::get<1>(it2->second);
      std::cout << ", symbol: " << SymbolInterner::GetSymbol(it2->first);
    }
  }
  std::cout << std::endl;
}

static Sexp *AnalyzeAtom(Sexp *form) {
  CONTRACT { PRECONDITION(!form->IsCons()); }

  GC_HELPER_FRAME;
  GC_PROTECT(form);

  if (form->IsAlreadyQuoted()) {
    // A note about this and the other blocks of code in this
    // file that have this pattern:
    //
    // Previously in this codebase, this was
    //   GcHeap::AllocateMeaning(new QuotedMeaning(form));
    // This is wrong because it introduces a GC hole. This
    // frame currently protects form, so it won't be collected,
    // but it /will/ be relocated, and this QuotedMeaning won't
    // be scanned for pointers, meaning that the form that QuotedMeaning
    // has is now invalid.
    //
    // The solution to this problem is to allocate the Meaning separately,
    // protect any pointers that it has, and /then/ call AllocateMeaning.
    // In this way we can ensure that any pointers contained in
    // under-construction
    // meanings get relocated if they need to.
    //
    // Note that we only have to do this if the meaning we are creating
    // contains managed pointers. If it doesn't, we don't care - we can
    // do whatever we want.
    QuotedMeaning *meaning = new QuotedMeaning(form);
    GC_PROTECT(meaning->Quoted());
    return GcHeap::AllocateMeaning(meaning);
  }

  if (form->IsSymbol()) {
    size_t up_index;
    size_t right_index;
    std::tie(up_index, right_index) =
        g_the_environment->Get(form->symbol_value);
    return GcHeap::AllocateMeaning(new ReferenceMeaning(up_index, right_index));
  }

  PANIC("unknown s-expression being analyzed");
}

static Sexp *AnalyzeQuote(Sexp *form) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);

  bool is_proper;
  size_t len;
  std::tie(is_proper, len) = form->Length();
  if (!is_proper || len != 1) {
    throw JetRuntimeException("invalid quote form");
  }

  QuotedMeaning *meaning = new QuotedMeaning(form->Car());
  GC_PROTECT(meaning->Quoted());
  return GcHeap::AllocateMeaning(meaning);
}

static Sexp *AnalyzeBegin(Sexp *form) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);
  GC_PROTECTED_LOCAL(last);
  GC_PROTECTED_LOCAL_VECTOR(body);

  if (!form->IsProperList()) {
    throw JetRuntimeException("invalid begin form");
  }

  form->ForEach([&](Sexp *form) {
    GC_HELPER_FRAME;
    GC_PROTECT(form);
    GC_PROTECTED_LOCAL(analysis_result);
    analysis_result = Analyze(form);

    body.push_back(analysis_result);
  });

  last = body.back();
  body.pop_back();
  SequenceMeaning *meaning = new SequenceMeaning(std::move(body), last);
  GC_PROTECT_VECTOR(meaning->Body());
  GC_PROTECT(meaning->FinalForm());
  return GcHeap::AllocateMeaning(meaning);
}

static Sexp *AnalyzeDefineFunction(Sexp *form) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);
  GC_PROTECTED_LOCAL(args);
  GC_PROTECTED_LOCAL(name);
  // this is the special define form,
  // (define (fun arg1 arg2 arg3) ...)
  //         ^----- form is pointing to this.
  //
  // we're going to do a quick and dirty transform into
  // (define fun (lambda arg1 arg2 arg3) ...)
  if (!form->Car()->IsCons()) {
    throw JetRuntimeException("invalid define-function form");
  }

  args = form->Car();
  if (!args->Car()->IsSymbol()) {
    throw JetRuntimeException("invalid define-function form");
  }

  name = args->Car();

  GC_PROTECTED_LOCAL(define_sym);
  GC_PROTECTED_LOCAL(define_form);
  GC_PROTECTED_LOCAL(define_args);
  GC_PROTECTED_LOCAL(lambda);
  GC_PROTECTED_LOCAL(lambda_sym);
  GC_PROTECTED_LOCAL(lambda_args);
  lambda_sym = GcHeap::AllocateSymbol(SymbolInterner::Lambda);
  lambda_args = GcHeap::AllocateCons(form->Cadr(), GcHeap::AllocateEmpty());
  lambda_args = GcHeap::AllocateCons(args->Cdr(), lambda_args);
  lambda = GcHeap::AllocateCons(lambda_sym, lambda_args);
  define_sym = GcHeap::AllocateSymbol(SymbolInterner::Define);
  define_args = GcHeap::AllocateCons(lambda, GcHeap::AllocateEmpty());
  define_args = GcHeap::AllocateCons(name, define_args);
  define_form = GcHeap::AllocateCons(define_sym, define_args);

  return Analyze(define_form);
}

static Sexp *AnalyzeDefine(Sexp *form, bool is_macro) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);
  GC_PROTECTED_LOCAL(binding);

  bool is_proper;
  size_t len;
  std::tie(is_proper, len) = form->Length();
  if (!is_proper) {
    throw JetRuntimeException("invalid define form");
  }

  if (!form->Car()->IsSymbol()) {
    // if the car isn't a symbol, it's either a list or
    // something invalid.
    return AnalyzeDefineFunction(form);
  }

  // if we're here, the length of the form must be two.
  if (len != 2) {
    throw JetRuntimeException("invalid define form");
  }

  size_t sym_name = form->Car()->symbol_value;
  size_t up_index;
  size_t right_index;
  std::tie(up_index, right_index) = g_the_environment->DefineGlobal(sym_name);
  if (is_macro) {
    g_the_environment->SetMacro(sym_name);
  }
  binding = Analyze(form->Cadr());
  DefinitionMeaning *meaning =
      new DefinitionMeaning(up_index, right_index, binding);
  GC_PROTECT(meaning->BindingValue());
  return GcHeap::AllocateMeaning(meaning);
}

static Sexp *AnalyzeIf(Sexp *form) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);
  GC_PROTECTED_LOCAL(cond);
  GC_PROTECTED_LOCAL(true_branch);
  GC_PROTECTED_LOCAL(false_branch);

  bool is_proper;
  size_t len;
  std::tie(is_proper, len) = form->Length();
  if (!is_proper || len < 2 || len > 3) {
    throw JetRuntimeException("invalid if form");
  }

  cond = Analyze(form->Car());
  true_branch = Analyze(form->Cadr());

  if (len == 3) {
    false_branch = Analyze(form->Caddr());
  } else if (len == 2) {
    // this is questionable and goes against
    // the guidance earlier in the file about protecting
    // meanings, but this is safe because we are quoting
    // the empty list, which doesn't get relocated.
    false_branch =
        GcHeap::AllocateMeaning(new QuotedMeaning(GcHeap::AllocateEmpty()));
  }

  ConditionalMeaning *meaning =
      new ConditionalMeaning(cond, true_branch, false_branch);
  GC_PROTECT(meaning->Condition());
  GC_PROTECT(meaning->TrueBranch());
  GC_PROTECT(meaning->FalseBranch());
  return GcHeap::AllocateMeaning(meaning);
}

static Sexp *AnalyzeLambda(Sexp *form) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);
  GC_PROTECTED_LOCAL(params);
  GC_PROTECTED_LOCAL_VECTOR(body);

  bool is_proper;
  size_t len;
  std::tie(is_proper, len) = form->Length();
  if (!is_proper || len < 2) {
    throw JetRuntimeException(
        "invalid lambda form: form not appropriate number of elements");
  }

  bool is_variadic;
  size_t required_params = 0;
  params = form->Car();
  if (!params->IsCons() && !params->IsEmpty()) {
    // the parameter list doesn't have to actually
    // be a list, it can be a symbol.
    if (!params->IsSymbol()) {
      throw JetRuntimeException("invalid lambda form: parameter description "
                                "must be a list or a symbol");
    }

    is_variadic = true;
    required_params = 0;
  } else if (params->IsEmpty()) {
    is_variadic = false;
    required_params = 0;
  } else {
    std::tie(is_proper, len) = form->Car()->Length();
    is_variadic = !is_proper;
    required_params = len;
  }

  g_the_environment->EnterScope();

  GC_PROTECTED_LOCAL(cursor);
  cursor = params;
  while (cursor->IsCons() && !cursor->Cdr()->IsEmpty()) {
    if (!cursor->Car()->IsSymbol()) {
      throw JetRuntimeException("invalid lambda form: parameter not a symbol");
    }

    g_the_environment->Define(cursor->Car()->symbol_value);
    cursor = cursor->Cdr();
  }

  if (cursor->IsCons()) {
    g_the_environment->Define(cursor->Car()->symbol_value);
  } else if (!cursor->IsCons() && !cursor->IsEmpty()) {
    // this means that the parameter list was improper, and the cdr
    // of the cursor is the "rest" parameter.
    if (!cursor->IsSymbol()) {
      throw JetRuntimeException("invalid lambda form: parameter not a sybmol");
    }

    g_the_environment->Define(cursor->symbol_value);
  }

  form->Cdr()->ForEach([&](Sexp *body_form) {
    GC_HELPER_FRAME;
    GC_PROTECT(body_form);

    body.push_back(Analyze(body_form));
  });

  g_the_environment->ExitScope();

  GC_PROTECTED_LOCAL(last);
  last = body.back();
  body.pop_back();
  SequenceMeaning *seq = new SequenceMeaning(body, last);
  GC_PROTECT_VECTOR(seq->Body());
  GC_PROTECT(seq->FinalForm());

  GC_PROTECTED_LOCAL(seq_meaning);
  seq_meaning = GcHeap::AllocateMeaning(seq);
  LambdaMeaning *meaning =
      new LambdaMeaning(required_params, is_variadic, seq_meaning);
  GC_PROTECT(meaning->Body());
  return GcHeap::AllocateMeaning(meaning);
}

static Sexp *AnalyzeSet(Sexp *form) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);
  GC_PROTECTED_LOCAL(binding);

  bool is_proper;
  size_t len;
  std::tie(is_proper, len) = form->Length();
  if (!is_proper || len != 2) {
    throw JetRuntimeException("invalid set! form");
  }

  if (!form->Car()->IsSymbol()) {
    throw JetRuntimeException("invalid define form");
  }

  size_t up_index;
  size_t right_index;
  size_t sym_name = form->Car()->symbol_value;
  std::tie(up_index, right_index) = g_the_environment->Get(sym_name);
  binding = Analyze(form->Cadr());
  SetMeaning *meaning = new SetMeaning(up_index, right_index, binding);
  GC_PROTECT(meaning->BindingValue());
  return GcHeap::AllocateMeaning(meaning);
}

static Sexp *AnalyzeInvocation(Sexp *form) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);
  GC_PROTECTED_LOCAL(base);
  GC_PROTECTED_LOCAL_VECTOR(arguments);

  bool is_proper;
  std::tie(is_proper, std::ignore) = form->Length();
  if (!is_proper) {
    throw JetRuntimeException("invalid invocation");
  }

  if (form->Car()->IsSymbol()) {
    if (g_the_environment->IsMacro(form->Car()->symbol_value)) {
      // this is a macro that needs to be expanded and evaluated
      // right now, before we analyze the arguments.
      GC_PROTECTED_LOCAL(macro_expansion);
      NYI();
      return Analyze(macro_expansion);
    }
  }

  base = Analyze(form->Car());
  form->Cdr()->ForEach([&](Sexp *arg) {
    GC_HELPER_FRAME;
    GC_PROTECT(arg);
    GC_PROTECTED_LOCAL(analyze_result);

    analyze_result = Analyze(arg);
    arguments.push_back(analyze_result);
  });

  InvocationMeaning *meaning =
      new InvocationMeaning(base, std::move(arguments));
  GC_PROTECT(meaning->Base());
  GC_PROTECT_VECTOR(meaning->Arguments());
  return GcHeap::AllocateMeaning(meaning);
}

static Sexp *Quasiquote(Sexp *form) {
  // a quasiquote form such as
  //   `(a ,b ,@c)
  // reads like
  //   (quote (a (unquote b) (unquote-splicing c))
  // which we in turn transform into
  //   (cons (list 'a) (cons (list b) c))
  //
  // we do this transform recursively:
  //   (quasiquote (car . cdr))                  => (cons (quasiquote-single
  //   car) (quasiquote cdr))
  //   (quasiquote-single car)                   => (list (quoted car))
  //   (quasiquote-single (unquote car)          => (list car)
  //   (quasiquote-single (unquote-splicing car) => car
  UNUSED_PARAMETER(form);
  NYI();
}

static Sexp *AnalyzeQuasiquote(Sexp *form) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);
  GC_PROTECTED_LOCAL(arg);
  GC_PROTECTED_LOCAL(transformed);

  size_t len;
  bool is_proper;
  std::tie(is_proper, len) = form->Length();
  if (!is_proper || len != 1) {
    throw JetRuntimeException("invalid quasiquote form");
  }

  arg = form->Car();
  if (!arg->IsCons()) {
    // anything not a list is simply quoted, exactly like quote does.
    QuotedMeaning *meaning = new QuotedMeaning(arg);
    GC_PROTECT(meaning->Quoted());
    return GcHeap::AllocateMeaning(meaning);
  }

  transformed = Quasiquote(arg);
  return Analyze(transformed);
}

// The `let` form is not a fundamental form
// in Jet and hopefully will be replaced by a macro
// in the future.
//
// As such, the `let` form doesn't translate into a LetMeaning,
// since those don't exist - instead we rewrite the let as
// a lambda, like the macro will do.
static Sexp *AnalyzeLet(Sexp *form) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);
  GC_PROTECTED_LOCAL(bindings);
  GC_PROTECTED_LOCAL_VECTOR(variables);
  GC_PROTECTED_LOCAL_VECTOR(binding_values);
  GC_PROTECTED_LOCAL_VECTOR(body_values);
  // (let ((var binding) ...) body ...)

  size_t len;
  bool is_proper;
  std::tie(is_proper, len) = form->Length();
  if (!is_proper) {
    throw JetRuntimeException("invalid let form");
  }

  if (!form->Car()->IsCons()) {
    throw JetRuntimeException("invalid let form: bad binding list");
  }

  bindings = form->Car();
  if (!bindings->IsProperList()) {
    throw JetRuntimeException("invalid let form: bad binding list");
  }

  g_the_environment->EnterScope();
  bindings->ForEach([&](Sexp *binding) {
    GC_HELPER_FRAME;
    GC_PROTECT(binding);

    if (!binding->IsCons() || !binding->IsProperList()) {
      throw JetRuntimeException("invalid let form: bad binding list");
    }

    size_t binding_len;
    std::tie(std::ignore, binding_len) = binding->Length();
    if (binding_len != 2) {
      throw JetRuntimeException("invalid let form: bad binding list");
    }

    if (!binding->Car()->IsSymbol()) {
      throw JetRuntimeException("invalid let form: bad variable name");
    }

    variables.push_back(binding->Car());
    g_the_environment->Define(binding->Car()->symbol_value);
  });

  assert(form->Cdr()->IsCons());
  form->Cdr()->ForEach([&](Sexp *body) {
    GC_HELPER_FRAME;
    GC_PROTECT(body);

    body_values.push_back(Analyze(body));
  });

  g_the_environment->ExitScope();

  // once we've exited the scope, we visit binding values.
  bindings->ForEach([&](Sexp *binding) {
    GC_HELPER_FRAME;
    GC_PROTECT(binding);

    binding_values.push_back(Analyze(binding->Cadr()));
  });

  GC_PROTECTED_LOCAL(last);
  last = body_values.back();
  body_values.pop_back();

  SequenceMeaning *body_meaning_value =
      new SequenceMeaning(std::move(body_values), last);
  GC_PROTECT_VECTOR(body_meaning_value->Body());
  GC_PROTECT(body_meaning_value->FinalForm());
  GC_PROTECTED_LOCAL(body_meaning);
  body_meaning = GcHeap::AllocateMeaning(body_meaning_value);
  LambdaMeaning *base_value =
      new LambdaMeaning(variables.size(), false, body_meaning);
  GC_PROTECT(base_value->Body());
  GC_PROTECTED_LOCAL(lambda_meaning);
  lambda_meaning = GcHeap::AllocateMeaning(base_value);
  InvocationMeaning *call_meaning =
      new InvocationMeaning(lambda_meaning, std::move(binding_values));
  GC_PROTECT(call_meaning->Base());
  GC_PROTECT_VECTOR(call_meaning->Arguments());
  return GcHeap::AllocateMeaning(call_meaning);
}

Sexp *AnalyzeShortCircuit(Sexp *form) {
  CONTRACT {
    PRECONDITION(form->IsCons());
    PRECONDITION(form->Car()->IsSymbol());
    PRECONDITION(form->Car()->symbol_value == SymbolInterner::And ||
                 form->Car()->symbol_value == SymbolInterner::Or);
  }

  GC_HELPER_FRAME;
  GC_PROTECT(form);
  GC_PROTECTED_LOCAL_VECTOR(args);

  bool is_proper;
  std::tie(is_proper, std::ignore) = form->Length();

  if (!is_proper) {
    throw JetRuntimeException("invalid short-circuiting form");
  }

  form->Cdr()->ForEach(
      [&](Sexp *argument) { args.push_back(Analyze(argument)); });

  if (form->Car()->symbol_value == SymbolInterner::And) {
    AndMeaning *meaning = new AndMeaning(args);
    GC_PROTECT_VECTOR(meaning->Arguments());
    return GcHeap::AllocateMeaning(meaning);
  } else {
    assert(form->Car()->symbol_value == SymbolInterner::Or);
    OrMeaning *meaning = new OrMeaning(args);
    GC_PROTECT_VECTOR(meaning->Arguments());
    return GcHeap::AllocateMeaning(meaning);
  }
}

Sexp *Analyze(Sexp *form) {
  CONTRACT {
    PRECONDITION(form != nullptr);
    PRECONDITION(g_the_environment != nullptr);
  }

  GC_HELPER_FRAME;
  GC_PROTECT(form);

  if (!form->IsCons()) {
    return AnalyzeAtom(form);
  }

  if (form->Car()->IsSymbol()) {
    switch (form->Car()->symbol_value) {
    case SymbolInterner::Quote:
      return AnalyzeQuote(form->Cdr());
    case SymbolInterner::Begin:
      return AnalyzeBegin(form->Cdr());
    case SymbolInterner::Define:
      return AnalyzeDefine(form->Cdr(), false);
    case SymbolInterner::DefMacro:
      return AnalyzeDefine(form->Cdr(), true);
    case SymbolInterner::If:
      return AnalyzeIf(form->Cdr());
    case SymbolInterner::Lambda:
      return AnalyzeLambda(form->Cdr());
    case SymbolInterner::SetBang:
      return AnalyzeSet(form->Cdr());
    case SymbolInterner::Quasiquote:
      return AnalyzeQuasiquote(form->Cdr());
    case SymbolInterner::Let:
      return AnalyzeLet(form->Cdr());
    case SymbolInterner::And:
    case SymbolInterner::Or:
      return AnalyzeShortCircuit(form);
    default:
      return AnalyzeInvocation(form);
    }
  }

  // this is some sort of goofy invocation, like
  // ((lambda (x) (+ x 1)) 1)
  return AnalyzeInvocation(form);
}
