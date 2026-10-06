/**
 * @file device.h
 * @brief Capa fina de lectura del dispositivo `/dev/mpu6050` para el servidor.
 */

#ifndef DEVICE_H
#define DEVICE_H

#include <stdint.h>
#include <stddef.h>
#include "filter.h"

/**
 * @brief Lee N lecturas (cada una 14 bytes) del device file (bloqueante hasta completar).
 * @details Retorna 0 si logró llenar `samples_count` muestras, -1 en error.
 * @param fd File descriptor abierto del dispositivo.
 * @param dst Buffer de salida de muestras.
 * @param samples_count Cantidad de muestras a leer.
 * @return 0 en éxito, -1 en error.
 */
int device_read_samples(int fd, mpu_sample_t *dst, size_t samples_count);

#endif // DEVICE_H
