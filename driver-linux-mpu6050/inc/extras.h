/**
 * @file extras.h
 * @brief Tipos y estructuras compartidas por el driver del MPU6050.
 */

#ifndef EXTRAS_H
#define EXTRAS_H

/**
 * @brief Definiciones compartidas no específicas del SoC ni del mapa de registros del sensor.
 */

/** @brief Tipos del kernel usados por estructuras compartidas. */
#include <linux/types.h>
#include <linux/io.h>

/**
 * @brief Forward declarations para evitar includes pesados en todas las unidades.
 */
struct cdev;
struct class;
struct device;

/**
 * @brief Alias de compatibilidad.
 * @details El kernel suele usar `u8/u16/u32`, pero el proyecto utiliza `uint*_t`.
 */
typedef u8 uint8_t;
typedef u16 uint16_t;
typedef u32 uint32_t;

/**
 * @brief Tamaño de un frame de datos del FIFO.
 * @details ACCEL(6) + TEMP(2) + GYRO(6) = 14 bytes.
 */
#define FRAME_LEN 14

/**
 * @brief Representación cruda de un frame del sensor (14 bytes).
 */
struct dataframe {
    uint8_t accel_x_h;
    uint8_t accel_x_l;
    uint8_t accel_y_h;
    uint8_t accel_y_l;
    uint8_t accel_z_h;
    uint8_t accel_z_l;
    uint8_t temp_h;
    uint8_t temp_l;
    uint8_t gyro_x_h;
    uint8_t gyro_x_l;
    uint8_t gyro_y_h;
    uint8_t gyro_y_l;
    uint8_t gyro_z_h;
    uint8_t gyro_z_l;
};

/**
 * @brief Estado privado del driver (recursos + configuración) compartido en el módulo.
 */
struct driver_data {
    dev_t dev_num;
    struct cdev *c_dev;
    struct class *class_ptr;
    struct device *device_ptr;
    struct device **device_ptrs; /**< array de devices cuando se crean múltiples instancias */
    int irq;
    bool irq_requested;
    void __iomem *conf_i2c2_sda;
    void __iomem *conf_i2c2_scl;
    void __iomem *per_cm_i2c2_clkctrl;
    void __iomem *i2c_module;
    void *clk;      /**< struct clk * (se guarda como void* para evitar acoplar headers) */
    void *pinctrl;  /**< struct pinctrl * */
    bool clk_enabled;
    bool hw_initialized; /**< true si probe() inicializó el hardware con éxito */
};

#endif /* EXTRAS_H */
