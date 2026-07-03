/*
  Copyright (c) 2025 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/

#ifndef NODES_H
#define NODES_H

#include "arena.h"
#include "hyperanim.h"
#include "json.h"

typedef struct StrToIdPair {
  char *key;
  size_t value;
} StrToIdPair;

void Print_HYA_Graph(const HYA_Graph *graph);
void Print_HYA_Node(const HYA_Node *node);

#define X(Type, name, NAME) void Print_##Type(const Type *node);
HYA_NODE_TYPE_LIST
#undef X

void Init_HYA_Node(struct json_object_s *object, HYA_Node *node,
                   StrToIdPair *node_map, StrToIdPair *type_map, Arena *arena);
#define X(Type, name, NAME)                                      \
  void Init_##Type(struct json_object_s *object, Type *node,     \
                   StrToIdPair *node_map, StrToIdPair *type_map, \
                   Arena *arena);
HYA_NODE_TYPE_LIST
#undef X

#endif  // #define NODES_H
