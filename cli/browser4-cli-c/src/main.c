#include "main.h"
#include "error.h"
#include "json.h"
#include "state.h"
#include "http.h"
#include "args.h"
#include "commands.h"
#include "help.h"
#include "snapshot.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* ---------------------------------------------------------------------------
 * Global output modes (thread-local equivalent for single-threaded CLI)
 * ------------------------------------------------------------------------- */

static int g_json_mode = 0;
static int g_quiet_mode = 0;
static JsonValue *g_json_output = NULL; /* Accumulator for --json */

static void json_output_init(void) {
    if (g_json_output) json_free(g_json_output);
    g_json_output = json_object();
}

static void json_output_field(const char *key, JsonValue *value) {
    if (!g_json_output) return;
    json_object_set(g_json_output, key, value);
}

static void json_output_field_str(const char *key, const char *value) {
    json_output_field(key, json_string(value));
}

static void json_output_field_bool(const char *key, int value) {
    json_output_field(key, json_bool(value));
}

static void json_output_field_null(const char *key) {
    json_output_field(key, json_null());
}

static void json_output_finish(void) {
    if (!g_json_output) return;
    char *s = json_serialize(g_json_output);
    printf("%s\n", s);
    free(s);
    json_free(g_json_output);
    g_json_output = NULL;
}

static int should_print(void) {
    return !g_quiet_mode && !g_json_mode;
}

/* Print line unless -q/--quiet or --json */
#define println(fmt, ...) do { \
    if (should_print()) printf(fmt "\n", ##__VA_ARGS__); \
} while(0)

/* ---------------------------------------------------------------------------
 * Session helpers
 * ------------------------------------------------------------------------- */

static char *get_session_id(CliState *state) {
    if (!state->session_id || !state->session_id[0]) {
        fprintf(stderr, "Session required\n");
        fprintf(stderr, "  No active session is currently stored for this CLI context.\n");
        fprintf(stderr, "  - run `browser4-cli open <url>` first.\n");
        return NULL;
    }
    return state->session_id;
}

static char *no_active_session_message(void) {
    return _strdup("Session required\n  No active session is currently stored for this CLI context.\n  - run `browser4-cli open <url>` first.");
}

static char *saved_session_expired_message(void) {
    return _strdup("Session refresh needed\n  The saved session expired or is no longer usable.\n  - run `browser4-cli open <url>` to create a fresh session, then retry.");
}

/* ---------------------------------------------------------------------------
 * Storage expression builders (JavaScript evaluation helpers)
 * ------------------------------------------------------------------------- */

static char *build_storage_list_expression(const char *storage_area) {
    size_t len = 512 + strlen(storage_area);
    char *expr = (char *)malloc(len);
    snprintf(expr, len,
        "(() => { "
        "  const storage = window.%s; "
        "  const entries = []; "
        "  for (let index = 0; index < storage.length; index += 1) { "
        "    const name = storage.key(index); "
        "    if (name == null) continue; "
        "    entries.push({ name, value: storage.getItem(name) ?? '' }); "
        "  } "
        "  return JSON.stringify(entries); "
        "})()", storage_area);
    return expr;
}

static char *build_storage_get_expression(const char *storage_area, const char *key) {
    size_t len = 512 + strlen(storage_area) + strlen(key) * 2;
    char *expr = (char *)malloc(len);
    snprintf(expr, len,
        "(() => { "
        "  const storage = window.%s; "
        "  const key = \"%s\"; "
        "  const value = storage.getItem(key); "
        "  return JSON.stringify({ found: value !== null, value: value ?? '' }); "
        "})()", storage_area, key);
    return expr;
}

static char *build_storage_set_expression(const char *storage_area, const char *key, const char *value) {
    size_t len = 512 + strlen(storage_area) + strlen(key) * 2 + strlen(value) * 2;
    char *expr = (char *)malloc(len);
    snprintf(expr, len,
        "(() => { "
        "  const storage = window.%s; "
        "  const key = \"%s\"; "
        "  const value = \"%s\"; "
        "  storage.setItem(key, value); "
        "  return JSON.stringify({ found: true, value: storage.getItem(key) ?? '' }); "
        "})()", storage_area, key, value);
    return expr;
}

static char *build_storage_delete_expression(const char *storage_area, const char *key) {
    size_t len = 512 + strlen(storage_area) + strlen(key) * 2;
    char *expr = (char *)malloc(len);
    snprintf(expr, len,
        "(() => { "
        "  const storage = window.%s; "
        "  const key = \"%s\"; "
        "  const existed = storage.getItem(key) !== null; "
        "  storage.removeItem(key); "
        "  return JSON.stringify({ existed }); "
        "})()", storage_area, key);
    return expr;
}

static char *build_storage_clear_expression(const char *storage_area) {
    size_t len = 256 + strlen(storage_area);
    char *expr = (char *)malloc(len);
    snprintf(expr, len,
        "(() => { "
        "  const storage = window.%s; "
        "  const cleared = storage.length; "
        "  storage.clear(); "
        "  return JSON.stringify({ cleared }); "
        "})()", storage_area);
    return expr;
}

/* ---------------------------------------------------------------------------
 * Command handler context
 * ------------------------------------------------------------------------- */

typedef struct {
    HttpClient   *client;
    const char   *base_url;
    const char   *session_name;
    const char   *command_name;
    JsonValue    *tool_params;
    const CommandDef *cmd_def;
} CmdCtx;

/* Forward declarations */
static int handle_open(CmdCtx *ctx);
static int handle_goto(CmdCtx *ctx);
static int handle_close(CmdCtx *ctx);
static int handle_close_all(CmdCtx *ctx);
static int handle_kill_all(CmdCtx *ctx);
static int handle_list(CmdCtx *ctx);
static int handle_install(CmdCtx *ctx);
static int handle_uninstall(CmdCtx *ctx);
static int handle_upgrade(CmdCtx *ctx);
static int handle_stop(CmdCtx *ctx);
static int handle_status(CmdCtx *ctx);
static int handle_snapshot_cmd(CmdCtx *ctx);
static int handle_screenshot_cmd(CmdCtx *ctx);
static int handle_tool_command(CmdCtx *ctx);
static int handle_agent_run(CmdCtx *ctx);
static int handle_agent_status(CmdCtx *ctx);
static int handle_agent_result(CmdCtx *ctx);
static int handle_swarm_create(CmdCtx *ctx);
static int handle_swarm_submit(CmdCtx *ctx);
static int handle_swarm_query(CmdCtx *ctx);
static int handle_swarm_status(CmdCtx *ctx);
static int handle_swarm_result(CmdCtx *ctx);
static int handle_cookie_list(CmdCtx *ctx);
static int handle_cookie_get(CmdCtx *ctx);
static int handle_cookie_set(CmdCtx *ctx);
static int handle_cookie_delete(CmdCtx *ctx);
static int handle_cookie_clear(CmdCtx *ctx);
static int handle_storage_list(CmdCtx *ctx, const char *storage_area);
static int handle_storage_get(CmdCtx *ctx, const char *storage_area);
static int handle_storage_set(CmdCtx *ctx, const char *storage_area);
static int handle_storage_delete(CmdCtx *ctx, const char *storage_area);
static int handle_storage_clear(CmdCtx *ctx, const char *storage_area);
static int handle_state_save(CmdCtx *ctx);
static int handle_state_load(CmdCtx *ctx);
static int handle_delete_data(CmdCtx *ctx);
static int handle_mouse_move(CmdCtx *ctx);
static int handle_batch(CmdCtx *ctx, int argc, char **argv);
static int handle_help(CmdCtx *ctx, int argc, char **argv);

