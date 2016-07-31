// Copyright (c) 2016 Sean Gillespie
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// afurnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
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

#include "util.h"
#include <functional>
#include <iostream>
#include <sstream>
#include <utility>

typedef bool jet_bool;
typedef int jet_fixnum;
typedef const char *jet_string;

struct Sexp;
struct Cons {
  Sexp *car;
  Sexp *cdr;
};

struct Function {
  class LambdaMeaning *func_meaning;
  Sexp *activation;
};

// This excessive use of templates is to ensure that we always safely
// marshal parameters from the interpreter to native code, by forcing
// that the compiler do it for us.
//
// This helper ultimately calls the native function by indexing the
// argument double-pointer given here, which is a pointer into a vector
// maintained by the runtime.
template <typename Ret, typename... Args, size_t... Index>
Ret NativeFunctionHelper(std::function<Ret(Args...)> wrapped, Sexp **args,
                         std::index_sequence<Index...>) {
  // GCC is silly and doesn't think that args is being used here,
  // despite it obviously being used by the parameter pack below.
#ifdef __GNUG__
  UNUSED_PARAMETER(args);
#endif
  return wrapped(args[Index]...);
}

// A wrapper around a native function that can be finalized by the runtime
// when it goes out of scope.
struct NativeFunction {
  std::function<Sexp *(Sexp **)> *func;
  size_t arity;

  NativeFunction(std::function<Sexp *(Sexp **)> *func, size_t arity)
      : func(func), arity(arity) {}

  template <typename Ret, typename... Args>
  NativeFunction(std::function<Ret(Args...)> wrapped) {
    arity = sizeof...(Args);
    func = new std::function<Sexp *(Sexp **)>([=](Sexp **args) {
      return NativeFunctionHelper(wrapped, args,
                                  std::make_index_sequence<sizeof...(Args)>{});
    });
  }

  NativeFunction() = default;
  ~NativeFunction() = default;
};

// An s-expression. All values at runtime are represented
// by this structure.
struct Sexp {
  enum Kind {
    EMPTY,
    CONS,
    SYMBOL,
    STRING,
    FIXNUM,
    BOOL,
    END_OF_FILE,
    ACTIVATION,
    FUNCTION,
    NATIVE_FUNCTION,
    MEANING,
  };

  Kind kind;
  union {
    // Cons, a linked list of values.
    Cons cons;
    // Symbol, an interned string value.
    size_t symbol_value;
    // String, a string value.
    jet_string string_value;
    // Fixnum, a fixed-sized integer value.
    jet_fixnum fixnum_value;
    // Bool, a boolean value.
    jet_bool bool_value;
    // An activation.
    class Activation *activation;
    // A native function to be called.
    NativeFunction native_function;
    // A Jet function to be called.
    Function function;
    // A meaning. Generally not exposed to the user,
    // but it's here so that it can be GC'd.
    class Meaning *meaning;
  };

  // Padding to ensure that the size of this structure evenly
  // divides a page. Also serves as a useful checksum.
  uint64_t padding;

  // Returns true if this sexp is the empty sexp.
  inline bool IsEmpty() const { return kind == Sexp::Kind::EMPTY; }

  // Returns true if this sexp is a cons sexp.
  inline bool IsCons() const { return kind == Sexp::Kind::CONS; }

  // Returns true if this sexp is a symbol.
  inline bool IsSymbol() const { return kind == Sexp::Kind::SYMBOL; }

  // Returns true if this sexp is a fixnum.
  inline bool IsFixnum() const { return kind == Sexp::Kind::FIXNUM; }

  // Returns true if this sexp is a bool.
  inline bool IsBool() const { return kind == Sexp::Kind::BOOL; }

  // Returns true if this sexp is a string.
  inline bool IsString() const { return kind == Sexp::Kind::STRING; }

  // Returns true if this sexp is the EOF object.
  inline bool IsEof() const { return kind == Sexp::Kind::END_OF_FILE; }

  // Returns true if this sexp is an activation.
  inline bool IsActivation() const { return kind == Sexp::Kind::ACTIVATION; }

  // Returns true if this sexp is a native function.
  inline bool IsNativeFunction() const {
    return kind == Sexp::Kind::NATIVE_FUNCTION;
  }

  // Returns true if this sexp is a function.
  inline bool IsFunction() const { return kind == Sexp::Kind::FUNCTION; }

  // Returns true if this sexp is a meaning.
  inline bool IsMeaning() const { return kind == Sexp::Kind::MEANING; }

  // Returns true if this sexp evaluates to itself when evaluated.
  // This includes most primitives.
  inline bool IsAlreadyQuoted() const {
    return !(IsEmpty() || IsCons() || IsSymbol());
  }

  inline Sexp *Car() const {
    assert(IsCons());
    return cons.car;
  }

  inline Sexp *Cdr() const {
    assert(IsCons());
    return cons.cdr;
  }

  inline Sexp *Cadr() const {
    assert(IsCons());
    assert(cons.cdr->IsCons());
    return cons.cdr->cons.car;
  }

  inline Sexp *Caddr() const {
    assert(IsCons());
    assert(cons.cdr->IsCons());
    assert(cons.cdr->cons.cdr->IsCons());
    return cons.cdr->cons.cdr->cons.car;
  }

  // Iterates through a proper list.
  void ForEach(std::function<void(Sexp *)> func);

  // Returns a tuple of whether or not the list is proper and the list's length.
  // A length of 0 on an improper list indicates it's not a list, while an
  // improper
  // list may have a nonzero length as well.
  inline std::tuple<bool, size_t> Length() const {
    if (!IsCons()) {
      // if it's not a list, it has length 0.
      return std::make_tuple(false, 0);
    }

    const Sexp *cursor = this;
    size_t count = 0;
    while (true) {
      if (cursor->IsEmpty()) {
        break;
      }

      if (!cursor->IsCons()) {
        return std::make_tuple(false, count);
      }

      count++;
      cursor = cursor->cons.cdr;
    }

    return std::make_tuple(true, count);
  }

  // Returns true if this is a proper list, false otherwise.
  // A list is proper if every car is a Cons or Empty.
  inline bool IsProperList() const {
    if (IsEmpty())
      return true;
    bool is_proper;
    std::tie(is_proper, std::ignore) = Length();
    return is_proper;
  }

  // Returns true of this sexp is "truthy", or evaluates to true
  // when used as the condition of the `if` special form.
  inline bool IsTruthy() const { return !(IsBool() && !this->bool_value); }

  // Trace all of the pointers contained in this sexp.
  void TracePointers(std::function<void(Sexp **)> func);

  // Finalizes this sexp. Should only be called by the garbage collector.
  void Finalize();

  // Dumps a debug representation of this sexp to the given ostream.
  void Dump(std::ostream &stream) const;

  // Dumps a debug representation of this sexp to standard out.
  void Dump() const { Dump(std::cout); }

  // Dumps a debug representation of this sexp to a string.
  std::string DumpString() const {
    std::ostringstream stream;
    Dump(stream);
    return stream.str();
  }

private:
  void DumpAtom(std::ostream &stream) const;
};

static_assert(PAGE_SIZE % sizeof(Sexp) == 0,
              "the size of sexp must evenly divide a page");
static_assert(sizeof(Sexp) < PAGE_SIZE,
              "the size of an s-expression must be less than a page");