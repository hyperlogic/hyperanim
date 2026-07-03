#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "json.h"
#include "hyperanim.h"

void PrintUsage() {
  printf("cook\n");
  printf("\n");
  printf("USAGE:\n");
  printf("  cook file.json\n");
}

char* ReadFile(const char* filename, size_t* out_size) {
  FILE* fp = fopen(filename, "rb");
  if (!fp) {
    return NULL;
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  long size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    return NULL;
  }
  rewind(fp);

  char *buf = malloc(size + 1); // +1 for optional NUL terminator
  if (!buf) {
    fclose(fp);
    return NULL;
  }

  size_t nread = fread(buf, 1, size, fp);
  if (nread != (size_t)size) {  // short read = error
    free(buf);
    fclose(fp);
    return NULL;
  }

  buf[size] = '\0';  // makes it safe to treat as a C string
  fclose(fp);

  if (out_size) {
    *out_size = nread;
  }
  return buf;
}

bool Traverse(struct json_value_s* root) {
  struct json_object_s* object = (struct json_object_s*)root->payload;
  struct json_object_element_s* elem = object->start;
  while(elem != NULL) {
    printf("%s\n", elem->name->string);
    elem = elem->next;
  }
  return true;
}

int main(int argc, const char* argv[]) {
  if (argc != 2) {
    printf("ERROR: Expected file.json argument\n");
    PrintUsage();
    return 1;
  }

  size_t buf_size = 0;
  const char* buf = ReadFile(argv[1], &buf_size);
  if (!buf) {
    printf("ERROR: loading %s\n", argv[1]);
    return 2;
  }

  struct json_value_s* root = json_parse((const void*)buf, buf_size);
  if (!root) {
    printf("ERROR: parsing %s\n", argv[1]);
    return 3;
  }
  if (root->type != json_type_object) {
    printf("ERROR: expected root to a json object\n");
    return 4;
  }

  if (!Traverse(root)) {
    printf("ERROR: traversal failed\n");
  }
}
