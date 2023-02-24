#include "parcoach/CollectiveList.h"

#include "PTACallGraph.h"
#include "Utils.h"

#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/raw_ostream.h"

#include <sstream>

using namespace llvm;
namespace parcoach {
namespace {
// Little helper to tell us if we should ignore the collective.
// We basically ignore the collective if the Communicator is not null and if
// it matches the communicator in the call instruction.
bool ShouldIgnore(CallInst const &CI, Collective const &Coll, Value *Comm) {
  if (!isa<MPICollective>(Coll) || !Comm) {
    // If it's not an mpi collective, or if we don't have a communicator,
    // we definitely shouldn't ignore it!
    return false;
  }
  MPICollective const &MPIColl = cast<MPICollective>(Coll);
  Value *CommForCollective = MPIColl.getCommunicator(CI);
  return CommForCollective != nullptr && CommForCollective != Comm;
}
} // namespace

bool CollectiveList::navs() const {
  if (List_.size() == 0) {
    return false;
  }
  return List_.front()->navs();
}

CollectiveList::CollectiveList(CollectiveList const &From)
    : LoopHeader_(From.LoopHeader_) {
  for (auto const &Elem : From.List_) {
    List_.emplace_back(Elem->copy());
  }
}

CollectiveList::CollectiveList(BasicBlock *Header, CollectiveList const &From)
    : CollectiveList(From) {
  // Override the loop header after copy construction.
  LoopHeader_ = Header;
}

void CollectiveList::add(CollectiveList const &Other,
                         std::insert_iterator<ListTy> Inserter) {
  if (!Other.List_.empty()) {
    if (Other.LoopHeader_) {
      Inserter = std::make_unique<CollListElement>(Other);
    } else {
      for (auto const &ListElem : Other.List_) {
        Inserter = ListElem->copy();
      }
    }
  }
}

void CollectiveList::extendWith(CollectiveList const &Other) {
  add(Other, std::inserter(List_, List_.end()));
}

void CollectiveList::prependWith(CollectiveList const &Other) {
  add(Other, std::inserter(List_, List_.begin()));
}

std::string CollectiveList::toString() const {
  std::string Ret;
  raw_string_ostream Err(Ret);
  if (LoopHeader_) {
    Err << "*";
    LoopHeader_.value()->printAsOperand(Err, false);
  }
  Err << "(";
  bool First = true;
  for (auto &Elem : List_) {
    if (!First) {
      Err << ", ";
    }
    Err << Elem->toString();
    First = false;
  }
  Err << ")";
  return Ret;
}

CollectiveList CollectiveList::CreateFromBB(BBToCollListMap const &CollLists,
                                            PTACallGraph const &PTACG,
                                            bool NeighborsAreNAVS,
                                            llvm::BasicBlock &BB,
                                            llvm::Value *Comm) {
  CollectiveList Current;
  if (NeighborsAreNAVS) {
    Current.extendWith<NavsElement>();
    return Current;
  }

  auto ActOnFunction = [&](CallInst &CI, Function const &F) {
    if (auto *Coll = Collective::find(F)) {
      if (!ShouldIgnore(CI, *Coll, Comm)) {
        Current.extendWith<CollElement>(*Coll);
      }
    } else if (CollLists.count(&F.getEntryBlock())) {
      auto const &ListForFunc = CollLists.find(&F.getEntryBlock())->second;
      Current.extendWith(ListForFunc);
    }
  };
  auto IsCI = [](Instruction &I) { return isa<CallInst>(I); };
  for (auto &I : make_filter_range(BB, IsCI)) {
    CallInst &CI = cast<CallInst>(I);
    if (!CI.getCalledFunction()) {
      // Iterate over functions which may be called
      auto Found = PTACG.getIndirectCallMap().find(&CI);
      if (Found == PTACG.getIndirectCallMap().end()) {
        continue;
      }
      for (Function const *MayCall : Found->second) {
        if (isIntrinsicDbgFunction(MayCall))
          continue;
        // FIXME: think about that: currently it pushes *all* the possible
        // collective called by *all* the may call functions.
        ActOnFunction(CI, *MayCall);
      }
    } else {
      ActOnFunction(CI, *CI.getCalledFunction());
    }
  }
  return Current;
}

} // namespace parcoach
