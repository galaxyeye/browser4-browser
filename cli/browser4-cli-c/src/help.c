#include "help.h"
#include "commands.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * String builder for help text
 * ------------------------------------------------------------------------- */

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
} HelpBuf;

static void hb_init(HelpBuf *hb) {
    hb->cap = 4096;
    hb->buf = (char *)malloc(hb->cap);
    hb->buf[0] = '\0';
    hb->len = 0;
}

static void hb_append(HelpBuf *hb, const char *s) {
    size_t slen = strlen(s);
    size_t needed = hb->len + slen + 1;
    while (hb->cap < needed) hb->cap *= 2;
    hb->buf = (char *)realloc(hb->buf, hb->cap);
    memcpy(hb->buf + hb->len, s, slen);
    hb->len += slen;
    hb->buf[hb->len] = '\0';
}

static void hb_append_char(HelpBuf *hb, char c) {
    char tmp[2] = {c, '\0'};
    hb_append(hb, tmp);
}

#include <stdarg.h>

static void hb_printf(HelpBuf *hb, const char *fmt, ...) {
    char tmp[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, args);
    va_end(args);
    hb_append(hb, tmp);
}

static void hb_format_gap(HelpBuf *hb, const char *prefix, const char *text, int threshold) {
    size_t plen = strlen(prefix);
    int gap = plen < (size_t)threshold ? threshold - (int)plen : 1;
    hb_append(hb, prefix);
    for (int i = 0; i < gap; i++) hb_append_char(hb, ' ');
    hb_append(hb, text);
}

static char *hb_take(HelpBuf *hb) {
    return hb->buf;
}

/* ---------------------------------------------------------------------------
 * Public command name
 * ------------------------------------------------------------------------- */

const char *public_command_name(const char *name) {
    static char buf[64];
    if (strcmp(name, "agent-run") == 0) return "agent run";
    if (strcmp(name, "agent-status") == 0) return "agent status";
    if (strcmp(name, "agent-result") == 0) return "agent result";
    if (strcmp(name, "swarm-create") == 0) return "swarm create";
    if (strcmp(name, "swarm-submit") == 0) return "swarm submit";
    if (strcmp(name, "swarm-query") == 0) return "swarm query";
    if (strcmp(name, "swarm-status") == 0) return "swarm status";
    if (strcmp(name, "swarm-result") == 0) return "swarm result";
    return name;
}

/* ---------------------------------------------------------------------------
 * Category display order
 * ------------------------------------------------------------------------- */

static const char *category_title(const char *cat_str) {
    if (strcmp(cat_str, "core") == 0) return "Core";
    if (strcmp(cat_str, "navigation") == 0) return "Navigation";
    if (strcmp(cat_str, "keyboard") == 0) return "Keyboard";
    if (strcmp(cat_str, "mouse") == 0) return "Mouse";
    if (strcmp(cat_str, "export") == 0) return "Save as";
    if (strcmp(cat_str, "tabs") == 0) return "Tabs";
    if (strcmp(cat_str, "storage") == 0) return "Storage";
    if (strcmp(cat_str, "network") == 0) return "Network";
    if (strcmp(cat_str, "devtools") == 0) return "DevTools";
    if (strcmp(cat_str, "agent") == 0) return "Agent";
    if (strcmp(cat_str, "swarm") == 0) return "Swarm";
    if (strcmp(cat_str, "install") == 0) return "Install";
    if (strcmp(cat_str, "config") == 0) return "Configuration";
    if (strcmp(cat_str, "browsers") == 0) return "Browser sessions";
    return cat_str;
}

static const char *category_order[] = {
    "core", "navigation", "keyboard", "mouse", "export", "tabs",
    "storage", "network", "devtools", "agent", "swarm", "install",
    "config", "browsers", NULL
};

/* ---------------------------------------------------------------------------
 * Generate help entry for a single command
 * ------------------------------------------------------------------------- */

static void generate_help_entry(HelpBuf *hb, const CommandDef *cmd) {
    char prefix[128];
    const char **args_text = (const char **)calloc(cmd->args_count, sizeof(char *));
    for (int i = 0; i < cmd->args_count; i++) {
        if (cmd->args[i].optional)
            args_text[i] = (const char *)(size_t)0; /* marker for "[" */
    }

    char args_str[256] = "";
    for (int i = 0; i < cmd->args_count; i++) {
        char part[64];
        if (cmd->args[i].optional) {
            snprintf(part, sizeof(part), "[%s]", cmd->args[i].name);
        } else {
            snprintf(part, sizeof(part), "<%s>", cmd->args[i].name);
        }
        if (i > 0) strcat(args_str, " ");
        strcat(args_str, part);
    }
    free(args_text);

    snprintf(prefix, sizeof(prefix), "  %s %s",
             public_command_name(cmd->name), args_str);
    /* Trim trailing space */
    size_t plen = strlen(prefix);
    while (plen > 0 && prefix[plen-1] == ' ') prefix[--plen] = '\0';

    hb_format_gap(hb, prefix, cmd->description, 30);
    hb_append_char(hb, '\n');
}

/* ---------------------------------------------------------------------------
 * Generate global help
 * ------------------------------------------------------------------------- */

