#include <iostream>
#include "options.h"
#include "util.h"

Options g_options;

const char *usage =
    "Jet interpreter, by Sean Gillespie\n"
    "\n"
    "usage: jet <file.jet> [-h|--help] [-s|--stdlib-path] [--gc-stress]\n"
    "\n"
    "options:\n"
    "   -h|--help         Displays this message.\n"
    "   -s|--stdlib-path  Sets the path to the Jet standard library.\n"
    "   --gc-stress       Enables GC stress. Debug builds only.\n"
    "   --heap-verify     Verify the heap before and after a GC. Debug builds only.";


[[noreturn]] static void ParseError(const char *msg) {
  std::cout << "command line parse error: " << msg << std::endl;
  std::exit(1);
}

void ParseOptions(int argc, char **argv) {
  g_options = Options();
  int i = 1;
  bool seen_input_file = false;
  while (i < argc) {
    if (strcmp("-h", argv[i]) == 0 || strcmp("--help", argv[i]) == 0) {
      std::cout << usage << std::endl;
      std::exit(0);
    }

    if (strcmp("-s", argv[i]) == 0 || strcmp("--stdlib-path", argv[i]) == 0) {
      i++;
      if (i >= argc) {
        ParseError("expected argument for standard library path");
      }

      g_options.stdlib_path = argv[i++];
      continue;
    }

    if (strcmp("--gc-stress", argv[i]) == 0) {
      i++;
      g_options.gc_stress = true;
      continue;
    }

    if (strcmp("--heap-verify", argv[i]) == 0) {
      i++;
      g_options.heap_verify = true;
      continue;
    }

    if (!seen_input_file) {
      seen_input_file = true;
      g_options.input_file = argv[i++];
    } else {
      std::cout << "unexpected positional argument: " << argv[i] << std::endl;
      std::exit(1);
    }
  }
}

void ValidateOptions() {
  if (g_options.input_file.empty()) {
    std::cout << "error: no input file\n" << std::endl;
    std::cout << usage << std::endl;
    std::exit(1);
  }

  if (g_options.stdlib_path.empty()) {
    std::cout << "error: no stdlib path, which is required for now.\n" << std::endl;
    std::cout << usage << std::endl;
    std::exit(1);
  }
}