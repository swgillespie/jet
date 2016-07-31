#include "contract.h"
#include "util.h"
#include <string>

using namespace std::literals;

ContractFrame *g_contract_frames;
ContractFrame *g_contract_current_frame;

void ContractFrame::AddContract(ContractFrame::Restriction restriction) {
  restrictions = static_cast<Restriction>(static_cast<int>(restrictions) |
                                          static_cast<int>(restriction));
}

void ContractFrame::CheckPrecondition(bool expr, const char *stringifed_expr) {
  if (!expr) {
    std::string panic_msg = "precondition failed in function '"s +
                            function_name + "': "s + stringifed_expr;
    PANIC(panic_msg.c_str());
  }
}

void ContractFrame::CheckContract(ContractFrame::Restriction restriction,
                                  const char *contract_fail_function) {
  if (restrictions & restriction) {
    std::string contract_fail_msg;
    if (restriction == ContractFrame::Restriction::NoGc) {
      contract_fail_msg = "GC in a region where GCs are prohibited";
    } else {
      PANIC("unknown contract!");
    }
    std::string panic_msg = "contract violation in function '"s +
                            function_name + "', caused by function '" +
                            contract_fail_function + "': " + contract_fail_msg;
    PANIC(panic_msg.c_str());
  }
}

void SignalContractViolation(ContractFrame::Restriction restriction,
                             const char *function) {
// this does nothing on non-debug builds, since we haven't built
// any contracts to check.
#ifndef DEBUG
  UNUSED_PARAMETER(restriction);
  UNUSED_PARAMETER(function);
  return;
#else
  // starting at the current contract frame, walk the stack upwards and
  // see if any of the contracts have asserted this contract.
  assert(g_contract_frames != nullptr);
  assert(g_contract_current_frame != nullptr);
  for (ContractFrame *frame = g_contract_current_frame;
       // the topmost frame is a sentinel frame that asserts no contracts.
       // no need to check it.
       frame != g_contract_frames; frame = frame->GetParent()) {
    frame->CheckContract(restriction, function);
  }
#endif // DEBUG
}
