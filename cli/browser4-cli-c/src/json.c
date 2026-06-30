#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>
#include <ctype.h>

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

#define JSON_VEC_INIT_CAP 4

static void json_pair_vec_push(JsonPairVec *vec, const char *key, JsonValue *value) {
    if (vec->count >= vec->capacity) {
        size_t new_cap = vec->capacity ? vec->capacity * 2 : JSON_VEC_INIT_CAP;
        JsonPair *new_items = (JsonPair *)realloc(vec->items, new_cap * sizeof(JsonPair));
        if (!new_items) return;
        vec->items = new_items;
        vec->capacity = new_cap;
    }
    vec->items[vec->count].key = key ? _strdup(key) : NULL;
    vec->items[vec->count].value = value;
    vec->count++;
}

static void json_value_vec_push(JsonValueVec *vec, JsonValue *value) {
    if (vec->count >= vec->capacity) {
        size_t new_cap = vec->capacity ? vec->capacity * 2 : JSON_VEC_INIT_CAP;
        JsonValue **new_items = (JsonValue **)realloc(vec->items, new_cap * sizeof(JsonValue *));
        if (!new_items) return;
        vec->items = new_items;
        vec->capacity = new_cap;
    }
    vec->items[vec->count] = value;
    vec->count++;
}

static int json_pair_vec_find(const JsonPairVec *vec, const char *key) {
    for (size_t i = 0; i < vec->count; i++) {
        if (vec->items[i].key && strcmp(vec->items[i].key, key) == 0) {
            return (int)i;
        }
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * Constructors
 * ------------------------------------------------------------------------- */

JsonValue *json_null(void) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (v) v->type = JSON_NULL;
    return v;
}

JsonValue *json_bool(int value) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (v) {
        v->type = JSON_BOOL;
        v->data.bool_val = value ? 1 : 0;
    }
    return v;
}

JsonValue *json_number(double value) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (v) {
        v->type = JSON_NUMBER;
        v->data.number_val = value;
    }
    return v;
}

JsonValue *json_string(const char *value) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (v) {
        v->type = JSON_STRING;
        v->data.string_val = value ? _strdup(value) : _strdup("");
    }
    return v;
}

JsonValue *json_string_take(char *value) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (v) {
        v->type = JSON_STRING;
        v->data.string_val = value ? value : _strdup("");
    }
    return v;
}

JsonValue *json_array(void) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (v) v->type = JSON_ARRAY;
    return v;
}

JsonValue *json_object(void) {
    JsonValue *v = (JsonValue *)calloc(1, sizeof(JsonValue));
    if (v) v->type = JSON_OBJECT;
    return v;
}

/* ---------------------------------------------------------------------------
 * Object manipulation
 * ------------------------------------------------------------------------- */

int json_object_set(JsonValue *obj, const char *key, JsonValue *value) {
    if (!obj || obj->type != JSON_OBJECT || !key) return -1;
    int idx = json_pair_vec_find(&obj->data.object_val, key);
    if (idx >= 0) {
        json_free(obj->data.object_val.items[idx].value);
        obj->data.object_val.items[idx].value = value;
    } else {
        json_pair_vec_push(&obj->data.object_val, key, value);
    }
    return 0;
}

JsonValue *json_object_get(const JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
    int idx = json_pair_vec_find(&obj->data.object_val, key);
    return idx >= 0 ? obj->data.object_val.items[idx].value : NULL;
}

int json_object_has(const JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return 0;
    return json_pair_vec_find(&obj->data.object_val, key) >= 0;
}

int json_object_remove(JsonValue *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return -1;
    JsonPairVec *vec = &obj->data.object_val;
    int idx = json_pair_vec_find(vec, key);
    if (idx < 0) return -1;
    free(vec->items[idx].key);
    json_free(vec->items[idx].value);
    if ((size_t)idx < vec->count - 1) {
        memmove(&vec->items[idx], &vec->items[idx + 1],
                (vec->count - idx - 1) * sizeof(JsonPair));
    }
    vec->count--;
    return 0;
}

