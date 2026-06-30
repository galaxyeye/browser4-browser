#include "http.h"
#include "state.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "ws2_32.lib")
#endif

#define DEFAULT_TIMEOUT_SECS  30
#define BATCH_TIMEOUT_SECS   120

/* ---------------------------------------------------------------------------
 * HttpClient
 * ------------------------------------------------------------------------- */

struct HttpClient {
    HINTERNET session;
    DWORD     timeout_ms;
};

HttpClient *http_client_new(void) {
    HttpClient *c = (HttpClient *)calloc(1, sizeof(HttpClient));
    if (!c) return NULL;
    c->timeout_ms = DEFAULT_TIMEOUT_SECS * 1000;
    c->session = WinHttpOpen(L"browser4-cli/1.0",
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (c->session) {
        WinHttpSetTimeouts(c->session, c->timeout_ms, c->timeout_ms,
                          c->timeout_ms, c->timeout_ms);
    }
    return c;
}

HttpClient *http_client_new_batch(void) {
    HttpClient *c = (HttpClient *)calloc(1, sizeof(HttpClient));
    if (!c) return NULL;
    c->timeout_ms = BATCH_TIMEOUT_SECS * 1000;
    c->session = WinHttpOpen(L"browser4-cli/1.0",
                              WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                              WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
    if (c->session) {
        WinHttpSetTimeouts(c->session, c->timeout_ms, c->timeout_ms,
                          c->timeout_ms, c->timeout_ms);
    }
    return c;
}

void http_client_free(HttpClient *client) {
    if (client) {
        if (client->session) WinHttpCloseHandle(client->session);
        free(client);
    }
}

/* ---------------------------------------------------------------------------
 * Internal URL parsing
 * ------------------------------------------------------------------------- */

typedef struct {
    wchar_t host[256];
    int     port;
    wchar_t path[1024];
    int     is_https;
} ParsedUrl;

static int parse_url(const char *url_str, ParsedUrl *out) {
    memset(out, 0, sizeof(*out));
    out->port = 8182;

    const char *p = url_str;

    /* Scheme */
    if (strncmp(p, "https://", 8) == 0) {
        out->is_https = 1;
        p += 8;
        out->port = 443;
    } else if (strncmp(p, "http://", 7) == 0) {
        out->is_https = 0;
        p += 7;
    }

    /* Host */
    const char *host_start = p;
    while (*p && *p != ':' && *p != '/' && *p != '\\') p++;

    size_t host_len = p - host_start;
    if (host_len >= 255) host_len = 254;
    MultiByteToWideChar(CP_UTF8, 0, host_start, (int)host_len, out->host, 255);
    out->host[host_len] = L'\0';

    /* Port */
    if (*p == ':') {
        p++;
        out->port = atoi(p);
        while (*p && isdigit((unsigned char)*p)) p++;
    }

    /* Path */
    if (*p == '/' || *p == '\\') {
        /* Include the rest as the path */
        MultiByteToWideChar(CP_UTF8, 0, p, -1, out->path, 1023);
    } else {
        wcscpy(out->path, L"/");
    }

    return 0;
}

/* ---------------------------------------------------------------------------
 * Internal: read full response body
 * ------------------------------------------------------------------------- */

static char *read_response_body(HINTERNET request, DWORD *out_size) {
    char *body = NULL;
    DWORD total = 0;
    DWORD cap = 0;

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) break;
        if (available == 0) break;

        if (total + available > cap) {
            cap = total + available + 4096;
            char *new_body = (char *)realloc(body, cap + 1);
            if (!new_body) { free(body); return NULL; }
            body = new_body;
        }

        DWORD read = 0;
        if (!WinHttpReadData(request, body + total, available, &read)) break;
        total += read;
    }

    if (body) body[total] = '\0';
    if (out_size) *out_size = total;
    return body ? body : _strdup("");
}

/* ---------------------------------------------------------------------------
 * Internal: send HTTP request and get response
 * ------------------------------------------------------------------------- */

