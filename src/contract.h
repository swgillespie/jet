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

// This header provides a limited contract system that
// is useful to assert useful invariants about the VM.
//
// # Motivation
// The GC has a hard requirement that, when a GC occurs, all native frames
// between the source of the GC and the interpreter are required to protect
// any managed pointers that they may have. Therefore, it is permitted
// for a function to omit managed pointer protection if it can be absolutely
// certain that this function or any of its callees will not trigger a GC.
//
// This is a difficult invariant to reason about, so the contract system
// exists to assert at runtime that the programmer's assertion that a GC
// is impossible is actually correct.
#pragma once

#include "util.h"

class ContractFrame {
public:
  enum Restriction {
    None = 0,
    NoGc = 1 << 0,
  };

  ContractFrame(const char *function, ContractFrame *parent)
      : restrictions(Restriction::None), function_name(function),
        parent(parent) {}

  void AddContract(ContractFrame::Restriction restriction);
  void CheckPrecondition(bool expr, const char *stringifed_expr);
  void CheckContract(ContractFrame::Restriction restriction,
                     const char *contract_fail_function);

  ContractFrame *GetParent() { return parent; }
  const ContractFrame *GetParent() const { return parent; }

private:
  ContractFrame::Restriction restrictions;
  const char *function_name;
  ContractFrame *parent;
};

extern ContractFrame *g_contract_frames;
extern ContractFrame *g_contract_current_frame;

class ContractFrameProtector {
private:
  ContractFrame *protected_frame;

public:
  ContractFrameProtector(const char *name) {
    assert(g_contract_frames != nullptr);
    assert(g_contract_current_frame != nullptr);
    ContractFrame *frame = new ContractFrame(name, g_contract_current_frame);
    g_contract_current_frame = frame;
    protected_frame = frame;
  }

  ~ContractFrameProtector() {
    assert(g_contract_current_frame != nullptr);
    g_contract_current_frame = g_contract_current_frame->GetParent();
    assert(g_contract_current_frame != nullptr);
    delete protected_frame;
  }

  void AddContract(ContractFrame::Restriction restriction) {
    protected_frame->AddContract(restriction);
  }

  void CheckPrecondition(bool expr, const char *stringifed_expr) {
    protected_frame->CheckPrecondition(expr, stringifed_expr);
  }
};

// Signals a violation of a given contract. This function performs a stack
// walk of the contract frames and asserts if any of the violated contracts
// are active.
void SignalContractViolation(ContractFrame::Restriction restriction,
                             const char *function);

// These macros are the public interface of this header file and should
// be the only thing used. All of the classes in this header file
// are implementation details - this is the interface.

#ifdef DEBUG
#define CONTRACT ContractFrameProtector __contract_frame(__PRETTY_FUNCTION__);
#define FORBID_GC                                                              \
  __contract_frame.AddContract(ContractFrame::Restriction::NoGc);
#define PRECONDITION(expr) __contract_frame.CheckPrecondition(expr, #expr)
#define CONTRACT_VIOLATIONS
#define PERFORMS_GC                                                            \
  SignalContractViolation(ContractFrame::Restriction::NoGc, __PRETTY_FUNCTION__)
#else
#define CONTRACT
#define FORBID_GC
#define PRECONDITION(expr)
#define CONTRACT_VIOLATIONS
#define PERFORMS_GC
#endif