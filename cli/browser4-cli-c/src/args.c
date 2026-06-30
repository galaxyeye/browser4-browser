#include "args.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

/* ---------------------------------------------------------------------------
 * Internal helpers
 * ------------------------------------------------------------------------- */

static char **argv_dup(char **src, int count) {
    char **dst = (char **)calloc(count, sizeof(char *));
    for (int i = 0; i < count; i++) dst[i] = _strdup(src[i]);
    return dst;
}

static int starts_with(const char *str, const char *prefix) {
    return strncmp(str, prefix, strlen(prefix)) == 0;
}

static char *strip_prefix(const char *str, const char *prefix) {
    return _strdup(str + strlen(prefix));
}

/* ---------------------------------------------------------------------------
 * Global flag parsing
 * ------------------------------------------------------------------------- */

CliGlobalFlags *parse_global_flags(int argc, char **argv) {
    CliGlobalFlags *flags = (CliGlobalFlags *)calloc(1, sizeof(CliGlobalFlags));

    /* Default session name from env */
    char *env_session = getenv("BROWSER4_CLI_SESSION");
    if (env_session && env_session[0]) {
        flags->session_name = _strdup(env_session);
    }

    /* Collect remaining args */
    char **remaining = (char **)malloc(argc * sizeof(char *));
    int rem_count = 0;
    int seen_command = 0;

    for (int i = 0; i < argc; i++) {
        const char *arg = argv[i];

        if (starts_with(arg, "-s=")) {
            char *val = strip_prefix(arg, "-s=");
            free(flags->session_name);
            flags->session_name = val;
        } else if (starts_with(arg, "--session=")) {
            char *val = strip_prefix(arg, "--session=");
            free(flags->session_name);
            flags->session_name = val;
        } else if ((strcmp(arg, "-s") == 0 || strcmp(arg, "--session") == 0) && i + 1 < argc) {
            i++;
            free(flags->session_name);
            flags->session_name = _strdup(argv[i]);
        } else if (!seen_command && strcmp(arg, "--json") == 0) {
            flags->json_mode = 1;
        } else if (!seen_command && (strcmp(arg, "-q") == 0 || strcmp(arg, "--quiet") == 0)) {
            flags->quiet_mode = 1;
        } else if (starts_with(arg, "--server=")) {
            char *val = strip_prefix(arg, "--server=");
            free(flags->server_url);
            flags->server_url = val;
        } else if (strcmp(arg, "--server") == 0 && i + 1 < argc) {
            i++;
            free(flags->server_url);
            flags->server_url = _strdup(argv[i]);
        } else if (!seen_command && starts_with(arg, "--proxy=")) {
            char *val = strip_prefix(arg, "--proxy=");
            free(flags->proxy_url);
            flags->proxy_url = val;
        } else if (!seen_command && strcmp(arg, "--proxy") == 0 && i + 1 < argc) {
            i++;
            free(flags->proxy_url);
            flags->proxy_url = _strdup(argv[i]);
        } else {
            if (arg[0] != '-') seen_command = 1;
            remaining[rem_count++] = _strdup(arg);
        }
    }

    flags->args = remaining;
    flags->args_count = rem_count;
    return flags;
}

void global_flags_free(CliGlobalFlags *flags) {
    if (!flags) return;
    free(flags->session_name);
    free(flags->server_url);
    free(flags->proxy_url);
    for (int i = 0; i < flags->args_count; i++) free(flags->args[i]);
    free(flags->args);
    free(flags);
}

/* ---------------------------------------------------------------------------
 * Short option map
 * ------------------------------------------------------------------------- */

JsonValue *build_short_option_map(ShortOptionEntry *entries, int count) {
    JsonValue *map = json_object();
    for (int i = 0; i < count; i++) {
        json_object_set(map, entries[i].short_name, json_string(entries[i].long_name));
    }
    return map;
}

/* ---------------------------------------------------------------------------
 * Raw argument parsing
 * ------------------------------------------------------------------------- */

static JsonValue *coerce_value(const char *val) {
    if (strcmp(val, "true") == 0) return json_bool(1);
    if (strcmp(val, "false") == 0) return json_bool(0);
    return json_string(val);
}

