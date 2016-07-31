//
// Created by Sean Gillespie on 7/26/16.
//

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