/* ---------------------------------------------------------------------------
 * with_session: execute a callback with the current session, recovering stale sessions
 * ------------------------------------------------------------------------- */

typedef char *(*SessionAction)(HttpClient *client, const char *base_url,
                                const char *session_id, JsonValue *params,
                                char **error_msg);

static int with_session(HttpClient *client, const char *base_url,
                        const char *session_name, int recover_stale,
                        SessionAction action, JsonValue *params,
                        char **out_result, char **error_msg) {
    CliState *state = read_state(session_name);
    char *session_id = get_session_id(state);
    if (!session_id) {
        cli_state_free(state);
        if (error_msg) *error_msg = no_active_session_message();
        return EXIT_SESSION;
    }

    char *result = action(client, base_url, session_id, params, error_msg);
    if (result) {
        *out_result = result;
        cli_state_free(state);
        return EXIT_SUCCESS_CLI;
    }

    if (!is_stale_session_error(*error_msg)) {
        cli_state_free(state);
        return EXIT_GENERAL;
    }

    /* Session is stale — invalidate and optionally recover */
    free(state->session_id);
    state->session_id = NULL;
    state->active_selector = NULL;
    if (state->last_mouse_position) { free(state->last_mouse_position); state->last_mouse_position = NULL; }
    write_state(state, session_name);

    if (!recover_stale) {
        cli_state_free(state);
        if (error_msg) { free(*error_msg); *error_msg = saved_session_expired_message(); }
        return EXIT_SESSION;
    }

    /* Create a new session */
    JsonValue *caps = json_object();
    JsonValue *open_args = json_object();
    json_object_set(open_args, "capabilities", caps);

    char *new_session_id = NULL;
    char *create_err = NULL;
    char *create_result = action(client, base_url, "open_session", open_args, &create_err);

    if (create_result) {
        /* Parse session ID from result */
        char *parse_err = NULL;
        JsonValue *parsed = json_parse(create_result, &parse_err);
        if (parsed) {
            JsonValue *sid = json_object_get(parsed, "sessionId");
            if (sid && sid->type == JSON_STRING) new_session_id = _strdup(sid->data.string_val);
            else new_session_id = _strdup(create_result);
            json_free(parsed);
        } else {
            new_session_id = _strdup(create_result);
            free(parse_err);
        }
        free(create_result);
    }
    free(create_err);

    if (!new_session_id) {
        cli_state_free(state);
        if (error_msg) *error_msg = _strdup("Failed to create replacement session");
        return EXIT_SERVER;
    }

    state->session_id = new_session_id;
    write_state(state, session_name);
    cli_state_free(state);

    /* Retry the original action */
    free(*error_msg);
    *error_msg = NULL;
    result = action(client, base_url, new_session_id, params, error_msg);
    if (result) {
        *out_result = result;
        return EXIT_SUCCESS_CLI;
    }
    return EXIT_GENERAL;
}

/* ---------------------------------------------------------------------------
 * Standard session tool call action
 * ------------------------------------------------------------------------- */

static char *session_call_tool_action(HttpClient *client, const char *base_url,
                                       const char *session_id, JsonValue *params,
                                       char **error_msg) {
    JsonValue *args = json_copy(params);
    json_object_set(args, "sessionId", json_string(session_id));

    /* Resolve tool name from the _tool_name field */
    const char *tool_name = json_obj_get_str(params, "_tool_name", NULL);
    if (!tool_name || !tool_name[0]) {
        json_free(args);
        if (error_msg) *error_msg = _strdup("No tool name specified");
        return NULL;
    }

    /* Remove internal fields before sending */
    json_object_remove(args, "_tool_name");
    json_object_remove(args, "_command_name");

    char *result = call_tool(client, base_url, tool_name, args, error_msg);
    json_free(args);
    return result;
}

/* ---------------------------------------------------------------------------
 * Post-command snapshot
 * ------------------------------------------------------------------------- */

static void post_command_snapshot(HttpClient *client, const char *base_url,
                                   const char *session_name) {
    /* Get page URL */
    JsonValue *url_args = json_object();
    json_object_set(url_args, "sessionId", json_string(""));
    char *url_err = NULL;
    char *url_result = with_session(client, base_url, session_name, 0,
                                     session_call_tool_action, url_args, NULL, &url_err)
                        == EXIT_SUCCESS_CLI ? NULL : NULL;
    /* Simplified: just make direct calls */
    free(url_err);

    /* Use direct call_tool with session state */
    CliState *state = read_state(session_name);
    if (!state->session_id) { cli_state_free(state); return; }

    char *err = NULL;
    JsonValue *args = json_object();
    json_object_set(args, "sessionId", json_string(state->session_id));
    normalize_refs(args);

    char *page_url = call_tool(client, base_url, "page_url", args, &err);
    free(err); err = NULL;

    JsonValue *title_args = json_object();
    json_object_set(title_args, "sessionId", json_string(state->session_id));
    char *page_title = call_tool(client, base_url, "page_title", title_args, &err);
    free(err); err = NULL;

    JsonValue *snap_args = json_object();
    json_object_set(snap_args, "sessionId", json_string(state->session_id));
    char *snap_content = call_tool(client, base_url, "browser_snapshot", snap_args, &err);
    free(err);

    if (page_url && page_title && snap_content) {
        char *out_path = resolve_output_path(NULL, "snapshot", "yml");
        if (save_snapshot(out_path, snap_content) == 0) {
            json_output_field_str("page_url", page_url);
            json_output_field_str("page_title", page_title);
            json_output_field_str("snapshot_path", out_path);

            println("### Page");
            println("- Page URL: %s", page_url);
            println("- Page Title: %s", page_title);
            println("### Snapshot");
            println("[Snapshot](%s)", out_path);
        }
        free(out_path);
    }

    free(page_url);
    free(page_title);
    free(snap_content);
    cli_state_free(state);
}

/* ---------------------------------------------------------------------------
 * Individual command handlers
 * ------------------------------------------------------------------------- */

