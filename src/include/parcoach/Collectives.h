#pragma once

#include "parcoach/Paradigms.h"

#include "llvm/ADT/StringRef.h"

#include <string>

namespace llvm {
class Function;
} // namespace llvm

namespace parcoach {
// Naive implementation for now, we may want to introduce a real class hierarchy
// if we need more feature on this.
struct Collective {
  enum class Kind {
#define COLLECTIVE(Name) C_##Name,
#define MPI_COLLECTIVE(Name, CommArgId) COLLECTIVE(Name)
#include "MPIRegistry.def"
#define OMP_COLLECTIVE(Name) COLLECTIVE(Name)
#include "OMPRegistry.def"
#define UPC_COLLECTIVE(Name) COLLECTIVE(Name)
#include "UPCRegistry.def"
#define CUDA_COLLECTIVE(Name, FunctionName) COLLECTIVE(Name)
#include "CUDARegistry.def"
#undef COLLECTIVE
  };
  std::string const Name;
  // Unique collective identifier
  Kind const K;
  bool enabled() const;
  Paradigm getParadigm() const { return P_; }
  static bool isCollective(llvm::Function const &);
  static Collective const *find(llvm::Function const &);
  Collective(Collective const &) = default;

protected:
  Collective(Paradigm P, Kind K, llvm::StringRef Name)
      : Name(Name), K(K), P_(P) {}

private:
  Paradigm const P_;
};

struct MPICollective : Collective {
  MPICollective(Kind K, llvm::StringRef Name, size_t ArgId)
      : Collective(Paradigm::MPI, K, Name), CommArgId(ArgId) {}
  int const CommArgId;
  static bool classof(Collective const *C) {
    return C->getParadigm() == Paradigm::MPI;
  }
};

struct OMPCollective : Collective {
  OMPCollective(Kind K, llvm::StringRef Name)
      : Collective(Paradigm::OMP, K, Name) {}
  static bool classof(Collective const *C) {
    return C->getParadigm() == Paradigm::OMP;
  }
};

struct CudaCollective : Collective {
  CudaCollective(Kind K, llvm::StringRef Name)
      : Collective(Paradigm::CUDA, K, Name) {}
  static bool classof(Collective const *C) {
    return C->getParadigm() == Paradigm::CUDA;
  }
};

struct UPCCollective : Collective {
  UPCCollective(Kind K, llvm::StringRef Name)
      : Collective(Paradigm::UPC, K, Name) {}
  static bool classof(Collective const *C) {
    return C->getParadigm() == Paradigm::UPC;
  }
};
} // namespace parcoach
