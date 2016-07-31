#pragma once

#include "sexp.h"
#include <functional>
#include <vector>

// An activation is the runtime variable storage for a scope.
// A new activation is introduced for every new syntactic scope.
class Activation {
private:
  Activation *parent;
  std::vector<Sexp *> slots;

public:
  Activation(Activation *parent_act)
      : parent(parent_act), slots() {}

  ~Activation() {}

  Activation(const Activation &) = delete;
  Activation &operator=(const Activation &) = delete;

  // Activation retrievals are encoded as a tuple of two
  // numbers: an "up" index and a "right" index. The "up"
  // index is the distance from the use of the variable
  // to the def of the variable - we have to go "up"
  // some number of activations to get to the activation
  // that contains the variable's location. The "right"
  // index is the slot number of that variable in the
  // target activation.
  //
  // This method panics if the up_index is not valid.
  // It is up to the semantic analysis stage to generate
  // correct coordinates for all activations except the
  // global activation. The global activation will permit
  // invalid right_indexes.
  Sexp *Get(size_t up_index, size_t right_index);

  // Sets an activation slot to the given value. Generally
  // only possible through the `set!` special form.
  void Set(size_t up_index, size_t right_index, Sexp *value);

  // Traces all of the pointers held live by this activation,
  // as required by the GC. This function traces parent
  // pointers as well, so it is only necessary to call this
  // function on the leaf activation.
  void TracePointers(std::function<void(Sexp **)> func);
};

// Eval needs to know the global activation currently in use,
// so it's stored here.
extern Activation* g_global_activation;