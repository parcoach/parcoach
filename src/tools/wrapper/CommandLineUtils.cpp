#include "CommandLineUtils.h"

#include "llvm/Support/Program.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;
namespace parcoach {

namespace {
std::string const ARGS_ARG{"--args"};
std::array<StringRef, 4> EmitIRArgs{"-g", "-S", "-emit-llvm", "-o"};
} // namespace

bool IsPreventLinkFlag(StringRef Arg) {
  return Arg == "-c" || Arg == "-S" || Arg == "-E";
}

int Execute(ArgList const &Args) {
  // Create parcoach command line
  WithColor::remark() << "Parcoach: running '";
  bool First = true;
  for (auto S : Args) {
    if (!First) {
      errs() << " ";
    }
    First = false;
    errs() << S;
  }
  errs() << "'\n";
  std::string ErrMsg;
  int Result = sys::ExecuteAndWait(Args[0], Args, None, {}, 0, 0, &ErrMsg);
  if (Result < 0) {
    WithColor::error() << ErrMsg << "\n";
  }

  return Result;
}

ArgList BuildParcoachArgs(ArgList const &Argv, StringRef ParcoachBin,
                          StringRef IRFile) {
  ArgList ParcoachArgs = {ParcoachBin};

  auto ArgsPos = llvm::find(Argv, ARGS_ARG);
  if (ArgsPos != Argv.end()) {
    ParcoachArgs.insert(ParcoachArgs.end(), Argv.begin(), ArgsPos);
  }
  ParcoachArgs.emplace_back(IRFile);
  return ParcoachArgs;
}

std::optional<FoundProgramResult> FindProgram(ArgList const &Argv) {
  auto ArgsPos = llvm::find(Argv, ARGS_ARG);
  auto ProgArg = (ArgsPos != Argv.end()) ? ++ArgsPos : Argv.begin();
  if (ProgArg == Argv.end()) {
    WithColor::error() << ARGS_ARG
                       << " must be followed by the program to run.";
    return std::nullopt;
  }
  auto Program = sys::findProgramByName(*ProgArg);
  if (!Program) {
    WithColor::error() << "unable to find '" << *ProgArg
                       << "' in PATH: " << Program.getError().message() << "\n";
    return std::nullopt;
  }
  return std::make_pair(ProgArg, *Program);
}

ArgList BuildOriginalCommandLine(ArgList const &Argv,
                                 FoundProgramResult &FoundProgram) {
  auto &[ProgPos, Program] = FoundProgram;

  ArgList OriginalProgramArgs;
  OriginalProgramArgs.reserve(std::distance(ProgPos, Argv.end()));
  OriginalProgramArgs.emplace_back(Program);
  std::copy(ProgPos + 1, Argv.end(), std::back_inserter(OriginalProgramArgs));
  return OriginalProgramArgs;
}

ArgList BuildEmitIRCommandLine(ArgList const &OriginalCommandLine,
                               StringRef IRFileName) {
  ArgList GenerateIRArgs;
  // The +1 is here for the temporary IR file name.
  GenerateIRArgs.reserve(OriginalCommandLine.size() + EmitIRArgs.size() + 1);

  // We manually push the program name because findProgramByName earlier found
  // the full path for us!
  GenerateIRArgs.push_back(OriginalCommandLine[0]);
  // Copy the original command line, excluding output arguments and any
  // -c/-S/-E flag.
  for (auto It = OriginalCommandLine.begin() + 1;
       It != OriginalCommandLine.end(); ++It) {
    if (IsPreventLinkFlag(*It)) {
      continue;
    }
    if (*It == "-o") {
      // Also skip the actual output file
      ++It;
      continue;
    }
    GenerateIRArgs.emplace_back(*It);
  }

  // Then append our flags to emit IR.
  append_range(GenerateIRArgs, EmitIRArgs);

  // And finally the filename.
  GenerateIRArgs.emplace_back(IRFileName);
  return GenerateIRArgs;
}
} // namespace parcoach
