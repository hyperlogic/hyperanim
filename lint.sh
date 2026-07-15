#!/bin/bash
set -e
cd "$(dirname "$0")"

# C sources to lint (excluding third-party cook/json.h).
FILES="src/*.h src/*.c play/*.c test/*.c"

# Formatting check; pass --fix to reformat in place.
if [ "$1" == "--fix" ]; then
  clang-format -i $FILES
else
  clang-format --dry-run --Werror $FILES
fi

# Static analysis. No compile database, so compiler flags follow the "--".
# --header-filter keeps diagnostics out of third-party json.h.

# TIDY_CHECKS="clang-analyzer-*,bugprone-*,performance-*,portability-*,-bugprone-easily-swappable-parameters"
# CFLAGS="-std=c11 -Icook -I."

# clang-tidy --checks="$TIDY_CHECKS" --warnings-as-errors="$TIDY_CHECKS" \
#   --header-filter='hyperanim\.h' cook/cook.c -- $CFLAGS

# # Analyze the header's implementation side too (it's a single-header lib).
# clang-tidy --checks="$TIDY_CHECKS" --warnings-as-errors="$TIDY_CHECKS" \
#   --header-filter='hyperanim\.h' hyperanim.h -- -x c -DHYA_IMPLEMENTATION $CFLAGS

echo "lint OK"
