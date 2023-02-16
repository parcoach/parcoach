#include "parcoach/Options.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_os_ostream.h"

using namespace llvm;

namespace parcoach {

cl::OptionCategory ParcoachCategory("Parcoach options");

namespace {
cl::opt<bool> OptOmpTaint("check-omp",
                          cl::desc("enable OpenMP collectives checking"),
                          cl::cat(ParcoachCategory));
cl::opt<bool> OptMpiTaint("check-mpi",
                          cl::desc("enable MPI collectives checking"),
                          cl::cat(ParcoachCategory));

cl::opt<bool> OptCudaTaint("check-cuda",
                           cl::desc("enable CUDA collectives checking"),
                           cl::cat(ParcoachCategory));

cl::opt<bool> OptUpcTaint("check-upc",
                          cl::desc("enable UPC collectives checking"),
                          cl::cat(ParcoachCategory));

// This sets the default paradigme to MPI; it's likely what we want.
cl::opt<parcoach::Paradigm> ActivatedParadigm(
    "check", cl::desc("Select enabled paradigm (mpi, omp, upc, cuda)"),
    cl::values(clEnumValN(parcoach::Paradigm::MPI, "mpi",
                          "Enable MPI checkin (this is the default")
#ifdef PARCOACH_ENABLE_OPENMP
                   ,
               clEnumValN(parcoach::Paradigm::OMP, "omp", "Enable OpenMP")
#endif
#ifdef PARCOACH_ENABLE_UPC
                   ,
               clEnumValN(parcoach::Paradigm::UPC, "upc", "Enable UPC")
#endif
#ifdef PARCOACH_ENABLE_CUDA
                   ,
               clEnumValN(parcoach::Paradigm::CUDA, "cuda", "Enable Cuda")
#endif
#ifdef PARCOACH_ENABLE_RMA
                   ,
               clEnumValN(parcoach::Paradigm::RMA, "rma", "Enable MPI-RMA")
#endif
                   ),
    cl::cat(ParcoachCategory));

} // namespace

Options const &Options::get() {
  static Options Instance;
  return Instance;
}

Options::Options() {
  if (OptMpiTaint) {
    ActivatedParadigm = Paradigm::MPI;
#ifdef PARCOACH_ENABLE_OPENMP
  } else if (OptOmpTaint) {
    ActivatedParadigm = Paradigm::OMP;
#endif
#ifdef PARCOACH_ENABLE_UPC
  } else if (OptUpcTaint) {
    ActivatedParadigm = Paradigm::UPC;
#endif
#ifdef PARCOACH_ENABLE_CUDA
  } else if (OptCudaTaint) {
    ActivatedParadigm = Paradigm::CUDA;
#endif
  }
}

bool Options::isActivated(Paradigm Par) const {
  return Par == ActivatedParadigm;
}
} // namespace parcoach