static int handle_open(CmdCtx *ctx) {
    CliState *state = read_state(ctx->session_name);

    /* Check if we can reuse existing session */
    int reused = 0;
    char *session_id = NULL;

    if (state->session_id && state->session_id[0]) {
        char *err = NULL;
        char *list_result = call_tool(ctx->client, ctx->base_url, "list_sessions",
                                       json_object(), &err);
        if (list_result) {
            /* Check if our session is active */
            char *parse_err = NULL;
            JsonValue *parsed = json_parse(list_result, &parse_err);
            int found = 0;
            if (parsed && parsed->type == JSON_ARRAY) {
                for (size_t i = 0; i < json_array_size(parsed); i++) {
                    JsonValue *item = json_array_get(parsed, i);
                    const char *sid = NULL;
                    if (item->type == JSON_STRING) sid = item->data.string_val;
                    else sid = json_obj_get_str(item, "sessionId", NULL);
                    if (sid && strcmp(sid, state->session_id) == 0) { found = 1; break; }
                }
            }
            json_free(parsed);
            free(parse_err);
            free(list_result);

            if (found) {
                session_id = _strdup(state->session_id);
                reused = 1;
            }
        }
        free(err);
    }

    if (!session_id) {
        /* Create new session */
        JsonValue *caps = json_object();
        if (get_bool(ctx->tool_params, "headless")) json_object_set(caps, "headed", json_bool(0));
        else if (get_bool(ctx->tool_params, "headed")) json_object_set(caps, "headed", json_bool(1));
        const char *pf = get_str(ctx->tool_params, "profile");
        if (pf) json_object_set(caps, "profilePath", json_string(pf));
        const char *pm = get_str(ctx->tool_params, "profile-mode");
        if (pm) json_object_set(caps, "profileMode", json_string(pm));
        const char *il = get_str(ctx->tool_params, "interact-level");
        if (il) json_object_set(caps, "interactLevel", json_string(il));

        JsonValue *open_args = json_object();
        json_object_set(open_args, "capabilities", caps);
        json_object_set(open_args, "sessionId", json_string(ctx->session_name ? ctx->session_name : "default"));

        char *err = NULL;
        char *result = call_tool(ctx->client, ctx->base_url, "open_session", open_args, &err);
        if (result) {
            char *parse_err = NULL;
            JsonValue *parsed = json_parse(result, &parse_err);
            if (parsed) {
                JsonValue *sid = json_object_get(parsed, "sessionId");
                session_id = _strdup(sid && sid->type == JSON_STRING ? sid->data.string_val : result);
                json_free(parsed);
            } else {
                session_id = _strdup(result);
                free(parse_err);
            }
            free(result);
        } else {
            fprintf(stderr, "Failed to open session: %s\n", err ? err : "unknown error");
            free(err);
            cli_state_free(state);
            return EXIT_SERVER;
        }
        free(err);

        state->session_id = _strdup(session_id);
        state->active_selector = NULL;
        if (state->last_mouse_position) { free(state->last_mouse_position); state->last_mouse_position = NULL; }
        write_state(state, ctx->session_name);
        println("Session opened: %s", session_id);
    }

    json_output_field_str("session_id", session_id);
    json_output_field_bool("reused", reused);

    /* Navigate if URL provided */
    const char *url = get_str(ctx->tool_params, "url");
    if (url && url[0] && strcmp(url, "about:blank") != 0) {
        if (reused) println("Session already open: %s", session_id);

        JsonValue *nav_args = json_object();
        json_object_set(nav_args, "sessionId", json_string(session_id));
        json_object_set(nav_args, "url", json_string(url));
        char *err = NULL;
        char *nav_result = call_tool(ctx->client, ctx->base_url, "browser_navigate", nav_args, &err);
        if (nav_result) {
            if (nav_result[0]) println("%s", nav_result);
            free(nav_result);
            post_command_snapshot(ctx->client, ctx->base_url, ctx->session_name);
        } else {
            fprintf(stderr, "Navigation failed: %s\n", err ? err : "unknown error");
            free(err);
            /* Try retry with new session */
            state->session_id = NULL;
            write_state(state, ctx->session_name);

            /* Create fresh session for retry */
            JsonValue *caps2 = json_object();
            JsonValue *open_args2 = json_object();
            json_object_set(open_args2, "capabilities", caps2);
            json_object_set(open_args2, "sessionId", json_string(ctx->session_name ? ctx->session_name : "default"));
            char *err2 = NULL;
            char *result2 = call_tool(ctx->client, ctx->base_url, "open_session", open_args2, &err2);
            if (result2) {
                char *new_sid = NULL;
                char *parse_err2 = NULL;
                JsonValue *parsed2 = json_parse(result2, &parse_err2);
                free(parse_err2);
                if (parsed2) {
                    JsonValue *sid = json_object_get(parsed2, "sessionId");
                    new_sid = _strdup(sid && sid->type == JSON_STRING ? sid->data.string_val : result2);
                    json_free(parsed2);
                } else { new_sid = _strdup(result2); }
                free(result2);

                free(state->session_id);
                state->session_id = new_sid;
                write_state(state, ctx->session_name);
                println("Session opened: %s", new_sid);

                JsonValue *retry_args = json_object();
                json_object_set(retry_args, "sessionId", json_string(new_sid));
                json_object_set(retry_args, "url", json_string(url));
                char *retry_result = call_tool(ctx->client, ctx->base_url, "browser_navigate", retry_args, &err2);
                if (retry_result) {
                    if (retry_result[0]) println("%s", retry_result);
                    free(retry_result);
                    post_command_snapshot(ctx->client, ctx->base_url, ctx->session_name);
                }
            }
            free(err2);
        }
    }

    free(session_id);
    cli_state_free(state);
    return EXIT_SUCCESS_CLI;
}

static int handle_goto(CmdCtx *ctx) {
    /* goto is essentially open with forced navigation */
    return handle_open(ctx);
}

static int handle_close(CmdCtx *ctx) {
    CliState *state = read_state(ctx->session_name);
    if (state->session_id && state->session_id[0]) {
        JsonValue *args = json_object();
        json_object_set(args, "sessionId", json_string(state->session_id));
        char *err = NULL;
        char *result = call_tool(ctx->client, ctx->base_url, "close_session", args, &err);
        free(result); free(err);
        json_output_field_str("session_id", state->session_id);
        json_output_field_bool("closed", 1);
    } else {
        json_output_field_null("session_id");
        json_output_field_bool("closed", 0);
    }
    clear_state(ctx->session_name);
    println("Session closed.");
    cli_state_free(state);
    return EXIT_SUCCESS_CLI;
}

static int handle_close_all(CmdCtx *ctx) {
    char *err = NULL;
    char *result = call_tool(ctx->client, ctx->base_url, "close_all_sessions", json_object(), &err);
    clear_all_state();
    if (result) {
        println("%s", result);
        free(result);
    } else {
        println("No reachable Browser4 servers responded to close-all.");
    }
    free(err);
    return EXIT_SUCCESS_CLI;
}

static int handle_kill_all(CmdCtx *ctx) {
    (void)ctx;
    fprintf(stderr, "Stopping Browser4 backend and cleaning up browser processes ...\n");
    /* First close all sessions */
    char *err = NULL;
    char *result = call_tool(ctx->client, ctx->base_url, "close_all_sessions", json_object(), &err);
    free(result); free(err);
    clear_all_state();
    println("kill-all complete.");
    return EXIT_SUCCESS_CLI;
}

