#include "state.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static char *get_home_dir(void) {
#ifdef _WIN32
    char *home = getenv("USERPROFILE");
    if (home && home[0]) return _strdup(home);
    char *drive = getenv("HOMEDRIVE");
    char *path = getenv("HOMEPATH");
    if (drive && path) {
        char buf[MAX_PATH];
        snprintf(buf, sizeof(buf), "%s%s", drive, path);
        return _strdup(buf);
    }
    return _strdup(".");
#else
    char *home = getenv("HOME");
    if (home && home[0]) return strdup(home);
    return strdup(".");
#endif
}

static char *path_join(const char *base, const char *part) {
    size_t blen = strlen(base);
    size_t plen = strlen(part);
    int need_sep = (blen > 0 && base[blen - 1] != '/' && base[blen - 1] != '\\');
    size_t total = blen + (need_sep ? 1 : 0) + plen + 1;
    char *result = (char *)malloc(total);
    if (need_sep) {
        snprintf(result, total, "%s/%s", base, part);
    } else {
        snprintf(result, total, "%s%s", base, part);
    }
    return result;
}

static char *state_file_path(const char *session_name) {
    char *dir = resolve_default_state_dir();
    char *path;
    if (session_name && session_name[0]) {
        char *sessions = path_join(dir, "sessions");
        char fname[512];
        snprintf(fname, sizeof(fname), "%s.json", session_name);
        path = path_join(sessions, fname);
        free(sessions);
    } else {
        path = path_join(dir, "cli-state.json");
    }
    free(dir);
    return path;
}

static void ensure_dir(const char *path) {
#ifdef _WIN32
    /* Recursively create directories on Windows */
    char *tmp = _strdup(path);
    for (char *p = tmp; *p; p++) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            CreateDirectoryA(tmp, NULL);
            *p = saved;
        }
    }
    CreateDirectoryA(tmp, NULL);
    free(tmp);
#else
    char *tmp = strdup(path);
    char *p = tmp;
    while (*p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
        p++;
    }
    mkdir(tmp, 0755);
    free(tmp);
#endif
}

/* ---------------------------------------------------------------------------
 * Directory resolution
 * ------------------------------------------------------------------------- */

char *resolve_default_state_dir(void) {
    /* Honour BROWSER4_CLI_STATE_DIR override */
    char *env_dir = getenv("BROWSER4_CLI_STATE_DIR");
    if (env_dir && env_dir[0]) {
        return _strdup(env_dir);
    }
    char *home = get_home_dir();
    char *result = path_join(home, ".browser4");
    free(home);
    return result;
}

/* ---------------------------------------------------------------------------
 * CliState management
 * ------------------------------------------------------------------------- */

CliState *cli_state_new(void) {
    CliState *s = (CliState *)calloc(1, sizeof(CliState));
    if (s) {
        s->base_url = _strdup("http://localhost:8182");
    }
    return s;
}

void cli_state_free(CliState *state) {
    if (!state) return;
    free(state->session_id);
    free(state->base_url);
    free(state->active_selector);
    free(state->session_name);
    if (state->last_mouse_position) {
        free(state->last_mouse_position);
    }
    free(state);
}

CliState *cli_state_clone(const CliState *state) {
    if (!state) return NULL;
    CliState *s = (CliState *)calloc(1, sizeof(CliState));
    if (!s) return NULL;
    s->session_id   = state->session_id   ? _strdup(state->session_id)   : NULL;
    s->base_url     = state->base_url     ? _strdup(state->base_url)     : _strdup("http://localhost:8182");
    s->active_selector = state->active_selector ? _strdup(state->active_selector) : NULL;
    s->session_name = state->session_name ? _strdup(state->session_name) : NULL;
    if (state->last_mouse_position) {
        s->last_mouse_position = (MousePosition *)malloc(sizeof(MousePosition));
        if (s->last_mouse_position) {
            s->last_mouse_position->x = state->last_mouse_position->x;
            s->last_mouse_position->y = state->last_mouse_position->y;
        }
    }
    return s;
}

/* ---------------------------------------------------------------------------
 * Read/write state
 * ------------------------------------------------------------------------- */

static CliState *state_from_json(JsonValue *json) {
    if (!json || json->type != JSON_OBJECT) return cli_state_new();

    CliState *s = (CliState *)calloc(1, sizeof(CliState));
    if (!s) return NULL;

    JsonValue *v;

    v = json_object_get(json, "sessionId");
    if (v && v->type == JSON_STRING) s->session_id = _strdup(v->data.string_val);

    v = json_object_get(json, "baseUrl");
    s->base_url = (v && v->type == JSON_STRING)
        ? _strdup(v->data.string_val)
        : _strdup("http://localhost:8182");

    v = json_object_get(json, "activeSelector");
    if (v && v->type == JSON_STRING) s->active_selector = _strdup(v->data.string_val);

    v = json_object_get(json, "sessionName");
    if (v && v->type == JSON_STRING) s->session_name = _strdup(v->data.string_val);

    v = json_object_get(json, "lastMousePosition");
    if (v && v->type == JSON_OBJECT) {
        s->last_mouse_position = (MousePosition *)malloc(sizeof(MousePosition));
        if (s->last_mouse_position) {
            s->last_mouse_position->x = json_obj_get_number(v, "x", 0.0);
            s->last_mouse_position->y = json_obj_get_number(v, "y", 0.0);
        }
    }

    return s;
}

