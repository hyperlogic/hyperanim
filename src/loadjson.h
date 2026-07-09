/*
  Copyright (c) 2025 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/

#ifndef LOADJSON_H
#define LOADJSON_H

#include "arena.h"
#include "context.h"
#include "hyperanim.h"
#include "json.h"

void Print_HYA_Graph(const HYA_Graph *graph);
void Print_HYA_Node(const HYA_Node *node, const HYA_Graph *graph);

HYA_Result Init_HYA_Graph(HYA_Graph *graph, Context *ctx,
                          struct json_value_s *value);
HYA_Result Init_HYA_Node(HYA_Node *node, Context *ctx,
                         struct json_value_s *value);

#define X(Type, name, NAME)                                        \
  void Print_##Type(const Type *node, const HYA_Graph *graph);     \
  HYA_Result Init_##Type(Type *node, Context *ctx,                 \
                         struct json_value_s *value);
HYA_NODE_TYPE_LIST
#undef X

#endif  // #define LOADJSON_H
