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
#include "sexp.h"
#include "activation.h"
#include "gc.h"
#include "interner.h"
#include "meaning.h"

#include <iostream>

void Sexp::Finalize() {
  if (IsString()) {
    free(const_cast<char *>(this->string_value));
    return;
  }

  if (IsActivation()) {
    delete this->activation;
    return;
  }

  if (IsNativeFunction()) {
    delete this->native_function.func;
    return;
  }

  if (IsFunction()) {
    // TODO this also screws up everything.
    //delete this->function.func_meaning;
    return;
  }

  if (IsMeaning()) {
    // TODO this screws up everything.
    // I'm choosing to leak meanings for now
    // until I figure out how to address this correctly.
    // delete this->meaning;

    return;
  }

  PANIC("finalized something that's not finalizable!");
}

void Sexp::DumpAtom(std::ostream &stream) const {
  if (IsString()) {
    stream << "\"" << this->string_value << "\"";
    return;
  }

  if (IsSymbol()) {
    stream << SymbolInterner::GetSymbol(this->symbol_value);
    return;
  }

  if (IsFixnum()) {
    stream << this->fixnum_value;
    return;
  }

  if (IsBool()) {
    if (this->bool_value) {
      stream << "#t";
    } else {
      stream << "#f";
    }

    return;
  }

  if (IsEof()) {
    stream << "#eof";
    return;
  }

  if (IsEmpty()) {
    stream << "()";
    return;
  }

  if (IsActivation()) {
    stream << "#<activation>";
    return;
  }

  if (IsFunction()) {
    stream << "#<function>";
    return;
  }

  if (IsNativeFunction()) {
    stream << "#<native function>";
    return;
  }

  if (IsMeaning()) {
    this->meaning->Dump(stream);
    return;
  }

  if (padding == 0xabababababababab) {
    PANIC("probable heap corruption detected!");
  }

  PANIC("unknown s-expression!");
}

void Sexp::Dump(std::ostream &stream) const {
  // Simple algorithm for pretty-printing an S-expression.
  // The algorithm goes like this:
  // 1) If self isn't a list, print it and return.
  // 2) If self is a list, print an open paren.
  //   2a) Print the car of the list.
  //   2b) If cdr is a list, recurse to step 2a with cdr as the new list
  //   2c) If cdr is (), print nothing,
  //   2d) If cdr is anything else, print a "." followed by a space
  //       and recursively print the cdr.
  if (!IsCons()) {
    DumpAtom(stream);
    return;
  }

  stream << "(";
  Sexp *car = this->cons.car;
  Sexp *cdr = this->cons.cdr;
  while (1) {
    assert(car != nullptr);
    assert(cdr != nullptr);
    car->Dump(stream);
    if (cdr->IsEmpty()) {
      break;
    }

    if (cdr->IsCons()) {
      car = cdr->cons.car;
      cdr = cdr->cons.cdr;
      stream << " ";
      continue;
    }

    stream << " . ";
    cdr->Dump(stream);
    break;
  }

  stream << ")";
}

void Sexp::TracePointers(std::function<void(Sexp **)> func) {
  switch (kind) {
  case Sexp::Kind::CONS:
    func(&this->cons.car);
    func(&this->cons.cdr);
    break;
  case Sexp::Kind::MACRO:
  case Sexp::Kind::FUNCTION:
    this->function.func_meaning->TracePointers(func);
    func(&this->function.activation);
    break;
  case Sexp::Kind::ACTIVATION:
    this->activation->TracePointers(func);
    break;
  case Sexp::Kind::MEANING:
    this->meaning->TracePointers(func);
  default:
    break;
  }
}

void Sexp::ForEach(std::function<void(Sexp *)> func) {
  // The helper frame is crucial here because we do not know
  // whether or not the passed-in lambda will trigger a GC.
  GC_HELPER_FRAME;
  GC_PROTECTED_LOCAL(cursor);

  assert(IsProperList());
  cursor = this;
  while (!cursor->IsEmpty()) {
    func(cursor->Car());
    cursor = cursor->Cdr();
  }
}