size_t json_object_size(const JsonValue *obj) {
    if (!obj || obj->type != JSON_OBJECT) return 0;
    return obj->data.object_val.count;
}

/* ---------------------------------------------------------------------------
 * Array manipulation
 * ------------------------------------------------------------------------- */

int json_array_append(JsonValue *arr, JsonValue *value) {
    if (!arr || arr->type != JSON_ARRAY) return -1;
    json_value_vec_push(&arr->data.array_val, value);
    return 0;
}

JsonValue *json_array_get(const JsonValue *arr, size_t index) {
    if (!arr || arr->type != JSON_ARRAY || index >= arr->data.array_val.count) return NULL;
    return arr->data.array_val.items[index];
}

size_t json_array_size(const JsonValue *arr) {
    if (!arr || arr->type != JSON_ARRAY) return 0;
    return arr->data.array_val.count;
}

/* ---------------------------------------------------------------------------
 * Accessor helpers
 * ------------------------------------------------------------------------- */

const char *json_as_string(const JsonValue *val) {
    if (!val || val->type != JSON_STRING) return NULL;
    return val->data.string_val;
}

double json_as_number(const JsonValue *val) {
    if (!val || val->type != JSON_NUMBER) return 0.0;
    return val->data.number_val;
}

int json_as_bool(const JsonValue *val) {
    if (!val || val->type != JSON_BOOL) return 0;
    return val->data.bool_val;
}

int json_as_int(const JsonValue *val) {
    if (!val || val->type != JSON_NUMBER) return 0;
    return (int)val->data.number_val;
}

int json_is_null(const JsonValue *val) {
    return val && val->type == JSON_NULL;
}

JsonType json_type(const JsonValue *val) {
    return val ? val->type : JSON_NULL;
}

const char *json_obj_get_str(const JsonValue *obj, const char *key, const char *default_val) {
    JsonValue *v = json_object_get(obj, key);
    if (!v || v->type != JSON_STRING) return default_val;
    return v->data.string_val;
}

double json_obj_get_number(const JsonValue *obj, const char *key, double default_val) {
    JsonValue *v = json_object_get(obj, key);
    if (!v || v->type != JSON_NUMBER) return default_val;
    return v->data.number_val;
}

int json_obj_get_bool(const JsonValue *obj, const char *key, int default_val) {
    JsonValue *v = json_object_get(obj, key);
    if (!v || v->type != JSON_BOOL) return default_val;
    return v->data.bool_val;
}

int json_obj_get_int(const JsonValue *obj, const char *key, int default_val) {
    JsonValue *v = json_object_get(obj, key);
    if (!v || v->type != JSON_NUMBER) return default_val;
    return (int)v->data.number_val;
}

/* ---------------------------------------------------------------------------
 * Serialization
 * ------------------------------------------------------------------------- */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} StringBuf;

static void sb_init(StringBuf *sb) {
    sb->cap = 256;
    sb->buf = (char *)malloc(sb->cap);
    sb->buf[0] = '\0';
    sb->len = 0;
}

static void sb_append(StringBuf *sb, const char *s) {
    size_t slen = strlen(s);
    size_t needed = sb->len + slen + 1;
    if (needed > sb->cap) {
        while (sb->cap < needed) sb->cap *= 2;
        sb->buf = (char *)realloc(sb->buf, sb->cap);
    }
    memcpy(sb->buf + sb->len, s, slen);
    sb->len += slen;
    sb->buf[sb->len] = '\0';
}

static void sb_append_char(StringBuf *sb, char c) {
    char tmp[2] = {c, '\0'};
    sb_append(sb, tmp);
}

static void sb_append_indent(StringBuf *sb, int indent) {
    for (int i = 0; i < indent; i++) sb_append(sb, "  ");
}

