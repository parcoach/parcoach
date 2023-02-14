#ifndef __INTERVAL__H__
#define __INTERVAL__H__
#include "util.h"
#include <mpi.h>
#include <stdint.h>
#include <string>

#define FILENAME_MAX_LENGTH 128
/* Access types of intervals, similar to access rights of memory.
 * These enums should be kept as is, as the checking of compatibility
 * between accesses is made with a binary OR on values */
typedef enum {
  LOCAL_READ = 0,
  LOCAL_WRITE = 1,
  RMA_READ = 2,
  RMA_WRITE = 3
} Access_type;

/* An interval is described with an access type, and two bounds
 * that delimits the intervals as such : [low_bound, up_bound[ */
struct Interval {
  Access_type access_type;
  uint64_t low_bound;
  uint64_t up_bound;
  /* Debug infos needed for user feedback */
  int line;
  char filename[FILENAME_MAX_LENGTH];
};

/* Convert an access type to a string */
std::string access_type_to_str(Access_type access);

/* Print the contents of the interval as this :
 * "access_type [low_bound, up_bound[" */
void print_interval(Interval &itv);

/* Returns an Interval object with the parameters specified in the
 * routine */
Interval *create_interval(uint64_t low_bound, uint64_t up_bound,
                          Access_type access_type, int line,
                          char const *filename);

void free_interval(Interval *itv);

/* Getter routines for interval specifics */
uint64_t get_low_bound(Interval *itv);
uint64_t get_up_bound(Interval *itv);
Access_type get_access_type(Interval *itv);
int get_fileline(Interval *itv);

/* Returns 0 if the two intervals do not intersect, 1 otherwise. */
int if_intersects(Interval itvA, Interval itvB);

/* Builds a new interval from the union of the two intervals given in
 * parameters */
Interval unite_intervals(Interval itvA, Interval itvB);

#endif //__INTERVAL_H__
