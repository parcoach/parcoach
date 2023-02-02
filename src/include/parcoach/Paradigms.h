#pragma once

#include "Config.h"

namespace parcoach {
enum class Paradigm {
  MPI,
#ifdef PARCOACH_ENABLE_OPENMP
  OMP,
#endif
#ifdef PARCOACH_ENABLE_CUDA
  CUDA,
#endif
#ifdef PARCOACH_ENABLE_RMA
  RMA,
#endif
#ifdef PARCOACH_ENABLE_UPC
  UPC,
#endif
};
}