static char *http_request(
    HttpClient *client,
    const char *base_url,
    const char *url_path,
    const char *method,
    const char *content_type,
    const char *body,
    DWORD body_len,
    char **error_msg)
{
    if (!client || !client->session) {
        if (error_msg) *error_msg = _strdup("HTTP client not initialised");
        return NULL;
    }

    /* Build full URL */
    char full_url[2048];
    size_t base_len = strlen(base_url);
    while (base_len > 0 && (base_url[base_len-1] == '/' || base_url[base_len-1] == '\\')) base_len--;
    const char *path_start = url_path;
    while (*path_start == '/' || *path_start == '\\') path_start++;
    snprintf(full_url, sizeof(full_url), "%.*s/%s", (int)base_len, base_url, path_start);

    ParsedUrl parsed;
    if (parse_url(full_url, &parsed) != 0) {
        if (error_msg) *error_msg = _strdup("Failed to parse URL");
        return NULL;
    }

    HINTERNET connect = WinHttpConnect(client->session, parsed.host,
                                        parsed.port, 0);
    if (!connect) {
        if (error_msg) *error_msg = _strdup("Failed to connect to server");
        return NULL;
    }

    DWORD flags = parsed.is_https ? WINHTTP_FLAG_SECURE : 0;
    wchar_t wmethod[16];
    MultiByteToWideChar(CP_UTF8, 0, method, -1, wmethod, 16);

    HINTERNET request = WinHttpOpenRequest(connect, wmethod, parsed.path,
                                            NULL, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) {
        WinHttpCloseHandle(connect);
        if (error_msg) *error_msg = _strdup("Failed to create HTTP request");
        return NULL;
    }

    /* Set content-type header if provided */
    if (content_type) {
        wchar_t wct[256];
        MultiByteToWideChar(CP_UTF8, 0, content_type, -1, wct, 256);
        WinHttpAddRequestHeaders(request, wct, (DWORD)wcslen(wct),
                                  WINHTTP_ADDREQ_FLAG_ADD);
    }

    /* Send request */
    BOOL sent;
    if (body && body_len > 0) {
        /* Need to construct the full header with Content-Type */
        wchar_t headers[512];
        if (content_type) {
            swprintf(headers, 512, L"Content-Type: %hs\r\n", content_type);
        } else {
            wcscpy(headers, L"");
        }
        sent = WinHttpSendRequest(request, headers, (DWORD)wcslen(headers),
                                   (LPVOID)body, body_len, body_len, 0);
    } else {
        sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    }

    if (!sent) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        if (err == ERROR_WINHTTP_TIMEOUT) {
            if (error_msg) *error_msg = _strdup("HTTP request timed out");
        } else {
            char buf[256];
            snprintf(buf, sizeof(buf), "HTTP request failed (error %lu)", err);
            if (error_msg) *error_msg = _strdup(buf);
        }
        return NULL;
    }

    /* Receive response */
    if (!WinHttpReceiveResponse(request, NULL)) {
        DWORD err = GetLastError();
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);

        if (err == ERROR_WINHTTP_TIMEOUT) {
            if (error_msg) *error_msg = _strdup("HTTP request timed out waiting for response");
        } else {
            /* Try to get status code even on error */
            DWORD status = 0;
            DWORD status_size = sizeof(status);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                               WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX);
            char buf[256];
            snprintf(buf, sizeof(buf), "HTTP request failed with status %lu", status);
            if (error_msg) *error_msg = _strdup(buf);
        }
        return NULL;
    }

    /* Get HTTP status code */
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
                        WINHTTP_NO_HEADER_INDEX);

    /* Read response body */
    char *response_body = read_response_body(request, NULL);

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);

    if (status >= 400) {
        if (error_msg) {
            char buf[512];
            snprintf(buf, sizeof(buf), "HTTP request failed with status %lu%s%s",
                     status,
                     response_body && response_body[0] ? ": " : "",
                     response_body ? response_body : "");
            *error_msg = _strdup(buf);
        }
        free(response_body);
        return NULL;
    }

    return response_body;
}

/* ---------------------------------------------------------------------------
 * MCP Tool Calls
 * ------------------------------------------------------------------------- */

