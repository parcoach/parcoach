#ifndef __INTERVAL_LIST__H__
#define __INTERVAL_LIST__H__

#include "interval.h"
#include "util.h"

typedef struct Interval_list {
  Interval itv;
  struct Interval_list *next_itv;
} Interval_list;

/* Utilitary routines to create, free, print and get information of
 * the interval list */
Interval_list *create_interval_list(void);
void free_interval_list(Interval_list **li_inter);
void print_interval_list(Interval_list *li_inter);
int interval_list_size(Interval_list *li_inter);
int is_empty_list(Interval_list *li_inter);

/* Add and remove elements of the list routines */
void insert_interval_tail(Interval_list *li_inter, Interval itv);
Interval_list *insert_interval_head(Interval_list *li_inter, Interval itv);
void remove_interval_tail(Interval_list **li_inter);
void remove_interval_head(Interval_list **li_inter);

/* Intersection checking routines */
int if_intersects_interval_list(Interval itv, Interval_list *li_inter);

#endif // __INTERVAL_LIST_H__
