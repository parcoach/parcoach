#include "ShowPAInterResults.h"
#include "Options.h"
#include "ParcoachAnalysisInter.h"
#include "Utils.h"

#include "llvm/Support/WithColor.h"

using namespace llvm;
namespace parcoach {

PreservedAnalyses ShowPAInterResult::run(Module &M, ModuleAnalysisManager &AM) {
  auto &PAInter = AM.getResult<InterproceduralAnalysis>(M);
  unsigned intersectionSize;
  int WnbAdded = 0, CnbAdded = 0;
  int WnbRemoved = 0, CnbRemoved = 0;
  auto CyanErr = []() { return WithColor(errs(), raw_ostream::Colors::CYAN); };
  if (!optNoDataFlow) {
    CyanErr() << "==========================================\n";
    CyanErr() << "===  PARCOACH INTER WITH DEP ANALYSIS  ===\n";
    CyanErr() << "==========================================\n";
    errs() << "Module name: " << M.getModuleIdentifier() << "\n";
    errs() << PAInter->getNbCollectivesFound() << " collective(s) found\n";
    errs() << PAInter->getNbCollectivesCondCalled()
           << " collective(s) conditionally called\n";
    errs() << PAInter->getNbWarnings() << " warning(s) issued\n";
    errs() << PAInter->getNbConds() << " cond(s) \n";
    errs() << PAInter->getConditionSet().size() << " different cond(s)\n";
    errs() << PAInter->getNbCC() << " CC functions inserted \n";

    intersectionSize = getBBSetIntersectionSize(
        PAInter->getConditionSet(), PAInter->getConditionSetParcoachOnly());

    CnbAdded = PAInter->getConditionSet().size() - intersectionSize;
    CnbRemoved =
        PAInter->getConditionSetParcoachOnly().size() - intersectionSize;
    errs() << CnbAdded << " condition(s) added and " << CnbRemoved
           << " condition(s) removed with dep analysis.\n";

    intersectionSize = getInstSetIntersectionSize(
        PAInter->getWarningSet(), PAInter->getWarningSetParcoachOnly());

    WnbAdded = PAInter->getWarningSet().size() - intersectionSize;
    WnbRemoved = PAInter->getWarningSetParcoachOnly().size() - intersectionSize;
    errs() << WnbAdded << " warning(s) added and " << WnbRemoved
           << " warning(s) removed with dep analysis.\n";
  } else {

    CyanErr() << "================================================\n";
    CyanErr() << "===== PARCOACH INTER WITHOUT DEP ANALYSIS ======\n";
    CyanErr() << "================================================\n";
    errs() << PAInter->getNbCollectivesFound() << " collective(s) found\n";
    errs() << PAInter->getNbWarningsParcoachOnly() << " warning(s) issued\n";
    errs() << PAInter->getNbCondsParcoachOnly() << " cond(s) \n";
    errs() << PAInter->getConditionSetParcoachOnly().size()
           << " different cond(s)\n";
    errs() << PAInter->getNbCC() << " CC functions inserted \n";
  }

  /* if (!optNoDataFlow) {
     errs() << "app," << PAInter->getNbCollectivesFound() << ","
       << PAInter->getNbWarnings() << ","
       << PAInter->getConditionSet().size() << "," << WnbAdded << ","
       << WnbRemoved << "," << CnbAdded << "," << CnbRemoved << ","
       << PAInter->getNbWarningsParcoachOnly() << ","
       << PAInter->getConditionSetParcoachOnly().size() << "\n";
   }*/
  CyanErr() << "==========================================\n";

#if 0
  // FIXME: restore timings with LLVM stuff
  if (optTimeStats) {
    errs() << "AA time : " << format("%.3f", (tend_aa - tstart_aa) * 1.0e3)
           << " ms\n";
    errs() << "Dep Analysis time : "
           << format("%.3f", (tend_depgraph - tstart_pta) * 1.0e3) << " ms\n";
    errs() << "Parcoach time : "
           << format("%.3f", (tend_parcoach - tstart_parcoach) * 1.0e3)
           << " ms\n";
    errs() << "Total time : " << format("%.3f", (tend - tstart) * 1.0e3)
           << " ms\n\n";

    errs() << "detailed timers:\n";
    errs() << "PTA time : " << format("%.3f", (tend_pta - tstart_pta) * 1.0e3)
           << " ms\n";
    errs() << "Region creation time : "
           << format("%.3f", (tend_regcreation - tstart_regcreation) * 1.0e3)
           << " ms\n";
    errs() << "Modref time : "
           << format("%.3f", (tend_modref - tstart_modref) * 1.0e3) << " ms\n";
    errs() << "ASSA generation time : "
           << format("%.3f", (tend_assa - tstart_assa) * 1.0e3) << " ms\n";
    errs() << "Dep graph generation time : "
           << format("%.3f", (tend_depgraph - tstart_depgraph) * 1.0e3)
           << " ms\n";
  }
#endif
  return PreservedAnalyses::all();
}
} // namespace parcoach
