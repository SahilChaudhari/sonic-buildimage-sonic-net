// SPDX-License-Identifier: MIT or GPL-2.0-only
// SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

#pragma once

struct _dnode {
	union {
		struct _dnode *head; /* ptr to head of list (sys_dlist_t) */
		struct _dnode *next; /* ptr to next node    (sys_dnode_t) */
	};
	union {
		struct _dnode *tail; /* ptr to tail of list (sys_dlist_t) */
		struct _dnode *prev; /* ptr to previous node (sys_dnode_t) */
	};
};

typedef struct _dnode sys_dlist_t;
typedef struct _dnode sys_dnode_t;


static inline void sys_dlist_init(sys_dlist_t *list)
{
  list->head = (sys_dnode_t *)list;
  list->tail = (sys_dnode_t *)list;
}

static inline void sys_dnode_init(sys_dnode_t *node)
{
  node->next = NULL;
  node->prev = NULL;
}

static inline bool sys_dnode_is_linked(const sys_dnode_t *node)
{
  return node->next != NULL;
}

static inline bool sys_dlist_is_empty(sys_dlist_t *list)
{
  return list->head == list;
}

static inline void sys_dlist_remove(sys_dnode_t *node)
{
  sys_dnode_t *const prev = node->prev;
  sys_dnode_t *const next = node->next;

  prev->next = next;
  next->prev = prev;
  sys_dnode_init(node);
}

static inline sys_dnode_t *sys_dlist_get(sys_dlist_t *list)
{
  sys_dnode_t *node = NULL;

  if (!sys_dlist_is_empty(list)) {
    node = list->head;
    sys_dlist_remove(node);
  }

  return node;
}

static inline sys_dnode_t *sys_dlist_peek_tail(sys_dlist_t *list)
{
  return sys_dlist_is_empty(list) ? NULL : list->tail;
}

static inline void sys_dlist_append(sys_dlist_t *list, sys_dnode_t *node)
{
  sys_dnode_t *const tail = list->tail;

  node->next = list;
  node->prev = tail;

  tail->next = node;
  list->tail = node;
}

static inline sys_dnode_t *sys_dlist_peek_head(sys_dlist_t *list)
{
  return sys_dlist_is_empty(list) ? NULL : list->head;
}

static inline sys_dnode_t *sys_dlist_peek_next_no_check(sys_dlist_t *list,
							sys_dnode_t *node)
{
  return (node == list->tail) ? NULL : node->next;
}

static inline sys_dnode_t *sys_dlist_peek_next(sys_dlist_t *list,
					       sys_dnode_t *node)
{
  return (node != NULL) ? sys_dlist_peek_next_no_check(list, node) : NULL;
}

#define SYS_DLIST_FOR_EACH_NODE(__dl, __dn)             \
  for (__dn = sys_dlist_peek_head(__dl); __dn != NULL;  \
       __dn = sys_dlist_peek_next(__dl, __dn))

#define SYS_DLIST_FOR_EACH_NODE_SAFE(__dl, __dn, __dns) \
  for (__dn = sys_dlist_peek_head(__dl),                \
         __dns = sys_dlist_peek_next(__dl, __dn);       \
       __dn != NULL; __dn = __dns,                      \
         __dns = sys_dlist_peek_next(__dl, __dn))
