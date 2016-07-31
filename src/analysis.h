// The analysis phase of execution turns an s-expression that will be
// executed into a Meaning, which is then interpreted. During this phase
// we will eliminate variable references by translating them into coordinates,
// which we will use at runtime to load and store variable references.

#pragma once

#include "meaning.h"
#include <memory>
#include <unordered_map>
#include <vector>

// An environment is a symbol table used for semantic analysis.
class Environment {
private:
  // The slot map maps symbols to the slot that they have been
  // assigned in this environment.
  std::vector<std::unordered_map<size_t, size_t>> slot_map;

public:
  Environment() { slot_map.emplace_back(); }

  ~Environment() {}

  Environment(const Environment &) = delete;
  Environment &operator=(const Environment &) = delete;

  // Attempts to look up a symbol in this environment. If
  // the symbol is not found, throws a JetRuntimeException.
  std::tuple<size_t, size_t> Get(size_t symbol);

  // Defines a symbol in the current environment.
  void Define(size_t symbol);

  // Defines a symbol in the global environment, returning
  // the up and right index into the this symbol.
  std::tuple<size_t, size_t> DefineGlobal(size_t symbol);

  // Pushes a new lexical scope onto the stack.
  void EnterScope();

  // Pops a lexical scope from the stack.
  void ExitScope();

  // Dumps this environment to standard out.
  void Dump();
};

extern Environment *g_the_environment;

// Analyzes an s-expression and creates a Meaning from it,
// suitable to be executed. This method throws a JetRuntimeException
// if it encounters an ill-formed program.
Sexp *Analyze(Sexp *form);
