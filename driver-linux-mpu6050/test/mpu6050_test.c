/**
 * @file mpu6050_test.c
 * @brief Programa de prueba en userspace para leer y visualizar datos de `/dev/mpu6050`.
 * @details
 * Lee frames crudos del FIFO (14 bytes por muestra: accel(6)+temp(2)+gyro(6)), promedia
 * N muestras por lectura y muestra valores en unidades físicas (g, °/s, °C).
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../inc/mpu6050_ioctl.h"

/**
 * @brief Bytes por frame leído desde el driver.
 * @details
 * Debe matchear el `FRAME_LEN` del lado kernel (ver `extras.h`). Se duplica acá para
 * evitar incluir headers del kernel en userspace.
 */
#define FRAME_LEN 14

/** @brief Secuencias ANSI para colorear la salida. */
#define C_RESET   "\x1b[0m"
#define C_RED     "\x1b[31m"
#define C_GREEN   "\x1b[32m"
#define C_YELLOW  "\x1b[33m"
#define C_BLUE    "\x1b[34m"
#define C_MAGENTA "\x1b[35m"
#define C_CYAN    "\x1b[36m"

/**
 * @brief Factores de escala para convertir valores crudos a unidades físicas.
 * @details Deben coincidir con la configuración aplicada en el driver.
 * (ACCEL=±2g y GYRO=±1000 dps).
 */
#define ACCEL_LSB_PER_G 16384.0
#define GYRO_LSB_PER_DPS 32.8

/**
 * @brief Estructura con una muestra (cruda) convertida a enteros con signo.
 */
struct ModuleData
{
    int16_t accel_outx;
    int16_t accel_outy;
    int16_t accel_outz;
    int16_t temp;
    int16_t gyro_outx;
    int16_t gyro_outy;
    int16_t gyro_outz;
};

static volatile sig_atomic_t g_stop = 0;

/**
 * @brief Handler de señal para terminar el bucle principal limpiamente.
 * @param sig Señal recibida.
 */
static void on_sigint(int sig)
{
    (void)sig;
    g_stop = 1;
}

/**
 * @brief Convierte 2 bytes big-endian a `int16_t`.
 * @param p Puntero al primer byte.
 * @return Valor con signo en endianness host.
 */
static int16_t be16s(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0] << 8 | (uint16_t)p[1]);
}

/**
 * @brief Imprime un buffer en hex (una línea).
 * @param buf Buffer de entrada.
 * @param len Cantidad de bytes.
 */
static void hexdump(const uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        printf("%02X%s", buf[i], (i + 1 == len) ? "" : " ");
    }
    printf("\n");
}

/**
 * @brief Timestamp monotónico en milisegundos.
 * @return Tiempo en ms.
 */
static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/**
 * @brief Parsea un frame de 14 bytes y lo convierte a estructura de muestra.
 * @param f Puntero al frame (14 bytes).
 * @return Estructura con valores crudos (int16_t) por canal.
 */
static struct ModuleData parse_frame(const uint8_t *f)
{
    struct ModuleData out;
    out.accel_outx = be16s(&f[0]);
    out.accel_outy = be16s(&f[2]);
    out.accel_outz = be16s(&f[4]);
    out.temp       = be16s(&f[6]);
    out.gyro_outx  = be16s(&f[8]);
    out.gyro_outy  = be16s(&f[10]);
    out.gyro_outz  = be16s(&f[12]);
    return out;
}

/**
 * @brief Convierte TEMP_OUT crudo a °C.
 * @param temp_raw Valor crudo.
 * @return Temperatura en °C.
 */
static double temp_raw_to_c(int16_t temp_raw)
{
    /* MPU6050 datasheet: Temp in °C = (TEMP_OUT / 340) + 36.53 */
    return (double)temp_raw / 340.0 + 36.53;
}

/**
 * @brief Convierte aceleración cruda a g.
 * @param accel_raw Valor crudo.
 * @return Aceleración en g.
 */
static double accel_raw_to_g(int16_t accel_raw)
{
    return (double)accel_raw / ACCEL_LSB_PER_G;
}

/**
 * @brief Convierte giroscopio crudo a °/s.
 * @param gyro_raw Valor crudo.
 * @return Velocidad angular en °/s.
 */
static double gyro_raw_to_dps(int16_t gyro_raw)
{
    return (double)gyro_raw / GYRO_LSB_PER_DPS;
}

/**
 * @brief Imprime ayuda de uso por stderr.
 * @param argv0 Nombre del programa.
 */
static void usage(const char *argv0)
{
    fprintf(stderr,
            "Uso: %s [frames_por_lectura] [delay_ms]\n\n"
            "- frames_por_lectura: cantidad de muestras a promediar por cada read() (default: 10)\n"
            "- delay_ms: pausa entre lecturas (default: 200)\n\n"
            "Ejemplo: %s 20 100\n",
            argv0, argv0);
}

/**
 * @brief Punto de entrada del programa de prueba.
 * @details Abre `/dev/mpu6050` en modo lectura y ejecuta un loop de lecturas.
 * @param argc Cantidad de argumentos.
 * @param argv Vector de argumentos.
 * @return Código de salida (0 en éxito).
 */
