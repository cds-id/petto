#ifndef CONFIG_H
#define CONFIG_H

/* Persistent user settings stored at $XDG_CONFIG_HOME/petto/config
 * (falls back to ~/.config/petto/config). Plain key=value text. */

typedef struct {
    char   pet_type[32];   /* "rocket" | "cat" | "jarvis" */
    int    pomodoro;       /* 0/1 */
    double work_min;
    double short_min;
    double long_min;
    int    long_every;
    int    spawn_x, spawn_y;
    int    onboarded;      /* 0 until first-run onboarding completes */
} Config;

/* Fill cfg with defaults. */
void config_defaults(Config *cfg);

/* Load config from disk into cfg (defaults first, then overrides). Returns 1
 * if a config file existed, 0 otherwise. */
int  config_load(Config *cfg);

/* Persist cfg to disk (creates the directory). Returns 0 on success. */
int  config_save(const Config *cfg);

/* Absolute config file path (static buffer). */
const char *config_path(void);

#endif
