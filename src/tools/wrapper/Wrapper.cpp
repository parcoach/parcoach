#include "CommandLineUtils.h"
#include "TempFileRAII.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;
using namespace parcoach;

namespace {
std::string const PARCOACH_BIN_NAME{"parcoach"};
}

bool isSourceFile(StringRef arg) {
  return arg.endswith(".c") || arg.endswith(".cpp") || arg.endswith(".f90");
}

int main(int argc, char const **argv) {
  ++argv;
  --argc;

  if (argc == 0)
    return 1;

  ArgList Argv;
  Argv.reserve(argc);

  for (int i = 0; i < argc; ++i) {
    Argv.push_back(argv[i]);
  }

  // Try to figure out our location, to help finding the main parcoach
  // executable.
  auto MyAbsoluteExe =
      sys::fs::getMainExecutable(argv[0], (void *)(intptr_t)&main);
  StringRef ExePath = sys::path::parent_path(MyAbsoluteExe);
  auto ParcoachBin = sys::findProgramByName(PARCOACH_BIN_NAME, ExePath);
  if (!ParcoachBin) {
    WithColor::error() << "unable to find '" << PARCOACH_BIN_NAME
                       << "' in PATH: " << ParcoachBin.getError().message()
                       << "\n";
    return 1;
  }

  auto FoundProgram = FindProgram(Argv);
  if (!FoundProgram) {
    return 1;
  }
  auto OriginalProgramArgs = BuildOriginalCommandLine(Argv, *FoundProgram);

  int OriginalResult = Execute(OriginalProgramArgs);

  // Assume the compilation links unless we find a flag stating otherwise.
  bool IsLinkerInvocation = none_of(OriginalProgramArgs, IsPreventLinkFlag);

  if (IsLinkerInvocation) {
    WithColor::remark()
        << "Parcoach: this is a linker invocation, not running parcoach.\n";
  }

  if (OriginalResult != 0 || IsLinkerInvocation) {
    return OriginalResult;
  }

  // Make sure we use a struct that automatically remove the temp file
  // whatever happens next.
  auto IRFile = TempFileRAII::CreateIRFile();
  if (!IRFile) {
    return OriginalResult;
  }
  // Create IR generation command line and run it.
  ArgList GenerateIRArgs =
      BuildEmitIRCommandLine(OriginalProgramArgs, IRFile->getName());
  int Result = Execute(GenerateIRArgs);

  if (Result != 0) {
    WithColor::error()
        << "It doesn't seem the original compiler support '-emit-llvm', "
        << "please make sure to use an LLVM frontend which supports emitting "
        << "LLVM IR.\n";
    return Result;
  }

  // This is a bit sloppy, but at the moment parcoach instrument the code if
  // one of these two flags is set.
  bool ShouldInstrument = llvm::any_of(Argv, [](StringRef Arg) {
    return Arg == "-check=rma" || Arg == "-instrum-inter";
  });
  // Create parcoach command line and run it.
  auto OutputFile =
      ShouldInstrument ? TempFileRAII::CreateIRFile() : std::nullopt;
  if (ShouldInstrument && !OutputFile) {
    return OriginalResult;
  }

  ArgList ParcoachArgs =
      BuildParcoachArgs(Argv, *ParcoachBin, *IRFile, OutputFile);
  Result = Execute(ParcoachArgs);

  if (ShouldInstrument) {
    ArgList InstrArgs = BuildOriginalCommandLine(Argv, *FoundProgram);
    std::replace_if(InstrArgs.begin(), InstrArgs.end(), isSourceFile,
                    OutputFile->getName());
    Result = Execute(InstrArgs);
  }

  return OriginalResult;
}
