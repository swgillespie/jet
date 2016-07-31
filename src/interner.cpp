//
// Created by Sean Gillespie on 7/26/16.
//

#include "interner.h"
#include "util.h"

SymbolInterner *g_the_interner;

size_t SymbolInterner::Intern(std::string str) {
  auto it = map.find(str);
  if (it != map.end()) {
    // this symbol has been interned already.
    return it->second;
  }

  // otherwise, we'll need to intern it.
  size_t idx = vec.size();
  map[str] = idx;
  vec.push_back(str);
  return idx;
}

const std::string &SymbolInterner::Get(size_t index) const {
  assert(index < vec.size());
  return vec[index];
}
