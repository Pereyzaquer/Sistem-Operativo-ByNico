/**
 * @file main.c
 * @brief CLI de arranque del servidor TD3.
 */

#include "server.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Imprime ayuda de uso.
 * @param prog Nombre del programa.
 */
static void usage(const char *prog) {
    fprintf(stderr,
        "Uso: %s [-b bind_addr] [-p port] [-d device] [-c config]\n"
        "Ej:  %s -b 0.0.0.0 -p 8080 -d /dev/mpu6050 -c server.conf\n",
        prog, prog);
}

/**
 * @brief Punto de entrada del servidor.
 * @details Parseo simple de flags `-b/-p/-d/-c` y delega a `run_server()`.
 * @param argc Cantidad de argumentos.
 * @param argv Vector de argumentos.
 * @return 0 en éxito, 1 en error de parseo o de ejecución.
 */
int main(int argc, char **argv) {
    const char *bind_addr = "0.0.0.0";
    int port = 8080;
    const char *dev = "/dev/mpu6050";
    const char *cfg = "server.conf";

    for (int i = 1; i < argc; ++i) {
        if ((i+1) < argc && strcmp(argv[i], "-b") == 0) bind_addr = argv[++i];
        else if ((i+1) < argc && strcmp(argv[i], "-p") == 0) port = atoi(argv[++i]);
        else if ((i+1) < argc && strcmp(argv[i], "-d") == 0) dev = argv[++i];
        else if ((i+1) < argc && strcmp(argv[i], "-c") == 0) cfg = argv[++i];
        else { usage(argv[0]); return 1; }
    }

    return run_server(bind_addr, port, dev, cfg) == 0 ? 0 : 1;
}
