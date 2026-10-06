/**
 * @file mpu6050_ioctl.h
 * @brief IOCTLs compartidos entre user space y kernel para el driver MPU6050.
 * @details
 * Este header define los comandos ioctl usados por `/dev/mpu6050`.
 *
 * Requisito del enunciado (recuperatorio):
 * - el driver debe exponer por ioctl un contador de lecturas desde que se instaló el módulo.
 */

#ifndef MPU6050_IOCTL_H
#define MPU6050_IOCTL_H

#ifdef __KERNEL__
#include <linux/ioctl.h>
#include <linux/types.h>
typedef __u64 mpu6050_u64;
#else
#include <sys/ioctl.h>
#include <stdint.h>
typedef uint64_t mpu6050_u64;
#endif

#define MPU6050_IOCTL_MAGIC 'M'

/**
 * @brief Devuelve el contador de lecturas exitosas desde module init.
 * @param[out] mpu6050_u64* puntero a u64 donde se escribe el contador.
 */
#define MPU6050_IOCTL_GET_READ_COUNT _IOR(MPU6050_IOCTL_MAGIC, 0x01, mpu6050_u64)

/**
 * @brief Resetea el contador de lecturas a 0.
 * @details No es requerido por el enunciado, pero es útil para pruebas.
 */
#define MPU6050_IOCTL_RESET_READ_COUNT _IO(MPU6050_IOCTL_MAGIC, 0x02)

#endif /* MPU6050_IOCTL_H */
