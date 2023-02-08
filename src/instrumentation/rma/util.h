#ifndef __UTIL__H__
#define __UTIL__H__

#include <stdio.h>

#ifndef NDEBUG
#define LOG fprintf
#else
#define LOG(...)                                                               \
  do {                                                                         \
  } while (0)
#endif // __DEBUG

#endif // __UTIL_H__
