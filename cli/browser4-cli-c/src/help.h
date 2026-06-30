#ifndef BROWSER4_CLI_HELP_H
#define BROWSER4_CLI_HELP_H

/* Convert kebab-case command name to public display form (e.g. "agent-run" -> "agent run") */
const char *public_command_name(const char *name);

/* Generate global help text listing all available commands by category */
char *generate_help(void);

/* Generate per-command help text */
char *generate_command_help(const char *name);

#endif /* BROWSER4_CLI_HELP_H */
