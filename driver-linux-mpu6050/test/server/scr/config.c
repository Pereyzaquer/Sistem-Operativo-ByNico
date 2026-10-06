/**
 * @file config.c
 * @brief Implementación de lectura de configuración del servidor.
 */

#include "config.h"
#include <stdio.h>
#include <stdlib.h>

/**
 * @brief Aplica defaults según el enunciado.
 */
void cfg_apply_defaults(server_config_t *out) {
    out->max_active_conns = 100;
    out->backlog = 20;
    out->filter_window = 2;
}

/**
 * @brief Carga configuración desde archivo, con fallback a defaults.
 * @details
 * El archivo es opcional: si no existe o no puede abrirse, se mantiene defaults.
 * El formato esperado son hasta 3 líneas con enteros: `max_active_conns`, `backlog`,
 * `filter_window`.
 */
int cfg_load(const char *path, server_config_t *out) {
    if (!out) return -1;
    cfg_apply_defaults(out);
    if (!path) return 0;

    FILE *f = fopen(path, "r");
    if (!f) return 0; // si no existe, usar defaults (como indica el enunciado)

    char line[128];
    int vals[3] = { out->max_active_conns, out->backlog, out->filter_window };
    int i = 0;
    while (i < 3 && fgets(line, sizeof(line), f)) {
        char *endp = NULL;
        long v = strtol(line, &endp, 10);
        if (endp != line) vals[i++] = (int)v;
    }
    fclose(f);

    out->max_active_conns = vals[0];
    out->backlog          = vals[1];
    out->filter_window    = vals[2];
    return 0;
}