char *call_tool(HttpClient *client, const char *base_url,
                const char *tool_name, JsonValue *args,
                char **error_msg)
{
    /* Normalize refs before sending */
    normalize_refs(args);

    /* Build request body: {"tool": "...", "arguments": {...}} */
    JsonValue *body_obj = json_object();
    json_object_set(body_obj, "tool", json_string(tool_name));
    json_object_set(body_obj, "arguments", json_copy(args));

    char *body_str = json_serialize(body_obj);
    json_free(body_obj);

    char *response = http_request(client, base_url, "/mcp/call-tool",
                                   "POST", "application/json",
                                   body_str, (DWORD)strlen(body_str),
                                   error_msg);
    free(body_str);

    if (!response) return NULL;

    /* Parse the MCP response */
    char *parse_err = NULL;
    JsonValue *data = json_parse(response, &parse_err);
    free(response);

    if (!data) {
        if (error_msg) {
            char buf[512];
            snprintf(buf, sizeof(buf), "Failed to parse MCP response JSON: %s",
                     parse_err ? parse_err : "unknown error");
            *error_msg = _strdup(buf);
        }
        free(parse_err);
        return NULL;
    }

    /* Check for isError */
    if (json_obj_get_bool(data, "isError", 0)) {
        JsonValue *content = json_object_get(data, "content");
        const char *err_text = NULL;
        if (content && content->type == JSON_ARRAY && json_array_size(content) > 0) {
            JsonValue *first = json_array_get(content, 0);
            err_text = json_obj_get_str(first, "text", NULL);
        }
        if (error_msg) *error_msg = _strdup(err_text ? err_text : "Unknown MCP error");
        json_free(data);
        return NULL;
    }

    /* Extract text payload */
    char *result = extract_mcp_text_payload(data);
    json_free(data);

    if (!result) {
        if (error_msg) *error_msg = _strdup("MCP response did not contain a readable payload.");
    }
    return result;
}

char *extract_mcp_text_payload(const JsonValue *data) {
    if (!data) return NULL;

    /* Plain string */
    if (data->type == JSON_STRING) {
        return _strdup(data->data.string_val);
    }

    /* content array */
    JsonValue *content = json_object_get(data, "content");
    if (content && content->type == JSON_ARRAY) {
        for (size_t i = 0; i < json_array_size(content); i++) {
            JsonValue *item = json_array_get(content, i);
            const char *text = json_obj_get_str(item, "text", NULL);
            if (text) return _strdup(text);
            JsonValue *json_payload = json_object_get(item, "json");
            if (json_payload) {
                return json_serialize(json_payload);
            }
        }
    }

    /* structuredContent */
    JsonValue *structured = json_object_get(data, "structuredContent");
    if (structured) {
        return json_serialize(structured);
    }

    /* Object/array fallback */
    if (data->type == JSON_OBJECT || data->type == JSON_ARRAY) {
        return json_serialize(data);
    }

    return NULL;
}

char *extract_http_text_payload(const char *response_text) {
    if (!response_text) return _strdup("");

    const char *trimmed = response_text;
    while (*trimmed && isspace((unsigned char)*trimmed)) trimmed++;

    if (!*trimmed) return _strdup("");

    /* Try parsing as JSON */
    char *parse_err = NULL;
    JsonValue *val = json_parse(trimmed, &parse_err);
    if (val) {
        char *result;
        if (val->type == JSON_STRING) {
            result = _strdup(val->data.string_val);
        } else {
            result = json_serialize(val);
        }
        json_free(val);
        return result;
    }
    free(parse_err);

    /* Return as-is (trimmed) */
    size_t len = strlen(trimmed);
    while (len > 0 && isspace((unsigned char)trimmed[len - 1])) len--;
    char *result = (char *)malloc(len + 1);
    memcpy(result, trimmed, len);
    result[len] = '\0';
    return result;
}

/* ---------------------------------------------------------------------------
 * Command APIs
 * ------------------------------------------------------------------------- */

char *submit_plain_command(HttpClient *client, const char *base_url,
                           const char *command, int async_mode,
                           char **error_msg)
{
    JsonValue *args = json_object();
    json_object_set(args, "command", json_string(command));
    json_object_set(args, "async", json_bool(async_mode));

    return call_tool(client, base_url, "command_run", args, error_msg);
}

char *get_command_status(HttpClient *client, const char *base_url,
                         const char *task_id, char **error_msg)
{
    JsonValue *args = json_object();
    json_object_set(args, "id", json_string(task_id));

    return call_tool(client, base_url, "command_status", args, error_msg);
}

