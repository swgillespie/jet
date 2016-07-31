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

// Small header for utilities common to all files.
#pragma once

#include <assert.h>
#include <exception>
#include <stdint.h>
#include <string>

#define UNUSED_PARAMETER(p) (void)p

#define PANIC(msg) PanicImpl(__FILE__, __LINE__, __func__, msg)
#define UNREACHABLE() PANIC("entered unreachable code")
#define NYI() PANIC("not implemented")

#define PAGE_SIZE 4096

#ifdef _WIN32
#define strdup _strdup
#endif

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