int main(int argc, char **argv)
{
    const char *dev_path = "/dev/mpu6050";
    size_t frames_per_read = 10;
    long delay_ms = 200;

    printf("\n\n ----------------------------- \n");

    printf("mpu6050_test: usando lenguaje estandar C11\n");

    if (argc >= 2) {
        if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
            usage(argv[0]);
            return 0;
        }
        frames_per_read = (size_t)strtoul(argv[1], NULL, 10);
    }
    if (argc >= 3) {
        delay_ms = strtol(argv[2], NULL, 10);
    }
    if (frames_per_read == 0) {
        fprintf(stderr, "frames_por_lectura debe ser > 0\n");
        return 2;
    }
    if (delay_ms < 0) {
        fprintf(stderr, "delay_ms debe ser >= 0\n");
        return 2;
    }

    const size_t bytes_to_read = frames_per_read * FRAME_LEN;
    uint8_t *buf = (uint8_t *)malloc(bytes_to_read);
    if (!buf) {
        perror("malloc");
        return 1;
    }

    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    /* Make sure we see prints even if the process crashes */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("mpu6050_test: uid=%ld euid=%ld gid=%ld egid=%ld\n",
            (long)getuid(), (long)geteuid(), (long)getgid(), (long)getegid());
    printf("Abriendo %s (O_RDONLY)...\n", dev_path);

    int fd = open(dev_path, O_RDONLY);
    if (fd < 0) {
        printf("No pude abrir %s: %s (errno=%d)\n", dev_path, strerror(errno), errno);
        printf("Tip: verifica que el modulo este cargado y el nodo exista: ls -l %s\n", dev_path);
        printf("Tip: mira el kernel log: dmesg | tail -n 120\n");
        free(buf);
        return 1;
    }

        printf("Open OK. Iniciando lecturas...\n");
        printf("Promediando %zu muestras por read() (%zu bytes). Ctrl+C para salir.\n",
            frames_per_read, bytes_to_read);

    while (!g_stop) {
        const uint64_t t0 = now_ms();
        ssize_t n = read(fd, buf, bytes_to_read);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            fprintf(stderr, "read() fallo: %s (errno=%d)\n", strerror(errno), errno);
            fprintf(stderr, "Tip: ultimo dmesg: dmesg | tail -n 80\n");
            break;
        }
        if (n == 0) {
            fprintf(stderr, "read() devolvio 0 (EOF?)\n");
            break;
        }
        if ((size_t)n % FRAME_LEN != 0) {
            fprintf(stderr, "read() devolvio %zd bytes (no multiplo de %d). bytes_pedidos=%zu\n",
                    n, FRAME_LEN, bytes_to_read);
            fprintf(stderr, "Primeros %zu bytes: ", (size_t)n);
            hexdump(buf, (size_t)n);
            break;
        }

        const size_t got_frames = (size_t)n / FRAME_LEN;
        const uint64_t t1 = now_ms();

        /* Average over the frames we got in this read() */
        int64_t sum_ax = 0, sum_ay = 0, sum_az = 0;
        int64_t sum_t  = 0;
        int64_t sum_gx = 0, sum_gy = 0, sum_gz = 0;

        for (size_t i = 0; i < got_frames; ++i) {
            struct ModuleData s = parse_frame(&buf[i * FRAME_LEN]);
            sum_ax += s.accel_outx;
            sum_ay += s.accel_outy;
            sum_az += s.accel_outz;
            sum_t  += s.temp;
            sum_gx += s.gyro_outx;
            sum_gy += s.gyro_outy;
            sum_gz += s.gyro_outz;
        }

        struct ModuleData avg;
        avg.accel_outx = (int16_t)(sum_ax / (int64_t)got_frames);
        avg.accel_outy = (int16_t)(sum_ay / (int64_t)got_frames);
        avg.accel_outz = (int16_t)(sum_az / (int64_t)got_frames);
        avg.temp       = (int16_t)(sum_t  / (int64_t)got_frames);
        avg.gyro_outx  = (int16_t)(sum_gx / (int64_t)got_frames);
        avg.gyro_outy  = (int16_t)(sum_gy / (int64_t)got_frames);
        avg.gyro_outz  = (int16_t)(sum_gz / (int64_t)got_frames);

        printf("read() OK: bytes=%zd frames=%zu dt=%" PRIu64 "ms\n", n, got_frames, (t1 - t0));
        printf(
            "AVG: "
            C_RED    "AX=%6d (%.3fg)" C_RESET "  "
            C_GREEN  "AY=%6d (%.3fg)" C_RESET "  "
            C_YELLOW "AZ=%6d (%.3fg)" C_RESET "  "
            C_CYAN   "T=%6d (%.2f°C)" C_RESET "  "
            C_MAGENTA"GX=%6d (%.2f°/s)" C_RESET "  "
            C_BLUE   "GY=%6d (%.2f°/s)" C_RESET "  "
            "GZ=%6d (%.2f°/s)\n",
            avg.accel_outx, accel_raw_to_g(avg.accel_outx),
            avg.accel_outy, accel_raw_to_g(avg.accel_outy),
            avg.accel_outz, accel_raw_to_g(avg.accel_outz),
            avg.temp, temp_raw_to_c(avg.temp),
            avg.gyro_outx, gyro_raw_to_dps(avg.gyro_outx),
            avg.gyro_outy, gyro_raw_to_dps(avg.gyro_outy),
            avg.gyro_outz, gyro_raw_to_dps(avg.gyro_outz));

        if (delay_ms > 0) {
            struct timespec ts;
            ts.tv_sec = delay_ms / 1000;
            ts.tv_nsec = (delay_ms % 1000) * 1000000L;
            nanosleep(&ts, NULL);
        }
    }

    {
        mpu6050_u64 cnt = 0;
        if (ioctl(fd, MPU6050_IOCTL_GET_READ_COUNT, &cnt) == 0) {
            printf("ioctl: read_count=%" PRIu64 "\n", (uint64_t)cnt);
        } else {
            fprintf(stderr, "ioctl(GET_READ_COUNT) fallo: %s (errno=%d)\n", strerror(errno), errno);
        }
    }

    close(fd);
    free(buf);
    printf("Saliendo.\n");
    return 0;
}
