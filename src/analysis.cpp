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
      return std::make_tuple(up_index, entry->second);
    }

    up_index++;
  }

  throw JetRuntimeException("possibly unbound symbol: "s +
                            SymbolInterner::GetSymbol(symbol));
}

void Environment::Define(size_t symbol) {
  std::unordered_map<size_t, size_t> &env = slot_map.back();
  size_t idx = env.size();
  env[symbol] = idx;
}

std::tuple<size_t, size_t> Environment::DefineGlobal(size_t symbol) {
  std::unordered_map<size_t, size_t> &env = slot_map.front();
  size_t idx = env.size();
  env[symbol] = idx;
  return std::make_tuple(slot_map.size() - 1, idx);
}

void Environment::EnterScope() { slot_map.emplace_back(); }

void Environment::ExitScope() { slot_map.pop_back(); }

void Environment::Dump() {
  size_t index = 0;
  for (auto it = slot_map.rbegin(); it != slot_map.rend(); it++) {
    std::cout << "frame: " << index << std::endl;
    for (auto it2 = it->begin(); it2 != it->end(); it2++) {
      std::cout << "  offset: " << it2->second;
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

static Sexp *AnalyzeDefine(Sexp *form) {
  GC_HELPER_FRAME;
  GC_PROTECT(form);
  GC_PROTECTED_LOCAL(binding);

  bool is_proper;
  size_t len;
  std::tie(is_proper, len) = form->Length();
  if (!is_proper || len != 2) {
    throw JetRuntimeException("invalid define form");
  }

  if (!form->Car()->IsSymbol()) {
    throw JetRuntimeException("invalid define form");
  }

  size_t sym_name = form->Car()->symbol_value;
  size_t up_index;
  size_t right_index;
  std::tie(up_index, right_index) = g_the_environment->DefineGlobal(sym_name);
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
  if (!is_proper || len != 3) {
    throw JetRuntimeException("invalid if form");
  }

  cond = Analyze(form->Car());
  true_branch = Analyze(form->Cadr());
  false_branch = Analyze(form->Caddr());
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
  GC_PROTECTED_LOCAL(body);

  bool is_proper;
  size_t len;
  std::tie(is_proper, len) = form->Length();
  if (!is_proper || len != 2) {
    throw JetRuntimeException(
        "invalid lambda form: form not appropriate number of elements");
  }

  bool is_variadic = false;
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

    g_the_environment->Define(cursor->symbol_value);
    cursor = cursor->Cdr();
  }

  if (!cursor->IsCons() && !cursor->IsEmpty()) {
    // this means that the parameter list was improper, and the cdr
    // of the cursor is the "rest" parameter.
    if (!cursor->IsSymbol()) {
      throw JetRuntimeException("invalid lambda form: parameter not a sybmol");
    }

    g_the_environment->Define(cursor->symbol_value);
  }

  body = Analyze(form->Cadr());
  g_the_environment->ExitScope();
  std::tie(std::ignore, len) = params->Length();
  LambdaMeaning *meaning =
      new LambdaMeaning(required_params, is_variadic, body);
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
  binding = Analyze(form->Cdr());
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
      return AnalyzeDefine(form->Cdr());
    case SymbolInterner::If:
      return AnalyzeIf(form->Cdr());
    case SymbolInterner::Lambda:
      return AnalyzeLambda(form->Cdr());
    case SymbolInterner::SetBang:
      return AnalyzeSet(form->Cdr());
    default:
      return AnalyzeInvocation(form);
    }
  }

  // this is some sort of goofy invocation, like
  // ((lambda (x) (+ x 1)) 1)
  return AnalyzeInvocation(form);
}
