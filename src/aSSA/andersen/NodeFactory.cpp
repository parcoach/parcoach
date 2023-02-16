#include "parcoach/andersen/NodeFactory.h"

#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/Support/raw_ostream.h"

#include <limits>

using namespace llvm;

unsigned const AndersNodeFactory::InvalidIndex =
    std::numeric_limits<unsigned int>::max();

AndersNodeFactory::AndersNodeFactory() {
  // Note that we can't use std::vector::emplace_back() here because
  // AndersNode's constructors are private hence std::vector cannot see it

  // Node #0 is always the universal ptr: the ptr that we don't know anything
  // about.
  nodes.push_back(AndersNode(AndersNode::VALUE_NODE, 0));
  // Node #0 is always the universal obj: the obj that we don't know anything
  // about.
  nodes.push_back(AndersNode(AndersNode::OBJ_NODE, 1));
  // Node #2 always represents the null pointer.
  nodes.push_back(AndersNode(AndersNode::VALUE_NODE, 2));
  // Node #3 is the object that null pointer points to
  nodes.push_back(AndersNode(AndersNode::OBJ_NODE, 3));

  assert(nodes.size() == 4);
}

NodeIndex AndersNodeFactory::createValueNode(Value const *Val) {
  // errs() << "inserting " << *val << "\n";
  unsigned NextIdx = nodes.size();
  nodes.push_back(AndersNode(AndersNode::VALUE_NODE, NextIdx, Val));
  if (Val != nullptr) {
    assert(!valueNodeMap.count(Val) &&
           "Trying to insert two mappings to revValueNodeMap!");
    valueNodeMap[Val] = NextIdx;
  }

  return NextIdx;
}

NodeIndex AndersNodeFactory::createObjectNode(Value const *Val) {
  unsigned NextIdx = nodes.size();
  nodes.push_back(AndersNode(AndersNode::OBJ_NODE, NextIdx, Val));
  if (Val != nullptr) {
    assert(!objNodeMap.count(Val) &&
           "Trying to insert two mappings to revObjNodeMap!");
    objNodeMap[Val] = NextIdx;
  }

  return NextIdx;
}

NodeIndex AndersNodeFactory::createReturnNode(llvm::Function const *F) {
  unsigned NextIdx = nodes.size();
  nodes.push_back(AndersNode(AndersNode::VALUE_NODE, NextIdx, F));

  assert(!returnMap.count(F) && "Trying to insert two mappings to returnMap!");
  returnMap[F] = NextIdx;

  return NextIdx;
}

NodeIndex AndersNodeFactory::createVarargNode(llvm::Function const *F) {
  unsigned NextIdx = nodes.size();
  nodes.push_back(AndersNode(AndersNode::OBJ_NODE, NextIdx, F));

  assert(!varargMap.count(F) && "Trying to insert two mappings to varargMap!");
  varargMap[F] = NextIdx;

  return NextIdx;
}

NodeIndex AndersNodeFactory::getValueNodeFor(Value const *Val) const {
  if (Constant const *C = dyn_cast<Constant>(Val)) {
    if (!isa<GlobalValue>(C)) {
      return getValueNodeForConstant(C);
    }
  }

  // errs() << "looking up " << *val << "\n";
  auto ItV = valueNodeMap.find(Val);
  if (ItV == valueNodeMap.end()) {
    return InvalidIndex;
  }
  return ItV->second;
}

NodeIndex
AndersNodeFactory::getValueNodeForConstant(llvm::Constant const *C) const {
  assert(isa<PointerType>(C->getType()) && "Not a constant pointer!");

  if (isa<ConstantPointerNull>(C) || isa<UndefValue>(C)) {
    return getNullPtrNode();
  }
  if (GlobalValue const *GV = dyn_cast<GlobalValue>(C)) {
    return getValueNodeFor(GV);
  }

  if (ConstantExpr const *CE = dyn_cast<ConstantExpr>(C)) {
    switch (CE->getOpcode()) {
    // Pointer to any field within a struct is treated as a pointer to the first
    // field
    case Instruction::GetElementPtr:
      return getValueNodeFor(C->getOperand(0));
    case Instruction::IntToPtr:
    case Instruction::PtrToInt:
      return getUniversalPtrNode();
    case Instruction::BitCast:
    case Instruction::AddrSpaceCast:
      return getValueNodeForConstant(CE->getOperand(0));
    default:
      errs() << "Constant Expr not yet handled: " << *CE << "\n";
      llvm_unreachable(0);
    }
  }

  llvm_unreachable("Unknown constant pointer!");
  return InvalidIndex;
}

NodeIndex AndersNodeFactory::getObjectNodeFor(Value const *Val) const {
  if (Constant const *C = dyn_cast<Constant>(Val)) {
    if (!isa<GlobalValue>(C)) {
      return getObjectNodeForConstant(C);
    }
  }

  auto ItV = objNodeMap.find(Val);
  if (ItV == objNodeMap.end()) {
    return InvalidIndex;
  }
  return ItV->second;
}

