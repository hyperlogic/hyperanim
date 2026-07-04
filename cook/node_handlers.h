/*
  Copyright (c) 2025 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/

#ifndef NODES_H
#define NODES_H

#include "arena.h"
#include "context.h"
#include "hyperanim.h"
#include "json.h"

void Print_HYA_Graph(const HYA_Graph *graph);
void Print_HYA_Node(const HYA_Graph *graph, const HYA_Node *node);
HYA_Result Init_HYA_Node(struct json_object_s *object, HYA_Node *node,
                         Context *ctx);

#define X(Type, name, NAME)                                        \
  void Print_##Type(const HYA_Graph *graph, const Type *node);     \
  HYA_Result Init_##Type(struct json_object_s *object, Type *node, \
                         Context *ctx);
HYA_NODE_TYPE_LIST
#undef X

#endif  // #define NODES_H
