#include "parcoach/SonarSerializationPass.h"

#include "parcoach/CollListFunctionAnalysis.h"
#include "parcoach/Options.h"
#include "parcoach/SerializableWarning.h"

#include "Config.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"

using namespace llvm;

namespace parcoach {
namespace {
cl::opt<std::string> DatabaseFile("analysis-db",
                                  cl::desc("File to store the warnings"),
                                  cl::cat(ParcoachCategory));

}

namespace serialization::sonar {
Optional<Database> loadDatabase() {
  assert(!DatabaseFile.empty() &&
         "This function assumes there is a file to load");

  ErrorOr<std::unique_ptr<MemoryBuffer>> FileOrErr =
      MemoryBuffer::getFileOrSTDIN(DatabaseFile, /*IsText=*/true);
  if (std::error_code EC = FileOrErr.getError()) {
    SMDiagnostic(DatabaseFile, SourceMgr::DK_Note,
                 EC.message() + ". Database will be created from scratch.")
        .print(ProgName, errs(), true, true);
    return Database{};
  }
  auto DB = Database::load((*FileOrErr)->getBuffer());
  if (auto E = DB.takeError()) {
    SMDiagnostic(DatabaseFile, SourceMgr::DK_Error,
                 "Could not load analysis database, no results will be save. " +
                     toString(std::move(E)))
        .print(ProgName, errs(), true, true);
    return {};
  }
  return std::move(*DB);
}

bool saveDatabase(Database const &DB) {
  assert(!DatabaseFile.empty() &&
         "This function assumes there is a file where to save the database");
  std::error_code EC;
  raw_fd_ostream Os{DatabaseFile, EC, sys::fs::OF_TextWithCRLF};
  if (EC) {
    errs() << "Could not open file: " << EC.message() << ", " << DatabaseFile
           << "\n";
    return false;
  }

  DB.write(Os);
  return true;
}

} // namespace serialization::sonar

PreservedAnalyses SonarSerializationPass::run(Module &M,
                                              ModuleAnalysisManager &AM) {
  if (DatabaseFile.empty()) {
    return PreservedAnalyses::all();
  }

  auto &Res = AM.getResult<CollectiveAnalysis>(M);

  auto DB = serialization::sonar::loadDatabase();
  if (!DB) {
    return PreservedAnalyses::all();
  }
  DB->append(*Res);
  serialization::sonar::saveDatabase(*DB);
  return PreservedAnalyses::all();
}

} // namespace parcoach
