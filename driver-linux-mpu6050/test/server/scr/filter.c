/**
 * @file filter.c
 * @brief Implementación del filtro de media móvil centrada.
 */

#include "filter.h"
#include <stdlib.h>
#include <string.h>

/**
 * @brief Inicializa el buffer circular y parámetros del filtro.
 */
int ma_init(ma_filter_t *f, size_t capacity, int window) {
    if (!f || capacity == 0 || window < 0) return -1;
    f->buf = (mpu_sample_t*)calloc(capacity, sizeof(mpu_sample_t));
    if (!f->buf) return -1;
    f->cap = capacity;
    f->head = 0;
    f->count = 0;
    f->window = window;
    return 0;
}

/**
 * @brief Libera memoria asociada al filtro.
 */
void ma_free(ma_filter_t *f) {
    if (f && f->buf) { free(f->buf); f->buf = NULL; }
}

/**
 * @brief "Wrap" de índice (puede ser negativo) al rango `[0, cap)`.
 */
static size_t idx_wrap(const ma_filter_t *f, long i) {
    long m = (long)f->cap;
    long r = i % m;
    return (size_t)((r < 0) ? r + m : r);
}

/**
 * @brief Inserta una muestra en el ring buffer.
 */
void ma_push(ma_filter_t *f, const mpu_sample_t *s) {
    if (!f || !f->buf || !s) return;
    f->buf[f->head] = *s;
    f->head = (f->head + 1) % f->cap;
    if (f->count < f->cap) f->count++;
}

/**
 * @brief Calcula promedio centrado (media móvil) de la última muestra insertada.
 * @details
 * Si no hay suficientes muestras para cubrir `W` hacia ambos lados, usa las disponibles.
 */
void ma_get_centered(const ma_filter_t *f, mpu_sample_t *out) {
    if (!f || !out || f->count == 0) { memset(out, 0, sizeof(*out)); return; }

    // La "muestra actual" es la última insertada -> head-1
    long center = (long)f->head - 1;
    int W = f->window;

    // Acumular desde center-W hasta center+W, respetando bordes (no más de count)
    // Si el buffer aún no tiene suficientes muestras, se toman las disponibles.
    int max_span = (int)((f->count < (size_t)(2*W + 1)) ? (int)f->count : (2*W + 1));
    // Queremos centrado; pero si faltan, distribuimos lo posible a cada lado
    int left = W, right = W;
    if (max_span < (2*W + 1)) {
        int deficit = (2*W + 1) - max_span;
        // restamos equitativamente primero a "right" y luego a "left"
        int cut_r = deficit / 2;
        int cut_l = deficit - cut_r;
        right -= cut_r;
        left  -= cut_l;
        if (right < 0) right = 0;
        if (left  < 0) left  = 0;
    }

    long start = center - left;
    long end   = center + right;

    long n = 0;
    long sum[MPU_CHANNELS] = {0};

    for (long i = start; i <= end; ++i) {
        size_t k = idx_wrap(f, i);
        for (int c = 0; c < MPU_CHANNELS; ++c) {
            sum[c] += f->buf[k].ch[c];
        }
        n++;
    }

    for (int c = 0; c < MPU_CHANNELS; ++c) {
        long avg = (n > 0) ? (sum[c] / n) : 0;
        out->ch[c] = (int16_t)avg;
    }
}