static char *sb_take(StringBuf *sb) {
    return sb->buf;
}

static void json_serialize_impl(const JsonValue *val, StringBuf *sb, int pretty, int indent);

static void json_serialize_string_content(const char *str, StringBuf *sb) {
    sb_append_char(sb, '"');
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '"':  sb_append(sb, "\\\""); break;
            case '\\': sb_append(sb, "\\\\"); break;
            case '\b': sb_append(sb, "\\b");  break;
            case '\f': sb_append(sb, "\\f");  break;
            case '\n': sb_append(sb, "\\n");  break;
            case '\r': sb_append(sb, "\\r");  break;
            case '\t': sb_append(sb, "\\t");  break;
            default:
                if ((unsigned char)*p < 0x20) {
                    char hex[8];
                    sprintf(hex, "\\u%04x", (unsigned char)*p);
                    sb_append(sb, hex);
                } else {
                    sb_append_char(sb, *p);
                }
                break;
        }
    }
    sb_append_char(sb, '"');
}

static void json_serialize_impl(const JsonValue *val, StringBuf *sb, int pretty, int indent) {
    if (!val) {
        sb_append(sb, "null");
        return;
    }

    switch (val->type) {
        case JSON_NULL:
            sb_append(sb, "null");
            break;
        case JSON_BOOL:
            sb_append(sb, val->data.bool_val ? "true" : "false");
            break;
        case JSON_NUMBER: {
            char buf[64];
            double n = val->data.number_val;
            if (floor(n) == n && fabs(n) < 1e15) {
                sprintf(buf, "%.0f", n);
            } else {
                sprintf(buf, "%.17g", n);
            }
            sb_append(sb, buf);
            break;
        }
        case JSON_STRING:
            json_serialize_string_content(val->data.string_val, sb);
            break;
        case JSON_ARRAY: {
            sb_append_char(sb, '[');
            size_t count = val->data.array_val.count;
            if (count > 0) {
                if (pretty) { sb_append_char(sb, '\n'); }
                for (size_t i = 0; i < count; i++) {
                    if (pretty) sb_append_indent(sb, indent + 1);
                    json_serialize_impl(val->data.array_val.items[i], sb, pretty, indent + 1);
                    if (i < count - 1) sb_append_char(sb, ',');
                    if (pretty) sb_append_char(sb, '\n');
                }
                if (pretty) sb_append_indent(sb, indent);
            }
            sb_append_char(sb, ']');
            break;
        }
        case JSON_OBJECT: {
            sb_append_char(sb, '{');
            size_t count = val->data.object_val.count;
            if (count > 0) {
                if (pretty) { sb_append_char(sb, '\n'); }
                for (size_t i = 0; i < count; i++) {
                    if (pretty) sb_append_indent(sb, indent + 1);
                    json_serialize_string_content(val->data.object_val.items[i].key, sb);
                    sb_append(sb, pretty ? ": " : ":");
                    json_serialize_impl(val->data.object_val.items[i].value, sb, pretty, indent + 1);
                    if (i < count - 1) sb_append_char(sb, ',');
                    if (pretty) sb_append_char(sb, '\n');
                }
                if (pretty) sb_append_indent(sb, indent);
            }
            sb_append_char(sb, '}');
            break;
        }
    }
}

char *json_serialize(const JsonValue *val) {
    StringBuf sb;
    sb_init(&sb);
    json_serialize_impl(val, &sb, 0, 0);
    return sb_take(&sb);
}

char *json_serialize_pretty(const JsonValue *val) {
    StringBuf sb;
    sb_init(&sb);
    json_serialize_impl(val, &sb, 1, 0);
    return sb_take(&sb);
}

/* ---------------------------------------------------------------------------
 * JSON Parser (recursive descent)
 * ------------------------------------------------------------------------- */