NodeIndex
AndersNodeFactory::getObjectNodeForConstant(llvm::Constant const *C) const {
  assert(isa<PointerType>(C->getType()) && "Not a constant pointer!");

  if (isa<ConstantPointerNull>(C)) {
    return getNullObjectNode();
  }
  if (GlobalValue const *GV = dyn_cast<GlobalValue>(C)) {
    return getObjectNodeFor(GV);
  }
  if (ConstantExpr const *CE = dyn_cast<ConstantExpr>(C)) {
    switch (CE->getOpcode()) {
    // Pointer to any field within a struct is treated as a pointer to the first
    // field
    case Instruction::GetElementPtr:
      return getObjectNodeForConstant(CE->getOperand(0));
    case Instruction::IntToPtr:
    case Instruction::PtrToInt:
      return getUniversalObjNode();
    case Instruction::BitCast:
    case Instruction::AddrSpaceCast:
      return getObjectNodeForConstant(CE->getOperand(0));
    default:
      errs() << "Constant Expr not yet handled: " << *CE << "\n";
      llvm_unreachable(0);
    }
  }

  llvm_unreachable("Unknown constant pointer!");
  return InvalidIndex;
}

NodeIndex AndersNodeFactory::getReturnNodeFor(llvm::Function const *F) const {
  auto ItF = returnMap.find(F);
  if (ItF == returnMap.end()) {
    return InvalidIndex;
  }
  return ItF->second;
}

NodeIndex AndersNodeFactory::getVarargNodeFor(llvm::Function const *F) const {
  auto ItF = varargMap.find(F);
  if (ItF == varargMap.end()) {
    return InvalidIndex;
  }
  return ItF->second;
}

void AndersNodeFactory::mergeNode(NodeIndex N0, NodeIndex N1) {
  assert(N0 < nodes.size() && N1 < nodes.size());
  nodes[N1].mergeTarget = N0;
}

NodeIndex AndersNodeFactory::getMergeTarget(NodeIndex N) {
  assert(N < nodes.size());
  NodeIndex Ret = nodes[N].mergeTarget;
  if (Ret != N) {
    std::vector<NodeIndex> Path(1, N);
    while (Ret != nodes[Ret].mergeTarget) {
      Path.push_back(Ret);
      Ret = nodes[Ret].mergeTarget;
    }
    for (auto Idx : Path) {
      nodes[Idx].mergeTarget = Ret;
    }
  }
  assert(Ret < nodes.size());
  return Ret;
}

NodeIndex AndersNodeFactory::getMergeTarget(NodeIndex N) const {
  assert(N < nodes.size());
  NodeIndex Ret = nodes[N].mergeTarget;
  while (Ret != nodes[Ret].mergeTarget) {
    Ret = nodes[Ret].mergeTarget;
  }
  return Ret;
}

void AndersNodeFactory::getAllocSites(
    std::vector<llvm::Value const *> &AllocSites) const {
  AllocSites.clear();
  AllocSites.reserve(objNodeMap.size());
  for (auto const &Mapping : objNodeMap) {
    AllocSites.push_back(Mapping.first);
  }
}

#ifndef NDEBUG
void AndersNodeFactory::dumpNode(NodeIndex Idx) const {
  AndersNode const &N = nodes.at(Idx);
  if (N.type == AndersNode::VALUE_NODE) {
    errs() << "[V ";
  } else if (N.type == AndersNode::OBJ_NODE) {
    errs() << "[O ";
  } else {
    assert(false && "Wrong type number!");
  }
  errs() << "#" << N.idx << "]";
}

void AndersNodeFactory::dumpNodeInfo() const {
  errs() << "\n----- Print AndersNodeFactory Info -----\n";
  for (auto const &Node : nodes) {
    dumpNode(Node.getIndex());
    errs() << ", val = ";
    Value const *Val = Node.getValue();
    if (Val == nullptr) {
      errs() << "nullptr";
    } else if (isa<Function>(Val)) {
      errs() << "  <func> " << Val->getName();
    } else {
      errs() << *Val;
    }
    errs() << "\n";
  }

  errs() << "\nReturn Map:\n";
  for (auto const &Mapping : returnMap) {
    errs() << Mapping.first->getName() << "  -->>  [Node #" << Mapping.second
           << "]\n";
  }
  errs() << "\nVararg Map:\n";
  for (auto const &Mapping : varargMap) {
    errs() << Mapping.first->getName() << "  -->>  [Node #" << Mapping.second
           << "]\n";
  }
  errs() << "----- End of Print -----\n";
}

void AndersNodeFactory::dumpRepInfo() const {
  errs() << "\n----- Print Node Merge Info -----\n";
  for (NodeIndex I = 0, E = nodes.size(); I < E; ++I) {
    NodeIndex Rep = getMergeTarget(I);
    if (Rep != I) {
      errs() << I << " -> " << Rep << "\n";
    }
  }
  errs() << "----- End of Print -----\n";
}
#endif
