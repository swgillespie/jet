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
  std::vector<std::unordered_map<size_t, std::tuple<bool, size_t>>> slot_map;

public:
  Environment() { slot_map.emplace_back(); }

  ~Environment() {}

  Environment(const Environment &) = delete;
  Environment &operator=(const Environment &) = delete;

  // Attempts to look up a symbol in this environment. If
  // the symbol is not found, throws a JetRuntimeException.
  std::tuple<size_t, size_t> Get(size_t symbol);

  // Returns whether or not the given symbol refers to a macro
  // in the current environment.
  bool IsMacro(size_t symbol);

  // Indicate that this symbol refers to a macro.
  void SetMacro(size_t symbol);

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
