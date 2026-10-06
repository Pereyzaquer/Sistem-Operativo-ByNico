/**
 * @file filter.h
 * @brief Filtro de media móvil centrada sobre muestras del MPU6050.
 */

#ifndef FILTER_H
#define FILTER_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief Cantidad de canales por muestra.
 * @details accel_x, accel_y, accel_z, temp, gyro_x, gyro_y, gyro_z.
 */
#define MPU_CHANNELS 7

/**
 * @brief Una muestra del sensor (7 canales en orden fijo).
 */
typedef struct {
    /**< cada muestra contiene 7 canales (en el orden del PDF) */
    int16_t ch[MPU_CHANNELS];
} mpu_sample_t;

/**
 * @brief Buffer circular usado por el filtro de media móvil.
 */
typedef struct {
    mpu_sample_t *buf;
    size_t cap;     /**< capacidad (número de muestras) */
    size_t head;    /**< siguiente posición a escribir */
    size_t count;   /**< muestras válidas en buffer */
    int window;     /**< "tamaño de la ventana" W (anteriores y posteriores) */
} ma_filter_t;

/**
 * @brief Inicializa el filtro.
 * @param f Instancia del filtro.
 * @param capacity Capacidad del buffer circular (cantidad de muestras).
 * @param window Tamaño de ventana W (anteriores y posteriores).
 * @return 0 en éxito, -1 en error.
 */
int  ma_init(ma_filter_t *f, size_t capacity, int window);

/**
 * @brief Libera recursos del filtro.
 * @param f Instancia del filtro.
 */
void ma_free(ma_filter_t *f);

/**
 * @brief Inserta una muestra nueva (actualiza el ring buffer).
 * @param f Instancia del filtro.
 * @param s Muestra a insertar.
 */
void ma_push(ma_filter_t *f, const mpu_sample_t *s);

/**
 * @brief Calcula la media centrada de la "muestra actual" (la última insertada).
 * @details Implementación tolerante a bordes: si faltan futuras/pasadas, usa las disponibles.
 * @param f Instancia del filtro.
 * @param out Muestra de salida (promediada).
 */
void ma_get_centered(const ma_filter_t *f, mpu_sample_t *out);

#endif // FILTER_H
