#include "rma_analyzer.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
void STORE(void *addr, uint64_t size, uint64_t line, char *filename) {
  uint64_t address = (uint64_t)addr;
  LOG(stderr, "Store address %lu\n", address);
  LOG(stderr, "size : %lu\n", (size / 8));

  /* We save this interval in all active windows, since load/store
   * instructions have an impact on all active windows */
  rma_analyzer_save_interval_all_wins(address, (size / 8) - 1, LOCAL_WRITE,
                                      line, filename);
}

void LOAD(void *addr, uint64_t size, uint64_t line, char *filename) {
  uint64_t address = (uint64_t)addr;
  LOG(stderr, "Load address %lu\n", address);
  LOG(stderr, "size : %lu\n", (size / 8));

  /* We save this interval in all active windows, since load/store
   * instructions have an impact on all active windows */
  rma_analyzer_save_interval_all_wins(address, (size / 8) - 1, LOCAL_READ, line,
                                      filename);
}
}
