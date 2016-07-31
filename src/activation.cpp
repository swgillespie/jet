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
#include "activation.h"
#include "contract.h"

#include <iostream>

Activation *g_global_activation;

Sexp *Activation::Get(size_t up_index, size_t right_index) {
  CONTRACT { FORBID_GC; }

  Activation *cursor = this;
  for (size_t i = 0; i < up_index; i++) {
    assert(cursor != nullptr);
    cursor = cursor->parent;
  }

  assert(right_index < cursor->slots.size());
  Sexp *result = cursor->slots[right_index];
  assert(result != nullptr);
  return result;
}

void Activation::Set(size_t up_index, size_t right_index, Sexp *value) {
  CONTRACT { FORBID_GC; }

  Activation *cursor = this;
  for (size_t i = 0; i < up_index; i++) {
    assert(cursor != nullptr);
    cursor = cursor->parent;
  }

  if (right_index >= cursor->slots.size()) {
    // we're defining something and need to
    // expand our slots.
    size_t diff = cursor->slots.size() - right_index;
    for (size_t i = 0; i <= diff; i++) {
      cursor->slots.emplace_back();
    }
  }

  assert(right_index < cursor->slots.size());
  cursor->slots[right_index] = value;
}

void Activation::TracePointers(std::function<void(Sexp **)> func) {
  for (auto it = slots.begin(); it != slots.end(); it++) {
    func(&(*it));
  }

  if (parent != nullptr) {
    parent->TracePointers(func);
  }
}
