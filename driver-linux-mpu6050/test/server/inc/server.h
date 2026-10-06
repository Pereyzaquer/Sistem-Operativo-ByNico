/**
 * @file server.h
 * @brief API del servidor TCP que publica lecturas del MPU6050.
 */

#ifndef SERVER_H
#define SERVER_H

#include "config.h"

/**
 * @brief Lanza el servidor TCP concurrente.
 * @details Función bloqueante: no retorna salvo error o finalización del proceso.
 * @param bind_addr Dirección IP para bind (ej: "0.0.0.0").
 * @param port Puerto TCP.
 * @param dev_path Ruta al device file (ej: `/dev/mpu6050`).
 * @param config_path Ruta al archivo de configuración (puede no existir).
 * @return 0 en éxito, -1 en error.
 */
int run_server(const char *bind_addr, int port,
               const char *dev_path,
               const char *config_path);

#endif // SERVER_H