JsonValue *parse_raw_args(char **raw_args, int count, JsonValue *short_to_long) {
    JsonValue *result = json_object();
    JsonValue *positional = json_array();

    for (int i = 0; i < count; i++) {
        const char *arg = raw_args[i];

        if (starts_with(arg, "--")) {
            const char *rest = arg + 2;
            const char *eq = strchr(rest, '=');
            if (eq) {
                /* --key=value */
                size_t key_len = eq - rest;
                char *key = (char *)malloc(key_len + 1);
                memcpy(key, rest, key_len);
                key[key_len] = '\0';
                const char *val = eq + 1;
                json_object_set(result, key, coerce_value(val));
                free(key);
            } else {
                /* Look ahead for value */
                if (i + 1 < count && !starts_with(raw_args[i + 1], "--")) {
                    json_object_set(result, rest, coerce_value(raw_args[i + 1]));
                    i++; /* consume value */
                } else {
                    json_object_set(result, rest, json_bool(1));
                }
            }
        } else if (arg[0] == '-' && short_to_long && arg[1] != '\0') {
            const char *rest = arg + 1;
            const char *eq = strchr(rest, '=');
            if (eq) {
                /* -x=value */
                size_t key_len = eq - rest;
                char *short_key = (char *)malloc(key_len + 1);
                memcpy(short_key, rest, key_len);
                short_key[key_len] = '\0';
                JsonValue *long_name = json_object_get(short_to_long, short_key);
                if (long_name && long_name->type == JSON_STRING) {
                    json_object_set(result, long_name->data.string_val,
                                   coerce_value(eq + 1));
                } else {
                    json_array_append(positional, json_string(arg));
                }
                free(short_key);
            } else {
                JsonValue *long_name = json_object_get(short_to_long, rest);
                if (long_name && long_name->type == JSON_STRING) {
                    if (i + 1 < count && !starts_with(raw_args[i + 1], "-")) {
                        json_object_set(result, long_name->data.string_val,
                                       coerce_value(raw_args[i + 1]));
                        i++;
                    } else {
                        json_object_set(result, long_name->data.string_val, json_bool(1));
                    }
                } else {
                    json_array_append(positional, json_string(arg));
                }
            }
        } else {
            json_array_append(positional, json_string(arg));
        }
    }

    json_object_set(result, "_", positional);
    return result;
}

/* ---------------------------------------------------------------------------
 * Build command args
 * ------------------------------------------------------------------------- */

JsonValue *build_command_args(JsonValue *raw_args, const char **arg_names, int arg_count,
                              char **error_msg) {
    JsonValue *result = json_copy(raw_args);

    JsonValue *pos_val = json_object_get(raw_args, "_");
    int pos_count = 0;
    char **pos_strs = NULL;

    if (pos_val && pos_val->type == JSON_ARRAY) {
        pos_count = (int)json_array_size(pos_val) - 1; /* skip command name */
        if (pos_count > 0) {
            pos_strs = (char **)calloc(pos_count, sizeof(char *));
            for (int i = 0; i < pos_count; i++) {
                JsonValue *v = json_array_get(pos_val, i + 1);
                if (v && v->type == JSON_STRING) {
                    pos_strs[i] = _strdup(v->data.string_val);
                } else if (v) {
                    char *s = json_serialize(v);
                    pos_strs[i] = s;
                } else {
                    pos_strs[i] = _strdup("");
                }
            }
        }
    }

    if (pos_count > arg_count) {
        if (error_msg) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                     "error: too many arguments: expected %d, received %d",
                     arg_count, pos_count);
            *error_msg = _strdup(buf);
        }
        if (pos_strs) {
            for (int i = 0; i < pos_count; i++) free(pos_strs[i]);
            free(pos_strs);
        }
        json_free(result);
        return NULL;
    }

    for (int i = 0; i < arg_count && i < pos_count; i++) {
        char *endptr;
        long n = strtol(pos_strs[i], &endptr, 10);
        if (*endptr == '\0') {
            json_object_set(result, arg_names[i], json_number((double)n));
        } else {
            double d = strtod(pos_strs[i], &endptr);
            if (*endptr == '\0' && strchr(pos_strs[i], '.')) {
                json_object_set(result, arg_names[i], json_number(d));
            } else {
                json_object_set(result, arg_names[i], json_string(pos_strs[i]));
            }
        }
    }

    if (pos_strs) {
        for (int i = 0; i < pos_count; i++) free(pos_strs[i]);
        free(pos_strs);
    }
    return result;
}

/* ---------------------------------------------------------------------------
 * Batch argument parsing
 * ------------------------------------------------------------------------- */

