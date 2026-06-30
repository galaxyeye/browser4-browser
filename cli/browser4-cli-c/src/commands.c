#include "commands.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ---------------------------------------------------------------------------
 * Category strings
 * ------------------------------------------------------------------------- */

const char *category_str(Category cat) {
    switch (cat) {
        case CAT_CORE:       return "core";
        case CAT_NAVIGATION: return "navigation";
        case CAT_KEYBOARD:   return "keyboard";
        case CAT_MOUSE:      return "mouse";
        case CAT_EXPORT:     return "export";
        case CAT_TABS:       return "tabs";
        case CAT_STORAGE:    return "storage";
        case CAT_NETWORK:    return "network";
        case CAT_DEVTOOLS:   return "devtools";
        case CAT_BROWSERS:   return "browsers";
        case CAT_CONFIG:     return "config";
        case CAT_INSTALL:    return "install";
        case CAT_AGENT:      return "agent";
        case CAT_SWARM:      return "swarm";
    }
    return "core";
}

/* ---------------------------------------------------------------------------
 * Helper functions
 * ------------------------------------------------------------------------- */

const char *get_str(JsonValue *args, const char *key) {
    JsonValue *v = json_object_get(args, key);
    if (v && v->type == JSON_STRING) return v->data.string_val;
    return NULL;
}

int get_bool(JsonValue *args, const char *key) {
    JsonValue *v = json_object_get(args, key);
    if (v && v->type == JSON_BOOL) return v->data.bool_val;
    return 0;
}

JsonValue *get_number_value(JsonValue *args, const char *key) {
    JsonValue *v = json_object_get(args, key);
    if (v && v->type == JSON_NUMBER) return v;
    return NULL;
}