static int handle_list(CmdCtx *ctx) {
    char *err = NULL;
    char *list_result = call_tool(ctx->client, ctx->base_url, "list_sessions", json_object(), &err);

    println("%-20s | %-40s | %-8s | %s", "Name", "Session ID", "Status", "Next open");
    println("%-20s-+-%-40s-+-%-8s-+-%-9s", "--------------------", "----------------------------------------", "--------", "---------");

    int backend_reachable = list_result != NULL;
    free(err);

    /* Default session */
    CliState *state = read_state(NULL);
    if (state->session_id && state->session_id[0]) {
        const char *status = backend_reachable ? "Active" : "Unknown";
        println("%-20s | %-40s | %-8s | %s", "(default)", state->session_id, status, "Reuse");
    }
    cli_state_free(state);

    /* Named sessions */
    char *dir = resolve_default_state_dir();
    char sessions_path[512];
    snprintf(sessions_path, sizeof(sessions_path), "%s/sessions", dir);
    free(dir);

#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pattern[512];
    snprintf(pattern, sizeof(pattern), "%s\\*.json", sessions_path);
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            char file_path[512];
            snprintf(file_path, sizeof(file_path), "%s\\%s", sessions_path, fd.cFileName);
            /* Extract name */
            char *dot = strrchr(fd.cFileName, '.');
            if (dot) *dot = '\0';

            FILE *f = fopen(file_path, "r");
            if (f) {
                fseek(f, 0, SEEK_END);
                long sz = ftell(f);
                fseek(f, 0, SEEK_SET);
                char *buf = (char *)malloc(sz + 1);
                if (buf) {
                    fread(buf, 1, sz, f);
                    buf[sz] = '\0';
                    char *parse_err = NULL;
                    JsonValue *j = json_parse(buf, &parse_err);
                    if (j) {
                        const char *sid = json_obj_get_str(j, "sessionId", NULL);
                        if (sid) {
                            println("%-20s | %-40s | %-8s | %s",
                                    fd.cFileName, sid,
                                    backend_reachable ? "Active" : "Unknown", "Reuse");
                        }
                        json_free(j);
                    }
                    free(parse_err);
                    free(buf);
                }
                fclose(f);
            }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#endif

    if (list_result) free(list_result);
    return EXIT_SUCCESS_CLI;
}

static int handle_install(CmdCtx *ctx) {
    println("Install command: runtime bundle download not yet implemented in C port.");
    println("Please use the Rust version of browser4-cli for install functionality.");
    json_output_field_str("status", "not_implemented");
    return EXIT_SUCCESS_CLI;
}

static int handle_uninstall(CmdCtx *ctx) {
    println("Uninstall command: not yet implemented in C port.");
    json_output_field_str("status", "not_implemented");
    return EXIT_SUCCESS_CLI;
}

static int handle_upgrade(CmdCtx *ctx) {
    println("Upgrade command: not yet implemented in C port.");
    json_output_field_str("status", "not_implemented");
    return EXIT_SUCCESS_CLI;
}

static int handle_stop(CmdCtx *ctx) {
    println("Stop command: not yet implemented in C port.");
    json_output_field_str("status", "not_implemented");
    return EXIT_SUCCESS_CLI;
}

static int handle_status(CmdCtx *ctx) {
    println("Status command: not yet fully implemented in C port.");
    json_output_field_str("status", "not_implemented");
    return EXIT_SUCCESS_CLI;
}

/* ---- Tool command (generic) ---- */
static int handle_tool_command(CmdCtx *ctx) {
    const CommandDef *cmd = ctx->cmd_def;
    const char *tool_name = cmd->tool_name_fn(ctx->tool_params);
    JsonValue *params = cmd->tool_params_fn(ctx->tool_params);

    if (!tool_name || !tool_name[0]) {
        json_free(params);
        return EXIT_SUCCESS_CLI; /* No MCP call needed (e.g., delete-data, list) */
    }

    json_object_set(params, "_tool_name", json_string(tool_name));
    json_object_set(params, "_command_name", json_string(cmd->name));

    char *error_msg = NULL;
    char *result = NULL;
    int exit_code = with_session(ctx->client, ctx->base_url, ctx->session_name, 0,
                                  session_call_tool_action, params,
                                  &result, &error_msg);
    json_free(params);

    if (exit_code == EXIT_SUCCESS_CLI && result) {
        if (result[0]) println("%s", result);

        /* Structured JSON for eval */
        if (strcmp(tool_name, "browser_evaluate") == 0) {
            json_output_field_str("result", result);
            const char *expr = get_str(ctx->tool_params, "expression");
            if (expr) json_output_field_str("expression", expr);
            const char *ref = get_str(ctx->tool_params, "ref");
            if (ref) json_output_field_str("ref", ref);
        }

        /* Persist active selector */
        const char *selector = get_str(ctx->tool_params, "ref");
        if (!selector) selector = get_str(ctx->tool_params, "selector");
        if (selector && selector[0]) {
            CliState *state = read_state(ctx->session_name);
            free(state->active_selector);
            state->active_selector = _strdup(selector);
            write_state(state, ctx->session_name);
            cli_state_free(state);
        }

        free(result);
        return EXIT_SUCCESS_CLI;
    }

    if (error_msg) {
        fprintf(stderr, "Error: %s\n", error_msg);
        free(error_msg);
    }
    return exit_code;
}

static int handle_mouse_move(CmdCtx *ctx) {
    int ret = handle_tool_command(ctx);

    /* Persist mouse position */
    JsonValue *x = get_number_value(ctx->tool_params, "x");
    JsonValue *y = get_number_value(ctx->tool_params, "y");
    if (x && y) {
        CliState *state = read_state(ctx->session_name);
        if (!state->last_mouse_position) {
            state->last_mouse_position = (MousePosition *)malloc(sizeof(MousePosition));
        }
        state->last_mouse_position->x = json_as_number(x);
        state->last_mouse_position->y = json_as_number(y);
        write_state(state, ctx->session_name);
        cli_state_free(state);
    }
    return ret;
}

/* ---- Snapshot command ---- */
static int handle_snapshot_cmd(CmdCtx *ctx) {
    const char *filename = get_str(ctx->tool_params, "filename");
    const CommandDef *cmd = ctx->cmd_def;
    const char *tool_name = cmd->tool_name_fn(ctx->tool_params);
    JsonValue *params = cmd->tool_params_fn(ctx->tool_params);
    json_object_set(params, "_tool_name", json_string(tool_name));

    char *error_msg = NULL;
    char *result = NULL;
    int exit_code = with_session(ctx->client, ctx->base_url, ctx->session_name, 0,
                                  session_call_tool_action, params,
                                  &result, &error_msg);
    json_free(params);

    if (exit_code == EXIT_SUCCESS_CLI && result) {
        /* Also get page URL and title */
        CliState *state = read_state(ctx->session_name);
        char *url_err = NULL;
        JsonValue *url_args = json_object();
        json_object_set(url_args, "sessionId", json_string(state->session_id));
        char *page_url = call_tool(ctx->client, ctx->base_url, "page_url", url_args, &url_err);
        free(url_err); cli_state_free(state);

        char *out_path = resolve_output_path(filename, "snapshot", "yml");
        save_snapshot(out_path, result);

        json_output_field_str("page_url", page_url ? page_url : "");
        json_output_field_str("snapshot_path", out_path);

        println("### Page");
        if (page_url) println("- Page URL: %s", page_url);
        println("### Snapshot");
        println("[Snapshot](%s)", out_path);

        free(page_url);
        free(out_path);
        free(result);
        return EXIT_SUCCESS_CLI;
    }

    if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); }
    return exit_code;
}