typedef struct {
    const char *input;
    size_t      pos;
    size_t      len;
} JsonParser;

static void parser_skip_whitespace(JsonParser *p) {
    while (p->pos < p->len && isspace((unsigned char)p->input[p->pos])) {
        p->pos++;
    }
}

static char parser_peek(JsonParser *p) {
    parser_skip_whitespace(p);
    if (p->pos < p->len) return p->input[p->pos];
    return '\0';
}

static char parser_next(JsonParser *p) {
    parser_skip_whitespace(p);
    if (p->pos < p->len) return p->input[p->pos++];
    return '\0';
}

static JsonValue *parse_value(JsonParser *p, char **error_msg);

static JsonValue *parse_string(JsonParser *p, char **error_msg) {
    if (parser_next(p) != '"') {
        if (error_msg) *error_msg = _strdup("Expected '\"'");
        return NULL;
    }

    StringBuf sb;
    sb_init(&sb);

    while (p->pos < p->len) {
        char c = p->input[p->pos++];
        if (c == '"') {
            JsonValue *v = json_string_take(sb_take(&sb));
            return v;
        }
        if (c == '\\') {
            if (p->pos >= p->len) {
                free(sb.buf);
                if (error_msg) *error_msg = _strdup("Unexpected end of input in string escape");
                return NULL;
            }
            char e = p->input[p->pos++];
            switch (e) {
                case '"':  sb_append_char(&sb, '"');  break;
                case '\\': sb_append_char(&sb, '\\'); break;
                case '/':  sb_append_char(&sb, '/');  break;
                case 'b':  sb_append_char(&sb, '\b'); break;
                case 'f':  sb_append_char(&sb, '\f'); break;
                case 'n':  sb_append_char(&sb, '\n'); break;
                case 'r':  sb_append_char(&sb, '\r'); break;
                case 't':  sb_append_char(&sb, '\t'); break;
                case 'u': {
                    /* Parse \uXXXX */
                    if (p->pos + 4 > p->len) {
                        free(sb.buf);
                        if (error_msg) *error_msg = _strdup("Unexpected end of input in \\u escape");
                        return NULL;
                    }
                    char hex[5] = {0};
                    for (int j = 0; j < 4; j++) hex[j] = p->input[p->pos++];
                    unsigned int codepoint = (unsigned int)strtol(hex, NULL, 16);
                    if (codepoint < 0x80) {
                        sb_append_char(&sb, (char)codepoint);
                    } else if (codepoint < 0x800) {
                        sb_append_char(&sb, (char)(0xC0 | (codepoint >> 6)));
                        sb_append_char(&sb, (char)(0x80 | (codepoint & 0x3F)));
                    } else {
                        sb_append_char(&sb, (char)(0xE0 | (codepoint >> 12)));
                        sb_append_char(&sb, (char)(0x80 | ((codepoint >> 6) & 0x3F)));
                        sb_append_char(&sb, (char)(0x80 | (codepoint & 0x3F)));
                    }
                    break;
                }
                default:
                    sb_append_char(&sb, e);
                    break;
            }
        } else {
            sb_append_char(&sb, c);
        }
    }

    free(sb.buf);
    if (error_msg) *error_msg = _strdup("Unterminated string");
    return NULL;
}

static JsonValue *parse_number(JsonParser *p, char **error_msg) {
    size_t start = p->pos;
    /* Move back one because the caller already checked the first char */
    if (start > 0) start--;

    /* Scan forward to find the end of the number */
    while (p->pos < p->len) {
        char c = p->input[p->pos];
        if (isdigit((unsigned char)c) || c == '.' || c == 'e' || c == 'E' ||
            c == '+' || c == '-') {
            p->pos++;
        } else {
            break;
        }
    }

    size_t num_len = p->pos - start;
    char *num_str = (char *)malloc(num_len + 1);
    memcpy(num_str, p->input + start, num_len);
    num_str[num_len] = '\0';

    double val = strtod(num_str, NULL);
    free(num_str);

    return json_number(val);
}

