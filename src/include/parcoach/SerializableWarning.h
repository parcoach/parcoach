#pragma once

#include "parcoach/Warning.h"

#include "llvm/Support/Error.h"

#include <string>
#include <vector>

namespace llvm {
class raw_fd_ostream;
}

namespace parcoach::serialization::sonar {

struct TextRange {
  int Line{};
};

struct Location {
  std::string Filename{};
  TextRange Range{};
  std::string Message{};
};

struct Warning {
  Location Message{};
  using SecondaryLocationsTy = std::vector<Location>;
  SecondaryLocationsTy Conditionals{};
  Warning() = default;
  Warning(parcoach::Warning const &W);
};

struct Database {
  std::vector<Warning> Issues;
  void append(WarningCollection const &Warnings);
  void write(llvm::raw_fd_ostream &Os) const;
  static llvm::Expected<Database> load(llvm::StringRef Json);
};
} // namespace parcoach::serialization::sonar