/* ---- Screenshot command ---- */
static int handle_screenshot_cmd(CmdCtx *ctx) {
    const char *filename = get_str(ctx->tool_params, "filename");
    const CommandDef *cmd = ctx->cmd_def;
    const char *tool_name = cmd->tool_name_fn(ctx->tool_params);
    JsonValue *params = cmd->tool_params_fn(ctx->tool_params);
    json_object_set(params, "_tool_name", json_string(tool_name));

    char *error_msg = NULL;
    char *result = NULL;
    int exit_code = with_session(ctx->client, ctx->base_url, ctx->session_name, 0,
                                  session_call_tool_action, params,
                                  &result, &error_msg);
    json_free(params);

    if (exit_code == EXIT_SUCCESS_CLI && result) {
        size_t bin_len = 0;
        unsigned char *bin = base64_decode(result, &bin_len);
        if (bin) {
            char *out_path = resolve_output_path(filename, "screenshot", "png");
            save_binary(out_path, bin, bin_len);
            println("[Screenshot](%s)", out_path);
            free(out_path);
            free(bin);
        } else {
            fprintf(stderr, "Failed to decode screenshot base64 data\n");
        }
        free(result);
        return EXIT_SUCCESS_CLI;
    }

    if (error_msg) { fprintf(stderr, "Error: %s\n", error_msg); free(error_msg); }
    return exit_code;
}

/* ---- Agent commands ---- */
static int handle_agent_run(CmdCtx *ctx) {
    const char *task = get_str(ctx->tool_params, "task");
    if (!task || !task[0]) {
        fprintf(stderr, "Task description is required.\n");
        return EXIT_USAGE;
    }

    char *err = NULL;
    char *result = submit_plain_command(ctx->client, ctx->base_url, task, 1, &err);
    if (result) {
        char *task_id = result;
        /* Strip quotes if present */
        if (task_id[0] == '"') {
            size_t len = strlen(task_id);
            if (len > 1 && task_id[len-1] == '"') {
                task_id[len-1] = '\0';
                task_id++;
            }
        }
        println("Task submitted: %s", task_id);
        println("Use 'browser4-cli agent status %s' to check progress.", task_id);
        json_output_field_str("task_id", task_id);
        free(result);
        return EXIT_SUCCESS_CLI;
    }

    fprintf(stderr, "Agent task submission failed: %s\n", err ? err : "unknown error");
    free(err);
    return EXIT_GENERAL;
}

static int handle_agent_status(CmdCtx *ctx) {
    const char *id = get_str(ctx->tool_params, "id");
    if (!id || !id[0]) { fprintf(stderr, "Task ID is required.\n"); return EXIT_USAGE; }

    char *err = NULL;
    char *result = get_command_status(ctx->client, ctx->base_url, id, &err);
    if (result) {
        println("%s", result);
        json_output_field_str("task_id", id);
        free(result);
        return EXIT_SUCCESS_CLI;
    }
    fprintf(stderr, "Error: %s\n", err ? err : "unknown");
    free(err);
    return EXIT_GENERAL;
}

static int handle_agent_result(CmdCtx *ctx) {
    const char *id = get_str(ctx->tool_params, "id");
    if (!id || !id[0]) { fprintf(stderr, "Task ID is required.\n"); return EXIT_USAGE; }

    char *err = NULL;
    char *result = get_command_result(ctx->client, ctx->base_url, id, &err);
    if (result) {
        println("%s", result);
        json_output_field_str("task_id", id);
        free(result);
        return EXIT_SUCCESS_CLI;
    }
    fprintf(stderr, "Error: %s\n", err ? err : "unknown");
    free(err);
    return EXIT_GENERAL;
}

/* ---- Swarm commands ---- */
static int handle_swarm_create(CmdCtx *ctx) {
    const char *profile_mode = get_str(ctx->tool_params, "profile-mode");
    if (profile_mode) {
        char *upper = _strdup(profile_mode);
        for (char *c = upper; *c; c++) *c = (char)toupper((unsigned char)*c);
        if (strcmp(upper, "SEQUENTIAL") != 0 && strcmp(upper, "TEMPORARY") != 0) {
            fprintf(stderr, "Swarm create only supports --profile-mode=SEQUENTIAL or TEMPORARY. Received: %s\n", upper);
            free(upper);
            return EXIT_USAGE;
        }
        free(upper);
    }

    JsonValue *caps = json_object();
    const char *pm = get_str(ctx->tool_params, "profile-mode");
    json_object_set(caps, "profileMode", json_string(pm ? pm : "SEQUENTIAL"));
    const char *mot = get_str(ctx->tool_params, "max-open-tabs"); if (mot) json_object_set(caps, "maxOpenTabs", json_string(mot));
    const char *mbc = get_str(ctx->tool_params, "max-browser-contexts"); if (mbc) json_object_set(caps, "maxBrowserContexts", json_string(mbc));
    const char *dm = get_str(ctx->tool_params, "display-mode"); if (dm) json_object_set(caps, "displayMode", json_string(dm));

    JsonValue *open_args = json_object();
    json_object_set(open_args, "capabilities", caps);
    json_object_set(open_args, "sessionId", json_string("SWARM"));

    char *err = NULL;
    char *result = call_tool(ctx->client, ctx->base_url, "open_session", open_args, &err);

    if (result) {
        CliState *state = read_state(ctx->session_name);
        free(state->session_id);
        state->session_id = _strdup("SWARM");
        write_state(state, ctx->session_name);
        cli_state_free(state);

        println("Swarm session created: SWARM");
        free(result);
        return EXIT_SUCCESS_CLI;
    }

    fprintf(stderr, "Failed to create swarm session: %s\n", err ? err : "unknown");
    free(err);
    return EXIT_SERVER;
}

