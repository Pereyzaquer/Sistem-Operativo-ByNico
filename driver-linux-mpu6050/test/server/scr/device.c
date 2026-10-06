/**
 * @file device.c
 * @brief Wrapper de lectura del device file para el servidor.
 */

#include "device.h"

#include "mpu6050_func.h"

/**
 * @brief Lee `samples_count` muestras desde el device.
 * @details Delegación directa a `mpu6050_read_samples()`.
 */
int device_read_samples(int fd, mpu_sample_t *dst, size_t samples_count) {
    return mpu6050_read_samples(fd, dst, samples_count);
}
