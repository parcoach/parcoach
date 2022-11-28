#pragma once

#include "parcoach/Paradigms.h"

#include "llvm/Support/CommandLine.h"

#include <string>

namespace parcoach {
struct Options {
  // Although it doesn't have any member yet, we use a singleton to make sure
  // the constructor is called and provides backwards compatibility with the
  // old -check-* options.
  Options();
  static Options const &get();
  // It may be silly as we could just compare in the caller, but this allows
  // for multiple paradigms being enabled in the future.
  // https://llvm.org/docs/CommandLine.html#parsing-a-list-of-options
  bool isActivated(Paradigm P) const;
};

extern llvm::cl::OptionCategory ParcoachCategory;

} // namespace parcoach
