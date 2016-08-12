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
#include "meaning.h"
#include "sexp.h"
#include <cstring>
#include <memory>
#include <vector>

// When execution crosses into native code, the interpreter
// will need to inform the GC that certain values are live
// since the GC otherwise would not be able to recover that information.
// The "Frame" is a object whose lifetime is tied to a native stack frame
// that has the ability to protect pointers that are on the stack of
// a native function.
//
// When a GC occurs, the GC will precisely scan the stack of frames and
// relocate any pointers that are protected by the frame. Using an unprotected
// variable in a non-GC-safe environment is a bug waiting to happen!
class Frame {
private:
  const char *name;
  std::vector<std::tuple<const char *, Sexp **>> roots;
  std::vector<std::tuple<const char *, std::vector<Sexp *> *>> vector_roots;
  Frame *parent;

public:
  Frame(const char *name, Frame *parent)
      : name(name), roots(), parent(parent) {}
  void Root(Sexp **pointer, const char *var_name) {
    roots.push_back(std::make_tuple(var_name, pointer));
  }
  void Root(std::vector<Sexp *> *vec, const char *var_name) {
    vector_roots.push_back(std::make_tuple(var_name, vec));
  }

  void
  TracePointers(std::function<void(std::tuple<const char *, Sexp **>)> func) {
    for (auto &it : roots) {
      func(it);
    }

    for (auto &vec : vector_roots) {
      for (auto &root : *std::get<1>(vec)) {
        Sexp **loc = &root;
        func(std::make_tuple(std::get<0>(vec), loc));
      }
    }
  }

  Frame *GetParent() { return parent; }
  const char *GetName() const { return name; }
};

extern Frame *g_frames;
extern Frame *g_current_frame;

// A FrameProtector is placed on the stack of a native function.
// It will insert a new frame onto the global list of frames upon construction
// and remove the frame from the list on destruction.
class FrameProtector {
private:
  Frame *protected_frame;

public:
  FrameProtector(const char *name) {
    assert(g_frames != nullptr);
    assert(g_current_frame != nullptr);
    Frame *frame = new Frame(name, g_current_frame);
    g_current_frame = frame;
    protected_frame = frame;
  }

  ~FrameProtector() {
    assert(g_current_frame != nullptr);
    g_current_frame = g_current_frame->GetParent();
    assert(g_current_frame != nullptr);
    delete protected_frame;
  }

  void ProtectValue(Sexp **value, const char *name) {
    assert(protected_frame != nullptr);
    protected_frame->Root(value, name);
  }

  void ProtectVector(std::vector<Sexp *> *vec, const char *name) {
    assert(protected_frame != nullptr);
    protected_frame->Root(vec, name);
  }

  FrameProtector(const FrameProtector &) = delete;
  FrameProtector &operator=(const FrameProtector &) = delete;
};

// These three macros are the means by which the interpreter should interact
// with
// the above two classes.

// GC_HELPER_FRAME introduces a new frame protector for the current stack frame.
// It pushes a new stack frame onto the stack that will get disposed upon the
// destruction of the native stack frame.
//
// It's important to note that it is not strictly necessary to introduce a GC
// helper frame if there are no pointers to protect, or if one can be sure that
// a GC will not occur.
#define GC_HELPER_FRAME FrameProtector __frame_prot(__func__)

// GC_PROTECT protects an lvalue from garbage collector and ensures that it will
// be relocated correctly upon garbage collection.
// The value MUST be an lvalue - in order
// for the GC to be able to relocate the pointer, the pointer must have a
// location in memory.
#define GC_PROTECT(value) __frame_prot.ProtectValue(&value, #value);

// GC_PROTECT_VECTOR does what GC_PROTECT does, but for a vector.
#define GC_PROTECT_VECTOR(value) __frame_prot.ProtectVector(&value, #value);

