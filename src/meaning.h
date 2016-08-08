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
#pragma once

#include "activation.h"
#include "sexp.h"
#include "stdlib.h"
#include <exception>
#include <functional>
#include <iostream>
#include <memory>

// A trampoline is the result of evaluating a meaning. The result
// will either be a concrete value or a thunk representing the
// next thing to evaluate.
struct Trampoline {
  enum Kind { Value, Thunk };

  Kind kind;
  union {
    Sexp *value;
    Sexp *activation;
  };
  Sexp *meaning;

  Trampoline(Sexp *value) : kind(Trampoline::Kind::Value), value(value) {}
  Trampoline(Sexp *act, Sexp *meaning)
      : kind(Trampoline::Kind::Thunk), activation(act), meaning(meaning) {}

  // Returns true if this trampoline is a value.
  inline bool IsValue() { return kind == Trampoline::Kind::Value; }

  // Returns true if this trampoline is a thunk.
  inline bool IsThunk() { return kind == Trampoline::Kind::Thunk; }
};

// A Meaning is an analyzed form of an s-expression. Meanings
// are eventually interpreted and executed directly. The result
// of semantic analysis is a meaning.
//
// This construction of meanings is heavily inspired by
// Fitzgen (Nick Fitzgerald)'s "Oxischeme", a Scheme implementation
// in Rust.
class Meaning {
public:
  // Evals this meaning using the given activation.
  virtual Trampoline Eval(Sexp *act) = 0;
  virtual void Dump(std::ostream &out) = 0;
  void Dump() { Dump(std::cout); }

  virtual ~Meaning() {}

  // Traces the managed pointers contained within this meaning.
  // Meanings that contain managed pointers must override this.
  virtual void TracePointers(std::function<void(Sexp **)> func) {
    UNUSED_PARAMETER(func);
  };
};

// A QuotedMeaning is a meaning for a quoted s-expression.
// A quoted s-expression, when evaluated, returns itself.
class QuotedMeaning : public Meaning {
private:
  Sexp *quoted;

public:
  QuotedMeaning(Sexp *quoted_value) : quoted(quoted_value) {}

  Trampoline Eval(Sexp *act) override;

  void TracePointers(std::function<void(Sexp **)> func) override {
    func(&quoted);
  }

  void Dump(std::ostream &out) override {
    out << "(meaning-quote ";
    quoted->Dump(out);
    out << ")";
  }

  Sexp *&Quoted() { return quoted; }
};

// A ReferenceMeaning is a meaning for a variable reference. When
// evaluated, this meaning looks up the reference in the given
// activation and returns the value.
class ReferenceMeaning : public Meaning {
private:
  size_t up_index;
  size_t right_index;

public:
  ReferenceMeaning(size_t up, size_t right)
      : up_index(up), right_index(right) {}

  Trampoline Eval(Sexp *act) override;

  void Dump(std::ostream &out) override {
    out << "(meaning-ref " << up_index << " " << right_index << ")";
  }
};

// A DefinitionMeaning is a meaning for the `define` form, which
// creates a new entry in the global activation.
class DefinitionMeaning : public Meaning {
private:
  size_t up_index;
  size_t right_index;
  Sexp *binding_value;

public:
  DefinitionMeaning(size_t up, size_t right, Sexp *value)
      : up_index(up), right_index(right), binding_value(value) {}

  Trampoline Eval(Sexp *act) override;
  void TracePointers(std::function<void(Sexp **)> func) override {
    func(&binding_value);
  }

  void Dump(std::ostream &out) override {
    assert(binding_value->IsMeaning());
    out << "(meaning-define " << up_index << " " << right_index << " ";
    binding_value->meaning->Dump(out);
    out << ")";
  }

  Sexp *&BindingValue() { return binding_value; }
};

// A SetMeaning is a meaning for the `set!` form, which sets a value
// in the target activation.
class SetMeaning : public Meaning {
private:
  size_t up_index;
  size_t right_index;
  Sexp *binding_value;

public:
  SetMeaning(size_t up, size_t right, Sexp *binding)
      : up_index(up), right_index(right), binding_value(binding) {}

  Trampoline Eval(Sexp *act) override;
  void TracePointers(std::function<void(Sexp **)> func) override {
    func(&binding_value);
  }

  void Dump(std::ostream &out) override {
    assert(binding_value->IsMeaning());
    out << "(meaning-set " << up_index << " " << right_index << " ";
    binding_value->meaning->Dump(out);
    out << ")";
  }

