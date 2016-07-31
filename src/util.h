// Small header for utilities common to all files.

#pragma once

#include <assert.h>
#include <stdint.h>
#include <exception>
#include <string>

#define UNUSED_PARAMETER(p) (void)p

#define PANIC(msg) PanicImpl(__FILE__, __LINE__, __PRETTY_FUNCTION__, msg)
#define UNREACHABLE() PANIC("entered unreachable code")
#define NYI() PANIC("not implemented")

// TODO(xplat)
#define PAGE_SIZE 4096

// Prints a message to standard error and aborts. Used for fatal internal
// errors.
[[noreturn]] void PanicImpl(const char *file, int line, const char *func,
                            const char *msg, ...);

// JetRuntimeExceptions are thrown when evaluating whenever a runtime
// error occurs.
class JetRuntimeException : public std::exception {
private:
  std::string what_msg;

public:
  JetRuntimeException(std::string msg) : what_msg(std::move(msg)) {}

  const char *what() const noexcept override { return what_msg.c_str(); }
};
