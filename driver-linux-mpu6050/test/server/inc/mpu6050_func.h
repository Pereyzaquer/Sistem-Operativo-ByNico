/**
 * @file mpu6050_func.h
 * @brief Helpers de userspace para leer frames del driver `/dev/mpu6050`.
 */

#ifndef MPU6050_FUNC_H
#define MPU6050_FUNC_H

#include <stddef.h>
#include <stdint.h>

#include "filter.h" /* mpu_sample_t */

/**
 * @brief Longitud de un frame FIFO del MPU6050 (TEMP+ACCEL+GYRO habilitados).
 * @details Layout: AX_H AX_L AY_H AY_L AZ_H AZ_L T_H T_L GX_H GX_L GY_H GY_L GZ_H GZ_L
 */
#define MPU6050_FRAME_LEN 14

/**
 * @brief Lee exactamente 1 frame (14 bytes) y lo convierte a 7 canales `int16_t`.
 * @param fd File descriptor del device.
 * @param out Muestra de salida.
 * @return 0 en éxito, -1 en error (errno seteado por `read()`).
 */
int mpu6050_read_sample(int fd, mpu_sample_t *out);

/**
 * @brief Lee N frames consecutivos.
 * @details Si falla, no garantiza cuántas muestras quedaron válidas.
 * @param fd File descriptor del device.
 * @param dst Buffer de salida.
 * @param samples_count Cantidad de muestras a leer.
 * @return 0 en éxito, -1 en error.
 */
int mpu6050_read_samples(int fd, mpu_sample_t *dst, size_t samples_count);

#endif /* MPU6050_FUNC_H */
