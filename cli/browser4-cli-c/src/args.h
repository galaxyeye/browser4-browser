#ifndef BROWSER4_CLI_ARGS_H
#define BROWSER4_CLI_ARGS_H

#include "json.h"

/* Parsed global flags that appear before the command name */
typedef struct {
    char  *session_name;  /* -s / --session */
    char  *server_url;    /* --server */
    int    json_mode;     /* --json */
    int    quiet_mode;    /* -q / --quiet */
    char  *proxy_url;     /* --proxy */
    char **args;          /* remaining arguments (command + its args) */
    int    args_count;
} CliGlobalFlags;

/* Parsed batch command flags */
typedef struct {
    int    bail;          /* --bail */
    int    json_input;    /* --json (read from stdin) */
    char **commands;      /* command strings */
    int    commands_count;
} BatchArgs;

/* A short-option to long-option mapping entry */
typedef struct {
    char *short_name;
    char *long_name;
} ShortOptionEntry;

/* ---------------------------------------------------------------------------
 * Global flag parsing
 * ------------------------------------------------------------------------- */

/* Parse global flags from argv. Returns a GlobalFlags (caller must free with global_flags_free). */
CliGlobalFlags *parse_global_flags(int argc, char **argv);

/* Free a CliGlobalFlags struct */
void global_flags_free(CliGlobalFlags *flags);

/* ---------------------------------------------------------------------------
 * Raw argument parsing
 * ------------------------------------------------------------------------- */

/* Build a short-to-long option map from an array of ShortOptionEntry */
JsonValue *build_short_option_map(ShortOptionEntry *entries, int count);

/* Parse raw CLI arguments into a JSON map suitable for command dispatch */
JsonValue *parse_raw_args(char **raw_args, int count, JsonValue *short_to_long);

/* Build a flat argument map from parsed raw args */
JsonValue *build_command_args(JsonValue *raw_args, const char **arg_names, int arg_count,
                              char **error_msg);

/* ---------------------------------------------------------------------------
 * Batch argument parsing
 * ------------------------------------------------------------------------- */

/* Parse batch command flags and positional command strings */
BatchArgs *parse_batch_args(char **raw_args, int count, char **error_msg);

/* Free a BatchArgs struct */
void batch_args_free(BatchArgs *args);

/* Split a batch command string into CLI tokens (handles quotes, escapes) */
char **parse_command_string(const char *command, int *out_count, char **error_msg);

/* Parse JSON stdin for batch --json mode */
char ***parse_batch_json_commands(const char *input, int *out_count, char **error_msg);

#endif /* BROWSER4_CLI_ARGS_H */
