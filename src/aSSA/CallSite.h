// This file define llvm::CallSite, because not available anymore in llvm-12
//
// Some part of this file comme from SVF source code:
//  - Copyright (C) <2013-2017>  <Yulei Sui>
//  - Licence: GPL-v3+
//  - https://github.com/SVF-tools/SVF/blob/SVF-2.2/include/Util/BasicTypes.h
//

#ifndef CALLSITE_H
#define CALLSITE_H

#if LLVM_VERSION_MAJOR >= 11

#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instructions.h"

namespace llvm {

class CallSite {
private:
  CallBase *CB = nullptr;
  int type = 0;

public:
  CallSite() {}

  explicit CallSite(Instruction *I) : CB(llvm::dyn_cast<CallBase>(I)) {
    if (I->getOpcode() == Instruction::Call)
      type = 1;
    else if (I->getOpcode() == Instruction::Invoke)
      type = 2;
    else if (I->getOpcode() == Instruction::CallBr)
      type = 3;
  }

  explicit CallSite(Value *V) : CB(llvm::dyn_cast<CallBase>(V)) {
    Instruction *I = llvm::dyn_cast<Instruction>(V);

    if (I->getOpcode() == Instruction::Call)
      type = 1;
    else if (I->getOpcode() == Instruction::Invoke)
      type = 2;
    else if (I->getOpcode() == Instruction::CallBr)
      type = 3;
  }

  CallBase *getInstruction() const { return CB; }
  using arg_iterator = User::const_op_iterator;
  Value *getArgument(unsigned ArgNo) const { return CB->getArgOperand(ArgNo); }
  Type *getType() const { return CB->getType(); }
  User::const_op_iterator arg_begin() const { return CB->arg_begin(); }
  User::const_op_iterator arg_end() const { return CB->arg_end(); }
  unsigned arg_size() const { return CB->arg_size(); }
  bool arg_empty() const { return CB->arg_empty(); }
  Value *getArgOperand(unsigned i) const { return CB->getArgOperand(i); }
  unsigned getNumArgOperands() const { return CB->getNumArgOperands(); }
  Function *getCalledFunction() const { return CB->getCalledFunction(); }
  Value *getCalledValue() const { return CB->getCalledOperand(); }
  Function *getCaller() const { return CB->getCaller(); }
  FunctionType *getFunctionType() const { return CB->getFunctionType(); }
  bool isCall() const { return type == 1; }

  bool operator==(const CallSite &CS) const { return CB == CS.CB; }
  bool operator!=(const CallSite &CS) const { return CB != CS.CB; }
  bool operator<(const CallSite &CS) const {
    return getInstruction() < CS.getInstruction();
  }
  operator bool() const { return CB != nullptr; }
};

class ImmutableCallSite : public CallSite {
public:
  explicit ImmutableCallSite(const Instruction *I)
      : CallSite((Instruction *)I) {}
  explicit ImmutableCallSite(const Value *V) : CallSite((Value *)V) {}
};

} // namespace llvm

#else /* LLVM_VERSION_MAJOR >= 12 */

#include "llvm/IR/CallSite.h"

#endif /* LLVM_VERSION_MAJOR >= 12 */
#endif /* CALLSITE_H */
