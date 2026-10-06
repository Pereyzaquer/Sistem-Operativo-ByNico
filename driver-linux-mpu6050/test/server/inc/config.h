/**
 * @file config.h
 * @brief Lectura y defaults de configuración del servidor.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

/**
 * @brief Estructura de configuración del servidor.
 */
typedef struct {
    int max_active_conns; /**< default 100 */
    int backlog;          /**< default 20 (sólo al inicio) */
    int filter_window;    /**< default 2 (W: anteriores y posteriores) */
} server_config_t;

/**
 * @brief Carga configuración desde un archivo de texto simple.
 * @details Lee hasta 3 líneas numéricas (una por parámetro). Si el archivo no existe,
 * usa defaults (comportamiento requerido por el enunciado).
 * @param path Ruta al archivo de configuración (puede ser NULL).
 * @param out Estructura de salida.
 * @return 0 en éxito (incluye "no existe"), -1 en error (ej: `out==NULL`).
 */
int  cfg_load(const char *path, server_config_t *out);

/**
 * @brief Aplica valores por defecto a la configuración.
 * @param out Estructura a inicializar.
 */
void cfg_apply_defaults(server_config_t *out);

#endif // CONFIG_H
