#include "interval_tree.h"
#include <assert.h>
#include <inttypes.h>
#include <iostream>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>

using namespace std;
static inline unsigned long max(unsigned long A, unsigned long B) {
  return (A > B ? A : B);
}

/*create a new interval Search tree Node */

Interval_tree *new_interval_tree(Interval *i) {
  Interval_tree *temp = new Interval_tree;
  temp->itv = i;
  temp->left = temp->right = NULL;
  temp->tree_size = 1;
  temp->tree_max_depth = 1;
  return temp;
}

/*this function aims to insert a new inetrval Search tree Node, here the low
 * value of interval is used to do comparisons */

void insert_interval_tree(Interval_tree *root, Interval *i) {
  /*If the tree is empty, return a new node */
  if (root == NULL)
    return;

  uint64_t low_bound = get_low_bound(i);
  uint64_t low_bound_root = get_low_bound(root->itv);
  /*Otherwise, recur down the tree, and get the low value of interval at root */

  /*If root's low value is smaller than new interval's low value -> go to left
   * subtree*/
  if (low_bound < low_bound_root) {
    if (root->left == NULL)
      root->left = new_interval_tree(i);
    else
      insert_interval_tree(root->left, i);
    /* Update max depth if needed */
    root->tree_max_depth =
        max(root->tree_max_depth, root->left->tree_max_depth + 1);
  }
  /*Else go to right subtree */
  else {
    if (root->right == NULL)
      root->right = new_interval_tree(i);
    else
      insert_interval_tree(root->right, i);
    /* Update max depth if needed */
    root->tree_max_depth =
        max(root->tree_max_depth, root->right->tree_max_depth + 1);
  }

  /* Increment size of the tree */
  root->tree_size++;
}

Interval *overlap_search(Interval_tree *root, Interval *i) {
  /*Empty tree*/
  if (root == NULL)
    return NULL;
  uint64_t low_bound = get_low_bound(i);
  uint64_t low_bound_root = get_low_bound(root->itv);
  RMA_DEBUG({
    cerr << "search between two intervals\n";
    print_interval(*root->itv);
    print_interval(*i);
  });

  /*check if the given interval overlaps with root*/
  if (if_intersects(*root->itv, *i))
    return root->itv;

  /*Check if the left subtree exists, and check overlap conditions */
  if (low_bound < low_bound_root)
    return overlap_search(root->left, i);
  /*check if the right subtree exists and check overlap conditions */

  if (low_bound > low_bound_root)
    return overlap_search(root->right, i);
  // TODO check something here !!
  return NULL;
}
/* this function aims to do inorder traversal of the binary search tree */
void in_order_print_tree(Interval_tree *root) {
  if (root == NULL)
    return;
  uint64_t low_bound_root = get_low_bound(root->itv);
  uint64_t up_bound_root = get_up_bound(root->itv);
  if (root->left != NULL) {
    // printf("the left subtree contains : \n");
    in_order_print_tree(root->left);
  }
  printf("[%" PRIu64 ", %" PRIu64 "]\n", low_bound_root, up_bound_root);
  if (root->right != NULL) {
    // printf("the right subtree contains : \n");
    in_order_print_tree(root->right);
  }
}

/* this function aims to free the whole BST from memory*/

void free_interval_tree(Interval_tree *root) {
  if (root == NULL)
    return;

  free_interval_tree(root->left);
  free_interval_tree(root->right);
  delete root->itv;
  delete root;
}

void print_interval_tree_stats(Interval_tree *root) {
  if (root != NULL) {
    printf("Tree statistics:\n");
    printf("Number of nodes in the tree: %lu \n", root->tree_size);
    printf("Size of the tree: %lu \n", root->tree_size * sizeof(Interval_tree));
    printf("Depth of the longest branch of the tree: %lu \n",
           root->tree_max_depth);
  }
}
