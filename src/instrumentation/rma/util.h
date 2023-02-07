#ifndef __UTIL__H__
#define __UTIL__H__

#include <stdio.h>

#if __DEBUG
#define LOG fprintf
#else
#define LOG(...)                                                               \
  do {                                                                         \
  } while (0)
#endif // __DEBUG

#endif // __UTIL_H__