// GC_PROTECTED_LOCAL declares a new local that is protected. It will be
// automatically relocated upon a GC.
#define GC_PROTECTED_LOCAL(value)                                              \
  Sexp *value = nullptr;                                                       \
  __frame_prot.ProtectValue(&value, #value);

// GC_PROTECTED_LOCAL_VECTOR declares a new vector of locals that is protected.
#define GC_PROTECTED_LOCAL_VECTOR(value)                                       \
  std::vector<Sexp *> value;                                                   \
  __frame_prot.ProtectVector(&value, #value)

// The GC does not currently require the use of a write barrier,
// but the VM has support for it anyway. When (if) we switch to
// a generationala collector, we'll need it.
#define GC_WRITE_BARRIER(ref, value)

class GcHeap;
extern GcHeap *g_heap;
extern Sexp g_the_empty_sexp;

// The GC heap. The two entry points, Allocate and Collect, are used
// to allocate and force collections respectively. ToggleStress is used
// to toggle the stress mode of the GC.
class GcHeap {
private:
  class GcHeapImpl;
  std::unique_ptr<GcHeapImpl> pimpl;
  GcHeap();
  ~GcHeap();

  Sexp *Allocate(bool should_finalize);
  void Collect();
  void ToggleStress();
  void ToggleHeapVerify();

public:
  // Initializes the GC.
  static void Initialize() {
    assert(g_heap == nullptr);
    g_heap = new GcHeap();
    // initialize the singleton empty sexp
    g_the_empty_sexp.kind = Sexp::Kind::EMPTY;
  }

  // Allocates a Cons on the managed heap, given a car
  // and a cdr.
  static Sexp *AllocateCons(Sexp *car, Sexp *cdr) {
    GC_HELPER_FRAME;
    GC_PROTECT(car);
    GC_PROTECT(cdr);

    assert(g_heap != nullptr);
    Sexp *s = g_heap->Allocate(false);
    assert(s != nullptr);
    s->kind = Sexp::Kind::CONS;
    s->cons.car = car;
    s->cons.cdr = cdr;
    return s;
  }

  // Since there's only one possible value for the empty
  // s-expression, the same value is re-used.
  static Sexp *AllocateEmpty() { return &g_the_empty_sexp; }

  // Allocates a fixnum on the heap.
  static Sexp *AllocateFixnum(jet_fixnum num) {
    assert(g_heap != nullptr);
    Sexp *s = g_heap->Allocate(false);
    assert(s != nullptr);
    s->kind = Sexp::Kind::FIXNUM;
    s->fixnum_value = num;
    return s;
  }

  // Allocates a symbol on the heap.
  static Sexp *AllocateSymbol(size_t sym) {
    assert(g_heap != nullptr);
    Sexp *s = g_heap->Allocate(false);
    assert(s != nullptr);
    s->kind = Sexp::Kind::SYMBOL;
    s->symbol_value = sym;
    return s;
  }

  // Allocates a string on the heap.
  static Sexp *AllocateString(jet_string str) {
    assert(g_heap != nullptr);
    Sexp *s = g_heap->Allocate(true);
    assert(s != nullptr);
    s->kind = Sexp::Kind::STRING;
    s->string_value = strdup(str);
    return s;
  }

  // Allocates a boolean on the heap.
  static Sexp *AllocateBool(jet_bool b) {
    assert(g_heap != nullptr);
    Sexp *s = g_heap->Allocate(false);
    assert(s != nullptr);
    s->kind = Sexp::Kind::BOOL;
    s->bool_value = b;
    return s;
  }

  static Sexp *AllocateEof() {
    assert(g_heap != nullptr);
    Sexp *s = g_heap->Allocate(false);
    assert(s != nullptr);
    s->kind = Sexp::Kind::END_OF_FILE;
    return s;
  }

  static Sexp *AllocateActivation(Sexp *parent) {
    GC_HELPER_FRAME;
    GC_PROTECT(parent);

    assert(g_heap != nullptr);
    Sexp *s = g_heap->Allocate(true);
    assert(s != nullptr);
    s->kind = Sexp::Kind::ACTIVATION;
    s->activation = new Activation(parent);
    return s;
  }

  static Sexp *AllocateFunction(LambdaMeaning *func, Sexp *activation) {
    GC_HELPER_FRAME;
    GC_PROTECT(activation);

    assert(g_heap != nullptr);
    assert(func != nullptr);
    // TODO(segilles) fix the leak
    Sexp *s = g_heap->Allocate(false);
    assert(s != nullptr);
    s->kind = Sexp::Kind::FUNCTION;
    s->function.func_meaning = func;
    s->function.activation = activation;
    return s;
  }

  static Sexp *AllocateNativeFunction(NativeFunction func) {
    assert(g_heap != nullptr);
    Sexp *s = g_heap->Allocate(true);
    assert(s != nullptr);
    s->kind = Sexp::Kind::NATIVE_FUNCTION;
    s->native_function = func;
    return s;
  }

  static Sexp *AllocateMeaning(Meaning *meaning) {
    assert(g_heap != nullptr);
    assert(meaning != nullptr);
    // TODO(segilles) fix the leak
    Sexp *s = g_heap->Allocate(false);
    assert(s != nullptr);
    s->kind = Sexp::Kind::MEANING;
    s->meaning = meaning;
    return s;
  }

  // Forces a collection.
  static void ForceCollect() {
    assert(g_heap != nullptr);
    g_heap->Collect();
  }

  // Toggles the stress status of the GC.
  // When the stress mode is on, a GC will
  // be done on every allocation.
  static void ToggleStressMode() {
    assert(g_heap != nullptr);
    g_heap->ToggleStress();
  }

  static void ToggleHeapVerifyMode() {
    assert(g_heap != nullptr);
    g_heap->ToggleHeapVerify();
  }
};
