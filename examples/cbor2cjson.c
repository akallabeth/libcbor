/*
 * Copyright (c) 2014-2020 Pavel Kalvoda <me@pavelkalvoda.com>
 *
 * libcbor is free software; you can redistribute it and/or modify
 * it under the terms of the MIT license. See LICENSE for details.
 */

#include <cjson/cJSON.h>
#include <stdio.h>
#include <string.h>

#include "cbor.h"

void usage(void) {
  printf("Usage: cbor2cjson <input file> [offset]\n");
  exit(1);
}

cJSON* cbor_to_cjson(cbor_item_t* item) {
  switch (cbor_typeof(item)) {
    case CBOR_TYPE_UINT:
      return cJSON_CreateNumber(cbor_get_int(item));
    case CBOR_TYPE_NEGINT:
      return cJSON_CreateNumber(-1 - cbor_get_int(item));
    case CBOR_TYPE_BYTESTRING:
      if (cbor_bytestring_is_definite(item)) {
        cbor_mutable_data data = cbor_bytestring_handle(item);
        const size_t size = cbor_bytestring_length(item);
        const size_t total = (size * 2) + 2;
        char* null_terminated_string = calloc(total, sizeof(char));
        null_terminated_string[0] = 'b';
        for (size_t x = 0; x < size; x++) {
          const size_t offset = 2 * x + 1;
          unsigned cur = data[x];
          snprintf(&null_terminated_string[offset], total - offset - 1, "%02X",
                   cur);
        }
        cJSON* result = cJSON_CreateString(null_terminated_string);
        free(null_terminated_string);
        return result;
      }
      return cJSON_CreateString("Unsupported CBOR item: Chunked Bytestring");
    case CBOR_TYPE_STRING:
      if (cbor_string_is_definite(item)) {
        // cJSON only handles null-terminated string
        char* null_terminated_string = malloc(cbor_string_length(item) + 1);
        memcpy(null_terminated_string, cbor_string_handle(item),
               cbor_string_length(item));
        null_terminated_string[cbor_string_length(item)] = 0;
        cJSON* result = cJSON_CreateString(null_terminated_string);
        free(null_terminated_string);
        return result;
      }
      return cJSON_CreateString("Unsupported CBOR item: Chunked string");
    case CBOR_TYPE_ARRAY: {
      cJSON* result = cJSON_CreateArray();
      for (size_t i = 0; i < cbor_array_size(item); i++) {
        cJSON_AddItemToArray(result, cbor_to_cjson(cbor_array_get(item, i)));
      }
      return result;
    }
    case CBOR_TYPE_MAP: {
      cJSON* result = cJSON_CreateObject();
      for (size_t i = 0; i < cbor_map_size(item); i++) {
        struct cbor_pair* cur = &cbor_map_handle(item)[i];
        char key[128] = {0};
        snprintf(key, sizeof(key), "Surrogate key %zu", i);
        // JSON only support string keys
        if (cbor_isa_string(cur->key) && cbor_string_is_definite(cur->key)) {
          size_t key_length = cbor_string_length(cur->key);
          if (key_length >= sizeof(key)) key_length = sizeof(key) - 1;
          // Null-terminated madness
          memcpy(key, cbor_string_handle(cur->key), key_length);
          key[key_length] = 0;
        }
        if (cbor_isa_uint(cur->key)) {
          uint64_t val = cbor_get_int(cur->key);
          snprintf(key, sizeof(key), "%llu", val);
        }
        cJSON_AddItemToObject(result, key,
                              cbor_to_cjson(cbor_map_handle(item)[i].value));
      }
      return result;
    }
    case CBOR_TYPE_TAG: {
      char key[128] = {0};
      uint64_t val = cbor_tag_value(item);
      cbor_item_t* titem = cbor_tag_item(item);
      cJSON* sub = cbor_to_cjson(titem);

      snprintf(key, sizeof(key), "tag_%llu", val);
      cJSON* result = cJSON_CreateObject();
      cJSON_AddItemToObject(result, key, sub);
      return result;
    }
    case CBOR_TYPE_FLOAT_CTRL:
      if (cbor_float_ctrl_is_ctrl(item)) {
        if (cbor_is_bool(item)) return cJSON_CreateBool(cbor_get_bool(item));
        if (cbor_is_null(item)) return cJSON_CreateNull();
        return cJSON_CreateString("Unsupported CBOR item: Control value");
      }
      return cJSON_CreateNumber(cbor_float_get_float(item));
  }

  return cJSON_CreateNull();
}

/*
 * Reads CBOR data from a file and outputs JSON using cJSON
 * $ ./examples/cbor2cjson examples/data/nested_array.cbor
 */

int main(int argc, char* argv[]) {
  if (argc < 2) usage();
  size_t offset = 0;
  if (argc > 2) offset = strtoull(argv[2], NULL, 0);
  FILE* f = fopen(argv[1], "rb");
  if (f == NULL) usage();
  fseek(f, 0, SEEK_END);
  size_t length = (size_t)ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char* buffer = malloc(length);
  fread(buffer, length, 1, f);
  fclose(f);

  if (offset > length) {
    fprintf(stderr, "offset %zu is larger than file %s %zu\n", offset, argv[1],
            length);
    free(buffer);
    return -1;
  }

  /* Assuming `buffer` contains `length` bytes of input data */
  struct cbor_load_result result;
  cbor_item_t* item = cbor_load(&buffer[offset], length - offset, &result);
  free(buffer);

  if (result.error.code != CBOR_ERR_NONE) {
    printf(
        "There was an error while reading the input near byte %zu (read %zu "
        "bytes in total): ",
        result.error.position, result.read);
    exit(1);
  }

  cJSON* cjson_item = cbor_to_cjson(item);
  char* json_string = cJSON_Print(cjson_item);
  printf("%s\n", json_string);
  free(json_string);
  fflush(stdout);

  /* Deallocate the result */
  cbor_decref(&item);
  cJSON_Delete(cjson_item);
}
