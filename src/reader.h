// The reader is responsible for drawing s-expressions from an input stream.

#pragma once

#include "sexp.h"
#include <exception>
#include <istream>
#include <string>

class ReadException : public std::exception {
private:
  std::string what_msg;

public:
  ReadException(std::string msg) : what_msg(std::move(msg)) {}

  const char *what() const noexcept override { return what_msg.c_str(); }
};

// Reads an s-expression from the given input stream, throwing a
// ReadException if a parse error occurs.
Sexp *Read(std::istream &input);