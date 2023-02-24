#include "parcoach/CFGVisitors.h"

namespace parcoach {
void LoopAggretationInfo::clear() {
  LoopHeaderToSuccessors.clear();
  LoopHeaderToIncomingBlock.clear();
  LoopSuccessorToLoopHeader.clear();
}
} // namespace parcoach
