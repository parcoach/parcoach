#pragma once

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"

#include <optional>

namespace parcoach {
struct TempFileRAII {
  llvm::StringRef getName() const { return FileName_; }
  TempFileRAII(llvm::SmallString<128> &&Name) : FileName_(Name) {}
  TempFileRAII() = delete;
  TempFileRAII(TempFileRAII const &) = delete;

  ~TempFileRAII();

  static std::optional<TempFileRAII> CreateIRFile();

private:
  llvm::SmallString<128> FileName_;
};

} // namespace parcoach
