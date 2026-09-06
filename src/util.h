/*
  Copyright (c) 2026 Anthony J. Thibault
  This software is licensed under the MIT License. See LICENSE for more
  details.
*/

#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <stdint.h>

uint8_t *ReadFile(const char *filename, size_t *out_size);

#endif  // UTIL_H