char *get_command_result(HttpClient *client, const char *base_url,
                         const char *task_id, char **error_msg)
{
    JsonValue *args = json_object();
    json_object_set(args, "id", json_string(task_id));

    return call_tool(client, base_url, "command_result", args, error_msg);
}

/* ---------------------------------------------------------------------------
 * Swarm APIs
 * ------------------------------------------------------------------------- */

char *submit_swarm_payload(HttpClient *client, const char *base_url,
                           const char *payload, char **error_msg)
{
    char *response = http_request(client, base_url, "/api/swarm/submit",
                                   "POST", "text/plain; charset=utf-8",
                                   payload, (DWORD)strlen(payload),
                                   error_msg);
    if (response) {
        char *extracted = extract_http_text_payload(response);
        free(response);
        return extracted;
    }
    return NULL;
}

char *submit_swarm_query(HttpClient *client, const char *base_url,
                         JsonValue *query, char **error_msg)
{
    char *body = json_serialize(query);

    char *response = http_request(client, base_url, "/api/swarm/query",
                                   "POST", "application/json; charset=utf-8",
                                   body, (DWORD)strlen(body),
                                   error_msg);
    free(body);

    if (response) {
        char *extracted = extract_http_text_payload(response);
        free(response);
        return extracted;
    }
    return NULL;
}

char *get_swarm_status(HttpClient *client, const char *base_url,
                       const char *task_id, char **error_msg)
{
    char path[512];
    snprintf(path, sizeof(path), "/api/swarm/%s/status", task_id);

    char *response = http_request(client, base_url, path, "GET", NULL, NULL, 0, error_msg);
    if (response) {
        char *extracted = extract_http_text_payload(response);
        free(response);
        return extracted;
    }
    return NULL;
}

char *get_swarm_result(HttpClient *client, const char *base_url,
                       const char *task_id, char **error_msg)
{
    char path[512];
    snprintf(path, sizeof(path), "/api/swarm/%s/result", task_id);

    char *response = http_request(client, base_url, path, "GET", NULL, NULL, 0, error_msg);
    if (response) {
        char *extracted = extract_http_text_payload(response);
        free(response);
        return extracted;
    }
    return NULL;
}

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

int is_stale_session_error(const char *message) {
    if (!message) return 0;
    /* Case-insensitive substring match */
    char *lower = _strdup(message);
    for (char *p = lower; *p; p++) *p = (char)tolower((unsigned char)*p);

    int result = strstr(lower, "cannot find context with specified id") != NULL
              || strstr(lower, "invalid session id") != NULL
              || strstr(lower, "session not found") != NULL
              || strstr(lower, "session does not exist") != NULL
              || strstr(lower, "target closed") != NULL
              || strstr(lower, "session closed") != NULL;

    free(lower);
    return result;
}

int is_backend_unreachable_error(const char *message) {
    if (!message) return 0;
    char *lower = _strdup(message);
    for (char *p = lower; *p; p++) *p = (char)tolower((unsigned char)*p);

    int result = strstr(lower, "connection refused") != NULL
              || strstr(lower, "error sending request") != NULL
              || strstr(lower, "tcp connect error") != NULL
              || strstr(lower, "failed to connect") != NULL
              || strstr(lower, "dns error") != NULL
              || strstr(lower, "timed out") != NULL;

    free(lower);
    return result;
}

int is_timeout_error(const char *message) {
    if (!message) return 0;
    char *lower = _strdup(message);
    for (char *p = lower; *p; p++) *p = (char)tolower((unsigned char)*p);

    int result = strstr(lower, "timed out") != NULL
              || strstr(lower, "deadline has elapsed") != NULL;

    free(lower);
    return result;
}

void normalize_refs(JsonValue *args) {
    if (!args || args->type != JSON_OBJECT) return;

    static const char *ref_keys[] = {"selector", "ref", "startRef", "endRef"};
    for (int i = 0; i < 4; i++) {
        JsonValue *val = json_object_get(args, ref_keys[i]);
        if (val && val->type == JSON_STRING) {
            char *resolved = resolve_ref(val->data.string_val);
            json_object_set(args, ref_keys[i], json_string_take(resolved));
        }
    }
}
