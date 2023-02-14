#pragma once

#ifndef NDEBUG
#define LOG fprintf
#else
#define LOG(...)                                                               \
  do {                                                                         \
  } while (0)
#endif // __DEBUG

#ifndef NDEBUG
#define RMA_DEBUG(X)                                                           \
  {                                                                            \
    ostringstream Err;                                                         \
    X;                                                                         \
  }
#else
#define RMA_DEBUG(...)                                                         \
  do {                                                                         \
  } while (0)
#endif