static JsonValue *parse_array(JsonParser *p, char **error_msg) {
    parser_next(p); /* consume '[' */
    JsonValue *arr = json_array();

    if (parser_peek(p) == ']') {
        parser_next(p);
        return arr;
    }

    while (1) {
        JsonValue *val = parse_value(p, error_msg);
        if (!val) {
            json_free(arr);
            return NULL;
        }
        json_array_append(arr, val);

        char c = parser_next(p);
        if (c == ']') break;
        if (c != ',') {
            json_free(arr);
            if (error_msg) *error_msg = _strdup("Expected ',' or ']' in array");
            return NULL;
        }
    }
    return arr;
}

static JsonValue *parse_object(JsonParser *p, char **error_msg) {
    parser_next(p); /* consume '{' */
    JsonValue *obj = json_object();

    if (parser_peek(p) == '}') {
        parser_next(p);
        return obj;
    }

    while (1) {
        /* Parse key (must be a string) */
        parser_skip_whitespace(p);
        JsonValue *key_val = parse_string(p, error_msg);
        if (!key_val) {
            json_free(obj);
            return NULL;
        }
        char *key = key_val->data.string_val;
        key_val->data.string_val = NULL;
        json_free(key_val);

        if (parser_next(p) != ':') {
            free(key);
            json_free(obj);
            if (error_msg) *error_msg = _strdup("Expected ':' after object key");
            return NULL;
        }

        JsonValue *val = parse_value(p, error_msg);
        if (!val) {
            free(key);
            json_free(obj);
            return NULL;
        }

        json_object_set(obj, key, val);
        free(key);

        char c = parser_next(p);
        if (c == '}') break;
        if (c != ',') {
            json_free(obj);
            if (error_msg) *error_msg = _strdup("Expected ',' or '}' in object");
            return NULL;
        }
    }
    return obj;
}

static int parse_check_keyword(JsonParser *p, const char *keyword) {
    size_t kwlen = strlen(keyword);
    if (p->pos + kwlen > p->len) return 0;
    if (strncmp(p->input + p->pos, keyword, kwlen) == 0) {
        /* Check that the next char is not alphanumeric (to avoid matching "nullify") */
        if (p->pos + kwlen < p->len && isalnum((unsigned char)p->input[p->pos + kwlen])) {
            return 0;
        }
        p->pos += kwlen;
        return 1;
    }
    return 0;
}

static JsonValue *parse_value(JsonParser *p, char **error_msg) {
    parser_skip_whitespace(p);
    if (p->pos >= p->len) {
        if (error_msg) *error_msg = _strdup("Unexpected end of input");
        return NULL;
    }

    char c = p->input[p->pos];

    if (c == '"') {
        return parse_string(p, error_msg);
    }
    if (c == '{') {
        return parse_object(p, error_msg);
    }
    if (c == '[') {
        return parse_array(p, error_msg);
    }
    if (c == 't' || c == 'f') {
        if (parse_check_keyword(p, "true")) return json_bool(1);
        if (parse_check_keyword(p, "false")) return json_bool(0);
        if (error_msg) *error_msg = _strdup("Unknown keyword");
        return NULL;
    }
    if (c == 'n') {
        if (parse_check_keyword(p, "null")) return json_null();
        if (error_msg) *error_msg = _strdup("Unknown keyword");
        return NULL;
    }
    if (c == '-' || isdigit((unsigned char)c)) {
        return parse_number(p, error_msg);
    }

    if (error_msg) {
        char buf[64];
        sprintf(buf, "Unexpected character '%c'", c);
        *error_msg = _strdup(buf);
    }
    return NULL;
}

