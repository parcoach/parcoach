#include "parcoach/Passes.h"

#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/RegionPass.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/AsmParser/Parser.h"
#include "llvm/CodeGen/CommandFlags.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LLVMRemarkStreamer.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/LegacyPassNameParser.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/InitializePasses.h"
#include "llvm/LinkAllIR.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Remarks/HotnessThresholdParser.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/IPO/WholeProgramDevirt.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Transforms/Utils/Debugify.h"

using namespace llvm;

namespace {

cl::opt<std::string> InputFilename(cl::Positional,
                                   cl::desc("<input bitcode file>"),
                                   cl::init("-"), cl::value_desc("filename"));

cl::opt<std::string> OutputFilename("o", cl::desc("Override output filename"),
                                    cl::value_desc("filename"));

cl::opt<bool> OutputAssembly("S", cl::desc("Write output as LLVM assembly"));

cl::opt<bool> NoOutput("disable-output",
                       cl::desc("Do not write result bitcode file"),
                       cl::Hidden);

cl::opt<bool> TimeTrace("time-trace", cl::desc("Record time trace"));

cl::opt<unsigned> TimeTraceGranularity(
    "time-trace-granularity",
    cl::desc(
        "Minimum time granularity (in microseconds) traced by time profiler"),
    cl::init(500), cl::Hidden);

cl::opt<std::string>
    TimeTraceFile("time-trace-file",
                  cl::desc("Specify time trace file destination"),
                  cl::value_desc("filename"));

static cl::opt<bool> ShowVersion("parcoach-version",
                                 cl::desc("Show PARCOACH version"));

enum OutputKind {
  OK_NoOutput,
  OK_OutputAssembly,
  OK_OutputBitcode,
};

struct TimeTracer {
  StringRef InputFilename;
  TimeTracer(StringRef ProgramName, StringRef Input) : InputFilename(Input) {
    if (TimeTrace)
      timeTraceProfilerInitialize(TimeTraceGranularity, ProgramName);
  }
  ~TimeTracer() {
    if (TimeTrace) {
      if (auto E = timeTraceProfilerWrite(TimeTraceFile, InputFilename)) {
        handleAllErrors(std::move(E), [&](StringError const &SE) {
          errs() << SE.getMessage() << "\n";
        });
        return;
      }
      timeTraceProfilerCleanup();
    }
  }
};
} // namespace

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  // Enable debug stream buffering.
  EnableDebugBuffering = true;

  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmPrinters();
  InitializeAllAsmParsers();

  // Initialize passes
  PassRegistry &Registry = *PassRegistry::getPassRegistry();
  initializeCore(Registry);
  initializeAnalysis(Registry);
  initializeTransformUtils(Registry);
  initializeInstrumentation(Registry);
  initializeTarget(Registry);

  cl::ParseCommandLineOptions(
      argc, argv, "parcoach: a static/dynamic analyzer for MPI collectives\n");

  if (ShowVersion) {
    parcoach::PrintVersion(outs());
    return 0;
  }

  LLVMContext Context;

  SMDiagnostic Err;
  // Load the input module...
  std::unique_ptr<Module> M = parseIRFile(InputFilename, Err, Context);

  if (!M) {
    Err.print(argv[0], errs());
    return 1;
  }

  TimeTracer Tracer(argv[0], InputFilename);
  TimeTraceScope TTS("Parcoach", "The main entry point of the tool.");

  // Immediately run the verifier to catch any problems before starting up the
  // pass pipelines.  Otherwise we can crash on broken code during
  // doInitialization().
  if (verifyModule(*M, &errs())) {
    errs() << argv[0] << ": " << InputFilename
           << ": error: input module is broken!\n";
    return 1;
  }

  // Figure out what stream we are supposed to write to...
  std::unique_ptr<ToolOutputFile> Out;
  OutputKind OK{OK_NoOutput};

  if (OutputFilename.empty()) {
    // By default we don't necessarily instrument the code, so if we don't
    // explicitly specify an output, just assume we just print the analysis
    // result.
    NoOutput = true;
  }

  if (!NoOutput) {
    std::error_code EC;
    sys::fs::OpenFlags Flags =
        OutputAssembly ? sys::fs::OF_TextWithCRLF : sys::fs::OF_None;
    Out.reset(new ToolOutputFile(OutputFilename, EC, Flags));
    if (EC) {
      errs() << EC.message() << '\n';
      return 1;
    }
    OK = OutputAssembly ? OK_OutputAssembly : OK_OutputBitcode;
  }

  Triple ModuleTriple(M->getTargetTriple());

  // Add an appropriate TargetLibraryInfo pass for the module's triple.
  TargetLibraryInfoImpl TLII(ModuleTriple);

  LoopAnalysisManager LAM;
  FunctionAnalysisManager FAM;
  CGSCCAnalysisManager CGAM;
  ModuleAnalysisManager MAM;

  PassInstrumentationCallbacks PIC;
  PipelineTuningOptions PTO;
  PassBuilder PB(nullptr, PTO, None, &PIC);

  // Register our TargetLibraryInfoImpl.
  FAM.registerPass([&] { return TargetLibraryAnalysis(TLII); });

  // Register all the basic analyses with the managers.
  PB.registerModuleAnalyses(MAM);
  parcoach::RegisterModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  parcoach::RegisterFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  ModulePassManager MPM;
  parcoach::RegisterPasses(MPM);

  switch (OK) {
  case OK_NoOutput:
    break; // No output pass needed.
  case OK_OutputAssembly:
    MPM.addPass(PrintModulePass(Out->os(), "", false));
    break;
  case OK_OutputBitcode:
    MPM.addPass(BitcodeWriterPass(Out->os(), false, false, false));
    break;
  }

  MPM.run(*M, MAM);

  if (Out) {
    Out->keep();
  }
  return 0;
}
