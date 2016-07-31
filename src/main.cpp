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
#include "analysis.h"
#include "builtins.h"
#include "contract.h"
#include "gc.h"
#include "interner.h"
#include "reader.h"
#include "sexp.h"
#include <fstream>
#include <iostream>

int ActualMain(char *filename) {
  GC_HELPER_FRAME;
  GC_PROTECTED_LOCAL(read);
  GC_PROTECTED_LOCAL(activation);
  GC_PROTECTED_LOCAL(meaning);

  activation = GcHeap::AllocateActivation(nullptr);
  g_global_activation = activation->activation;
  LoadBuiltins(activation);

  std::ifstream input(filename);
  while (1) {
    try {
      read = Read(input);
      if (read->IsEof()) {
        return 0;
      }

      meaning = Analyze(read);
      Evaluate(meaning, activation);
    } catch (JetRuntimeException &exn) {
      std::cerr << "runtime error: " << exn.what() << std::endl;
      return 1;
    } catch (ReadException &exn) {
      std::cerr << "read error: " << exn.what() << std::endl;
      return 1;
    }
  }
}

void InitializeRuntime() {
  GcHeap::Initialize();
  SymbolInterner::Initialize();
  // GcHeap::ToggleStressMode();
  g_frames = new Frame("<global>", nullptr);
  g_current_frame = g_frames;
#ifdef DEBUG
  g_contract_frames = new ContractFrame("<global>", nullptr);
  g_contract_current_frame = g_contract_frames;
#endif
  g_the_environment = new Environment();
}

int main(int argc, char **argv) {
  InitializeRuntime();
  if (argc != 2) {
    std::cout << "usage: jet <file.jet>" << std::endl;
    return 1;
  }

  return ActualMain(argv[1]);
}