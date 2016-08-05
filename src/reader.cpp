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
#include "reader.h"
#include "contract.h"
#include "gc.h"
#include "interner.h"
#include <sstream>

using namespace std::literals;

// Prototypes for our mutually-recursive read functions
static Sexp *ReadSublist(std::istream &);
static Sexp *ReadAtom(std::istream &);
static Sexp *ReadToplevel(std::istream &);

static char Peek(std::istream &input) { return (char)input.peek(); }

static void Expect(std::istream &input, char c) {
  char read = (char)input.get();
  if (read != c) {
    throw ReadException("unexpected char: expected "s + c + ", got "s + read);
  }
}

static void SkipWhitespace(std::istream &input) {
  while (true) {
    char p = Peek(input);
    if (isspace(p)) {
      input.get();
      continue;
    }

    if (p == ';') {
      // this is a comment. skip to the next newline.
      // TODO(xplat) windows line endings
      input.get();
      while (Peek(input) != '\n') {
        input.get();
      }

      continue;
    }

    break;
  }
}

static bool IsIdentStart(char c) {
  if (isalpha(c))
    return true;

  switch (c) {
  case '_':
  case '-':
  case '+':
  case '/':
  case '*':
  case '?':
  case '!':
  case '=':
  case '.':
    return true;
  default:
    return false;
  }
}

static bool IsIdentBody(char c) { return IsIdentStart(c) || isdigit(c); }

static Sexp *ReadSublist(std::istream &input) {
  // at this point, we are reading a list and we've already read
  // the opening paren.
  //   (call 1 2 3)
  //    ^--- we are here
  GC_HELPER_FRAME;
  GC_PROTECTED_LOCAL(car);
  GC_PROTECTED_LOCAL(cdr);

  if (Peek(input) == ')') {
    return GcHeap::AllocateEmpty();
  }

  SkipWhitespace(input);
  car = ReadAtom(input);
  SkipWhitespace(input);
  if (Peek(input) != ')') {
    if (Peek(input) == '.') {
      // this is an improper list.
      Expect(input, '.');
      cdr = ReadAtom(input);
      return GcHeap::AllocateCons(car, cdr);
    }

    cdr = ReadSublist(input);
    return GcHeap::AllocateCons(car, cdr);
  }

  return GcHeap::AllocateCons(car, GcHeap::AllocateEmpty());
}

static Sexp *ReadSymbol(std::istream &input) {
  std::ostringstream buf;
  buf << (char)input.get();

  while (true) {
    char peeked = Peek(input);
    if (IsIdentBody(peeked)) {
      buf << peeked;
      input.get();
      continue;
    }

    break;
  }

  size_t intern_index = SymbolInterner::InternSymbol(buf.str());
  return GcHeap::AllocateSymbol(intern_index);
}

static Sexp *ReadFixnum(std::istream &input) {
  std::ostringstream buf;
  buf << (char)input.get();
  while (true) {
    char peeked = Peek(input);
    if (isdigit(peeked)) {
      buf << peeked;
      input.get();
      continue;
    } else if (!isspace(peeked) && peeked != ')' && peeked != '(') {
      throw ReadException("unexpected char in numeric literal: "s + peeked);
    }

    break;
  }

  // GcHeap::ForceCollect();

  jet_fixnum num = atoi(buf.str().c_str());
  return GcHeap::AllocateFixnum(num);
}

static Sexp *ReadHash(std::istream &input) {
  assert(Peek(input) == '#');
  Expect(input, '#');
  char peeked = Peek(input);
  switch (peeked) {
  case 't':
    input.get();
    return GcHeap::AllocateBool(true);
  case 'f':
    input.get();
    return GcHeap::AllocateBool(false);
  default:
    // #eof
    break;
  }

  Expect(input, 'e');
  Expect(input, 'o');
  Expect(input, 'f');
  return GcHeap::AllocateEof();
}

static Sexp *ReadString(std::istream &input) {
  assert(Peek(input) == '"');
  Expect(input, '"');
  std::ostringstream buf;
  while (Peek(input) != '"') {
    char peeked = Peek(input);
    if (peeked == EOF) {
      throw ReadException("unexpected EOF while scanning string literal");
    }

    buf << peeked;
    input.get();
  }

  Expect(input, '"');
  return GcHeap::AllocateString(buf.str().c_str());
}

static Sexp *ReadQuote(std::istream &input) {
  GC_HELPER_FRAME;
  GC_PROTECTED_LOCAL(quoted);

  assert(Peek(input) == '\'');
  Expect(input, '\'');
  quoted = ReadToplevel(input);
  size_t quote_sym = SymbolInterner::InternSymbol("quote");
  return GcHeap::AllocateCons(
      GcHeap::AllocateSymbol(quote_sym),
      GcHeap::AllocateCons(quoted, GcHeap::AllocateEmpty()));
}

static Sexp *ReadAtom(std::istream &input) {
  GC_HELPER_FRAME;
  GC_PROTECTED_LOCAL(result);

  SkipWhitespace(input);
  char peeked = Peek(input);
  if (IsIdentStart(peeked)) {
    return ReadSymbol(input);
  }

  if (isdigit(peeked)) {
    return ReadFixnum(input);
  }

  if (peeked == '(') {
    Expect(input, '(');
    result = ReadSublist(input);
    SkipWhitespace(input);
    Expect(input, ')');
    return result;
  }

  if (peeked == '#') {
    return ReadHash(input);
  }

  if (peeked == '\'') {
    return ReadQuote(input);
  }

  if (peeked == '"') {
    return ReadString(input);
  }

  throw ReadException("unexpected char when scanning atom: "s + peeked);
}

static Sexp *ReadToplevel(std::istream &input) {
  SkipWhitespace(input);
  if (Peek(input) != '(') {
    return ReadAtom(input);
  }

  Expect(input, '(');
  SkipWhitespace(input);
  if (Peek(input) == ')') {
    Expect(input, ')');
    return GcHeap::AllocateEmpty();
  }

  Sexp *sublist = ReadSublist(input);
  SkipWhitespace(input);
  Expect(input, ')');
  return sublist;
}

Sexp *Read(std::istream &input) {
  SkipWhitespace(input);
  if (Peek(input) == EOF) {
    return GcHeap::AllocateEof();
  }

  return ReadToplevel(input);
}
