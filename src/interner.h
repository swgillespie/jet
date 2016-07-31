#pragma once

#include "util.h"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using namespace std::literals;

class SymbolInterner;
extern SymbolInterner *g_the_interner;

// The Interner is responsible for interning symbols. Symbols and strings are
// similar to one another, but symbols are guaranteed to be interned - two
// symbols can quickly and efficiently be checked for equality by comparing
// their intern indexes.
class SymbolInterner {
private:
  std::unordered_map<std::string, size_t> map;
  std::vector<std::string> vec;

public:
  SymbolInterner() {}
  ~SymbolInterner() {}

  SymbolInterner(const SymbolInterner &) = delete;
  SymbolInterner &operator=(const SymbolInterner &) = delete;

  // Interns a string. The SymbolInterner makes takes ownership of the
  // argument and interns it, returning an index. This index
  // can be used to recover the copied string using the
  // Get and operator[] methods.
  size_t Intern(std::string str);

  // Retrieves an interned string, given an index. Panics when the index is not
  // valid,
  // which should not happen during normal operation.
  //
  // IMPORTANT: since string_view isn't around yet in the C++ version I'm
  // targeting,
  // I'm using a reference for what is an immutable view into the interner's
  // data
  // structures. Consider this an immutable borrow, in the Rust parlance - any
  // mutation
  // of the SymbolInterner while this pointer is live will invalidate the
  // reference.
  const std::string &Get(size_t index) const;

  // Delegates to Get.
  const std::string &operator[](size_t index) const { return Get(index); }

  static void Initialize() {
    assert(g_the_interner == nullptr);
    g_the_interner = new SymbolInterner();
    g_the_interner->Intern("quote");
    g_the_interner->Intern("define");
    g_the_interner->Intern("set!");
    g_the_interner->Intern("lambda");
    g_the_interner->Intern("if");
    g_the_interner->Intern("begin");
  }

  static size_t InternSymbol(std::string str) {
    assert(g_the_interner != nullptr);
    return g_the_interner->Intern(str);
  }

  static const std::string &GetSymbol(size_t index) {
    assert(g_the_interner != nullptr);
    return g_the_interner->Get(index);
  }

  // All of these constants reflect the order in which they are interned
  // int he initialization function. Don't mess this order up!
  static const size_t Quote = 0;
  static const size_t Define = 1;
  static const size_t SetBang = 2;
  static const size_t Lambda = 3;
  static const size_t If = 4;
  static const size_t Begin = 5;
};