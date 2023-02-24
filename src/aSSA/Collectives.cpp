#include "parcoach/Collectives.h"
#include "parcoach/Options.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instructions.h"

namespace parcoach {
using namespace llvm;
namespace {

struct Registry {
  static auto const &get() {
    static Registry Instance;
    return Instance.Data;
  }

  ~Registry() {
    for (auto const &ItC : Data) {
      delete ItC.second;
    }
  }

private:
  // NOTE: we must use dynamically allocated memory here, storing a Collective
  // would not allocate enough space for derived types.
  // We unfortunately can't use a unique_ptr as initializer lists don't have
  // a move semantic.
  // Therefore we use this trick of having a singleton instance with a custom
  // destructor.
  Registry() = default;
  StringMap<Collective const *> const Data{
#define MPI_COLLECTIVE(Name, CommArgId)                                        \
  {#Name, new MPICollective(Collective::Kind::C_##Name, #Name, CommArgId)},
#include "parcoach/MPIRegistry.def"
#define COLLECTIVE(Name, FunctionName, Type)                                   \
  {#Name, new Type##Collective(Collective::Kind::C_##Name, #FunctionName)},
#ifdef PARCOACH_ENABLE_OPENMP
#define OMP_COLLECTIVE(Name) COLLECTIVE(Name, Name, OMP)
#include "parcoach/OMPRegistry.def"
#endif
#ifdef PARCOACH_ENABLE_UPC
#define UPC_COLLECTIVE(Name) COLLECTIVE(Name, Name, UPC)
#include "parcoach/UPCRegistry.def"
#endif
#ifdef PARCOACH_ENABLE_CUDA
#define CUDA_COLLECTIVE(Name, FunctionName) COLLECTIVE(Name, FunctionName, Cuda)
#include "parcoach/CUDARegistry.def"
#endif
#undef COLLECTIVE
  };
};

} // namespace

bool Collective::enabled() const { return Options::get().isActivated(P_); }

Collective const *Collective::find(Function const &F) {
  auto It = Registry::get().find(F.getName());
  if (It != Registry::get().end() && It->second->enabled()) {
    return It->second;
  }
  return {};
}

bool Collective::isCollective(Function const &F) {
  return Collective::find(F) != nullptr;
}

Value *MPICollective::getCommunicator(CallInst const &CI) const {
  // NOTE: we expect CI to actually call "this" collective, but we don't assert
  // for it since it may be an indirect call.
  if (CommArgId < 0) {
    return nullptr;
  }
  Value *Comm = CI.getArgOperand(CommArgId);
  // This is flaky, we should do a better job at tracking communicator aliases.
  while (auto *PtrToComm = dyn_cast<LoadInst>(Comm)) {
    Comm = PtrToComm->getPointerOperand();
  }
  return Comm;
}
} // namespace parcoach