static int handle_swarm_submit(CmdCtx *ctx) {
    const char *url = get_str(ctx->tool_params, "url");
    const char *seed_file = get_str(ctx->tool_params, "seed-file");
    const char *query_raw = get_str(ctx->tool_params, "sql");

    if ((!url || !url[0]) && (!seed_file || !seed_file[0])) {
        fprintf(stderr, "Either a URL or --seed-file is required.\n");
        return EXIT_USAGE;
    }

    /* Collect URLs */
    char **urls = NULL;
    int url_count = 0;
    int url_cap = 0;

    if (url && url[0]) {
        urls = (char **)realloc(urls, sizeof(char *) * (++url_cap));
        urls[url_count++] = _strdup(url);
    }

    if (seed_file && seed_file[0]) {
        FILE *f = fopen(seed_file, "r");
        if (!f) {
            fprintf(stderr, "Failed to read seed file '%s'\n", seed_file);
            for (int i = 0; i < url_count; i++) free(urls[i]);
            free(urls);
            return EXIT_USAGE;
        }
        char line[4096];
        while (fgets(line, sizeof(line), f)) {
            char *trimmed = line;
            while (*trimmed && isspace((unsigned char)*trimmed)) trimmed++;
            size_t tlen = strlen(trimmed);
            while (tlen > 0 && isspace((unsigned char)trimmed[tlen-1])) trimmed[--tlen] = '\0';
            if (trimmed[0] && trimmed[0] != '#') {
                if (url_count >= url_cap) {
                    url_cap = url_cap ? url_cap * 2 : 4;
                    urls = (char **)realloc(urls, sizeof(char *) * url_cap);
                }
                urls[url_count++] = _strdup(trimmed);
            }
        }
        fclose(f);
    }

    if (url_count == 0) {
        fprintf(stderr, "No URLs to submit.\n");
        free(urls);
        return EXIT_USAGE;
    }

    /* Build load options */
    const char *deadline = get_str(ctx->tool_params, "deadline");
    const char *expires = get_str(ctx->tool_params, "expires");
    int refresh = get_bool(ctx->tool_params, "refresh");
    int parse = get_bool(ctx->tool_params, "parse");
    int store_content = get_bool(ctx->tool_params, "store-content");

    char opts[1024] = "";
    if (deadline) { strcat(opts, " -deadline "); strcat(opts, deadline); }
    if (expires) { strcat(opts, " -expires "); strcat(opts, expires); }
    if (refresh) strcat(opts, " -refresh");
    if (parse) strcat(opts, " -parse");
    if (store_content) strcat(opts, " -storeContent");

    /* Submit each URL */
    for (int i = 0; i < url_count; i++) {
        char *method;
        char *task_result;

        if (query_raw && query_raw[0]) {
            JsonValue *payload = json_object();
            json_object_set(payload, "url", json_string(urls[i]));
            json_object_set(payload, "args", json_string(opts));
            json_object_set(payload, "query", json_string(query_raw));
            char *err = NULL;
            task_result = submit_swarm_query(ctx->client, ctx->base_url, payload, &err);
            json_free(payload);
            free(err);
            method = "query";
        } else {
            char command[4096];
            snprintf(command, sizeof(command), "%s%s", urls[i], opts);
            char *err = NULL;
            task_result = submit_swarm_payload(ctx->client, ctx->base_url, command, &err);
            free(err);
            method = "submit";
        }

        if (task_result) {
            char *task_id = task_result;
            if (task_id[0] == '"') {
                size_t len = strlen(task_id);
                if (len > 1 && task_id[len-1] == '"') { task_id[len-1] = '\0'; task_id++; }
            }
            println("Task Submitted: %s -> Task ID: %s (via %s)", urls[i], task_id, method);
            free(task_result);
        }
    }

    for (int i = 0; i < url_count; i++) free(urls[i]);
    free(urls);
    return EXIT_SUCCESS_CLI;
}

static int handle_swarm_query(CmdCtx *ctx) {
    /* Delegates to swarm_submit logic with query required */
    const char *query_raw = get_str(ctx->tool_params, "sql");
    if (!query_raw || !query_raw[0]) {
        fprintf(stderr, "--sql is required. Provide an inline X-SQL query or @file.sql.\n");
        return EXIT_USAGE;
    }
    return handle_swarm_submit(ctx);
}

static int handle_swarm_status(CmdCtx *ctx) {
    const char *id = get_str(ctx->tool_params, "id");
    if (!id || !id[0]) { fprintf(stderr, "Task ID is required.\n"); return EXIT_USAGE; }

    char *err = NULL;
    char *result = get_swarm_status(ctx->client, ctx->base_url, id, &err);
    if (result) {
        println("%s", result);
        json_output_field_str("task_id", id);
        free(result);
        return EXIT_SUCCESS_CLI;
    }
    fprintf(stderr, "Error: %s\n", err ? err : "unknown");
    free(err);
    return EXIT_GENERAL;
}

static int handle_swarm_result(CmdCtx *ctx) {
    const char *id = get_str(ctx->tool_params, "id");
    if (!id || !id[0]) { fprintf(stderr, "Task ID is required.\n"); return EXIT_USAGE; }

    char *err = NULL;
    char *result = get_swarm_result(ctx->client, ctx->base_url, id, &err);
    if (result) {
        println("%s", result);
        json_output_field_str("task_id", id);
        free(result);
        return EXIT_SUCCESS_CLI;
    }
    fprintf(stderr, "Error: %s\n", err ? err : "unknown");
    free(err);
    return EXIT_GENERAL;
}

/* ---- Cookie handlers ---- */
static int handle_cookie_list(CmdCtx *ctx) {
    /* Get storage state, filter cookies */
    return handle_tool_command(ctx);
}

static int handle_cookie_get(CmdCtx *ctx) {
    /* Get storage state, find cookie by name */
    return handle_tool_command(ctx);
}

static int handle_cookie_set(CmdCtx *ctx) {
    return handle_tool_command(ctx);
}

static int handle_cookie_delete(CmdCtx *ctx) {
    return handle_tool_command(ctx);
}

static int handle_cookie_clear(CmdCtx *ctx) {
    return handle_tool_command(ctx);
}

/* ---- Storage handlers ---- */
static int handle_storage_list(CmdCtx *ctx, const char *storage_area) {
    (void)ctx; (void)storage_area;
    return handle_tool_command(ctx);
}

static int handle_storage_get(CmdCtx *ctx, const char *storage_area) {
    (void)ctx; (void)storage_area;
    return handle_tool_command(ctx);
}

static int handle_storage_set(CmdCtx *ctx, const char *storage_area) {
    (void)ctx; (void)storage_area;
    return handle_tool_command(ctx);
}

static int handle_storage_delete(CmdCtx *ctx, const char *storage_area) {
    (void)ctx; (void)storage_area;
    return handle_tool_command(ctx);
}

static int handle_storage_clear(CmdCtx *ctx, const char *storage_area) {
    (void)ctx; (void)storage_area;
    return handle_tool_command(ctx);
}

static int handle_state_save(CmdCtx *ctx) {
    return handle_tool_command(ctx);
}

static int handle_state_load(CmdCtx *ctx) {
    return handle_tool_command(ctx);
}

static int handle_delete_data(CmdCtx *ctx) {
    return handle_tool_command(ctx);
}

/* ---------------------------------------------------------------------------
 * Batch execution
 * ------------------------------------------------------------------------- */

static int execute_command(HttpClient *client, const char *command_name,
                           JsonValue *tool_params, const char *base_url,
                           const char *session_name, int *out_exit_code);

static int handle_batch(CmdCtx *ctx, int argc, char **argv) {
    (void)ctx;
    /* Parse batch-specific args from the remaining argv */
    /* The batch command has its own argument structure */
    return EXIT_SUCCESS_CLI;
}

/* ---------------------------------------------------------------------------
 * Command execution dispatcher
 * ------------------------------------------------------------------------- */

