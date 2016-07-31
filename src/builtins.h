#pragma once

#include "sexp.h"

// Loads all of our builtins into the chosen activation,
// assuming that the activation that we've been given is the global
// activation, i.e. the activation where `defines` go.
void LoadBuiltins(Sexp *activation);