#include "rma_analyzer.h"
#include "util.h"

using namespace parcoach::rma;
extern "C" {
void STORE(void *addr, uint64_t size, uint64_t line, char *filename) {
  uint64_t address = (uint64_t)addr;
  RMA_DEBUG(std::cerr << "Store address " << address << "\n");
  RMA_DEBUG(std::cerr << "size " << size / 8 << "\n");

  /* We save this interval in all active windows, since load/store
   * instructions have an impact on all active windows */
  rma_analyzer_save_interval_all_wins(Access(MemoryAccess{address, (size / 8)},
                                             AccessType::LOCAL_WRITE,
                                             DebugInfo(line, filename)));
}

void LOAD(void *addr, uint64_t size, uint64_t line, char *filename) {
  uint64_t address = (uint64_t)addr;
  RMA_DEBUG(std::cerr << "Load address " << address << "\n");
  RMA_DEBUG(std::cerr << "size " << size / 8 << "\n");

  /* We save this interval in all active windows, since load/store
   * instructions have an impact on all active windows */
  rma_analyzer_save_interval_all_wins(Access(MemoryAccess{address, (size / 8)},
                                             AccessType::LOCAL_READ,
                                             DebugInfo(line, filename)));
}
}
