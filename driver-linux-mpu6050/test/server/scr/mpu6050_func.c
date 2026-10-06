/**
 * @file mpu6050_func.c
 * @brief Helpers de acceso al MPU6050 desde userspace.
 * @details
 * Este módulo NO habla I2C: lee frames crudos desde `/dev/mpu6050`.
 */

#define _GNU_SOURCE

#include "mpu6050_func.h"

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

static int read_full(int fd, void *buf, size_t n)
{
	uint8_t *p = (uint8_t *)buf;
	size_t got = 0;
	while (got < n) {
		ssize_t r = read(fd, p + got, n - got);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		if (r == 0) {
			errno = EIO;
			return -1;
		}
		got += (size_t)r;
	}
	return 0;
}

/**
 * @brief Convierte 2 bytes big-endian a `int16_t`.
 */
static inline int16_t be16s(const uint8_t *p)
{
	return (int16_t)((uint16_t)p[0] << 8 | (uint16_t)p[1]);
}

/**
 * @brief Lee 1 muestra del device y la convierte a 7 canales.
 */
int mpu6050_read_sample(int fd, mpu_sample_t *out)
{
	if (!out) {
		errno = EINVAL;
		return -1;
	}

	uint8_t frame[MPU6050_FRAME_LEN];
	if (read_full(fd, frame, sizeof(frame)) < 0)
		return -1;

	/* Orden fijo del frame FIFO (TEMP habilitada):
	 * 0..5 accel, 6..7 temp, 8..13 gyro
	 */
	out->ch[0] = be16s(&frame[0]);
	out->ch[1] = be16s(&frame[2]);
	out->ch[2] = be16s(&frame[4]);
	out->ch[3] = be16s(&frame[6]);
	out->ch[4] = be16s(&frame[8]);
	out->ch[5] = be16s(&frame[10]);
	out->ch[6] = be16s(&frame[12]);
	return 0;
}

/**
 * @brief Lee N muestras consecutivas del device y las convierte.
 */
int mpu6050_read_samples(int fd, mpu_sample_t *dst, size_t samples_count)
{
	if (!dst && samples_count != 0) {
		errno = EINVAL;
		return -1;
	}
	if (samples_count == 0)
		return 0;

	size_t bytes = samples_count * (size_t)MPU6050_FRAME_LEN;
	uint8_t *buf = (uint8_t *)malloc(bytes);
	if (!buf) {
		errno = ENOMEM;
		return -1;
	}

	if (read_full(fd, buf, bytes) < 0) {
		free(buf);
		return -1;
	}

	for (size_t i = 0; i < samples_count; ++i) {
		const uint8_t *frame = buf + (i * (size_t)MPU6050_FRAME_LEN);
		dst[i].ch[0] = be16s(&frame[0]);
		dst[i].ch[1] = be16s(&frame[2]);
		dst[i].ch[2] = be16s(&frame[4]);
		dst[i].ch[3] = be16s(&frame[6]);
		dst[i].ch[4] = be16s(&frame[8]);
		dst[i].ch[5] = be16s(&frame[10]);
		dst[i].ch[6] = be16s(&frame[12]);
	}

	free(buf);
	return 0;
}

