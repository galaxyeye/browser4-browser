#ifndef BROWSER4_CLI_COMMANDS_H
#define BROWSER4_CLI_COMMANDS_H

#include "json.h"

/* Command category */
typedef enum {
    CAT_CORE,
    CAT_NAVIGATION,
    CAT_KEYBOARD,
    CAT_MOUSE,
    CAT_EXPORT,
    CAT_TABS,
    CAT_STORAGE,
    CAT_NETWORK,
    CAT_DEVTOOLS,
    CAT_BROWSERS,
    CAT_CONFIG,
    CAT_INSTALL,
    CAT_AGENT,
    CAT_SWARM,
} Category;

/* Describes a single positional argument */
typedef struct {
    const char *name;
    const char *description;
    int         optional;
} ArgDef;

/* Describes a named option */
typedef struct {
    const char *name;
    const char *description;
    int         is_bool;
    const char *short_name;  /* optional, e.g. "y" for -y */
} OptionDef;

/* Forward declaration */
typedef struct CommandDef CommandDef;

/* Function pointer types for tool name and params resolution */
typedef const char *(*ToolNameFn)(JsonValue *args);
typedef JsonValue  *(*ToolParamsFn)(JsonValue *args);

/* A single CLI command definition */
struct CommandDef {
    const char   *name;
    const char   *description;
    Category      category;
    int           hidden;
    int           batch_supported;
    const ArgDef *args;
    int           args_count;
    const OptionDef *options;
    int           options_count;
    ToolNameFn    tool_name_fn;
    ToolParamsFn  tool_params_fn;
};

/* Get all command definitions */
const CommandDef *all_commands(int *out_count);

/* Find a command by name (returns NULL if not found) */
const CommandDef *find_command(const char *name);

/* Convert category enum to string */
const char *category_str(Category cat);

/* Helper functions for command parameter building */
const char *get_str(JsonValue *args, const char *key);
int         get_bool(JsonValue *args, const char *key);
JsonValue  *get_number_value(JsonValue *args, const char *key);
int         looks_like_selector_or_ref(const char *value);

#endif /* BROWSER4_CLI_COMMANDS_H */
