#ifndef BROWSER4_CLI_STATE_H
#define BROWSER4_CLI_STATE_H

#include "json.h"

/* Mouse position for restoring pointer state across CLI invocations */
typedef struct {
    double x;
    double y;
} MousePosition;

/* Persistent CLI state stored on disk (mirrors Rust CliState) */
typedef struct {
    char *session_id;        /* "sessionId" */
    char *base_url;          /* "baseUrl" */
    char *active_selector;   /* "activeSelector" */
    char *session_name;      /* "sessionName" */
    MousePosition *last_mouse_position; /* "lastMousePosition" — NULL if unset */
} CliState;

/* Initialise a CliState with default values */
CliState *cli_state_new(void);

/* Free a CliState */
void cli_state_free(CliState *state);

/* Clone a CliState */
CliState *cli_state_clone(const CliState *state);

/* Read persisted CLI state from disk. Returns a new CliState (caller owns). */
CliState *read_state(const char *session_name);

/* Write CLI state to disk. Creates directories as needed. Returns 0 on success. */
int write_state(const CliState *state, const char *session_name);

/* Clear persisted CLI state for a session (remove the file). */
void clear_state(const char *session_name);

/* Clear default state plus all named session state files. */
void clear_all_state(void);

/* Resolve the default state directory (~/.browser4). Returns heap-allocated string. */
char *resolve_default_state_dir(void);

/* Convert CLI element refs: e15 -> backend:15, pass-through otherwise. Returns heap-allocated string. */
char *resolve_ref(const char *raw_ref);

#endif /* BROWSER4_CLI_STATE_H */
