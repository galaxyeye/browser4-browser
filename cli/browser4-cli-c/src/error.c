#include "error.h"
#include <stdlib.h>
#include <string.h>

CliError *cli_error_new(ExitCode code, const char *message) {
    CliError *err = (CliError *)malloc(sizeof(CliError));
    if (!err) return NULL;
    err->code = code;
    err->message = message ? _strdup(message) : _strdup("unknown error");
    return err;
}

void cli_error_free(CliError *err) {
    if (err) {
        free(err->message);
        free(err);
    }
}

CliError *cli_error_usage(const char *message) {
    return cli_error_new(EXIT_USAGE, message);
}

CliError *cli_error_general(const char *message) {
    return cli_error_new(EXIT_GENERAL, message);
}

CliError *cli_error_session(const char *message) {
    return cli_error_new(EXIT_SESSION, message);
}

CliError *cli_error_server(const char *message) {
    return cli_error_new(EXIT_SERVER, message);
}
