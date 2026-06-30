#ifndef BROWSER4_CLI_ERROR_H
#define BROWSER4_CLI_ERROR_H

/* Normalised exit codes matching the Rust implementation */
typedef enum {
    EXIT_SUCCESS_CLI = 0,
    EXIT_GENERAL     = 1,
    EXIT_USAGE       = 2,
    EXIT_SESSION     = 3,
    EXIT_SERVER      = 4,
    EXIT_BATCH_PARTIAL = 5,
} ExitCode;

/* Structured error with machine-readable code and human-readable message */
typedef struct {
    ExitCode code;
    char    *message; /* owned, must be freed */
} CliError;

/* Allocate and initialise a CliError */
CliError *cli_error_new(ExitCode code, const char *message);

/* Free a CliError */
void cli_error_free(CliError *err);

/* Convenience: create a usage error (code 2) */
CliError *cli_error_usage(const char *message);

/* Convenience: create a general error (code 1) */
CliError *cli_error_general(const char *message);

/* Convenience: create a session error (code 3) */
CliError *cli_error_session(const char *message);

/* Convenience: create a server error (code 4) */
CliError *cli_error_server(const char *message);

#endif /* BROWSER4_CLI_ERROR_H */
