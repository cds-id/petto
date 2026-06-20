#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static char g_dir[512];
static char g_path[600];

static void resolve_paths(void) {
    if (g_path[0]) return;
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    if (xdg && xdg[0])
        snprintf(g_dir, sizeof g_dir, "%s/petto", xdg);
    else if (home && home[0])
        snprintf(g_dir, sizeof g_dir, "%s/.config/petto", home);
    else
        snprintf(g_dir, sizeof g_dir, "./.petto");
    snprintf(g_path, sizeof g_path, "%s/config", g_dir);
}

const char *config_path(void) {
    resolve_paths();
    return g_path;
}

void config_defaults(Config *cfg) {
    memset(cfg, 0, sizeof *cfg);
    snprintf(cfg->pet_type, sizeof cfg->pet_type, "%s", "rocket");
    cfg->pomodoro   = 0;
    cfg->work_min   = 25;
    cfg->short_min  = 5;
    cfg->long_min   = 15;
    cfg->long_every = 4;
    cfg->spawn_x    = 200;
    cfg->spawn_y    = 200;
    cfg->onboarded  = 0;
}

/* trim leading/trailing whitespace in place */
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    char *e = s + strlen(s);
    while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' ||
                     e[-1] == '\t'))
        *--e = 0;
    return s;
}

int config_load(Config *cfg) {
    config_defaults(cfg);
    resolve_paths();
    FILE *f = fopen(g_path, "r");
    if (!f) return 0;

    char line[256];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(line);
        char *v = trim(eq + 1);

        if      (strcmp(k, "pet_type") == 0)
            snprintf(cfg->pet_type, sizeof cfg->pet_type, "%s", v);
        else if (strcmp(k, "pomodoro") == 0)   cfg->pomodoro   = atoi(v);
        else if (strcmp(k, "work_min") == 0)    cfg->work_min   = atof(v);
        else if (strcmp(k, "short_min") == 0)   cfg->short_min  = atof(v);
        else if (strcmp(k, "long_min") == 0)    cfg->long_min   = atof(v);
        else if (strcmp(k, "long_every") == 0)  cfg->long_every = atoi(v);
        else if (strcmp(k, "spawn_x") == 0)     cfg->spawn_x    = atoi(v);
        else if (strcmp(k, "spawn_y") == 0)     cfg->spawn_y    = atoi(v);
        else if (strcmp(k, "onboarded") == 0)   cfg->onboarded  = atoi(v);
    }
    fclose(f);
    return 1;
}

/* mkdir -p for the config dir */
static void ensure_dir(const char *path) {
    char tmp[512];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

int config_save(const Config *cfg) {
    resolve_paths();
    ensure_dir(g_dir);
    FILE *f = fopen(g_path, "w");
    if (!f) return -1;
    fprintf(f,
            "# petto config\n"
            "pet_type=%s\n"
            "pomodoro=%d\n"
            "work_min=%g\n"
            "short_min=%g\n"
            "long_min=%g\n"
            "long_every=%d\n"
            "spawn_x=%d\n"
            "spawn_y=%d\n"
            "onboarded=%d\n",
            cfg->pet_type, cfg->pomodoro, cfg->work_min, cfg->short_min,
            cfg->long_min, cfg->long_every, cfg->spawn_x, cfg->spawn_y,
            cfg->onboarded);
    fclose(f);
    return 0;
}
