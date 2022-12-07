#include "TempFileRAII.h"

#include "llvm/Support/Program.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;
namespace parcoach {

TempFileRAII::~TempFileRAII() {
  assert(!FileName_.empty() && "This construct only supports non-empty names.");
  auto EC = sys::fs::remove(FileName_);
  if (EC) {
    WithColor::remark()
        << "Parcoach wrapper: could not remove temporary file '" << FileName_
        << "': " << EC.message()
        << "\nIgnoring the error and returning the original exit code.\n";
  }
}

std::optional<TempFileRAII> TempFileRAII::CreateIRFile() {
  SmallString<128> Name;
  auto EC = sys::fs::createUniqueFile("parcoach-ir-%%%%%%.ll", Name);
  if (EC) {
    WithColor::remark()
        << "Parcoach wrapper: Error creating a temporary file for emitting "
        << "the IR: " << EC.message()
        << "\nI will exit now and return the original program exit code\n";
    return std::nullopt;
  }
  return std::make_optional<TempFileRAII>(std::move(Name));
}
} // namespace parcoach
