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