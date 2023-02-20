#pragma once

#ifndef NDEBUG
#include <sstream>
#define RMA_DEBUG(X)                                                           \
  {                                                                            \
    std::ostringstream Err;                                                    \
    X;                                                                         \
  }
#else
#define RMA_DEBUG(...)                                                         \
  do {                                                                         \
  } while (0)
#endif