BatchArgs *parse_batch_args(char **raw_args, int count, char **error_msg) {
    BatchArgs *args = (BatchArgs *)calloc(1, sizeof(BatchArgs));
    int parsing_options = 1;
    char **cmds = (char **)malloc(count * sizeof(char *));
    int cmd_count = 0;

    for (int i = 0; i < count; i++) {
        const char *arg = raw_args[i];
        if (parsing_options) {
            if (strcmp(arg, "--") == 0) { parsing_options = 0; continue; }
            if (strcmp(arg, "--bail") == 0) { args->bail = 1; continue; }
            if (strcmp(arg, "--json") == 0) { args->json_input = 1; continue; }
            parsing_options = 0;
        }
        cmds[cmd_count++] = _strdup(arg);
    }

    if (args->json_input && cmd_count > 0) {
        for (int i = 0; i < cmd_count; i++) free(cmds[i]);
        free(cmds);
        batch_args_free(args);
        if (error_msg) *error_msg = _strdup("Batch --json mode does not accept positional command arguments.");
        return NULL;
    }

    if (!args->json_input && cmd_count == 0) {
        free(cmds);
        batch_args_free(args);
        if (error_msg) *error_msg = _strdup("Batch requires at least one command argument or JSON input via --json.");
        return NULL;
    }

    args->commands = cmds;
    args->commands_count = cmd_count;
    return args;
}

void batch_args_free(BatchArgs *args) {
    if (!args) return;
    for (int i = 0; i < args->commands_count; i++) free(args->commands[i]);
    free(args->commands);
    free(args);
}

/* ---------------------------------------------------------------------------
 * Parse command string (handles quotes, escapes)
 * ------------------------------------------------------------------------- */

char **parse_command_string(const char *command, int *out_count, char **error_msg) {
    char **tokens = NULL;
    int tok_count = 0;
    int tok_cap = 0;
    char *current = NULL;
    size_t cur_len = 0;
    size_t cur_cap = 0;
    int in_single = 0, in_double = 0, escaped = 0;
    int token_started = 0;

    for (const char *p = command; *p; p++) {
        char ch = *p;

        if (escaped) {
            if (cur_len + 1 >= cur_cap) { cur_cap = cur_cap ? cur_cap * 2 : 64; current = (char *)realloc(current, cur_cap); }
            current[cur_len++] = ch;
            escaped = 0;
            token_started = 1;
            continue;
        }

        if (ch == '\\' && !in_single) { escaped = 1; continue; }
        if (ch == '\'' && !in_double) { in_single = !in_single; token_started = 1; continue; }
        if (ch == '"' && !in_single) { in_double = !in_double; token_started = 1; continue; }

        if (isspace((unsigned char)ch) && !in_single && !in_double) {
            if (token_started) {
                if (cur_len >= cur_cap) { cur_cap = cur_cap ? cur_cap * 2 : 64; current = (char *)realloc(current, cur_cap); }
                current[cur_len] = '\0';
                if (tok_count >= tok_cap) { tok_cap = tok_cap ? tok_cap * 2 : 8; tokens = (char **)realloc(tokens, tok_cap * sizeof(char *)); }
                tokens[tok_count++] = current;
                current = NULL;
                cur_len = 0;
                cur_cap = 0;
                token_started = 0;
            }
            continue;
        }

        if (cur_len + 1 >= cur_cap) { cur_cap = cur_cap ? cur_cap * 2 : 64; current = (char *)realloc(current, cur_cap); }
        current[cur_len++] = ch;
        token_started = 1;
    }

    if (escaped) {
        free(current);
        for (int i = 0; i < tok_count; i++) free(tokens[i]);
        free(tokens);
        if (error_msg) *error_msg = _strdup("Command ends with an unfinished escape sequence.");
        *out_count = 0;
        return NULL;
    }
    if (in_single || in_double) {
        free(current);
        for (int i = 0; i < tok_count; i++) free(tokens[i]);
        free(tokens);
        if (error_msg) *error_msg = _strdup("Command has an unclosed quote.");
        *out_count = 0;
        return NULL;
    }
    if (token_started) {
        if (cur_len >= cur_cap) { cur_cap = cur_cap ? cur_cap * 2 : 64; current = (char *)realloc(current, cur_cap); }
        current[cur_len] = '\0';
        if (tok_count >= tok_cap) { tok_cap = tok_cap ? tok_cap * 2 : 8; tokens = (char **)realloc(tokens, tok_cap * sizeof(char *)); }
        tokens[tok_count++] = current;
    } else {
        free(current);
    }

    if (tok_count == 0) {
        free(tokens);
        if (error_msg) *error_msg = _strdup("Batch command entries cannot be empty.");
        *out_count = 0;
        return NULL;
    }

    *out_count = tok_count;
    return tokens;
}

