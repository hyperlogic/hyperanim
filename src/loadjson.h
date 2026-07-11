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

void PrintGraph(const HYA_Graph *graph);
void PrintNode(const HYA_Node *node, const HYA_Graph *graph);

HYA_Result InitGraph(HYA_Graph *graph, Context *ctx,
                     struct json_value_s *value);
HYA_Result InitNode(HYA_Node *node, Context *ctx,
                    struct json_value_s *value);

#define X(Name, name, NAME)                                             \
  void Print##Name##Node(const HYA_##Name##Node *node,                  \
                         const HYA_Graph *graph);                       \
  HYA_Result Init##Name##Node(HYA_##Name##Node *node, Context *ctx,     \
                              struct json_value_s *value);
HYA_NODE_NAME_LIST
#undef X

#endif  // #define LOADJSON_H
