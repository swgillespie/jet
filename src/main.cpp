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
#include "analysis.h"
#include "builtins.h"
#include "contract.h"
#include "gc.h"
#include "interner.h"
#include "reader.h"
#include "sexp.h"
#include "options.h"
#include <fstream>
#include "options.h"

const char path_sep =
#ifdef _WIN32
    '\\';
#else
    '/';
#endif

int EvalFile(std::ifstream& input, Sexp *activation) {
  GC_HELPER_FRAME;
  GC_PROTECTED_LOCAL(read);
  GC_PROTECTED_LOCAL(meaning);
  GC_PROTECT(activation);

  while (1) {
    try {
      read = Read(input);
      if (read->IsEof()) {
        return 0;
      }

      meaning = Analyze(read);
      // std::cout << "analyzed: " << meaning->DumpString() << std::endl;
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

int ActualMain(char *filename) {
  GC_HELPER_FRAME;
  GC_PROTECTED_LOCAL(activation);

  activation = GcHeap::AllocateActivation(nullptr);
  g_global_activation = activation;
  GC_PROTECT(g_global_activation);
  LoadBuiltins(activation);

  std::string prelude_file = g_options.stdlib_path + path_sep + "prelude.jet";
  std::ifstream prelude(prelude_file);
  std::ifstream input(g_options.input_file);

  if (prelude.fail()) {
    std::cerr << "failed to open prelude (" << prelude_file << ")" << std::endl;
    return 1;
  }

  if (input.fail()) {
    std::cerr << "failed to open file: " << filename << std::endl;
    return 1;
  }

  int exitCode = EvalFile(prelude, activation);
  if (exitCode != 0) {
    return exitCode;
  }

  return EvalFile(input, activation);
}

void InitializeRuntime() {
  GcHeap::Initialize();
  SymbolInterner::Initialize();
  g_frames = new Frame("<global>", nullptr);
  g_current_frame = g_frames;
#ifdef DEBUG
  if (g_options.gc_stress) {
    GcHeap::ToggleStressMode();
  }

  if (g_options.heap_verify) {
    GcHeap::ToggleHeapVerifyMode();
  }

  g_contract_frames = new ContractFrame("<global>", nullptr);
  g_contract_current_frame = g_contract_frames;
#endif
  g_the_environment = new Environment();
}

int main(int argc, char **argv) {
  ParseOptions(argc, argv);
  ValidateOptions();
  InitializeRuntime();
  return ActualMain(argv[1]);
}
