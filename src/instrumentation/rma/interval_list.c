#include "interval_list.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

Interval_list *create_interval_list(void) { return NULL; }

/* Free interval list from its head. This is why we need a pointer to
 * the head of the list, so that at the end the head of the user's
 * list is properly set to NULL */
void free_interval_list(Interval_list **li_inter) {
  if (is_empty_list(*li_inter))
    return;

  while (*li_inter != NULL)
    remove_interval_head(li_inter);

  assert(*li_inter == NULL);
}

void print_interval_list(Interval_list *li_inter) {
  if (is_empty_list(li_inter)) {
    LOG(stderr, "rien à afficher la liste est vide\n");
    return;
  }
  while (li_inter != NULL) {
    print_interval(li_inter->itv);
    li_inter = li_inter->next_itv;
  }
}

int interval_list_size(Interval_list *li_inter) {
  int size = 0;
  if (!is_empty_list(li_inter)) {
    while (li_inter != NULL) {
      size++;
      li_inter = li_inter->next_itv;
    }
  }
  return size;
}

int is_empty_list(Interval_list *li_inter) { return (li_inter == NULL); }

void insert_interval_tail(Interval_list *li_inter, Interval itv) {
  Interval_list *new_tail;
  new_tail = malloc(sizeof(Interval_list));

  if (new_tail == NULL) {
    LOG(stderr, "Erreur : probleme allocation !\n");
    exit(EXIT_FAILURE);
  }

  /* Fill up new last link of the list */
  new_tail->itv = itv;
  new_tail->next_itv = NULL;

  /* If the list is empty, the new tail is the whole list, just
   * return it */
  if (is_empty_list(li_inter))
    return;

  /* If not, go through the list to find last link and add the new
   * tail at the end */
  Interval_list *tmp_chain;
  tmp_chain = li_inter;
  while (tmp_chain->next_itv != NULL)
    tmp_chain = tmp_chain->next_itv;

  /* We found the last link, just add it */
  tmp_chain->next_itv = new_tail;
}

Interval_list *insert_interval_head(Interval_list *li_inter, Interval itv) {
  Interval_list *new_head;
  new_head = malloc(sizeof(Interval_list));

  if (new_head == NULL) {
    LOG(stderr, "Erreur : probleme allocation !\n");
    exit(EXIT_FAILURE);
  }

  new_head->itv = itv;

  if (is_empty_list(li_inter))
    new_head->next_itv = NULL;
  else
    new_head->next_itv = li_inter;

  return new_head;
}

// Enlver un intervalle de la fin de liste ça peut servire en cas de flush et
// pour libérer la mémoire
void remove_interval_tail(Interval_list **li_inter) {
  /* If the list is empty, nothing to do */
  if (is_empty_list(*li_inter))
    return;

  /* If the list has only one element in it, just free this link */
  if ((*li_inter)->next_itv == NULL) {
    free(*li_inter);
    return;
  }

  /* If the list has several elements in it, go through the list to
   * find last link */

  /* We need here to remember the last link and the second last link
   * to properly setup the end of the list */
  Interval_list *last_link = *li_inter;
  Interval_list *second_last_link = *li_inter;

  while (last_link->next_itv != NULL) {
    second_last_link = last_link;
    last_link = last_link->next_itv;
  }

  /* Setup new end of list */
  second_last_link->next_itv = NULL;

  free(last_link);
}

void remove_interval_head(Interval_list **li_inter) {
  /* If the list is empty, nothing to do */
  if (is_empty_list(*li_inter))
    return;

  Interval_list *tmp_head = (*li_inter)->next_itv;

  // LOG(stderr,"I removed ");
  // print_interval((*li_inter)->itv);
  // LOG(stderr,"remain in the list \n");
  // print_interval_list(tmp_head);

  free(*li_inter);

  /* Affect new head of list to user pointer */
  *li_inter = tmp_head;
}

int if_intersects_interval_list(Interval itv, Interval_list *li_inter) {
  /* If the list is empty, there is no intersection */
  if (is_empty_list(li_inter)) {
    return 0;
  }

  /* Check links of the list one by one */
  Interval_list *tmp_link = li_inter;
  if (if_intersects(itv, tmp_link->itv)) {
    return 1;
  }
  while (tmp_link->next_itv != NULL) {
    tmp_link = tmp_link->next_itv;
    if (if_intersects(itv, tmp_link->itv)) {
      return 1;
    }
  }

  return 0;
}