  Sexp *&BindingValue() { return binding_value; }
};

// A ConditionaMeaning is a meaning for the `if` form, which evaluates
// a condition and executes one of the two meanings based on if the
// result of the condition.
class ConditionalMeaning : public Meaning {
private:
  Sexp *condition;
  Sexp *true_branch;
  Sexp *false_branch;

public:
  ConditionalMeaning(Sexp *cond, Sexp *tb, Sexp *fb)
      : condition(cond), true_branch(tb), false_branch(fb) {}

  Trampoline Eval(Sexp *act) override;
  void TracePointers(std::function<void(Sexp **)> func) override {
    func(&condition);
    func(&true_branch);
    func(&false_branch);
  }

  void Dump(std::ostream &out) override {
    assert(condition->IsMeaning());
    assert(true_branch->IsMeaning());
    assert(false_branch->IsMeaning());
    out << "(meaning-if ";
    condition->meaning->Dump(out);
    out << " ";
    true_branch->meaning->Dump(out);
    out << " ";
    false_branch->meaning->Dump(out);
    out << ")";
  }

  Sexp *&Condition() { return condition; }
  Sexp *&TrueBranch() { return true_branch; }
  Sexp *&FalseBranch() { return false_branch; }
};

// A SequenceMeaning is a meaning for the `begin` form, which
// evaluates a number of forms for their side-effects only while
// returning the value of the final form.
class SequenceMeaning : public Meaning {
private:
  std::vector<Sexp *> body;
  Sexp *final_form;

public:
  SequenceMeaning(std::vector<Sexp *> body, Sexp * final)
      : body(std::move(body)), final_form(final) {}

  Trampoline Eval(Sexp *act) override;
  void TracePointers(std::function<void(Sexp **)> func) override {
    for (auto &form : body) {
      func(&form);
    }

    func(&final_form);
  }

  void Dump(std::ostream &out) override {
    assert(final_form->IsMeaning());
    out << "(meaning-sequence ";
    for (Sexp *b : body) {
      assert(b->IsMeaning());
      b->meaning->Dump(out);
      out << " ";
    }

    if (!final_form->IsEmpty()) {
      final_form->Dump(out);
    }

    out << ")";
  }

  std::vector<Sexp *> &Body() { return body; }
  Sexp *&FinalForm() { return final_form; }
};

// A LambdaMeaning is a meaning for the `lambda` form, which
// introduces a new function.
class LambdaMeaning : public Meaning {
private:
  size_t arity;
  bool is_variadic;
  Sexp *body;

public:
  LambdaMeaning(size_t arity, bool is_variadic, Sexp *body)
      : arity(arity), is_variadic(is_variadic), body(body) {}

  Trampoline Eval(Sexp *act) override;
  void TracePointers(std::function<void(Sexp **)> func) override {
    func(&body);
  }

  void Dump(std::ostream &out) override {
    assert(body->IsMeaning());
    out << "(meaning-lambda " << arity << " ";
    body->meaning->Dump(out);
    out << ")";
  }

  size_t Arity() const { return arity; }
  bool IsVariadic() const { return is_variadic; }
  Sexp *&Body() { return body; }
};

// An InvocationMeaning is a meaning for function calls, which
// is the normal cause of action when evaluating a list.
class InvocationMeaning : public Meaning {
private:
  Sexp *base;
  std::vector<Sexp *> arguments;

public:
  InvocationMeaning(Sexp *base, std::vector<Sexp *> args)
      : base(base), arguments(std::move(args)) {}

  Trampoline Eval(Sexp *act) override;

  void TracePointers(std::function<void(Sexp **)> func) override {
    func(&base);
    for (auto &arg : arguments) {
      func(&arg);
    }
  }

  void Dump(std::ostream &out) override {
    assert(base->IsMeaning());
    out << "(meaning-invocation ";

    base->Dump(out);
    out << " ";

    for (Sexp *b : arguments) {
      assert(b->IsMeaning());
      b->meaning->Dump(out);
      out << " ";
    }

    out << ")";
  }

  Sexp *&Base() { return base; }
  std::vector<Sexp *> &Arguments() { return arguments; }
};

// Completely evaluate a meaning, calling thunks repeatedly
// until a value is returned.
Sexp *Evaluate(Sexp *meaning, Sexp *act);