/*
 * Symbols tcl_dl.c reaches that are not the list machinery: matrix init,
 * JSON, Arrow, and MessagePack. Calling one of those commands fails.
 * Creating a list and resolving polys:0:0 does not call them.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <tcl.h>
#include <df.h>

/* dl_isMatrix. The matrix commands themselves stay out of this build. */
extern int dynListIsMatrix(DYN_LIST *dl);
int tclIsDynMatrix(Tcl_Interp *interp, DYN_LIST *dl) {
    (void)interp;
    return dynListIsMatrix(dl) ? TCL_OK : TCL_ERROR;
}

/* Date helpers used by dl_days and dl_date. Not used when painting a shape. */
int trDateToDays(int day, int month, int year) {
    (void)day; (void)month; (void)year; return 0;
}
void trDaysToDate(int days, int *day, int *month, int *year) {
    (void)days; *day = 1; *month = 1; *year = 1970;
}

/* Gzip file helpers. Tcl's bundled zlib is built without them. */
typedef struct gzFile_s { int unused; } *gzFile;
gzFile gzopen(const char *path, const char *mode) {
    (void)path; (void)mode; return NULL;
}
gzFile gzdopen(int fd, const char *mode) {
    (void)fd; (void)mode; return NULL;
}
int gzwrite(gzFile file, const void *buf, unsigned len) {
    (void)file; (void)buf; (void)len; return 0;
}
int gzread(gzFile file, void *buf, unsigned len) {
    (void)file; (void)buf; (void)len; return -1;
}
int gzclose(gzFile file) { (void)file; return -1; }
const char *gzerror(gzFile file, int *errnum) {
    (void)file; if (errnum) *errnum = -1; return "gzip is not in this build";
}

struct DYN_GROUP;
struct DYN_LIST;
typedef struct json_t { int type; } json_t;

int Dm_Init(Tcl_Interp *interp) { (void)interp; return TCL_OK; }

json_t *dg_to_json(struct DYN_GROUP *dg) { (void)dg; return NULL; }
json_t *dg_element_to_json(struct DYN_GROUP *dg, int element) {
    (void)dg; (void)element; return NULL;
}
json_t *dl_to_json(struct DYN_LIST *dl) { (void)dl; return NULL; }
json_t *dl_element_to_json(struct DYN_LIST *dl, int element) {
    (void)dl; (void)element; return NULL;
}
json_t *dg_to_hybrid_json(struct DYN_GROUP *dg) { (void)dg; return NULL; }

char *json_dumps(const json_t *json, size_t flags) {
    (void)json; (void)flags; return NULL;
}
void json_decref(json_t *json) { (void)json; }
int json_is_array(const json_t *json) { (void)json; return 0; }
int json_is_string(const json_t *json) { (void)json; return 0; }
int json_is_integer(const json_t *json) { (void)json; return 0; }
int json_is_real(const json_t *json) { (void)json; return 0; }
const char *json_string_value(const json_t *json) { (void)json; return ""; }
size_t json_string_length(const json_t *json) { (void)json; return 0; }
long long json_integer_value(const json_t *json) { (void)json; return 0; }
double json_real_value(const json_t *json) { (void)json; return 0; }

int dg_to_arrow_buffer(struct DYN_GROUP *dg, uint8_t **data, size_t *size) {
    (void)dg; (void)data; (void)size; return -1;
}
int dg_to_arrow_file(struct DYN_GROUP *dg, const char *filename) {
    (void)dg; (void)filename; return -1;
}
const char *dg_arrow_last_error(void) { return "arrow is not in this build"; }
struct DYN_GROUP *arrow_buffer_to_dg(const uint8_t *data, size_t size, const char *group_name) {
    (void)data; (void)size; (void)group_name; return NULL;
}
struct DYN_GROUP *arrow_file_to_dg(const char *filename, const char *group_name) {
    (void)filename; (void)group_name; return NULL;
}

int dg_to_msgpack_buffer(struct DYN_GROUP *dg, char **buffer, size_t *buffer_size) {
    (void)dg; (void)buffer; (void)buffer_size; return -1;
}
int dg_to_hybrid_msgpack_buffer(struct DYN_GROUP *dg, char **buffer, size_t *buffer_size) {
    (void)dg; (void)buffer; (void)buffer_size; return -1;
}
int dg_write_msgpack_file(struct DYN_GROUP *dg, const char *filename) {
    (void)dg; (void)filename; return -1;
}
int dg_write_hybrid_msgpack_file(struct DYN_GROUP *dg, const char *filename) {
    (void)dg; (void)filename; return -1;
}
int dg_get_msgpack_data(struct DYN_GROUP *dg, char **data, size_t *size) {
    (void)dg; (void)data; (void)size; return -1;
}
int dg_get_hybrid_msgpack_data(struct DYN_GROUP *dg, char **data, size_t *size) {
    (void)dg; (void)data; (void)size; return -1;
}

size_t compress_buffer_to_lz4_file(unsigned char *data, int src_size, FILE *out) {
    (void)data; (void)src_size; (void)out; return 0;
}
int decompress_lz4_file_to_buffer(FILE *in, int *size, unsigned char **data) {
    (void)in; (void)size; (void)data; return -1;
}