static JsonValue *state_to_json(const CliState *state) {
    JsonValue *obj = json_object();

    if (state->session_id && state->session_id[0]) {
        json_object_set(obj, "sessionId", json_string(state->session_id));
    }
    if (state->base_url && state->base_url[0]) {
        json_object_set(obj, "baseUrl", json_string(state->base_url));
    }
    if (state->active_selector && state->active_selector[0]) {
        json_object_set(obj, "activeSelector", json_string(state->active_selector));
    }
    if (state->session_name && state->session_name[0]) {
        json_object_set(obj, "sessionName", json_string(state->session_name));
    }
    if (state->last_mouse_position) {
        JsonValue *pos = json_object();
        json_object_set(pos, "x", json_number(state->last_mouse_position->x));
        json_object_set(pos, "y", json_number(state->last_mouse_position->y));
        json_object_set(obj, "lastMousePosition", pos);
    }

    return obj;
}

CliState *read_state(const char *session_name) {
    char *path = state_file_path(session_name);

    FILE *f = fopen(path, "r");
    if (!f) {
        free(path);
        return cli_state_new();
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) {
        fclose(f);
        free(path);
        return cli_state_new();
    }

    char *buf = (char *)malloc(size + 1);
    if (!buf) {
        fclose(f);
        free(path);
        return cli_state_new();
    }

    size_t read = fread(buf, 1, size, f);
    fclose(f);
    buf[read] = '\0';

    char *parse_err = NULL;
    JsonValue *json = json_parse(buf, &parse_err);
    free(buf);

    if (!json) {
        free(parse_err);
        free(path);
        return cli_state_new();
    }

    CliState *state = state_from_json(json);
    json_free(json);
    free(path);
    return state;
}

int write_state(const CliState *state, const char *session_name) {
    char *path = state_file_path(session_name);

    /* Ensure parent directory exists */
    char *last_sep = strrchr(path, '/');
    if (!last_sep) last_sep = strrchr(path, '\\');
    if (last_sep) {
        char saved = *last_sep;
        *last_sep = '\0';
        ensure_dir(path);
        *last_sep = saved;
    }

    JsonValue *json = state_to_json(state);
    char *serialized = json_serialize_pretty(json);
    json_free(json);

    FILE *f = fopen(path, "w");
    if (!f) {
        free(serialized);
        free(path);
        return -1;
    }

    fprintf(f, "%s\n", serialized);
    fclose(f);
    free(serialized);
    free(path);
    return 0;
}

void clear_state(const char *session_name) {
    char *path = state_file_path(session_name);
    remove(path);
    free(path);
}

void clear_all_state(void) {
    clear_state(NULL);

    char *dir = resolve_default_state_dir();
    char *sessions = path_join(dir, "sessions");

#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof(pattern), "%s\\*.json", sessions);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            char file_path[MAX_PATH];
            snprintf(file_path, sizeof(file_path), "%s\\%s", sessions, fd.cFileName);
            remove(file_path);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    /* On POSIX we'd use opendir/readdir, but since we target Windows, skip */
#endif

    free(sessions);
    free(dir);
}

/* ---------------------------------------------------------------------------
 * Element ref resolution
 * ------------------------------------------------------------------------- */

char *resolve_ref(const char *raw_ref) {
    if (!raw_ref) return _strdup("");
    const char *trimmed = raw_ref;
    while (*trimmed && isspace((unsigned char)*trimmed)) trimmed++;

    /* Check for e<digits> pattern (case-insensitive) */
    if ((trimmed[0] == 'e' || trimmed[0] == 'E') && trimmed[1]) {
        const char *p = trimmed + 1;
        int all_digits = 1;
        while (*p && !isspace((unsigned char)*p)) {
            if (!isdigit((unsigned char)*p)) {
                all_digits = 0;
                break;
            }
            p++;
        }
        if (all_digits) {
            char result[256];
            snprintf(result, sizeof(result), "backend:%s", trimmed + 1);
            return _strdup(result);
        }
    }

    /* Pass-through: strip trailing whitespace */
    size_t len = strlen(trimmed);
    while (len > 0 && isspace((unsigned char)trimmed[len - 1])) len--;
    char *result = (char *)malloc(len + 1);
    memcpy(result, trimmed, len);
    result[len] = '\0';
    return result;
}