/* ---------------------------------------------------------------------------
 * Parse batch JSON commands
 * ------------------------------------------------------------------------- */

char ***parse_batch_json_commands(const char *input, int *out_count, char **error_msg) {
    char *parse_err = NULL;
    JsonValue *val = json_parse(input, &parse_err);
    if (!val) {
        if (error_msg) {
            char buf[512];
            snprintf(buf, sizeof(buf), "Invalid batch JSON input: %s",
                     parse_err ? parse_err : "unknown error");
            *error_msg = _strdup(buf);
        }
        free(parse_err);
        *out_count = 0;
        return NULL;
    }

    if (val->type != JSON_ARRAY) {
        json_free(val);
        if (error_msg) *error_msg = _strdup("Batch JSON input must be an array.");
        *out_count = 0;
        return NULL;
    }

    size_t count = json_array_size(val);
    char ***commands = (char ***)calloc(count, sizeof(char **));
    int *cmd_counts = (int *)calloc(count, sizeof(int));
    int cmd_count = 0;

    for (size_t i = 0; i < count; i++) {
        JsonValue *entry = json_array_get(val, i);
        char **tokens = NULL;
        int tok_count = 0;

        if (entry->type == JSON_STRING) {
            tokens = parse_command_string(entry->data.string_val, &tok_count, error_msg);
            if (!tokens) {
                /* error_msg already set */
                for (int j = 0; j < cmd_count; j++) {
                    for (int k = 0; k < cmd_counts[j]; k++) free(commands[j][k]);
                    free(commands[j]);
                }
                free(commands);
                free(cmd_counts);
                json_free(val);
                *out_count = 0;
                return NULL;
            }
        } else if (entry->type == JSON_ARRAY) {
            size_t arr_count = json_array_size(entry);
            if (arr_count == 0) {
                for (int j = 0; j < cmd_count; j++) {
                    for (int k = 0; k < cmd_counts[j]; k++) free(commands[j][k]);
                    free(commands[j]);
                }
                free(commands);
                free(cmd_counts);
                json_free(val);
                char buf[128];
                snprintf(buf, sizeof(buf), "Batch JSON command at index %zu must not be empty.", i);
                if (error_msg) *error_msg = _strdup(buf);
                *out_count = 0;
                return NULL;
            }
            tokens = (char **)calloc(arr_count, sizeof(char *));
            for (size_t j = 0; j < arr_count; j++) {
                JsonValue *part = json_array_get(entry, j);
                if (!part || part->type != JSON_STRING) {
                    for (size_t k = 0; k < j; k++) free(tokens[k]);
                    free(tokens);
                    for (int jj = 0; jj < cmd_count; jj++) {
                        for (int kk = 0; kk < cmd_counts[jj]; kk++) free(commands[jj][kk]);
                        free(commands[jj]);
                    }
                    free(commands);
                    free(cmd_counts);
                    json_free(val);
                    char buf[128];
                    snprintf(buf, sizeof(buf), "Batch JSON command at index %zu must contain only string arguments.", i);
                    if (error_msg) *error_msg = _strdup(buf);
                    *out_count = 0;
                    return NULL;
                }
                tokens[j] = _strdup(part->data.string_val);
            }
            tok_count = (int)arr_count;
        } else {
            for (int j = 0; j < cmd_count; j++) {
                for (int k = 0; k < cmd_counts[j]; k++) free(commands[j][k]);
                free(commands[j]);
            }
            free(commands);
            free(cmd_counts);
            json_free(val);
            char buf[128];
            snprintf(buf, sizeof(buf), "Batch JSON command at index %zu must be a string or string array.", i);
            if (error_msg) *error_msg = _strdup(buf);
            *out_count = 0;
            return NULL;
        }

        commands[cmd_count] = tokens;
        cmd_counts[cmd_count] = tok_count;
        cmd_count++;
    }

    json_free(val);
    free(cmd_counts);

    if (cmd_count == 0) {
        free(commands);
        if (error_msg) *error_msg = _strdup("Batch JSON input must contain at least one command.");
        *out_count = 0;
        return NULL;
    }

    *out_count = cmd_count;
    return commands;
}
