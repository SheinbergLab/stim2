/* Stand-in declarations so tcl_dl.c compiles. The JSON commands are not
   part of this build; the functions they call are stubs that fail. */
#ifndef JANSSON_H
#define JANSSON_H
#include <stddef.h>
typedef struct json_t { int type; } json_t;
typedef long long json_int_t;
#define JSON_INDENT(n) ((size_t)(n) << 11)
#define JSON_COMPACT 0x20
#define JSON_ENSURE_ASCII 0x40
#define JSON_SORT_KEYS 0x80
char *json_dumps(const json_t *json, size_t flags);
void json_decref(json_t *json);
int json_is_array(const json_t *json);
int json_is_string(const json_t *json);
int json_is_integer(const json_t *json);
int json_is_real(const json_t *json);
const char *json_string_value(const json_t *json);
size_t json_string_length(const json_t *json);
json_int_t json_integer_value(const json_t *json);
double json_real_value(const json_t *json);
#endif
