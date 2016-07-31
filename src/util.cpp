#include "util.h"

#include <exception>
#include <stdarg.h>
#include <stdio.h>

[[noreturn]] void PanicImpl(const char *file, int line, const char *func,
                            const char *msg, ...) {
  va_list args;
  va_start(args, msg);
  fprintf(stderr, "internal error (%s, in function %s, at line %d): \n", file,
          func, line);
  vfprintf(stderr, msg, args);
  fprintf(stderr, "\n");
  fflush(stdout);
  abort();
}