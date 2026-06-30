#ifndef BROWSER4_CLI_HTTP_H
#define BROWSER4_CLI_HTTP_H

#include "json.h"

/* Opaque handle for the HTTP client session */
typedef struct HttpClient HttpClient;

/* Create a new HTTP client with default timeout */
HttpClient *http_client_new(void);

/* Create a new HTTP client with extended timeout (for batch operations) */
HttpClient *http_client_new_batch(void);

/* Free the HTTP client */
void http_client_free(HttpClient *client);

/* ---------------------------------------------------------------------------
 * MCP Tool Calls
 * ------------------------------------------------------------------------- */

/* Call an MCP tool on the Browser4 server. Returns heap-allocated JSON string, or NULL on error.
 * Sets *error_msg if an error occurs. */
char *call_tool(HttpClient *client, const char *base_url,
                const char *tool_name, JsonValue *args,
                char **error_msg);

/* Submit a plain-text command to the server. async_mode=1 returns task ID immediately. */
char *submit_plain_command(HttpClient *client, const char *base_url,
                           const char *command, int async_mode,
                           char **error_msg);

/* Get command status by task ID */
char *get_command_status(HttpClient *client, const char *base_url,
                         const char *task_id, char **error_msg);

/* Get command result by task ID */
char *get_command_result(HttpClient *client, const char *base_url,
                         const char *task_id, char **error_msg);

/* ---------------------------------------------------------------------------
 * Swarm APIs
 * ------------------------------------------------------------------------- */

/* Submit swarm payload (plain text) */
char *submit_swarm_payload(HttpClient *client, const char *base_url,
                           const char *payload, char **error_msg);

/* Submit swarm X-SQL query (JSON) */
char *submit_swarm_query(HttpClient *client, const char *base_url,
                         JsonValue *query, char **error_msg);

/* Get swarm task status */
char *get_swarm_status(HttpClient *client, const char *base_url,
                       const char *task_id, char **error_msg);

/* Get swarm task result */
char *get_swarm_result(HttpClient *client, const char *base_url,
                       const char *task_id, char **error_msg);

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Check if an error message indicates a stale/expired session */
int is_stale_session_error(const char *message);

/* Check if an error message indicates the backend is unreachable */
int is_backend_unreachable_error(const char *message);

/* Check if an error message indicates a timeout */
int is_timeout_error(const char *message);

/* Normalize element ref fields in tool arguments (e15 -> backend:15) */
void normalize_refs(JsonValue *args);

/* Extract text payload from MCP response JSON */
char *extract_mcp_text_payload(const JsonValue *data);

/* Extract plain text payload from HTTP response */
char *extract_http_text_payload(const char *response_text);

#endif /* BROWSER4_CLI_HTTP_H */
