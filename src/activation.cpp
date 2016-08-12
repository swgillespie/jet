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
#include "activation.h"
#include "contract.h"
#include "gc.h"

#include <iostream>

Sexp *g_global_activation;

Sexp *Activation::Get(size_t up_index, size_t right_index) {
  CONTRACT { FORBID_GC; }

  Activation *cursor = this;
  for (size_t i = 0; i < up_index; i++) {
    assert(cursor != nullptr);
    assert(cursor->parent->IsActivation());
    cursor = cursor->parent->activation;
  }

  assert(cursor != nullptr);

  // at this point, if the right_index isn't valid (it's out of bounds
  // or it refers to a null slot), then it's an uninitialized read.
  //
  // unfortunately, at this point we've eliminated variable names,
  // so we can't give a very good error message. The analysis phase
  // should have emitted a warning when it saw that this could happen.
  if (right_index >= cursor->slots.size()) {
    throw JetRuntimeException("invalid read of uninitialized variable. Run "
                              "with --warnings for more details.");
  }

  Sexp *result = cursor->slots[right_index];
  if (result == nullptr) {
    throw JetRuntimeException("invalid read of uninitialized variable. Run "
                              "with --warnings for more details.");
  }

  return result;
}

void Activation::Set(size_t up_index, size_t right_index, Sexp *value) {
  CONTRACT { FORBID_GC; }

  // we should never (barring call/cc, not implemented) be putting
  // activations in another activation.
  assert(!value->IsActivation());

  Activation *cursor = this;
  for (size_t i = 0; i < up_index; i++) {
    assert(cursor != nullptr);
    assert(cursor->parent->IsActivation());
    cursor = cursor->parent->activation;
  }

  assert(cursor != nullptr);
  if (right_index >= cursor->slots.size()) {
    // we're defining something and need to
    // expand our slots.
    size_t diff = right_index - cursor->slots.size();
    for (size_t i = 0; i <= diff; i++) {
      cursor->slots.push_back(nullptr);
    }
  }

  assert(right_index < cursor->slots.size());
  cursor->slots[right_index] = value;
}

void Activation::TracePointers(std::function<void(Sexp **)> func) {
  Sexp **data = slots.data();
  for (size_t i = 0; i < slots.size(); i++) {
    if (data[i] != nullptr) {
      func(&data[i]);
    }
  }

  if (parent != nullptr) {
    func(&parent);
  }
}
