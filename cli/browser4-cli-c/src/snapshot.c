#include "snapshot.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <sys/time.h>
#endif

#define SNAPSHOT_DIR_COMPONENT1 ".browser4-cli"
#define SNAPSHOT_DIR_COMPONENT2 "snapshot"

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static void ensure_dir(const char *path) {
    char *tmp = _strdup(path);
    for (char *p = tmp; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
#ifdef _WIN32
            CreateDirectoryA(tmp, NULL);
#else
            mkdir(tmp, 0755);
#endif
            *p = saved;
        }
    }
#ifdef _WIN32
    CreateDirectoryA(tmp, NULL);
#else
    mkdir(tmp, 0755);
#endif
    free(tmp);
}

static char *get_cwd(void) {
    char buf[4096];
#ifdef _WIN32
    GetCurrentDirectoryA(sizeof(buf), buf);
#else
    if (!getcwd(buf, sizeof(buf))) buf[0] = '.';
#endif
    return _strdup(buf);
}

/* ---------------------------------------------------------------------------
 * Timestamped filename
 * ------------------------------------------------------------------------- */

char *timestamped_filename(const char *prefix, const char *ext) {
    time_t now = time(NULL);
    struct tm *tm_info;
#ifdef _WIN32
    struct tm tm_buf;
    gmtime_s(&tm_buf, &now);
    tm_info = &tm_buf;
#else
    tm_info = gmtime(&now);
#endif

    char ts[64];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H-%M-%SZ", tm_info);

    size_t len = strlen(prefix) + 1 + strlen(ts) + 1 + strlen(ext) + 1;
    char *result = (char *)malloc(len);
    snprintf(result, len, "%s-%s.%s", prefix, ts, ext);
    return result;
}

/* ---------------------------------------------------------------------------
 * Resolve output path
 * ------------------------------------------------------------------------- */

char *resolve_output_path(const char *filename, const char *prefix, const char *ext) {
    char *name = NULL;
    if (filename && filename[0]) {
        name = _strdup(filename);
    } else {
        name = timestamped_filename(prefix, ext);
    }

    char *cwd = get_cwd();
    size_t len = strlen(cwd) + 1 + strlen(SNAPSHOT_DIR_COMPONENT1) + 1 + strlen(SNAPSHOT_DIR_COMPONENT2) + 1 + strlen(name) + 1;
    char *result = (char *)malloc(len);
    snprintf(result, len, "%s/%s/%s/%s", cwd, SNAPSHOT_DIR_COMPONENT1, SNAPSHOT_DIR_COMPONENT2, name);
    free(cwd);
    free(name);
    return result;
}

/* ---------------------------------------------------------------------------
 * Save snapshot
 * ------------------------------------------------------------------------- */

int save_snapshot(const char *path, const char *content) {
    /* Ensure parent directory exists */
    char *path_copy = _strdup(path);
    char *last_sep = strrchr(path_copy, '/');
    if (!last_sep) last_sep = strrchr(path_copy, '\\');
    if (last_sep) {
        *last_sep = '\0';
        ensure_dir(path_copy);
    }
    free(path_copy);

    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "%s", content);
    fclose(f);

    /* Rotation is best-effort */
    return 0;
}

/* ---------------------------------------------------------------------------
 * Save binary
 * ------------------------------------------------------------------------- */

int save_binary(const char *path, const unsigned char *data, size_t len) {
    char *path_copy = _strdup(path);
    char *last_sep = strrchr(path_copy, '/');
    if (!last_sep) last_sep = strrchr(path_copy, '\\');
    if (last_sep) {
        *last_sep = '\0';
        ensure_dir(path_copy);
    }
    free(path_copy);

    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    return written == len ? 0 : -1;
}

/* ---------------------------------------------------------------------------
 * Base64 decode
 * ------------------------------------------------------------------------- */

static int b64_char_value(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

unsigned char *base64_decode(const char *input, size_t *out_len) {
    if (!input) { *out_len = 0; return NULL; }

    size_t in_len = strlen(input);
    /* Strip whitespace */
    const char *p = input;
    size_t clean_len = 0;
    for (size_t i = 0; i < in_len; i++) {
        if (input[i] != '\n' && input[i] != '\r' && input[i] != ' ' && input[i] != '\t') {
            clean_len++;
        }
    }

    if (clean_len % 4 != 0) { *out_len = 0; return NULL; }

    size_t max_out = (clean_len / 4) * 3;
    unsigned char *out = (unsigned char *)malloc(max_out + 1);
    if (!out) { *out_len = 0; return NULL; }

    size_t out_pos = 0;
    int buffer[4];
    int buf_pos = 0;

    for (size_t i = 0; i < in_len; i++) {
        char c = input[i];
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
        if (c == '=') {
            buffer[buf_pos++] = -1;
        } else {
            int val = b64_char_value(c);
            if (val < 0) { free(out); *out_len = 0; return NULL; }
            buffer[buf_pos++] = val;
        }

        if (buf_pos == 4) {
            if (buffer[0] >= 0) out[out_pos++] = (unsigned char)((buffer[0] << 2) | (buffer[1] >> 4));
            if (buffer[1] >= 0 && buffer[2] >= 0) out[out_pos++] = (unsigned char)((buffer[1] << 4) | (buffer[2] >> 2));
            if (buffer[2] >= 0 && buffer[3] >= 0) out[out_pos++] = (unsigned char)((buffer[2] << 6) | buffer[3]);
            buf_pos = 0;
        }
    }

    *out_len = out_pos;
    return out;
}
