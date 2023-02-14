#ifndef __INTERVAL_TREE__H__
#define __INTERVAL_TREE__H__

#include "interval.h"
#include <stdbool.h>

/*structure to represent a node in Interval Search Tree */
typedef struct Interval_tree {
  Interval *itv;
  struct Interval_tree *left;
  struct Interval_tree *right;
  unsigned long tree_size;
  unsigned long tree_max_depth;
} Interval_tree;

/* this function aims to create a new Interval Search Tree Node */

Interval_tree *new_interval_tree(Interval *i);

/*This function aims to insert a new inetrval Search tree Node, here the low
 * value of interval is used to do comparisons */

void insert_interval_tree(Interval_tree *root, Interval *i);

/*This function aims to check if given two intervals overlap*/

bool do_overlap(Interval i, Interval j);

/*The main function that checks if the given interval overlap with another
 * interval in the interval tree */

Interval *overlap_search(Interval_tree *root, Interval *i);

/* this function aims to do inorder traversal of the binary search tree */

void in_order_print_tree(Interval_tree *root);

/*this function aims to free the whole BST from memory*/

void free_interval_tree(Interval_tree *root);

/* Prints interval tree statistics:
 * - Number of nodes in the tree
 * - Size of the tree
 * - Depth of the longest branch in the tree
 */
void print_interval_tree_stats(Interval_tree *root);

#endif // __INTERVAL_TREE_H__
