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
#include "options.h"
#include "util.h"
#include <iostream>

Options g_options;

const char *usage =
    "Jet interpreter, by Sean Gillespie\n"
    "\n"
    "usage: jet <file.jet> [-h|--help] [-s|--stdlib-path] [--gc-stress]\n"
    "                      [-w|--warnings] [--heap-verify]"
    "\n"
    "options:\n"
    "   -h|--help         Displays this message.\n"
    "   -s|--stdlib-path  Sets the path to the Jet standard library.\n"
    "   -w|--warnings     Emits warnings for possibly unbound variables.\n"
    "   --gc-stress       Enables GC stress. Debug builds only.\n"
    "   --heap-verify     Verify the heap before and after a GC. Debug builds "
    "only.";

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

    if (strcmp("-w", argv[i]) == 0 || strcmp("--warnings", argv[i]) == 0) {
      i++;
      g_options.emit_warnings = true;
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
    std::cout << "error: no stdlib path, which is required for now.\n"
              << std::endl;
    std::cout << usage << std::endl;
    std::exit(1);
  }
}