JsonValue *json_parse(const char *input, char **error_msg) {
    if (!input) {
        if (error_msg) *error_msg = _strdup("NULL input");
        return NULL;
    }
    JsonParser p;
    p.input = input;
    p.pos = 0;
    p.len = strlen(input);

    JsonValue *val = parse_value(&p, error_msg);

    /* Check for trailing garbage */
    if (val) {
        parser_skip_whitespace(&p);
        if (p.pos < p.len) {
            json_free(val);
            if (error_msg) *error_msg = _strdup("Trailing characters after JSON value");
            return NULL;
        }
    }
    return val;
}

/* ---------------------------------------------------------------------------
 * Memory management
 * ------------------------------------------------------------------------- */

void json_free(JsonValue *val) {
    if (!val) return;

    switch (val->type) {
        case JSON_STRING:
            free(val->data.string_val);
            break;
        case JSON_ARRAY:
            for (size_t i = 0; i < val->data.array_val.count; i++) {
                json_free(val->data.array_val.items[i]);
            }
            free(val->data.array_val.items);
            break;
        case JSON_OBJECT:
            for (size_t i = 0; i < val->data.object_val.count; i++) {
                free(val->data.object_val.items[i].key);
                json_free(val->data.object_val.items[i].value);
            }
            free(val->data.object_val.items);
            break;
        default:
            break;
    }
    free(val);
}

JsonValue *json_copy(const JsonValue *val) {
    if (!val) return NULL;

    switch (val->type) {
        case JSON_NULL:   return json_null();
        case JSON_BOOL:   return json_bool(val->data.bool_val);
        case JSON_NUMBER: return json_number(val->data.number_val);
        case JSON_STRING: return json_string(val->data.string_val);
        case JSON_ARRAY: {
            JsonValue *arr = json_array();
            for (size_t i = 0; i < val->data.array_val.count; i++) {
                json_array_append(arr, json_copy(val->data.array_val.items[i]));
            }
            return arr;
        }
        case JSON_OBJECT: {
            JsonValue *obj = json_object();
            for (size_t i = 0; i < val->data.object_val.count; i++) {
                json_object_set(obj, val->data.object_val.items[i].key,
                               json_copy(val->data.object_val.items[i].value));
            }
            return obj;
        }
    }
    return NULL;
}

/* ---------------------------------------------------------------------------
 * Utility
 * ------------------------------------------------------------------------- */

int json_object_merge(JsonValue *dst, JsonValue *src) {
    if (!dst || dst->type != JSON_OBJECT || !src || src->type != JSON_OBJECT) return -1;
    for (size_t i = 0; i < src->data.object_val.count; i++) {
        json_object_set(dst, src->data.object_val.items[i].key,
                       json_copy(src->data.object_val.items[i].value));
    }
    return 0;
}

int json_equals(const JsonValue *a, const JsonValue *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    if (a->type != b->type) return 0;

    switch (a->type) {
        case JSON_NULL: return 1;
        case JSON_BOOL: return a->data.bool_val == b->data.bool_val;
        case JSON_NUMBER: return fabs(a->data.number_val - b->data.number_val) < 1e-12;
        case JSON_STRING: return strcmp(a->data.string_val, b->data.string_val) == 0;
        case JSON_ARRAY:
            if (a->data.array_val.count != b->data.array_val.count) return 0;
            for (size_t i = 0; i < a->data.array_val.count; i++) {
                if (!json_equals(a->data.array_val.items[i], b->data.array_val.items[i])) return 0;
            }
            return 1;
        case JSON_OBJECT:
            if (a->data.object_val.count != b->data.object_val.count) return 0;
            for (size_t i = 0; i < a->data.object_val.count; i++) {
                JsonValue *bv = json_object_get(b, a->data.object_val.items[i].key);
                if (!bv) return 0;
                if (!json_equals(a->data.object_val.items[i].value, bv)) return 0;
            }
            return 1;
    }
    return 0;
}

JsonValue *json_string_fmt(const char *fmt, ...) {
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    return json_string(buf);
}