static int execute_command(HttpClient *client, const char *command_name,
                           JsonValue *tool_params, const char *base_url,
                           const char *session_name, int *out_exit_code) {
    const CommandDef *cmd = find_command(command_name);
    if (!cmd) {
        fprintf(stderr, "Unknown command: %s\n", command_name);
        *out_exit_code = EXIT_USAGE;
        return EXIT_USAGE;
    }

    CmdCtx ctx = { client, base_url, session_name, command_name, tool_params, cmd };
    int ret;

    if (strcmp(command_name, "open") == 0) ret = handle_open(&ctx);
    else if (strcmp(command_name, "goto") == 0) ret = handle_goto(&ctx);
    else if (strcmp(command_name, "close") == 0) ret = handle_close(&ctx);
    else if (strcmp(command_name, "close-all") == 0) ret = handle_close_all(&ctx);
    else if (strcmp(command_name, "kill-all") == 0) ret = handle_kill_all(&ctx);
    else if (strcmp(command_name, "list") == 0) ret = handle_list(&ctx);
    else if (strcmp(command_name, "install") == 0) ret = handle_install(&ctx);
    else if (strcmp(command_name, "uninstall") == 0) ret = handle_uninstall(&ctx);
    else if (strcmp(command_name, "upgrade") == 0) ret = handle_upgrade(&ctx);
    else if (strcmp(command_name, "stop") == 0) ret = handle_stop(&ctx);
    else if (strcmp(command_name, "status") == 0) ret = handle_status(&ctx);
    else if (strcmp(command_name, "snapshot") == 0) ret = handle_snapshot_cmd(&ctx);
    else if (strcmp(command_name, "screenshot") == 0) ret = handle_screenshot_cmd(&ctx);
    else if (strcmp(command_name, "agent-run") == 0) ret = handle_agent_run(&ctx);
    else if (strcmp(command_name, "agent-status") == 0) ret = handle_agent_status(&ctx);
    else if (strcmp(command_name, "agent-result") == 0) ret = handle_agent_result(&ctx);
    else if (strcmp(command_name, "swarm-create") == 0) ret = handle_swarm_create(&ctx);
    else if (strcmp(command_name, "swarm-submit") == 0) ret = handle_swarm_submit(&ctx);
    else if (strcmp(command_name, "swarm-query") == 0) ret = handle_swarm_query(&ctx);
    else if (strcmp(command_name, "swarm-status") == 0) ret = handle_swarm_status(&ctx);
    else if (strcmp(command_name, "swarm-result") == 0) ret = handle_swarm_result(&ctx);
    else if (strcmp(command_name, "mousemove") == 0) ret = handle_mouse_move(&ctx);
    else if (strcmp(command_name, "batch") == 0) ret = handle_batch(&ctx, 0, NULL);
    else ret = handle_tool_command(&ctx);

    *out_exit_code = ret;
    return ret;
}

/* ---------------------------------------------------------------------------
 * handle_help (standalone)
 * ------------------------------------------------------------------------- */

static int handle_help(CmdCtx *ctx, int argc, char **argv) {
    (void)ctx;
    /* argv[0] is "help", argv[1] might be a command name */
    if (argc > 1 && argv[1] && argv[1][0] != '-') {
        char *help_text = generate_command_help(argv[1]);
        printf("%s", help_text);
        free(help_text);
    } else {
        char *help_text = generate_help();
        printf("%s", help_text);
        free(help_text);
    }
    return EXIT_SUCCESS_CLI;
}

/* ---------------------------------------------------------------------------
 * main() — Entry point
 * ------------------------------------------------------------------------- */