int looks_like_selector_or_ref(const char *value) {
    if (!value || !*value) return 0;
    const char *trimmed = value;
    while (*trimmed && isspace((unsigned char)*trimmed)) trimmed++;
    if (!*trimmed) return 0;

    if (trimmed[0] == '#' || trimmed[0] == '.' || trimmed[0] == '[') return 1;
    if (strncmp(trimmed, "//", 2) == 0) return 1;
    if (strncmp(trimmed, "xpath:", 6) == 0) return 1;
    if (strncmp(trimmed, "css:", 4) == 0) return 1;
    if (strncmp(trimmed, "backend:", 8) == 0) return 1;
    if (strncmp(trimmed, "text=", 5) == 0) return 1;
    if ((trimmed[0] == 'e' || trimmed[0] == 'E') && trimmed[1]) {
        int all_digits = 1;
        for (const char *p = trimmed + 1; *p && !isspace((unsigned char)*p); p++) {
            if (!isdigit((unsigned char)*p)) { all_digits = 0; break; }
        }
        return all_digits;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * Internal: resolve positional args to key/ref pairs
 * ------------------------------------------------------------------------- */

static char **get_raw_positionals(JsonValue *args, int *out_count) {
    JsonValue *pos = json_object_get(args, "_");
    if (!pos || pos->type != JSON_ARRAY) {
        *out_count = 0;
        return NULL;
    }
    size_t sz = json_array_size(pos);
    if (sz <= 1) { *out_count = 0; return NULL; }

    int n = (int)(sz - 1);
    char **result = (char **)calloc(n, sizeof(char *));
    for (int i = 0; i < n; i++) {
        JsonValue *v = json_array_get(pos, i + 1);
        if (v && v->type == JSON_STRING) result[i] = _strdup(v->data.string_val);
        else result[i] = _strdup("");
    }
    *out_count = n;
    return result;
}

static void free_positionals(char **p, int n) {
    for (int i = 0; i < n; i++) free(p[i]);
    free(p);
}

/* ---------------------------------------------------------------------------
 * Tool name / params functions for each command
 * ------------------------------------------------------------------------- */

/* ---- open ---- */
static const char *_tool_name_open(JsonValue *args) {
    const char *url = get_str(args, "url");
    return (url && url[0]) ? "browser_navigate" : "browser_snapshot";
}
static JsonValue *_tool_params_open(JsonValue *args) {
    JsonValue *p = json_object();
    const char *url = get_str(args, "url");
    json_object_set(p, "url", json_string(url ? url : "about:blank"));
    if (get_bool(args, "headless")) json_object_set(p, "headed", json_bool(0));
    else if (get_bool(args, "headed")) json_object_set(p, "headed", json_bool(1));
    const char *pf = get_str(args, "profile");
    if (pf) json_object_set(p, "profilePath", json_string(pf));
    const char *pm = get_str(args, "profile-mode");
    if (pm) json_object_set(p, "profileMode", json_string(pm));
    const char *il = get_str(args, "interact-level");
    if (il) json_object_set(p, "interactLevel", json_string(il));
    return p;
}

/* ---- goto ---- */
static const char *_tool_name_goto(JsonValue *args) { (void)args; return "browser_navigate"; }
static JsonValue *_tool_params_goto(JsonValue *args) {
    JsonValue *p = json_object();
    const char *url = get_str(args, "url");
    json_object_set(p, "url", json_string(url ? url : ""));
    return p;
}

/* ---- empty (no MCP call needed) ---- */
static const char *_tool_name_empty(JsonValue *args) { (void)args; return ""; }
static JsonValue *_tool_params_empty(JsonValue *args) { (void)args; return json_object(); }

/* ---- navigation ---- */
static const char *_tool_name_go_back(JsonValue *args) { (void)args; return "browser_navigate_back"; }
static const char *_tool_name_go_forward(JsonValue *args) { (void)args; return "browser_navigate_forward"; }
static const char *_tool_name_reload(JsonValue *args) { (void)args; return "browser_reload"; }
static JsonValue *_tool_params_nav_empty(JsonValue *args) { (void)args; return json_object(); }

/* ---- press/type (resolve key/text and ref) ---- */
static JsonValue *_tool_params_press(JsonValue *args) {
    int npos = 0; char **pos = get_raw_positionals(args, &npos);
    JsonValue *p = json_object();
    const char *key = NULL, *ref = NULL;
    if (npos >= 2) {
        if (looks_like_selector_or_ref(pos[0]) && !looks_like_selector_or_ref(pos[1])) {
            ref = pos[0]; key = pos[1];
        } else { key = pos[0]; ref = pos[1]; }
    } else if (npos == 1) { key = pos[0]; ref = get_str(args, "ref"); }
    else { key = get_str(args, "key"); ref = get_str(args, "ref"); }
    json_object_set(p, "key", json_string(key ? key : ""));
    if (ref) json_object_set(p, "ref", json_string(ref));
    free_positionals(pos, npos);
    return p;
}
static const char *_tool_name_press(JsonValue *args) { (void)args; return "browser_press_key"; }

static JsonValue *_tool_params_type(JsonValue *args) {
    int npos = 0; char **pos = get_raw_positionals(args, &npos);
    JsonValue *p = json_object();
    const char *text = NULL, *ref = NULL;
    if (npos >= 2) {
        if (looks_like_selector_or_ref(pos[0]) && !looks_like_selector_or_ref(pos[1])) {
            ref = pos[0]; text = pos[1];
        } else { text = pos[0]; ref = pos[1]; }
    } else if (npos == 1) { text = pos[0]; ref = get_str(args, "ref"); }
    else { text = get_str(args, "text"); ref = get_str(args, "ref"); }
    json_object_set(p, "text", json_string(text ? text : ""));
    if (ref) json_object_set(p, "ref", json_string(ref));
    if (get_bool(args, "submit")) json_object_set(p, "submit", json_bool(1));
    free_positionals(pos, npos);
    return p;
}
static const char *_tool_name_type(JsonValue *args) { (void)args; return "browser_press_sequentially"; }

/* ---- keydown/keyup ---- */
static const char *_tool_name_keydown(JsonValue *args) { (void)args; return "browser_keydown"; }
static const char *_tool_name_keyup(JsonValue *args) { (void)args; return "browser_keyup"; }
static JsonValue *_tool_params_key(JsonValue *args) {
    JsonValue *p = json_object();
    const char *k = get_str(args, "key");
    json_object_set(p, "key", json_string(k ? k : ""));
    return p;
}

/* ---- mouse ---- */
static const char *_tool_name_mousemove(JsonValue *args) { (void)args; return "browser_mouse_move_xy"; }
static JsonValue *_tool_params_mousemove(JsonValue *args) {
    JsonValue *p = json_object();
    JsonValue *x = get_number_value(args, "x");
    JsonValue *y = get_number_value(args, "y");
    json_object_set(p, "x", json_copy(x ? x : json_number(0)));
    json_object_set(p, "y", json_copy(y ? y : json_number(0)));
    return p;
}

static const char *_tool_name_mousedown(JsonValue *args) { (void)args; return "browser_mouse_down"; }
static const char *_tool_name_mouseup(JsonValue *args) { (void)args; return "browser_mouse_up"; }
static JsonValue *_tool_params_mouse_button(JsonValue *args) {
    JsonValue *p = json_object();
    const char *b = get_str(args, "button");
    if (b) json_object_set(p, "button", json_string(b));
    return p;
}

static const char *_tool_name_mousewheel(JsonValue *args) { (void)args; return "browser_mouse_wheel"; }
static JsonValue *_tool_params_mousewheel(JsonValue *args) {
    JsonValue *p = json_object();
    JsonValue *dx = get_number_value(args, "dx");
    JsonValue *dy = get_number_value(args, "dy");
    json_object_set(p, "deltaX", json_copy(dx ? dx : json_number(0)));
    json_object_set(p, "deltaY", json_copy(dy ? dy : json_number(0)));
    return p;
}

/* ---- click/dblclick/drag/fill/hover/select/check/uncheck ---- */
static const char *_tool_name_click(JsonValue *args) { (void)args; return "browser_click"; }
static JsonValue *_tool_params_click(JsonValue *args) {
    JsonValue *p = json_object();
    const char *ref = get_str(args, "ref");
    json_object_set(p, "ref", json_string(ref ? ref : ""));
    const char *b = get_str(args, "button");
    if (b) json_object_set(p, "button", json_string(b));
    JsonValue *mod = json_object_get(args, "modifiers");
    if (mod) json_object_set(p, "modifiers", json_copy(mod));
    return p;
}
static JsonValue *_tool_params_dblclick(JsonValue *args) {
    JsonValue *p = _tool_params_click(args);
    json_object_set(p, "doubleClick", json_bool(1));
    return p;
}

static const char *_tool_name_drag(JsonValue *args) { (void)args; return "browser_drag"; }
static JsonValue *_tool_params_drag(JsonValue *args) {
    JsonValue *p = json_object();
    const char *sr = get_str(args, "startRef");
    const char *er = get_str(args, "endRef");
    json_object_set(p, "startRef", json_string(sr ? sr : ""));
    json_object_set(p, "endRef", json_string(er ? er : ""));
    return p;
}

static const char *_tool_name_fill(JsonValue *args) { (void)args; return "browser_type"; }
static JsonValue *_tool_params_fill(JsonValue *args) {
    JsonValue *p = json_object();
    const char *ref = get_str(args, "ref");
    const char *text = get_str(args, "text");
    json_object_set(p, "ref", json_string(ref ? ref : ""));
    json_object_set(p, "text", json_string(text ? text : ""));
    if (get_bool(args, "submit")) json_object_set(p, "submit", json_bool(1));
    return p;
}

static const char *_tool_name_hover(JsonValue *args) { (void)args; return "browser_hover"; }
static JsonValue *_tool_params_hover(JsonValue *args) {
    JsonValue *p = json_object();
    const char *ref = get_str(args, "ref");
    json_object_set(p, "ref", json_string(ref ? ref : ""));
    return p;
}

static const char *_tool_name_select(JsonValue *args) { (void)args; return "browser_select_option"; }
static JsonValue *_tool_params_select(JsonValue *args) {
    JsonValue *p = json_object();
    const char *ref = get_str(args, "ref");
    const char *val = get_str(args, "val");
    json_object_set(p, "ref", json_string(ref ? ref : ""));
    JsonValue *vals = json_array();
    json_array_append(vals, json_string(val ? val : ""));
    json_object_set(p, "values", vals);
    return p;
}

static const char *_tool_name_check(JsonValue *args) { (void)args; return "browser_check"; }
static JsonValue *_tool_params_check(JsonValue *args) {
    JsonValue *p = json_object();
    const char *ref = get_str(args, "ref");
    json_object_set(p, "ref", json_string(ref ? ref : ""));
    return p;
}
static const char *_tool_name_uncheck(JsonValue *args) { (void)args; return "browser_uncheck"; }

/* ---- snapshot ---- */
static const char *_tool_name_snapshot(JsonValue *args) { (void)args; return "browser_snapshot"; }
static JsonValue *_tool_params_snapshot(JsonValue *args) {
    JsonValue *p = json_object();
    const char *f = get_str(args, "filename");
    if (f) json_object_set(p, "filename", json_string(f));
    return p;
}

/* ---- eval ---- */
static const char *_tool_name_eval(JsonValue *args) { (void)args; return "browser_evaluate"; }
static JsonValue *_tool_params_eval(JsonValue *args) {
    JsonValue *p = json_object();
    const char *expr = get_str(args, "expression");
    json_object_set(p, "expression", json_string(expr ? expr : ""));
    const char *ref = get_str(args, "ref");
    if (ref) json_object_set(p, "ref", json_string(ref));
    return p;
}

/* ---- console ---- */
static const char *_tool_name_console(JsonValue *args) {
    return get_bool(args, "clear") ? "browser_console_clear" : "browser_console_messages";
}
static JsonValue *_tool_params_console(JsonValue *args) {
    if (get_bool(args, "clear")) return json_object();
    JsonValue *p = json_object();
    const char *l = get_str(args, "min-level");
    if (l) json_object_set(p, "level", json_string(l));
    return p;
}

/* ---- dialog ---- */
static const char *_tool_name_dialog(JsonValue *args) { (void)args; return "browser_handle_dialog"; }
static JsonValue *_tool_params_dialog_accept(JsonValue *args) {
    JsonValue *p = json_object();
    json_object_set(p, "accept", json_bool(1));
    const char *pt = get_str(args, "prompt");
    if (pt) json_object_set(p, "promptText", json_string(pt));
    return p;
}
static JsonValue *_tool_params_dialog_dismiss(JsonValue *args) {
    (void)args;
    JsonValue *p = json_object();
    json_object_set(p, "accept", json_bool(0));
    return p;
}

/* ---- resize ---- */
static const char *_tool_name_resize(JsonValue *args) { (void)args; return "browser_resize"; }
static JsonValue *_tool_params_resize(JsonValue *args) {
    JsonValue *p = json_object();
    JsonValue *w = get_number_value(args, "w");
    JsonValue *h = get_number_value(args, "h");
    json_object_set(p, "width", json_copy(w ? w : json_number(0)));
    json_object_set(p, "height", json_copy(h ? h : json_number(0)));
    return p;
}

/* ---- tabs ---- */
static const char *_tool_name_tabs(JsonValue *args) { (void)args; return "browser_tabs"; }
static JsonValue *_tool_params_tab_list(JsonValue *args) {
    (void)args;
    JsonValue *p = json_object();
    json_object_set(p, "action", json_string("list"));
    return p;
}
static JsonValue *_tool_params_tab_new(JsonValue *args) {
    JsonValue *p = json_object();
    json_object_set(p, "action", json_string("new"));
    const char *url = get_str(args, "url");
    if (url) json_object_set(p, "url", json_string(url));
    return p;
}
static JsonValue *_tool_params_tab_close(JsonValue *args) {
    JsonValue *p = json_object();
    json_object_set(p, "action", json_string("close"));
    JsonValue *idx = json_object_get(args, "index");
    if (idx) json_object_set(p, "index", json_copy(idx));
    return p;
}
static JsonValue *_tool_params_tab_select(JsonValue *args) {
    JsonValue *p = json_object();
    json_object_set(p, "action", json_string("select"));
    JsonValue *idx = json_object_get(args, "index");
    json_object_set(p, "index", json_copy(idx ? idx : json_number(0)));
    return p;
}

/* ---- screenshot ---- */
static const char *_tool_name_screenshot(JsonValue *args) { (void)args; return "browser_take_screenshot"; }
static JsonValue *_tool_params_screenshot(JsonValue *args) {
    JsonValue *p = json_object();
    const char *r = get_str(args, "ref");
    if (r) json_object_set(p, "ref", json_string(r));
    const char *f = get_str(args, "filename");
    if (f) json_object_set(p, "filename", json_string(f));
    if (get_bool(args, "full-page")) json_object_set(p, "fullPage", json_bool(1));
    return p;
}

/* ---- pdf ---- */
static const char *_tool_name_pdf(JsonValue *args) { (void)args; return "browser_pdf_save"; }
static JsonValue *_tool_params_pdf(JsonValue *args) {
    JsonValue *p = json_object();
    const char *f = get_str(args, "filename");
    if (f) json_object_set(p, "filename", json_string(f));
    return p;
}

/* ---- upload ---- */
static const char *_tool_name_upload(JsonValue *args) { (void)args; return "browser_file_upload"; }
static JsonValue *_tool_params_upload(JsonValue *args) {
    JsonValue *p = json_object();
    const char *ref = get_str(args, "ref");
    const char *file = get_str(args, "file");
    json_object_set(p, "ref", json_string(ref ? ref : ""));
    JsonValue *paths = json_array();
    json_array_append(paths, json_string(file ? file : ""));
    json_object_set(p, "paths", paths);
    return p;
}

/* ---- storage ---- */
static const char *_tool_name_storage_save(JsonValue *args) { (void)args; return "browser_save_storage_state"; }
static JsonValue *_tool_params_storage_save(JsonValue *args) {
    JsonValue *p = json_object();
    const char *f = get_str(args, "filename");
    if (f) json_object_set(p, "filename", json_string(f));
    return p;
}
static const char *_tool_name_storage_load(JsonValue *args) { (void)args; return "browser_load_storage_state"; }
static JsonValue *_tool_params_storage_load(JsonValue *args) {
    JsonValue *p = json_object();
    const char *f = get_str(args, "filename");
    json_object_set(p, "filename", json_string(f ? f : ""));
    return p;
}

/* ---- cookie-list/get/set/delete/clear ---- */
static const char *_tool_name_cookie_list(JsonValue *args) { (void)args; return "browser_save_storage_state"; }
static JsonValue *_tool_params_cookie_list(JsonValue *args) {
    JsonValue *p = json_object();
    const char *d = get_str(args, "domain");
    if (d) json_object_set(p, "domain", json_string(d));
    const char *path = get_str(args, "path");
    if (path) json_object_set(p, "path", json_string(path));
    return p;
}

static const char *_tool_name_cookie_get(JsonValue *args) { (void)args; return "browser_save_storage_state"; }
static JsonValue *_tool_params_cookie_get(JsonValue *args) {
    JsonValue *p = json_object();
    const char *n = get_str(args, "name");
    json_object_set(p, "name", json_string(n ? n : ""));
    return p;
}

static const char *_tool_name_cookie_set(JsonValue *args) { (void)args; return "browser_load_storage_state"; }
static JsonValue *_tool_params_cookie_set(JsonValue *args) {
    JsonValue *p = json_object();
    const char *n = get_str(args, "name");
    const char *v = get_str(args, "value");
    json_object_set(p, "name", json_string(n ? n : ""));
    json_object_set(p, "value", json_string(v ? v : ""));
    const char *d = get_str(args, "domain"); if (d) json_object_set(p, "domain", json_string(d));
    const char *path = get_str(args, "path"); if (path) json_object_set(p, "path", json_string(path));
    const char *exp = get_str(args, "expires"); if (exp) json_object_set(p, "expires", json_string(exp));
    if (get_bool(args, "httpOnly")) json_object_set(p, "httpOnly", json_bool(1));
    if (get_bool(args, "secure")) json_object_set(p, "secure", json_bool(1));
    const char *ss = get_str(args, "sameSite"); if (ss) json_object_set(p, "sameSite", json_string(ss));
    return p;
}

static const char *_tool_name_cookie_delete(JsonValue *args) { (void)args; return "delete_cookies"; }
static JsonValue *_tool_params_cookie_delete(JsonValue *args) {
    JsonValue *p = json_object();
    const char *n = get_str(args, "name");
    json_object_set(p, "name", json_string(n ? n : ""));
    const char *d = get_str(args, "domain"); if (d) json_object_set(p, "domain", json_string(d));
    const char *path = get_str(args, "path"); if (path) json_object_set(p, "path", json_string(path));
    return p;
}

static const char *_tool_name_cookie_clear(JsonValue *args) { (void)args; return "clear_browser_cookies"; }

/* ---- localstorage/sessionstorage (all use browser_evaluate internally) ---- */
static const char *_tool_name_storage_eval(JsonValue *args) { (void)args; return "browser_evaluate"; }
static JsonValue *_tool_params_storage_get(JsonValue *args) {
    JsonValue *p = json_object();
    const char *k = get_str(args, "key");
    json_object_set(p, "key", json_string(k ? k : ""));
    return p;
}
static JsonValue *_tool_params_storage_set(JsonValue *args) {
    JsonValue *p = json_object();
    const char *k = get_str(args, "key");
    const char *v = get_str(args, "value");
    json_object_set(p, "key", json_string(k ? k : ""));
    json_object_set(p, "value", json_string(v ? v : ""));
    return p;
}

/* ---- agent ---- */
static const char *_tool_name_extract(JsonValue *args) { (void)args; return "agent_extract"; }
static JsonValue *_tool_params_extract(JsonValue *args) {
    JsonValue *p = json_object();
    const char *instr = get_str(args, "instruction");
    json_object_set(p, "instruction", json_string(instr ? instr : ""));
    const char *schema = get_str(args, "schema");
    if (schema) json_object_set(p, "schema", json_string(schema));
    return p;
}
static const char *_tool_name_summarize(JsonValue *args) { (void)args; return "agent_summarize"; }
static JsonValue *_tool_params_summarize(JsonValue *args) {
    JsonValue *p = json_object();
    const char *instr = get_str(args, "instruction");
    if (instr) json_object_set(p, "instruction", json_string(instr));
    const char *sel = get_str(args, "selector");
    if (sel) json_object_set(p, "selector", json_string(sel));
    return p;
}
static const char *_tool_name_agent_run(JsonValue *args) { (void)args; return "command_run"; }
static JsonValue *_tool_params_agent_run(JsonValue *args) {
    JsonValue *p = json_object();
    const char *task = get_str(args, "task");
    json_object_set(p, "task", json_string(task ? task : ""));
    return p;
}
static const char *_tool_name_command_status(JsonValue *args) { (void)args; return "command_status"; }
static JsonValue *_tool_params_command_status(JsonValue *args) {
    JsonValue *p = json_object();
    const char *id = get_str(args, "id");
    json_object_set(p, "id", json_string(id ? id : ""));
    return p;
}
static const char *_tool_name_command_result(JsonValue *args) { (void)args; return "command_result"; }
static const char *_tool_name_swarm_create(JsonValue *args) { (void)args; return "open_session"; }
static JsonValue *_tool_params_swarm_create(JsonValue *args) {
    JsonValue *p = json_object();
    const char *pm = get_str(args, "profile-mode");
    if (pm) {
        char *upper = _strdup(pm);
        for (char *c = upper; *c; c++) *c = (char)toupper((unsigned char)*c);
        json_object_set(p, "profileMode", json_string_take(upper));
    } else {
        json_object_set(p, "profileMode", json_string("SEQUENTIAL"));
    }
    const char *mot = get_str(args, "max-open-tabs"); if (mot) json_object_set(p, "maxOpenTabs", json_string(mot));
    const char *mbc = get_str(args, "max-browser-contexts"); if (mbc) json_object_set(p, "maxBrowserContexts", json_string(mbc));
    const char *dm = get_str(args, "display-mode"); if (dm) json_object_set(p, "displayMode", json_string(dm));
    return p;
}
static const char *_tool_name_swarm_submit(JsonValue *args) { (void)args; return "command_run"; }
static JsonValue *_tool_params_swarm_submit(JsonValue *args) {
    JsonValue *p = json_object();
    const char *url = get_str(args, "url"); if (url) json_object_set(p, "url", json_string(url));
    const char *sf = get_str(args, "seed-file"); if (sf) json_object_set(p, "seedFile", json_string(sf));
    const char *sql = get_str(args, "sql"); if (sql) json_object_set(p, "sql", json_string(sql));
    const char *dl = get_str(args, "deadline"); if (dl) json_object_set(p, "deadline", json_string(dl));
    const char *exp = get_str(args, "expires"); if (exp) json_object_set(p, "expires", json_string(exp));
    if (get_bool(args, "refresh")) json_object_set(p, "refresh", json_bool(1));
    if (get_bool(args, "parse")) json_object_set(p, "parse", json_bool(1));
    if (get_bool(args, "store-content")) json_object_set(p, "storeContent", json_bool(1));
    return p;
}
static const char *_tool_name_swarm_query(JsonValue *args) { (void)args; return "swarm_query"; }
static JsonValue *_tool_params_swarm_query(JsonValue *args) {
    JsonValue *p = json_object();
    const char *url = get_str(args, "url"); if (url) json_object_set(p, "url", json_string(url));
    const char *sql = get_str(args, "sql"); if (sql) json_object_set(p, "sql", json_string(sql));
    const char *sf = get_str(args, "seed-file"); if (sf) json_object_set(p, "seedFile", json_string(sf));
    const char *dl = get_str(args, "deadline"); if (dl) json_object_set(p, "deadline", json_string(dl));
    const char *exp = get_str(args, "expires"); if (exp) json_object_set(p, "expires", json_string(exp));
    if (get_bool(args, "refresh")) json_object_set(p, "refresh", json_bool(1));
    return p;
}

/* ---- install params ---- */
static JsonValue *_tool_params_install(JsonValue *args) {
    JsonValue *p = json_object();
    const char *tag = get_str(args, "tag"); if (tag) json_object_set(p, "tag", json_string(tag));
    if (get_bool(args, "force")) json_object_set(p, "force", json_bool(1));
    return p;
}
static JsonValue *_tool_params_uninstall(JsonValue *args) {
    JsonValue *p = json_object();
    if (get_bool(args, "yes")) json_object_set(p, "yes", json_bool(1));
    return p;
}
static JsonValue *_tool_params_upgrade(JsonValue *args) {
    JsonValue *p = json_object();
    const char *tag = get_str(args, "tag"); if (tag) json_object_set(p, "tag", json_string(tag));
    if (get_bool(args, "force")) json_object_set(p, "force", json_bool(1));
    return p;
}
static JsonValue *_tool_params_status(JsonValue *args) {
    JsonValue *p = json_object();
    const char *server = get_str(args, "server"); if (server) json_object_set(p, "server", json_string(server));
    return p;
}

/* ---------------------------------------------------------------------------
 * Command definitions table — arrays defined separately for MSVC compatibility
 * ------------------------------------------------------------------------- */

/* Shared arg/option arrays (reused by multiple commands to reduce duplication) */
static const ArgDef args_empty[] = {{NULL,NULL,0}};
static const ArgDef _args_url_opt[] = {{"url","The URL to navigate to",1}};
static const ArgDef _args_url_req[] = {{"url","The URL to navigate to",0}};
static const ArgDef _args_key_opt[] = {{"key","Name of the key to press",0}};
static const ArgDef _args_key_req_ref_opt[] = {{"key","Name of the key to press or a character to generate",0},{"ref","Optional CSS selector or element reference",1}};
static const ArgDef _args_text_req_ref_opt[] = {{"text","Text to type into the element",0},{"ref","Optional CSS selector or element reference",1}};
static const ArgDef _args_xy[] = {{"x","X coordinate",0},{"y","Y coordinate",0}};
static const ArgDef _args_button_opt[] = {{"button","Button to press, defaults to left",1}};
static const ArgDef _args_dxdy[] = {{"dx","Horizontal scroll delta",0},{"dy","Vertical scroll delta",0}};
static const ArgDef _args_ref_req[] = {{"ref","Exact target element reference from the page snapshot",0}};
static const ArgDef _args_ref_req_button_opt[] = {{"ref","Exact target element reference from the page snapshot",0},{"button","Button to click, defaults to left",1}};
static const ArgDef _args_drag[] = {{"startRef","Exact source element reference",0},{"endRef","Exact target element reference",0}};
static const ArgDef _args_fill[] = {{"ref","Exact target element reference",0},{"text","Text to fill into the element",0}};
static const ArgDef _args_select[] = {{"ref","Exact target element reference",0},{"val","Value to select in the dropdown",0}};
static const ArgDef _args_upload[] = {{"ref","CSS selector or element reference for the file input",0},{"file","The absolute paths to the files to upload",0}};
static const ArgDef _args_expr_ref_opt[] = {{"expression","JavaScript expression or function to evaluate",0},{"ref","Optional CSS selector or snapshot ref",1}};
static const ArgDef _args_prompt_opt[] = {{"prompt","The text of the prompt in case of a prompt dialog",1}};
static const ArgDef _args_wh[] = {{"w","Width of the browser window",0},{"h","Height of the browser window",0}};
static const ArgDef _args_filename_opt[] = {{"filename","Optional file path",1}};
static const ArgDef _args_filename_req[] = {{"filename","Path to a storage-state JSON file",0}};
static const ArgDef _args_name_req[] = {{"name","Cookie name",0}};
static const ArgDef _args_name_val[] = {{"name","Cookie name",0},{"value","Cookie value",0}};
static const ArgDef _args_key_req[] = {{"key","localStorage key",0}};
static const ArgDef _args_key_val[] = {{"key","localStorage key",0},{"value","Value to store",0}};
static const ArgDef _args_instr_req[] = {{"instruction","What data to extract",0}};
static const ArgDef _args_instr_opt[] = {{"instruction","Summarization instruction",1}};
static const ArgDef _args_task_req[] = {{"task","Natural language task for the agent to execute",0}};
static const ArgDef _args_id_req[] = {{"id","Task ID returned by agent run",0}};
static const ArgDef _args_url_opt_swarm[] = {{"url","URL or X-SQL payload to submit",1}};
static const ArgDef _args_url_req_swarm[] = {{"url","Target page URL to load and run the query against",0}};
static const ArgDef _args_tag_opt[] = {{"tag","Release tag to upgrade to",1}};
static const ArgDef _args_minlevel_opt[] = {{"min-level","Level of the console messages to return",1}};
static const ArgDef _args_batch[] = {{"command...","Quoted command strings to execute sequentially",1}};
static const ArgDef _args_url_new_tab[] = {{"url","The URL to navigate to in the new tab",1}};
static const ArgDef _args_index_opt[] = {{"index","Zero-based tab index. If omitted, current tab is closed.",1}};
static const ArgDef _args_index_req[] = {{"index","Zero-based tab index",0}};
static const ArgDef _args_ref_opt_scr[] = {{"ref","Exact target element reference from the page snapshot",1}};

/* Shared option arrays */
static const OptionDef _opts_empty[] = {{NULL,NULL,0,NULL}};
static const OptionDef _opts_open[] = {
    {"headed","Run browser in headed mode",1,NULL},
    {"headless","Run browser in headless mode",1,NULL},
    {"profile","Path to browser profile directory",0,NULL},
    {"profile-mode","Browser profile mode (temporary, sequential, default)",0,NULL},
    {"interact-level","Interaction level for the new session",0,NULL}};
static const OptionDef _opts_install[] = {{"tag","Release tag to install, for example v4.9.3",0,NULL},{"force","Force re-download even when already installed",1,NULL}};
static const OptionDef _opts_uninstall[] = {{"yes","Skip confirmation prompts",1,"y"}};
static const OptionDef _opts_batch[] = {{"bail","Stop on the first command failure",1,NULL},{"json","Read commands as JSON from stdin",1,NULL}};
static const OptionDef _opts_submit[] = {{"submit","Whether to submit entered text (press Enter after)",1,NULL}};
static const OptionDef _opts_modifiers[] = {{"modifiers","Modifier keys to press",0,NULL}};
static const OptionDef _opts_filename[] = {{"filename","Save snapshot to file",0,NULL}};
static const OptionDef _opts_clear[] = {{"clear","Whether to clear the console list",1,NULL}};
static const OptionDef _opts_cookie_list[] = {{"domain","Only include cookies with the exact domain",0,NULL},{"path","Only include cookies with the exact path",0,NULL}};
static const OptionDef _opts_cookie_set[] = {
    {"domain","Cookie domain",0,NULL},{"path","Cookie path",0,NULL},
    {"expires","Cookie expiration Unix timestamp",0,NULL},
    {"httpOnly","Mark the cookie as HttpOnly",1,NULL},
    {"secure","Mark the cookie as Secure",1,NULL},
    {"sameSite","Cookie SameSite policy (Strict, Lax, None)",0,NULL}};
static const OptionDef _opts_cookie_delete[] = {{"domain","Cookie domain override",0,NULL},{"path","Cookie path override",0,NULL}};
static const OptionDef _opts_screenshot[] = {{"filename","File name to save the screenshot to",0,NULL},{"full-page","When true, takes a screenshot of the full scrollable page",1,NULL}};
static const OptionDef _opts_pdf[] = {{"filename","File name to save the pdf to",0,NULL}};
static const OptionDef _opts_list[] = {{"all","List all browser sessions across all workspaces",1,NULL}};
static const OptionDef _opts_force[] = {{"force","Force re-download even when the requested version is already installed",1,NULL}};
static const OptionDef _opts_server[] = {{"server","Server URL to check",0,NULL}};
static const OptionDef _opts_extract[] = {{"schema","JSON schema to constrain the extracted data structure",0,NULL}};
static const OptionDef _opts_summarize[] = {{"selector","CSS selector to limit the scope of summarization",0,NULL}};
static const OptionDef _opts_swarm_create[] = {
    {"profile-mode","Browser profile mode (SEQUENTIAL or TEMPORARY)",0,NULL},
    {"max-open-tabs","Maximum open tabs per browser context",0,NULL},
    {"max-browser-contexts","Number of isolated browser environments",0,NULL},
    {"display-mode","Display mode: GUI, HEADLESS, SUPERVISED",0,NULL}};
static const OptionDef _opts_swarm_submit[] = {
    {"seed-file","File containing URLs to submit, one per line",0,NULL},
    {"sql","X-SQL query to execute against the page",0,NULL},
    {"deadline","Deadline for task completion (ISO 8601)",0,NULL},
    {"expires","Cache expiration duration (e.g. 1d, 1h)",0,NULL},
    {"refresh","Force a fresh fetch, ignoring cache",1,NULL},
    {"parse","Parse page immediately after fetching",1,NULL},
    {"store-content","Persist page content to storage",1,NULL}};
static const OptionDef _opts_swarm_query[] = {
    {"sql","X-SQL query to execute",0,NULL},
    {"seed-file","File containing URLs to submit, one per line",0,NULL},
    {"deadline","Deadline for task completion (ISO 8601)",0,NULL},
    {"expires","Cache expiration duration (e.g. 1d, 1h)",0,NULL},
    {"refresh","Force a fresh fetch, ignoring cache",1,NULL}};

/* Count helper */
#define ALEN(a) (sizeof(a)/sizeof((a)[0]))

const CommandDef *all_commands(int *out_count) {
    static const CommandDef commands[] = {
        /* ---- Browsers / Core ---- */
        {"open", "Open a browser session or refresh the saved one if it is no longer active",
         CAT_BROWSERS, 0, 0, _args_url_opt, ALEN(_args_url_opt), _opts_open, ALEN(_opts_open),
         _tool_name_open, _tool_params_open},
        {"close", "Close the browser", CAT_BROWSERS, 0, 0, NULL, 0, NULL, 0,
         _tool_name_empty, _tool_params_empty},
        {"install", "Install the self-contained Browser4 runtime bundle",
         CAT_INSTALL, 0, 0, NULL, 0, _opts_install, ALEN(_opts_install),
         _tool_name_empty, _tool_params_install},
        {"uninstall", "Remove all globally installed browser4-cli and its runtime data",
         CAT_INSTALL, 0, 0, NULL, 0, _opts_uninstall, ALEN(_opts_uninstall),
         _tool_name_empty, _tool_params_uninstall},
        {"batch", "Execute multiple commands in one invocation",
         CAT_CORE, 0, 0, _args_batch, ALEN(_args_batch), _opts_batch, ALEN(_opts_batch),
         _tool_name_empty, _tool_params_empty},
        {"goto", "Navigate to a URL, auto-opening or refreshing the session when needed",
         CAT_NAVIGATION, 0, 1, _args_url_req, ALEN(_args_url_req), NULL, 0,
         _tool_name_goto, _tool_params_goto},
        {"go-back", "Go back to the previous page", CAT_NAVIGATION, 0, 1,
         NULL, 0, NULL, 0, _tool_name_go_back, _tool_params_nav_empty},
        {"go-forward", "Go forward to the next page", CAT_NAVIGATION, 0, 1,
         NULL, 0, NULL, 0, _tool_name_go_forward, _tool_params_nav_empty},
        {"reload", "Reload the current page", CAT_NAVIGATION, 0, 1,
         NULL, 0, NULL, 0, _tool_name_reload, _tool_params_nav_empty},
        /* ---- Keyboard ---- */
        {"press", "Press a key on the focused element or an optional target ref",
         CAT_KEYBOARD, 0, 1, _args_key_req_ref_opt, ALEN(_args_key_req_ref_opt), NULL, 0,
         _tool_name_press, _tool_params_press},
        {"type", "Type text into the focused element or an optional target ref",
         CAT_KEYBOARD, 0, 1, _args_text_req_ref_opt, ALEN(_args_text_req_ref_opt),
         _opts_submit, ALEN(_opts_submit),
         _tool_name_type, _tool_params_type},
        {"keydown", "Press a key down on the keyboard", CAT_KEYBOARD, 0, 1,
         _args_key_opt, ALEN(_args_key_opt), NULL, 0, _tool_name_keydown, _tool_params_key},
        {"keyup", "Press a key up on the keyboard", CAT_KEYBOARD, 0, 1,
         _args_key_opt, ALEN(_args_key_opt), NULL, 0, _tool_name_keyup, _tool_params_key},
        /* ---- Mouse ---- */
        {"mousemove", "Move mouse to a given position", CAT_MOUSE, 0, 1,
         _args_xy, ALEN(_args_xy), NULL, 0, _tool_name_mousemove, _tool_params_mousemove},
        {"mousedown", "Press mouse down", CAT_MOUSE, 0, 1,
         _args_button_opt, ALEN(_args_button_opt), NULL, 0, _tool_name_mousedown, _tool_params_mouse_button},
        {"mouseup", "Press mouse up", CAT_MOUSE, 0, 1,
         _args_button_opt, ALEN(_args_button_opt), NULL, 0, _tool_name_mouseup, _tool_params_mouse_button},
        {"mousewheel", "Scroll mouse wheel", CAT_MOUSE, 0, 1,
         _args_dxdy, ALEN(_args_dxdy), NULL, 0, _tool_name_mousewheel, _tool_params_mousewheel},
        {"click", "Perform click on a web page", CAT_MOUSE, 0, 1,
         _args_ref_req_button_opt, ALEN(_args_ref_req_button_opt),
         _opts_modifiers, ALEN(_opts_modifiers),
         _tool_name_click, _tool_params_click},
        {"dblclick", "Perform double click on a web page", CAT_MOUSE, 0, 1,
         _args_ref_req_button_opt, ALEN(_args_ref_req_button_opt),
         _opts_modifiers, ALEN(_opts_modifiers),
         _tool_name_click, _tool_params_dblclick},
        {"drag", "Perform drag and drop between two elements", CAT_MOUSE, 0, 1,
         _args_drag, ALEN(_args_drag), NULL, 0, _tool_name_drag, _tool_params_drag},
        {"fill", "Fill text into editable element", CAT_KEYBOARD, 0, 1,
         _args_fill, ALEN(_args_fill), _opts_submit, ALEN(_opts_submit),
         _tool_name_fill, _tool_params_fill},
        {"hover", "Hover over element on page", CAT_MOUSE, 0, 1,
         _args_ref_req, ALEN(_args_ref_req), NULL, 0, _tool_name_hover, _tool_params_hover},
        {"select", "Select an option in a dropdown", CAT_CORE, 0, 1,
         _args_select, ALEN(_args_select), NULL, 0, _tool_name_select, _tool_params_select},
        {"upload", "Upload one or multiple files", CAT_CORE, 1, 1,
         _args_upload, ALEN(_args_upload), NULL, 0, _tool_name_upload, _tool_params_upload},
        {"check", "Check a checkbox or radio button", CAT_CORE, 0, 1,
         _args_ref_req, ALEN(_args_ref_req), NULL, 0, _tool_name_check, _tool_params_check},
        {"uncheck", "Uncheck a checkbox or radio button", CAT_CORE, 0, 1,
         _args_ref_req, ALEN(_args_ref_req), NULL, 0, _tool_name_uncheck, _tool_params_check},
        {"snapshot", "Capture page snapshot to obtain element ref", CAT_CORE, 0, 1,
         NULL, 0, _opts_filename, ALEN(_opts_filename),
         _tool_name_snapshot, _tool_params_snapshot},
        {"eval", "Evaluate JavaScript expression on page or element", CAT_CORE, 0, 1,
         _args_expr_ref_opt, ALEN(_args_expr_ref_opt), NULL, 0, _tool_name_eval, _tool_params_eval},
        {"console", "List console messages", CAT_DEVTOOLS, 1, 0,
         _args_minlevel_opt, ALEN(_args_minlevel_opt), _opts_clear, ALEN(_opts_clear),
         _tool_name_console, _tool_params_console},
        {"dialog-accept", "Accept a dialog", CAT_CORE, 0, 1,
         _args_prompt_opt, ALEN(_args_prompt_opt), NULL, 0, _tool_name_dialog, _tool_params_dialog_accept},
        {"dialog-dismiss", "Dismiss a dialog", CAT_CORE, 0, 1,
         NULL, 0, NULL, 0, _tool_name_dialog, _tool_params_dialog_dismiss},
        {"resize", "Resize the browser window", CAT_CORE, 0, 1,
         _args_wh, ALEN(_args_wh), NULL, 0, _tool_name_resize, _tool_params_resize},
        {"delete-data", "Delete session data", CAT_CORE, 0, 0,
         NULL, 0, NULL, 0, _tool_name_empty, _tool_params_empty},
        /* ---- Storage ---- */
        {"state-save", "Save cookies and localStorage to a JSON file", CAT_STORAGE, 0, 0,
         _args_filename_opt, ALEN(_args_filename_opt), NULL, 0,
         _tool_name_storage_save, _tool_params_storage_save},
        {"state-load", "Load cookies and localStorage from a JSON file", CAT_STORAGE, 0, 0,
         _args_filename_req, ALEN(_args_filename_req), NULL, 0,
         _tool_name_storage_load, _tool_params_storage_load},
        {"cookie-list", "List browser cookies", CAT_STORAGE, 0, 0,
         NULL, 0, _opts_cookie_list, ALEN(_opts_cookie_list),
         _tool_name_cookie_list, _tool_params_cookie_list},
        {"cookie-get", "Get a cookie by name", CAT_STORAGE, 0, 0,
         _args_name_req, ALEN(_args_name_req), NULL, 0,
         _tool_name_cookie_get, _tool_params_cookie_get},
        {"cookie-set", "Set a browser cookie", CAT_STORAGE, 0, 0,
         _args_name_val, ALEN(_args_name_val), _opts_cookie_set, ALEN(_opts_cookie_set),
         _tool_name_cookie_set, _tool_params_cookie_set},
        {"cookie-delete", "Delete a browser cookie by name", CAT_STORAGE, 0, 0,
         _args_name_req, ALEN(_args_name_req), _opts_cookie_delete, ALEN(_opts_cookie_delete),
         _tool_name_cookie_delete, _tool_params_cookie_delete},
        {"cookie-clear", "Clear all browser cookies", CAT_STORAGE, 0, 0,
         NULL, 0, NULL, 0, _tool_name_cookie_clear, _tool_params_nav_empty},
        {"localstorage-list", "List localStorage entries", CAT_STORAGE, 0, 0,
         NULL, 0, NULL, 0, _tool_name_storage_eval, _tool_params_nav_empty},
        {"localstorage-get", "Get a localStorage value by key", CAT_STORAGE, 0, 0,
         _args_key_req, ALEN(_args_key_req), NULL, 0,
         _tool_name_storage_eval, _tool_params_storage_get},
        {"localstorage-set", "Set a localStorage value", CAT_STORAGE, 0, 0,
         _args_key_val, ALEN(_args_key_val), NULL, 0,
         _tool_name_storage_eval, _tool_params_storage_set},
        {"localstorage-delete", "Delete a localStorage entry", CAT_STORAGE, 0, 0,
         _args_key_req, ALEN(_args_key_req), NULL, 0,
         _tool_name_storage_eval, _tool_params_storage_get},
        {"localstorage-clear", "Clear localStorage", CAT_STORAGE, 0, 0,
         NULL, 0, NULL, 0, _tool_name_storage_eval, _tool_params_nav_empty},
        {"sessionstorage-list", "List sessionStorage entries", CAT_STORAGE, 0, 0,
         NULL, 0, NULL, 0, _tool_name_storage_eval, _tool_params_nav_empty},
        {"sessionstorage-get", "Get a sessionStorage value by key", CAT_STORAGE, 0, 0,
         _args_key_req, ALEN(_args_key_req), NULL, 0,
         _tool_name_storage_eval, _tool_params_storage_get},
        {"sessionstorage-set", "Set a sessionStorage value", CAT_STORAGE, 0, 0,
         _args_key_val, ALEN(_args_key_val), NULL, 0,
         _tool_name_storage_eval, _tool_params_storage_set},
        {"sessionstorage-delete", "Delete a sessionStorage entry", CAT_STORAGE, 0, 0,
         _args_key_req, ALEN(_args_key_req), NULL, 0,
         _tool_name_storage_eval, _tool_params_storage_get},
        {"sessionstorage-clear", "Clear sessionStorage", CAT_STORAGE, 0, 0,
         NULL, 0, NULL, 0, _tool_name_storage_eval, _tool_params_nav_empty},
        /* ---- Export ---- */
        {"screenshot", "Screenshot of the current page or element", CAT_EXPORT, 0, 1,
         _args_ref_opt_scr, ALEN(_args_ref_opt_scr), _opts_screenshot, ALEN(_opts_screenshot),
         _tool_name_screenshot, _tool_params_screenshot},
        {"pdf", "Save page as PDF", CAT_EXPORT, 1, 1,
         NULL, 0, _opts_pdf, ALEN(_opts_pdf), _tool_name_pdf, _tool_params_pdf},
        /* ---- Tabs ---- */
        {"tab-list", "List all tabs", CAT_TABS, 0, 1,
         NULL, 0, NULL, 0, _tool_name_tabs, _tool_params_tab_list},
        {"tab-new", "Create a new tab", CAT_TABS, 0, 1,
         _args_url_new_tab, ALEN(_args_url_new_tab), NULL, 0,
         _tool_name_tabs, _tool_params_tab_new},
        {"tab-close", "Close a browser tab", CAT_TABS, 0, 1,
         _args_index_opt, ALEN(_args_index_opt), NULL, 0,
         _tool_name_tabs, _tool_params_tab_close},
        {"tab-select", "Select a browser tab", CAT_TABS, 0, 1,
         _args_index_req, ALEN(_args_index_req), NULL, 0,
         _tool_name_tabs, _tool_params_tab_select},
        /* ---- Browsers / Sessions ---- */
        {"list", "List browser sessions with their status and next-open behavior", CAT_BROWSERS, 0, 0,
         NULL, 0, _opts_list, ALEN(_opts_list), _tool_name_empty, _tool_params_empty},
        {"close-all", "Close all browser sessions without stopping the Browser4 backend", CAT_BROWSERS, 0, 0,
         NULL, 0, NULL, 0, _tool_name_empty, _tool_params_empty},
        {"kill-all", "Forcefully stop the Browser4 backend and kill Browser4 browser processes", CAT_BROWSERS, 0, 0,
         NULL, 0, NULL, 0, _tool_name_empty, _tool_params_empty},
        {"upgrade", "Upgrade Browser4 to the latest version (or a specified release tag)", CAT_BROWSERS, 0, 0,
         _args_tag_opt, ALEN(_args_tag_opt), _opts_force, ALEN(_opts_force),
         _tool_name_empty, _tool_params_upgrade},
        {"stop", "Gracefully stop the Browser4 server", CAT_BROWSERS, 0, 0,
         NULL, 0, NULL, 0, _tool_name_empty, _tool_params_empty},
        {"status", "Show Browser4 server status (version, port, health)", CAT_BROWSERS, 0, 0,
         NULL, 0, _opts_server, ALEN(_opts_server), _tool_name_empty, _tool_params_status},
        /* ---- Agent ---- */
        {"extract", "Extract structured data from the current page", CAT_AGENT, 0, 0,
         _args_instr_req, ALEN(_args_instr_req), _opts_extract, ALEN(_opts_extract),
         _tool_name_extract, _tool_params_extract},
        {"summarize", "Summarize page content using AI", CAT_AGENT, 0, 0,
         _args_instr_opt, ALEN(_args_instr_opt), _opts_summarize, ALEN(_opts_summarize),
         _tool_name_summarize, _tool_params_summarize},
        {"agent-run", "Run an autonomous agent task (async, returns task ID)", CAT_AGENT, 0, 0,
         _args_task_req, ALEN(_args_task_req), NULL, 0,
         _tool_name_agent_run, _tool_params_agent_run},
        {"agent-status", "Check the status of a running agent task", CAT_AGENT, 0, 0,
         _args_id_req, ALEN(_args_id_req), NULL, 0,
         _tool_name_command_status, _tool_params_command_status},
        {"agent-result", "Get the result of a completed agent task", CAT_AGENT, 0, 0,
         _args_id_req, ALEN(_args_id_req), NULL, 0,
         _tool_name_command_result, _tool_params_command_status},
        /* ---- Swarm ---- */
        {"swarm-create", "Create a swarm scrape session with parallel browser contexts", CAT_SWARM, 0, 0,
         NULL, 0, _opts_swarm_create, ALEN(_opts_swarm_create),
         _tool_name_swarm_create, _tool_params_swarm_create},
        {"swarm-submit", "Submit URL(s) or X-SQL payloads as scrape jobs", CAT_SWARM, 0, 0,
         _args_url_opt_swarm, ALEN(_args_url_opt_swarm),
         _opts_swarm_submit, ALEN(_opts_swarm_submit),
         _tool_name_swarm_submit, _tool_params_swarm_submit},
        {"swarm-query", "Submit an X-SQL query to extract structured data from a loaded webpage", CAT_SWARM, 0, 0,
         _args_url_req_swarm, ALEN(_args_url_req_swarm),
         _opts_swarm_query, ALEN(_opts_swarm_query),
         _tool_name_swarm_query, _tool_params_swarm_query},
        {"swarm-status", "Check the status of a scrape job", CAT_SWARM, 0, 0,
         _args_id_req, ALEN(_args_id_req), NULL, 0,
         _tool_name_command_status, _tool_params_command_status},
        {"swarm-result", "Get the result of a completed scrape job", CAT_SWARM, 0, 0,
         _args_id_req, ALEN(_args_id_req), NULL, 0,
         _tool_name_command_result, _tool_params_command_status},
    };

    *out_count = sizeof(commands) / sizeof(CommandDef);
    return commands;
}

const CommandDef *find_command(const char *name) {
    if (!name) return NULL;
    int count = 0;
    const CommandDef *cmds = all_commands(&count);
    for (int i = 0; i < count; i++) {
        if (strcmp(cmds[i].name, name) == 0) return &cmds[i];
    }
    return NULL;
}
