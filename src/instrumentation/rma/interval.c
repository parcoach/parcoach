#include "interval.h"
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Convert an access type to a string */
char *access_type_to_str(Access_type access) {
  if (access == LOCAL_READ)
    return "LOCAL_READ";
  else if (access == LOCAL_WRITE)
    return "LOCAL_WRITE";
  else if (access == RMA_READ)
    return "RMA_READ";
  else // access == RMA_WRITE
    return "RMA_WRITE";
}

/* Check if the access rights are compatible. */
static inline int check_access_rights(Interval itvA, Interval itvB) {
  /* By doing a bitwise OR on the two access types, the resulting
   * value is an error only if the two bits are at 1 (i.e. there is at
   * least a local WRITE access combined with a remote access, or a
   * remote WRITE access). */
  return ((itvA.access_type | itvB.access_type) == RMA_WRITE);
}

void print_interval(Interval itv) {
  LOG(stderr, "Interval = [%" PRIu64 " ,%" PRIu64 "] ", itv.low_bound,
      itv.up_bound);
  if (itv.access_type == LOCAL_READ)
    LOG(stderr, "LOCAL READ\n");
  else if (itv.access_type == LOCAL_WRITE)
    LOG(stderr, "LOCAL WRITE\n");
  else if (itv.access_type == RMA_READ)
    LOG(stderr, "RMA READ\n");
  else // RMA WRITE
    LOG(stderr, "RMA WRITE\n");
}

Interval *create_interval(uint64_t low_bound, uint64_t up_bound,
                          Access_type access_type, int line, char *filename) {
  Interval *itv = malloc(sizeof(Interval)); // TODO free
  itv->low_bound = low_bound;
  itv->up_bound = up_bound;
  itv->access_type = access_type;
  itv->line = line;
  if (filename != NULL) {
    strncpy(&(itv->filename[0]), filename, FILENAME_MAX_LENGTH - 1);
    itv->filename[FILENAME_MAX_LENGTH - 1] = '\0';
  } else {
    itv->filename[0] = '\0';
  }
  return itv;
}

void free_interval(Interval *itv) { free(itv); }

uint64_t get_low_bound(Interval *itv) { return itv->low_bound; }

uint64_t get_up_bound(Interval *itv) { return itv->up_bound; }

Access_type get_access_type(Interval *itv) { return itv->access_type; }

int get_fileline(Interval *itv) { return itv->line; }

char *get_filename(Interval *itv) { return &(itv->filename[0]); }

int if_intersects(Interval itvA, Interval itvB) {
  // itvA.low_bound > itvB.low_bound or itvA.up_bound < itvB.low_bound
  // No intersection
  if ((itvA.low_bound > itvB.up_bound) || (itvA.up_bound < itvB.low_bound)) {
    // LOG(stderr,"Pas d'intersection !\n");
    return 0;
  } else {
    /* Several intersection cases are checked here :
       - itvB.low_bound < itvA.low_bound < itvA.up_bound < itvB.up_bound
         Intersection of A & B : A completely included in B
       - itvA.low_bound < itvB.low_bound < itvB.up_bound < itvA.up_bound
         Intersection of A & B : B completely included in A
       - itvB.low_bound < itvA.low_bound < itvB.up_bound < itvA.up_bound,
         Intersection of A & B = [itvA.low_bound ; itvB.up_bound]
       - itvA.low_bound < itvB.low_bound < itvA.up_bound < itvB.up_bound
         Intersection de A & B = [itvB.low_bound ; itvA.up_bound] */
    return check_access_rights(itvA, itvB);
  }
  return 0;
}

Interval unite_intervals(Interval itvA, Interval itvB) {
  // TODO : TBD
  return itvA;
}