int main(int argc, char **argv) {
    /* Parse global flags */
    CliGlobalFlags *flags = parse_global_flags(argc - 1, argv + 1);

    g_json_mode = flags->json_mode;
    g_quiet_mode = flags->quiet_mode;

    if (g_json_mode) json_output_init();

    /* No command — show help */
    if (flags->args_count == 0) {
        char *help_text = generate_help();
        printf("%s", help_text);
        free(help_text);
        global_flags_free(flags);
        return EXIT_SUCCESS_CLI;
    }

    /* --version */
    if (strcmp(flags->args[0], "--version") == 0 || strcmp(flags->args[0], "-v") == 0) {
        printf("browser4-cli %s\n", BROWSER4_CLI_VERSION);
        global_flags_free(flags);
        return EXIT_SUCCESS_CLI;
    }

    /* --help or help */
    if (strcmp(flags->args[0], "--help") == 0 || strcmp(flags->args[0], "help") == 0) {
        int help_ret = handle_help(NULL, flags->args_count, flags->args);
        global_flags_free(flags);
        if (g_json_mode) json_output_finish();
        return help_ret;
    }

    /* Determine base URL */
    const char *base_url = flags->server_url ? flags->server_url : DEFAULT_BASE_URL;

    /* Parse command and its args */
    const char *command_name = flags->args[0];
    int raw_count = flags->args_count - 1;
    char **raw_args = raw_count > 0 ? flags->args + 1 : NULL;

    /* Build short option map for the command */
    const CommandDef *cmd = find_command(command_name);
    ShortOptionEntry *short_entries = NULL;
    int short_count = 0;
    if (cmd && cmd->options_count > 0) {
        short_count = cmd->options_count;
        short_entries = (ShortOptionEntry *)calloc(short_count, sizeof(ShortOptionEntry));
        int actual = 0;
        for (int i = 0; i < cmd->options_count; i++) {
            if (cmd->options[i].short_name) {
                short_entries[actual].short_name = (char *)cmd->options[i].short_name;
                short_entries[actual].long_name = (char *)cmd->options[i].name;
                actual++;
            }
        }
        short_count = actual;
    }

    JsonValue *short_map = build_short_option_map(short_entries, short_count);
    free(short_entries);

    /* Parse raw args into JSON map */
    JsonValue *raw_map = parse_raw_args(raw_args, raw_count, short_map);
    json_free(short_map);

    /* Build command args with positional mapping */
    const char **arg_names = NULL;
    int arg_count = 0;
    if (cmd) {
        arg_count = cmd->args_count;
        if (arg_count > 0) {
            arg_names = (const char **)calloc(arg_count, sizeof(char *));
            for (int i = 0; i < arg_count; i++) {
                arg_names[i] = cmd->args[i].name;
            }
        }
    }

    char *build_err = NULL;
    JsonValue *tool_params = build_command_args(raw_map, arg_names, arg_count, &build_err);
    free(arg_names);
    json_free(raw_map);

    if (!tool_params) {
        fprintf(stderr, "%s\n", build_err ? build_err : "Failed to parse arguments");
        free(build_err);
        global_flags_free(flags);
        if (g_json_mode) json_output_finish();
        return EXIT_USAGE;
    }

    /* Handle batch command specially */
    if (strcmp(command_name, "batch") == 0) {
        /* Parse batch args from remaining argv */
        int batch_argc = raw_count;
        char **batch_argv = raw_args;
        char *batch_err = NULL;
        BatchArgs *batch = parse_batch_args(batch_argv, batch_argc, &batch_err);
        if (!batch) {
            fprintf(stderr, "%s\n", batch_err ? batch_err : "Invalid batch arguments");
            free(batch_err);
            json_free(tool_params);
            global_flags_free(flags);
            if (g_json_mode) json_output_finish();
            return EXIT_USAGE;
        }

        HttpClient *client = http_client_new_batch();
        int overall_exit = EXIT_SUCCESS_CLI;

        if (batch->json_input) {
            /* Read commands from stdin */
            char stdin_buf[65536];
            size_t stdin_len = fread(stdin_buf, 1, sizeof(stdin_buf) - 1, stdin);
            stdin_buf[stdin_len] = '\0';

            char *json_err = NULL;
            int cmd_count = 0;
            char ***commands = parse_batch_json_commands(stdin_buf, &cmd_count, &json_err);
            if (!commands) {
                fprintf(stderr, "%s\n", json_err ? json_err : "Invalid batch JSON");
                free(json_err);
                http_client_free(client);
                batch_args_free(batch);
                json_free(tool_params);
                global_flags_free(flags);
                return EXIT_USAGE;
            }

            for (int i = 0; i < cmd_count; i++) {
                int cmd_argc = 0;
                /* Count tokens */
                while (commands[i][cmd_argc]) cmd_argc++;

                /* Parse this command's args */
                const char *sub_cmd_name = commands[i][0];
                const CommandDef *sub_cmd = find_command(sub_cmd_name);

                int sub_raw_count = cmd_argc - 1;
                char **sub_raw = sub_raw_count > 0 ? &commands[i][1] : NULL;

                JsonValue *sub_short_map = NULL;
                if (sub_cmd && sub_cmd->options_count > 0) {
                    ShortOptionEntry *se = (ShortOptionEntry *)calloc(sub_cmd->options_count, sizeof(ShortOptionEntry));
                    int ac = 0;
                    for (int j = 0; j < sub_cmd->options_count; j++) {
                        if (sub_cmd->options[j].short_name) {
                            se[ac].short_name = (char *)sub_cmd->options[j].short_name;
                            se[ac].long_name = (char *)sub_cmd->options[j].name;
                            ac++;
                        }
                    }
                    sub_short_map = build_short_option_map(se, ac);
                    free(se);
                }

                JsonValue *sub_raw_map = parse_raw_args(sub_raw, sub_raw_count, sub_short_map);
                json_free(sub_short_map);

                const char **sub_arg_names = NULL;
                int sub_arg_count = 0;
                if (sub_cmd) {
                    sub_arg_count = sub_cmd->args_count;
                    if (sub_arg_count > 0) {
                        sub_arg_names = (const char **)calloc(sub_arg_count, sizeof(char *));
                        for (int j = 0; j < sub_arg_count; j++) sub_arg_names[j] = sub_cmd->args[j].name;
                    }
                }

                char *sub_err = NULL;
                JsonValue *sub_params = build_command_args(sub_raw_map, sub_arg_names, sub_arg_count, &sub_err);
                free(sub_arg_names);
                json_free(sub_raw_map);

                if (!sub_params) {
                    fprintf(stderr, "Batch command[%d] '%s': %s\n", i, sub_cmd_name, sub_err ? sub_err : "parse error");
                    free(sub_err);
                    if (batch->bail) { overall_exit = EXIT_BATCH_PARTIAL; break; }
                    overall_exit = EXIT_BATCH_PARTIAL;
                    continue;
                }

                int sub_exit = 0;
                execute_command(client, sub_cmd_name, sub_params, base_url,
                               flags->session_name, &sub_exit);
                json_free(sub_params);

                if (sub_exit != EXIT_SUCCESS_CLI && batch->bail) {
                    overall_exit = EXIT_BATCH_PARTIAL;
                    break;
                }
            }

            for (int i = 0; i < cmd_count; i++) {
                for (int j = 0; commands[i][j]; j++) free(commands[i][j]);
                free(commands[i]);
            }
            free(commands);
        } else {
            /* Argument mode: each batch arg is a command string */
            for (int i = 0; i < batch->commands_count; i++) {
                int tok_count = 0;
                char *parse_err = NULL;
                char **tokens = parse_command_string(batch->commands[i], &tok_count, &parse_err);
                if (!tokens) {
                    fprintf(stderr, "Batch command[%d] parse error: %s\n", i, parse_err ? parse_err : "unknown");
                    free(parse_err);
                    if (batch->bail) { overall_exit = EXIT_BATCH_PARTIAL; break; }
                    overall_exit = EXIT_BATCH_PARTIAL;
                    continue;
                }

                const char *sub_cmd_name = tokens[0];
                const CommandDef *sub_cmd = find_command(sub_cmd_name);
                int sub_raw_count = tok_count - 1;
                char **sub_raw = sub_raw_count > 0 ? &tokens[1] : NULL;

                JsonValue *sub_short_map = NULL;
                if (sub_cmd && sub_cmd->options_count > 0) {
                    ShortOptionEntry *se = (ShortOptionEntry *)calloc(sub_cmd->options_count, sizeof(ShortOptionEntry));
                    int ac = 0;
                    for (int j = 0; j < sub_cmd->options_count; j++) {
                        if (sub_cmd->options[j].short_name) {
                            se[ac].short_name = (char *)sub_cmd->options[j].short_name;
                            se[ac].long_name = (char *)sub_cmd->options[j].name;
                            ac++;
                        }
                    }
                    sub_short_map = build_short_option_map(se, ac);
                    free(se);
                }

                JsonValue *sub_raw_map = parse_raw_args(sub_raw, sub_raw_count, sub_short_map);
                json_free(sub_short_map);

                const char **sub_arg_names = NULL;
                int sub_arg_count = 0;
                if (sub_cmd) {
                    sub_arg_count = sub_cmd->args_count;
                    if (sub_arg_count > 0) {
                        sub_arg_names = (const char **)calloc(sub_arg_count, sizeof(char *));
                        for (int j = 0; j < sub_arg_count; j++) sub_arg_names[j] = sub_cmd->args[j].name;
                    }
                }

                char *sub_err = NULL;
                JsonValue *sub_params = build_command_args(sub_raw_map, sub_arg_names, sub_arg_count, &sub_err);
                free(sub_arg_names);
                json_free(sub_raw_map);

                if (!sub_params) {
                    fprintf(stderr, "Batch command[%d] '%s': %s\n", i, sub_cmd_name, sub_err ? sub_err : "parse error");
                    free(sub_err);
                    for (int j = 0; j < tok_count; j++) free(tokens[j]);
                    free(tokens);
                    if (batch->bail) { overall_exit = EXIT_BATCH_PARTIAL; break; }
                    overall_exit = EXIT_BATCH_PARTIAL;
                    continue;
                }

                int sub_exit = 0;
                execute_command(client, sub_cmd_name, sub_params, base_url,
                               flags->session_name, &sub_exit);
                json_free(sub_params);

                for (int j = 0; j < tok_count; j++) free(tokens[j]);
                free(tokens);

                if (sub_exit != EXIT_SUCCESS_CLI && batch->bail) {
                    overall_exit = EXIT_BATCH_PARTIAL;
                    break;
                }
            }
        }

        http_client_free(client);
        batch_args_free(batch);
        json_free(tool_params);
        global_flags_free(flags);
        if (g_json_mode) json_output_finish();
        return overall_exit;
    }

    /* Execute single command */
    HttpClient *client = http_client_new();
    int exit_code = 0;
    execute_command(client, command_name, tool_params, base_url,
                    flags->session_name, &exit_code);

    http_client_free(client);
    json_free(tool_params);
    global_flags_free(flags);
    if (g_json_mode) json_output_finish();
    return exit_code;
}