char *generate_help(void) {
    HelpBuf hb;
    hb_init(&hb);

    hb_append(&hb, "Usage: browser4-cli <command> [args] [options]\n");
    hb_append(&hb, "Usage: browser4-cli -s=<session> <command> [args] [options]\n");

    int cmd_count;
    const CommandDef *cmds = all_commands(&cmd_count);

    for (int ci = 0; category_order[ci]; ci++) {
        const char *cat_name = category_order[ci];
        int has_cmds = 0;
        for (int i = 0; i < cmd_count; i++) {
            if (!cmds[i].hidden && strcmp(category_str(cmds[i].category), cat_name) == 0) {
                has_cmds = 1;
                break;
            }
        }
        if (!has_cmds) continue;

        hb_printf(&hb, "\n%s:\n", category_title(cat_name));
        for (int i = 0; i < cmd_count; i++) {
            if (!cmds[i].hidden && strcmp(category_str(cmds[i].category), cat_name) == 0) {
                generate_help_entry(&hb, &cmds[i]);
            }
        }
    }

    hb_append(&hb, "\nGlobal options:\n");
    hb_format_gap(&hb, "  --help [command]", "print help", 30); hb_append_char(&hb, '\n');
    hb_format_gap(&hb, "  --version", "print version", 30); hb_append_char(&hb, '\n');
    hb_format_gap(&hb, "  --json", "emit machine-parseable JSON to stdout", 30); hb_append_char(&hb, '\n');
    hb_format_gap(&hb, "  -q, --quiet", "suppress normal output, only show errors", 30); hb_append_char(&hb, '\n');
    hb_format_gap(&hb, "  -s=<name>", "named session label", 30); hb_append_char(&hb, '\n');
    hb_format_gap(&hb, "  --server=<url>", "override Browser4 server URL", 30); hb_append_char(&hb, '\n');

    return hb_take(&hb);
}

/* ---------------------------------------------------------------------------
 * Generate per-command help
 * ------------------------------------------------------------------------- */

char *generate_command_help(const char *name) {
    const CommandDef *cmd = find_command(name);
    if (!cmd) return _strdup("Unknown command. Use 'browser4-cli --help' to list all commands.\n");

    HelpBuf hb;
    hb_init(&hb);

    /* Build usage line */
    char args_text[256] = "";
    for (int i = 0; i < cmd->args_count; i++) {
        char part[64];
        if (cmd->args[i].optional) snprintf(part, sizeof(part), "[%s]", cmd->args[i].name);
        else snprintf(part, sizeof(part), "<%s>", cmd->args[i].name);
        if (i > 0) strcat(args_text, " ");
        strcat(args_text, part);
    }
    hb_printf(&hb, "browser4-cli %s %s\n\n",
              public_command_name(cmd->name), args_text);
    hb_printf(&hb, "%s\n\n", cmd->description);

    /* Arguments */
    if (cmd->args_count > 0) {
        hb_append(&hb, "Arguments:\n");
        for (int i = 0; i < cmd->args_count; i++) {
            char label[64];
            if (cmd->args[i].optional) snprintf(label, sizeof(label), "  [%s]", cmd->args[i].name);
            else snprintf(label, sizeof(label), "  <%s>", cmd->args[i].name);
            hb_format_gap(&hb, label, cmd->args[i].description, 30);
            hb_append_char(&hb, '\n');
        }
    }

    /* Options */
    if (cmd->options_count > 0) {
        hb_append(&hb, "Options:\n");
        for (int i = 0; i < cmd->options_count; i++) {
            char label[64];
            snprintf(label, sizeof(label), "  --%s", cmd->options[i].name);
            hb_format_gap(&hb, label, cmd->options[i].description, 30);
            hb_append_char(&hb, '\n');
        }
    }

    /* Command-specific notes */
    if (strcmp(cmd->name, "batch") == 0) {
        hb_append(&hb, "\nNotes:\n");
        hb_append(&hb, "  - Quote each subcommand so it is parsed as one batch item.\n");
        hb_append(&hb, "  - Use --bail to stop execution on the first failed subcommand.\n");
        hb_append(&hb, "  - Use --json to read command arrays from stdin JSON payload.\n");
        hb_append(&hb, "\nExamples:\n");
        hb_append(&hb, "  browser4-cli batch \"open https://playwright.dev\" \"snapshot\"\n");
        hb_append(&hb, "  browser4-cli batch --bail \"open https://playwright.dev\" \"click e1\" \"screenshot\"\n");
        hb_append(&hb, "  echo '[ [\"open\", \"https://playwright.dev\"], [\"snapshot\"] ]' | browser4-cli batch --json\n");
    }

    if (strcmp(cmd->name, "eval") == 0) {
        hb_append(&hb, "Examples:\n");
        hb_append(&hb, "  browser4-cli eval \"document.title\"\n");
        hb_append(&hb, "  browser4-cli eval \"element => element.textContent\" \"#click-target\"\n");
        hb_append(&hb, "  browser4-cli eval \"element => element.textContent\" e5\n");
    }

    if (strcmp(cmd->name, "open") == 0) {
        hb_append(&hb, "\nExamples:\n");
        hb_append(&hb, "  browser4-cli open https://browser4.io/\n");
        hb_append(&hb, "  browser4-cli open --headed https://browser4.io/\n");
        hb_append(&hb, "  browser4-cli open --headless https://browser4.io/\n");
    }

    if (strcmp(cmd->name, "goto") == 0) {
        hb_append(&hb, "\nExamples:\n");
        hb_append(&hb, "  browser4-cli goto https://browser4.io/\n");
        hb_append(&hb, "  browser4-cli -s mysession goto https://browser4.io/\n");
    }

    if (strcmp(cmd->name, "install") == 0) {
        hb_append(&hb, "\nNotes:\n");
        hb_append(&hb, "  - Downloads the self-contained Browser4 runtime bundle.\n");
        hb_append(&hb, "\nExamples:\n");
        hb_append(&hb, "  browser4-cli install\n");
        hb_append(&hb, "  browser4-cli install --tag=v4.9.3\n");
        hb_append(&hb, "  browser4-cli install --tag=4.9.3 --force\n");
    }

    return hb_take(&hb);
}
