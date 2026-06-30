#ifndef BROWSER4_CLI_JSON_H
#define BROWSER4_CLI_JSON_H

#include <stdint.h>
#include <stddef.h>

/* JSON value types */
typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} JsonType;

/* Forward declaration */
typedef struct JsonValue JsonValue;

/* A key-value pair for objects */
typedef struct {
    char     *key;   /* owned */
    JsonValue *value; /* owned */
} JsonPair;

/* Dynamic array of pairs (for objects) or values (for arrays) */
typedef struct {
    JsonPair *items;
    size_t    count;
    size_t    capacity;
} JsonPairVec;

typedef struct {
    JsonValue **items;
    size_t      count;
    size_t      capacity;
} JsonValueVec;

/* The JSON value — tagged union */
struct JsonValue {
    JsonType type;
    union {
        int            bool_val;
        double         number_val;
        char          *string_val;    /* owned */
        JsonValueVec   array_val;
        JsonPairVec    object_val;
    } data;
};

/* ---------------------------------------------------------------------------
 * Constructors
 * ------------------------------------------------------------------------- */
JsonValue *json_null(void);
JsonValue *json_bool(int value);
JsonValue *json_number(double value);
JsonValue *json_string(const char *value);       /* copies value */
JsonValue *json_string_take(char *value);        /* takes ownership */
JsonValue *json_array(void);
JsonValue *json_object(void);

/* ---------------------------------------------------------------------------
 * Object manipulation
 * ------------------------------------------------------------------------- */
/* Set a key-value pair. If key exists, old value is freed. Returns 0 on success. */
int json_object_set(JsonValue *obj, const char *key, JsonValue *value);

/* Get a value by key (borrowed reference). Returns NULL if not found or not an object. */
JsonValue *json_object_get(const JsonValue *obj, const char *key);

/* Check if key exists. Returns 0 if not found, non-zero if found. */
int json_object_has(const JsonValue *obj, const char *key);

/* Remove a key (frees the value). Returns 0 if key existed, -1 if not. */
int json_object_remove(JsonValue *obj, const char *key);

/* Number of entries in the object */
size_t json_object_size(const JsonValue *obj);

/* ---------------------------------------------------------------------------
 * Array manipulation
 * ------------------------------------------------------------------------- */
/* Append a value to an array. Returns 0 on success. */
int json_array_append(JsonValue *arr, JsonValue *value);

/* Get a value by index (borrowed reference). Returns NULL if out of bounds. */
JsonValue *json_array_get(const JsonValue *arr, size_t index);

/* Number of entries in the array */
size_t json_array_size(const JsonValue *arr);

/* ---------------------------------------------------------------------------
 * Accessor helpers (type-safe)
 * ------------------------------------------------------------------------- */
const char *json_as_string(const JsonValue *val);    /* NULL if not a string */
double      json_as_number(const JsonValue *val);    /* 0.0 if not a number */
int         json_as_bool(const JsonValue *val);      /* 0 if not a bool */
int         json_as_int(const JsonValue *val);       /* (int)number_val or 0 */
int         json_is_null(const JsonValue *val);
JsonType    json_type(const JsonValue *val);

/* Convenience: get a string value from an object, or default */
const char *json_obj_get_str(const JsonValue *obj, const char *key, const char *default_val);
double      json_obj_get_number(const JsonValue *obj, const char *key, double default_val);
int         json_obj_get_bool(const JsonValue *obj, const char *key, int default_val);
int         json_obj_get_int(const JsonValue *obj, const char *key, int default_val);

/* ---------------------------------------------------------------------------
 * Serialization
 * ------------------------------------------------------------------------- */
/* Serialize to compact JSON string (caller must free) */
char *json_serialize(const JsonValue *val);

/* Serialize to pretty-printed JSON string (caller must free) */
char *json_serialize_pretty(const JsonValue *val);

/* ---------------------------------------------------------------------------
 * Parsing
 * ------------------------------------------------------------------------- */
/* Parse a JSON string. Returns NULL on error (sets *error_msg if non-NULL). */
JsonValue *json_parse(const char *input, char **error_msg);

/* ---------------------------------------------------------------------------
 * Memory management
 * ------------------------------------------------------------------------- */
/* Deep-free a JSON value */
void json_free(JsonValue *val);

/* Deep-copy a JSON value */
JsonValue *json_copy(const JsonValue *val);

/* ---------------------------------------------------------------------------
 * Utility
 * ------------------------------------------------------------------------- */
/* Merge src object into dst object (shallow merge — values are owned by dst after) */
int json_object_merge(JsonValue *dst, JsonValue *src);

/* Equality comparison (deep) */
int json_equals(const JsonValue *a, const JsonValue *b);

/* Create a JSON string from a printf-style format */
JsonValue *json_string_fmt(const char *fmt, ...);

#endif /* BROWSER4_CLI_JSON_H */
