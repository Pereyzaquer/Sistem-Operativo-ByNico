/**
 * @file server.c
 * @brief Servidor TCP concurrente que publica datos del MPU6050.
 * @details
 * Arquitectura general:
 * - Proceso padre: socket de escucha, acepta clientes y crea un proceso hijo por cliente.
 * - Proceso hijo "sensor": único lector de `/dev/mpu6050`, aplica filtrado y publica
 *   muestras crudas + promediadas en memoria compartida (mmap anónimo).
 * - Procesos hijo "cliente": responden HTTP (dashboard + JSON) o stream binario
 *   consultando snapshots consistentes de la memoria compartida.
 *
 * Concurrencia:
 * - Se utiliza un contador `seq` (estilo seqlock) para publicar/leer snapshots sin locks.
 * - El control de conexiones activas se implementa con un contador atómico compartido.
 */

#define _GNU_SOURCE
#include "server.h"
#include "filter.h"
#include "device.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#include <signal.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <time.h>
#include <sys/mman.h>

#if defined(__GNUC__) || defined(__clang__)
#define TD3_UNUSED __attribute__((unused))
#else
#define TD3_UNUSED
#endif

/* Logging con colores (solo si stdout es tty y NO_COLOR no está seteada). */
#define TD3_ANSI_BLUE   "\x1b[1;34m"
#define TD3_ANSI_GREEN  "\x1b[1;32m"
#define TD3_ANSI_RED    "\x1b[1;31m"
#define TD3_ANSI_YELLOW "\x1b[1;33m"
#define TD3_ANSI_ORANGE "\x1b[1;38;5;208m"
#define TD3_ANSI_RESET  "\x1b[0m"

static int td3_log_use_color(void) {
    static int inited = 0;
    static int use_color = 0;
    if (!inited) {
        inited = 1;
        use_color = (isatty(STDOUT_FILENO) != 0) && (getenv("NO_COLOR") == NULL);
    }
    return use_color;
}

static void td3_logf(const char *ansi_color, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    if (ansi_color && td3_log_use_color()) fputs(ansi_color, stdout);
    vfprintf(stdout, fmt, ap);
    if (ansi_color && td3_log_use_color()) fputs(TD3_ANSI_RESET, stdout);
    fflush(stdout);

    va_end(ap);
}

/**
 * @brief Máximo de muestras usadas para promediar en el sensor child.
 * @details Se usa como cota superior para buffers y para limitar carga de CPU.
 */
#define MAX_AVG_SAMPLES 51

/**
 * @brief Cantidad de mediciones históricas a graficar.
 */
#define HIST_SAMPLES 200

static server_config_t g_cfg;
static pid_t g_sensor_pid = -1;
static const char *g_config_path = NULL;
static volatile sig_atomic_t g_reload_cfg = 0;

static double accel_ms2_from_raw(int16_t v);
static double gyro_dps_from_raw(int16_t v);
static double temp_c_from_raw(int16_t v);

/**
 * @brief Estado compartido entre procesos (sensor + clientes).
 * @details
 * El publicador (sensor) incrementa `seq` antes y después de escribir (odd/even).
 * Los lectores toman un snapshot si `seq` es par y no cambia durante la copia.
 */
typedef struct {
    atomic_uint seq;               /**< seqlock: escritor alterna odd/even */
    mpu_sample_t raw;
    mpu_sample_t avg;
    struct timespec ts;
    unsigned meas_us;              /**< tiempo que tardó la última lectura (microsegundos) */

    /* Histórico circular de las últimas mediciones (raw + avg) para graficar.
     * hist_pos apunta al próximo índice a escribir.
     */
    unsigned hist_pos;
    unsigned hist_count;
    int16_t hist_raw[HIST_SAMPLES][MPU_CHANNELS];
    int16_t hist_avg[HIST_SAMPLES][MPU_CHANNELS];

    atomic_int cfg_max_active_conns; /**< config compartida (SIGUSR2) */
    atomic_int cfg_filter_window;    /**< config compartida (SIGUSR2) */

    atomic_int active_clients;
} shared_state_t;

/** @brief Puntero a la región mmap compartida. */
static shared_state_t *g_sh = NULL;

/**
 * @brief Señal SIGUSR2: recarga variables desde config (excepto backlog).
 * @details Sólo actualiza `max_active_conns` y `filter_window`.
 * @param sig Señal recibida.
 */
static void on_sigusr2(int sig) {
    (void)sig;
    /* No llamar a fopen/strtol/etc dentro del handler.
     * Recargamos la config en el loop principal.
     */
    g_reload_cfg = 1;
}

/**
 * @brief Señal SIGCHLD: cosecha procesos hijo.
 * @details
 * - Si muere el sensor child, se imprime diagnóstico.
 * - Si muere un client child, decrementa `active_clients`.
 * @param sig Señal recibida.
 */
static void on_sigchld(int sig) {
    (void)sig;
    int saved = errno;
    for (;;) {
        int status = 0;
        pid_t p = waitpid(-1, &status, WNOHANG);
        if (p <= 0) break;
        if (p == g_sensor_pid) {
            fprintf(stderr, "[server] sensor child exited (pid=%d, status=%d)\n", (int)p, status);
            g_sensor_pid = -1;
        } else {
            if (g_sh) atomic_fetch_sub(&g_sh->active_clients, 1);
        }
    }
    errno = saved;
}

/**
 * @brief Marca un file descriptor como close-on-exec.
 * @param fd File descriptor.
 * @return 0 en éxito, -1 en error.
 */
static int set_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

/**
 * @brief Obtiene un snapshot consistente del estado compartido.
 * @details Usa `seq` como seqlock sin locks.
 * @param sh Estado compartido.
 * @param raw Salida de muestra cruda.
 * @param avg Salida de muestra promediada.
 * @param ts Salida opcional timestamp.
 * @param meas_us_out Salida opcional tiempo de medición.
 * @param seq_out Salida opcional del `seq` leído.
 * @return 0 en éxito, -1 si parámetros inválidos.
 */
static int shared_snapshot(shared_state_t *sh, mpu_sample_t *raw, mpu_sample_t *avg, struct timespec *ts, unsigned *meas_us_out, unsigned *seq_out) {
    if (!sh || !raw || !avg) return -1;
    for (;;) {
        unsigned s1 = atomic_load_explicit(&sh->seq, memory_order_acquire);
        if (s1 == 0 || (s1 & 1U)) continue;
        *raw = sh->raw;
        *avg = sh->avg;
        if (ts) *ts = sh->ts;
        if (meas_us_out) *meas_us_out = sh->meas_us;
        unsigned s2 = atomic_load_explicit(&sh->seq, memory_order_acquire);
        if (s1 == s2) {
            if (seq_out) *seq_out = s2;
            return 0;
        }
    }
}

static int shared_snapshot_hist(shared_state_t *sh,
                                mpu_sample_t *raw, mpu_sample_t *avg,
                                int16_t hist_raw_out[HIST_SAMPLES][MPU_CHANNELS],
                                int16_t hist_avg_out[HIST_SAMPLES][MPU_CHANNELS],
                                unsigned *hist_pos_out, unsigned *hist_count_out)
{
    if (!sh || !raw || !avg || !hist_raw_out || !hist_avg_out || !hist_pos_out || !hist_count_out)
        return -1;

    for (;;) {
        unsigned s1 = atomic_load_explicit(&sh->seq, memory_order_acquire);
        if (s1 == 0 || (s1 & 1U))
            continue;

        *raw = sh->raw;
        *avg = sh->avg;
        *hist_pos_out = sh->hist_pos;
        *hist_count_out = sh->hist_count;
        memcpy(hist_raw_out, sh->hist_raw, sizeof(sh->hist_raw));
        memcpy(hist_avg_out, sh->hist_avg, sizeof(sh->hist_avg));

        unsigned s2 = atomic_load_explicit(&sh->seq, memory_order_acquire);
        if (s1 == s2)
            return 0;
    }
}

/**
 * @brief Publica un nuevo estado en memoria compartida.
 * @details Incrementa `seq` a odd antes de escribir y a even al finalizar.
 */
static void shared_publish(shared_state_t *sh, const mpu_sample_t *raw, const mpu_sample_t *avg, const struct timespec *ts, unsigned meas_us) {
    if (!sh || !raw || !avg) return;
    atomic_fetch_add_explicit(&sh->seq, 1U, memory_order_acq_rel); // odd
    sh->raw = *raw;
    sh->avg = *avg;
    if (ts) sh->ts = *ts;
    sh->meas_us = meas_us;

    /* Update ring buffer */
    unsigned pos = sh->hist_pos % HIST_SAMPLES;
    for (int i = 0; i < MPU_CHANNELS; ++i) {
        sh->hist_raw[pos][i] = raw->ch[i];
        sh->hist_avg[pos][i] = avg->ch[i];
    }
    sh->hist_pos = (pos + 1U) % HIST_SAMPLES;
    if (sh->hist_count < HIST_SAMPLES)
        sh->hist_count++;

    atomic_fetch_add_explicit(&sh->seq, 1U, memory_order_acq_rel); // even
}

static int channel_from_query(const char *path_with_query)
{
    if (!path_with_query)
        return 0;
    const char *q = strchr(path_with_query, '?');
    if (!q)
        return 0;
    q++; // skip '?'
    const char *p = strcasestr(q, "ch=");
    if (!p)
        return 0;
    p += 3;

    if (strncmp(p, "ax", 2) == 0) return 0;
    if (strncmp(p, "ay", 2) == 0) return 1;
    if (strncmp(p, "az", 2) == 0) return 2;
    if (*p == 't' || strncmp(p, "temp", 4) == 0) return 3;
    if (strncmp(p, "gx", 2) == 0) return 4;
    if (strncmp(p, "gy", 2) == 0) return 5;
    if (strncmp(p, "gz", 2) == 0) return 6;

    // numeric fallback: ch=0..6
    if (*p >= '0' && *p <= '6') return *p - '0';
    return 0;
}

static const char *channel_name(int ch)
{
    switch (ch) {
    case 0: return "AX";
    case 1: return "AY";
    case 2: return "AZ";
    case 3: return "T";
    case 4: return "GX";
    case 5: return "GY";
    case 6: return "GZ";
    default: return "AX";
    }
}

static const char *channel_unit(int ch)
{
    switch (ch) {
    case 0: case 1: case 2: return "m/s^2";
    case 3: return "°C";
    case 4: case 5: case 6: return "°/s";
    default: return "";
    }
}

static double channel_to_eng(int ch, int16_t v)
{
    switch (ch) {
    case 0: case 1: case 2: return accel_ms2_from_raw(v);
    case 3: return temp_c_from_raw(v);
    case 4: case 5: case 6: return gyro_dps_from_raw(v);
    default: return (double)v;
    }
}

static void svg_polyline(char *dst, size_t cap, size_t *used,
                         const double *y, size_t n,
                         double minv, double maxv,
                         int w, int h, int ml, int mt, int mr, int mb)
{
    if (!dst || !used || cap == 0 || !y || n == 0)
        return;

    const int iw = w - ml - mr;
    const int ih = h - mt - mb;
    if (iw <= 1 || ih <= 1)
        return;

    const double span = (maxv > minv) ? (maxv - minv) : 1.0;

    for (size_t i = 0; i < n; ++i) {
        const double xn = (n == 1) ? 0.0 : ((double)i / (double)(n - 1));
        const double yn = (y[i] - minv) / span;
        const int x = ml + (int)(xn * (double)iw);
        const int ypx = mt + (ih - (int)(yn * (double)ih));
        int nwr = snprintf(dst + *used, cap - *used, "%d,%d%s", x, ypx, (i + 1 == n) ? "" : " ");
        if (nwr < 0) return;
        *used += (size_t)nwr;
        if (*used >= cap) { *used = cap - 1; dst[*used] = '\0'; return; }
    }
}

/**
 * @brief Responde con el HTML del dashboard.
 * @details La actualización dinámica se hace vía SSE (`/events`) para mantener
 * una sola conexión abierta y evitar forks continuos.
 */
static void http_send_dashboard(int cfd,
                               const mpu_sample_t *raw, const mpu_sample_t *avg,
                               int ch,
                               const int16_t hist_raw[HIST_SAMPLES][MPU_CHANNELS],
                               const int16_t hist_avg[HIST_SAMPLES][MPU_CHANNELS],
                               unsigned hist_pos,
                               unsigned hist_count) {
    /* Render de dashboard + gráfico SVG de las últimas HIST_SAMPLES muestras.
     * La selección de canal via query param: /?ch=ax|ay|az|t|gx|gy|gz
     */

    if (ch < 0 || ch >= MPU_CHANNELS) ch = 0;

    // Estilo simple + valores embebidos.
    char body[32768];

    if (hist_count > HIST_SAMPLES) hist_count = HIST_SAMPLES;
    size_t npts = (size_t)hist_count;
    if (npts == 0) npts = 1;

    double series_raw[HIST_SAMPLES];
    double series_avg[HIST_SAMPLES];
    if (hist_count == 0) {
        series_raw[0] = channel_to_eng(ch, raw ? raw->ch[ch] : 0);
        series_avg[0] = channel_to_eng(ch, avg ? avg->ch[ch] : 0);
    } else {
        unsigned oldest = (hist_pos + HIST_SAMPLES - hist_count) % HIST_SAMPLES;
        for (unsigned i = 0; i < hist_count; ++i) {
            unsigned idx = (oldest + i) % HIST_SAMPLES;
            series_raw[i] = channel_to_eng(ch, hist_raw[idx][ch]);
            series_avg[i] = channel_to_eng(ch, hist_avg[idx][ch]);
        }
    }

    double minv = series_raw[0];
    double maxv = series_raw[0];
    for (size_t i = 0; i < npts; ++i) {
        if (series_raw[i] < minv) minv = series_raw[i];
        if (series_raw[i] > maxv) maxv = series_raw[i];
        if (series_avg[i] < minv) minv = series_avg[i];
        if (series_avg[i] > maxv) maxv = series_avg[i];
    }
    if (maxv <= minv) { maxv = minv + 1.0; }
    const double pad = (maxv - minv) * 0.10;
    minv -= pad;
    maxv += pad;

    char pts_raw[8192];
    char pts_avg[8192];
    size_t used_raw = 0, used_avg = 0;
    pts_raw[0] = '\0';
    pts_avg[0] = '\0';

    const int svg_w = 980;
    const int svg_h = 260;
    const int ml = 44, mt = 16, mr = 16, mb = 32;
    svg_polyline(pts_raw, sizeof(pts_raw), &used_raw, series_raw, npts, minv, maxv, svg_w, svg_h, ml, mt, mr, mb);
    svg_polyline(pts_avg, sizeof(pts_avg), &used_avg, series_avg, npts, minv, maxv, svg_w, svg_h, ml, mt, mr, mb);

    /* Arrays JS con el histórico inicial (en unidades físicas). */
    char js_raw[8192];
    char js_avg[8192];
    size_t jru = 0, jau = 0;
    js_raw[0] = '\0';
    js_avg[0] = '\0';
    for (size_t i = 0; i < npts; ++i) {
        int wr = snprintf(js_raw + jru, sizeof(js_raw) - jru, "%s%.6f", (i ? "," : ""), series_raw[i]);
        if (wr < 0) break;
        jru += (size_t)wr;
        if (jru >= sizeof(js_raw)) { js_raw[sizeof(js_raw) - 1] = '\0'; break; }

        int wa = snprintf(js_avg + jau, sizeof(js_avg) - jau, "%s%.6f", (i ? "," : ""), series_avg[i]);
        if (wa < 0) break;
        jau += (size_t)wa;
        if (jau >= sizeof(js_avg)) { js_avg[sizeof(js_avg) - 1] = '\0'; break; }
    }

    const double axr = accel_ms2_from_raw(raw ? raw->ch[0] : 0);
    const double ayr = accel_ms2_from_raw(raw ? raw->ch[1] : 0);
    const double azr = accel_ms2_from_raw(raw ? raw->ch[2] : 0);
    const double tr  = temp_c_from_raw(raw ? raw->ch[3] : 0);
    const double gxr = gyro_dps_from_raw(raw ? raw->ch[4] : 0);
    const double gyr = gyro_dps_from_raw(raw ? raw->ch[5] : 0);
    const double gzr = gyro_dps_from_raw(raw ? raw->ch[6] : 0);

    const double axa = accel_ms2_from_raw(avg ? avg->ch[0] : 0);
    const double aya = accel_ms2_from_raw(avg ? avg->ch[1] : 0);
    const double aza = accel_ms2_from_raw(avg ? avg->ch[2] : 0);
    const double ta  = temp_c_from_raw(avg ? avg->ch[3] : 0);
    const double gxa = gyro_dps_from_raw(avg ? avg->ch[4] : 0);
    const double gya = gyro_dps_from_raw(avg ? avg->ch[5] : 0);
    const double gza = gyro_dps_from_raw(avg ? avg->ch[6] : 0);

    int blen = snprintf(body, sizeof(body),
        "<!doctype html><html><head><meta charset='utf-8'><title>TD3 Server</title></head>"
        "<body style='margin:0;background:#0047AB;font-family:\"Trebuchet MS\",\"Segoe UI\",Verdana,sans-serif'>"

        "<div id='runDot' style='position:fixed;top:14px;left:14px;width:16px;height:16px;"
        "border-radius:50%%;background:#ff0000;border:2px solid #fff;opacity:1'></div>"

        "<div style='display:flex;justify-content:center;margin-top:18px'>"
        "  <div style='width:420px;background:#fff;border:2px solid #000;border-radius:18px;padding:16px 18px;text-align:center'>"
        "    <div style='font-weight:700'>UTN - FRBA</div>"
        "    <div style='font-weight:700;margin-top:6px'>TD3</div>"
        "    <div style='margin-top:6px'>Nicolas Pereyra</div>"
        "  </div>"
        "</div>"

        "<div style='max-width:1120px;margin:26px auto 0 auto;background:#fff;border:2px solid #000;border-radius:18px;padding:24px'>"
        "  <div style='display:flex;gap:18px;margin-top:18px'>"
        "    <div style='flex:1;border:2px solid #000;border-radius:18px;padding:14px 16px'>"
        "      <div style='display:flex;justify-content:space-between;font-weight:700'>"
        "        <span>Datos</span><span>Promedio</span><span>Crudo</span>"
        "      </div>"
        "      <div style='border-top:2px solid #000;margin-top:10px;padding-top:10px'>"
        "        <table style='width:100%%;border-collapse:collapse'>"
        "          <tr><td style='padding:6px 0'>AX [m/s^2]</td><td style='text-align:center'><span id='avg_ax'>%.3f</span></td><td style='text-align:right'><span id='raw_ax'>%.3f</span></td></tr>"
        "          <tr><td style='padding:6px 0'>AY [m/s^2]</td><td style='text-align:center'><span id='avg_ay'>%.3f</span></td><td style='text-align:right'><span id='raw_ay'>%.3f</span></td></tr>"
        "          <tr><td style='padding:6px 0'>AZ [m/s^2]</td><td style='text-align:center'><span id='avg_az'>%.3f</span></td><td style='text-align:right'><span id='raw_az'>%.3f</span></td></tr>"
        "          <tr><td style='padding:6px 0'>T [°C]</td><td style='text-align:center'><span id='avg_t'>%.2f</span></td><td style='text-align:right'><span id='raw_t'>%.2f</span></td></tr>"
        "          <tr><td style='padding:6px 0'>GX [°/s]</td><td style='text-align:center'><span id='avg_gx'>%.2f</span></td><td style='text-align:right'><span id='raw_gx'>%.2f</span></td></tr>"
        "          <tr><td style='padding:6px 0'>GY [°/s]</td><td style='text-align:center'><span id='avg_gy'>%.2f</span></td><td style='text-align:right'><span id='raw_gy'>%.2f</span></td></tr>"
        "          <tr><td style='padding:6px 0'>GZ [°/s]</td><td style='text-align:center'><span id='avg_gz'>%.2f</span></td><td style='text-align:right'><span id='raw_gz'>%.2f</span></td></tr>"
        "        </table>"
        "      </div>"
        "      <div style='display:flex;justify-content:space-between;align-items:center;margin-top:14px;gap:10px;flex-wrap:wrap'>"
        "        <div style='display:flex;gap:10px;align-items:center;flex-wrap:wrap'>"
        "          <button id='btnPause' type='button' style='height:34px;font-weight:700;border:2px solid #000;border-radius:10px;background:#fff'>Pausar</button>"
        "          <button id='btnResume' type='button' style='height:34px;font-weight:700;border:2px solid #000;border-radius:10px;background:#fff'>Reanudar</button>"
        "        </div>"
        "        <div style='font-size:12px;color:#222'>Leyenda: <span style='color:#d62728;font-weight:700'>Rojo=RAW</span> | <span style='color:#1f77b4;font-weight:700'>Azul=AVG</span></div>"
        "      </div>"
        "    </div>"
        "  </div>"
        "</div>"

        "<div style='max-width:1120px;margin:18px auto 28px auto;background:#fff;border:2px solid #000;border-radius:18px;padding:18px 24px'>"
        "  <div style='display:flex;justify-content:space-between;align-items:center;gap:16px;flex-wrap:wrap'>"
        "    <div style='font-weight:700'>Grafico ultimas %d mediciones (desplaza a la izquierda)</div>"
        "    <form method='GET' action='/' style='margin:0;display:flex;align-items:center;gap:10px'>"
        "      <label for='ch' style='font-weight:700'>Canal:</label>"
        "      <select id='ch' name='ch' style='height:34px'>"
        "        <option value='ax'%s>AX accel</option>"
        "        <option value='ay'%s>AY accel</option>"
        "        <option value='az'%s>AZ accel</option>"
        "        <option value='t'%s>T temp</option>"
        "        <option value='gx'%s>GX gyro</option>"
        "        <option value='gy'%s>GY gyro</option>"
        "        <option value='gz'%s>GZ gyro</option>"
        "      </select>"
        "      <button type='submit' style='height:34px;font-weight:700'>Ver</button>"
        "    </form>"
        "  </div>"
        "  <div style='margin-top:10px;color:#111'>"
        "    <div style='font-weight:700'>Canal seleccionado: %s [%s]</div>"
        "    <div style='font-size:12px;color:#444'>X: antiguedad de muestra (de -%zu a 0). Y: valor.</div>"
        "  </div>"
        "  <div style='margin-top:10px;overflow-x:auto'>"
        "    <svg id='plot' width='%d' height='%d' viewBox='0 0 %d %d' xmlns='http://www.w3.org/2000/svg' style='background:#fafafa;border:1px solid #000;border-radius:12px'>"
        "      <line x1='%d' y1='%d' x2='%d' y2='%d' stroke='#000' stroke-width='1'/>"
        "      <line x1='%d' y1='%d' x2='%d' y2='%d' stroke='#000' stroke-width='1'/>"
        "      <text id='y_unit' x='%d' y='%d' font-size='12' font-family='Verdana' fill='#111'>%s</text>"
        "      <text id='y_max' x='%d' y='%d' font-size='11' font-family='Verdana' fill='#111'>%.3f</text>"
        "      <text id='y_min' x='%d' y='%d' font-size='11' font-family='Verdana' fill='#111'>%.3f</text>"
        "      <text id='x_left' x='%d' y='%d' font-size='11' font-family='Verdana' fill='#111'>-%d</text>"
        "      <text id='x_right' x='%d' y='%d' font-size='11' font-family='Verdana' fill='#111'>0</text>"
        "      <polyline id='poly_raw' fill='none' stroke='#d62728' stroke-width='2' points='%s'/>"
        "      <polyline id='poly_avg' fill='none' stroke='#1f77b4' stroke-width='2' points='%s'/>"
        "      <text x='%d' y='%d' font-size='12' font-family='Verdana' fill='#d62728'>raw</text>"
        "      <text x='%d' y='%d' font-size='12' font-family='Verdana' fill='#1f77b4'>avg</text>"
        "    </svg>"
        "  </div>"
        "</div>"

        "<script>\n"
        "(function(){\n"
        "  function setTxt(id, v){ var e=document.getElementById(id); if(e) e.textContent=v; }\n"
        "  var dot=document.getElementById('runDot');\n"
        "  var N="
        "%d"
        ";\n"
        "  var ch="
        "%d"
        ";\n"
        "  var unit='"
        "%s"
        "';\n"
        "  var rawSeries=["
        "%s"
        "];\n"
        "  var avgSeries=["
        "%s"
        "];\n"
        "  function pad(arr){ if(arr.length===0) arr=[0]; while(arr.length<N) arr.unshift(arr[0]); if(arr.length>N) arr=arr.slice(arr.length-N); return arr; }\n"
        "  rawSeries=pad(rawSeries); avgSeries=pad(avgSeries);\n"
        "  var polyRaw=document.getElementById('poly_raw');\n"
        "  var polyAvg=document.getElementById('poly_avg');\n"
        "  var yUnit=document.getElementById('y_unit');\n"
        "  var yMax=document.getElementById('y_max');\n"
        "  var yMin=document.getElementById('y_min');\n"
        "  if(yUnit) yUnit.textContent=unit;\n"
        "  function pick(o){\n"
        "    if(!o) return 0;\n"
        "    switch(ch){\n"
        "      case 0: return o.ax_ms2;\n"
        "      case 1: return o.ay_ms2;\n"
        "      case 2: return o.az_ms2;\n"
        "      case 3: return o.temp_c;\n"
        "      case 4: return o.gx_dps;\n"
        "      case 5: return o.gy_dps;\n"
        "      case 6: return o.gz_dps;\n"
        "      default: return o.ax_ms2;\n"
        "    }\n"
        "  }\n"
        "  function render(){\n"
        "    var svgW="
        "%d"
        ", svgH="
        "%d"
        ", ml="
        "%d"
        ", mt="
        "%d"
        ", mr="
        "%d"
        ", mb="
        "%d"
        ";\n"
        "    var iw=svgW-ml-mr, ih=svgH-mt-mb;\n"
        "    var min=rawSeries[0], max=rawSeries[0];\n"
        "    for(var i=0;i<rawSeries.length;i++){ var a=rawSeries[i], b=avgSeries[i]; if(a<min)min=a; if(a>max)max=a; if(b<min)min=b; if(b>max)max=b; }\n"
        "    if(!(max>min)){ max=min+1; }\n"
        "    var pad=(max-min)*0.10; min-=pad; max+=pad;\n"
        "    if(yMax) yMax.textContent=max.toFixed(3);\n"
        "    if(yMin) yMin.textContent=min.toFixed(3);\n"
        "    function pts(arr){\n"
        "      var out='';\n"
        "      for(var i=0;i<arr.length;i++){\n"
        "        var xn=(arr.length===1)?0:(i/(arr.length-1));\n"
        "        var yn=(arr[i]-min)/(max-min);\n"
        "        var x=Math.round(ml + xn*iw);\n"
        "        var y=Math.round(mt + (ih - yn*ih));\n"
        "        out += x+','+y+(i+1===arr.length?'':' ');\n"
        "      }\n"
        "      return out;\n"
        "    }\n"
        "    if(polyRaw) polyRaw.setAttribute('points', pts(rawSeries));\n"
        "    if(polyAvg) polyAvg.setAttribute('points', pts(avgSeries));\n"
        "  }\n"
        "  render();\n"
        "  var es=null;\n"
        "  var paused=false;\n"
        "  var bP=document.getElementById('btnPause');\n"
        "  var bR=document.getElementById('btnResume');\n"
        "  function setBtns(){ if(!bP||!bR) return; bP.disabled=paused; bR.disabled=!paused; }\n"
        "  function stopStream(){\n"
        "    paused=true;\n"
        "    if(es){ try{ es.close(); }catch(e){} es=null; }\n"
        "    if(dot){ dot.style.background='#777777'; }\n"
        "  }\n"
        "  function startStream(){\n"
        "    if(es) return;\n"
        "    paused=false;\n"
        "    if(dot){ dot.style.background='#ff0000'; }\n"
        "    try {\n"
        "      es=new EventSource('/events');\n"
        "      es.onmessage=function(ev){\n"
        "        try {\n"
        "          var d=JSON.parse(ev.data);\n"
        "          if(dot){ dot.style.background='#00c853'; setTimeout(function(){ if(!paused){dot.style.background='#ff0000';} }, 120); }\n"
        "          if(d && d.raw && d.avg){\n"
        "            setTxt('raw_ax', (d.raw.ax_ms2).toFixed(3)); setTxt('avg_ax', (d.avg.ax_ms2).toFixed(3));\n"
        "            setTxt('raw_ay', (d.raw.ay_ms2).toFixed(3)); setTxt('avg_ay', (d.avg.ay_ms2).toFixed(3));\n"
        "            setTxt('raw_az', (d.raw.az_ms2).toFixed(3)); setTxt('avg_az', (d.avg.az_ms2).toFixed(3));\n"
        "            setTxt('raw_t',  (d.raw.temp_c).toFixed(2));  setTxt('avg_t',  (d.avg.temp_c).toFixed(2));\n"
        "            setTxt('raw_gx', (d.raw.gx_dps).toFixed(2));  setTxt('avg_gx', (d.avg.gx_dps).toFixed(2));\n"
        "            setTxt('raw_gy', (d.raw.gy_dps).toFixed(2));  setTxt('avg_gy', (d.avg.gy_dps).toFixed(2));\n"
        "            setTxt('raw_gz', (d.raw.gz_dps).toFixed(2));  setTxt('avg_gz', (d.avg.gz_dps).toFixed(2));\n"
        "            rawSeries.push(pick(d.raw)); avgSeries.push(pick(d.avg));\n"
        "            if(rawSeries.length>N) rawSeries.shift();\n"
        "            if(avgSeries.length>N) avgSeries.shift();\n"
        "            render();\n"
        "          }\n"
        "        } catch(e){}\n"
        "      };\n"
        "      es.onerror=function(){ if(!paused){ /* keep trying */ } };\n"
        "    } catch(e){ es=null; }\n"
        "  }\n"
        "  if(bP) bP.onclick=function(){ stopStream(); setBtns(); };\n"
        "  if(bR) bR.onclick=function(){ startStream(); setBtns(); };\n"
        "  startStream();\n"
        "  setBtns();\n"
        "})();\n"
        "</script>\n"
        "</body></html>",
        axa, axr, aya, ayr, aza, azr, ta, tr, gxa, gxr, gya, gyr, gza, gzr,
        HIST_SAMPLES,
        (ch==0?" selected":""), (ch==1?" selected":""), (ch==2?" selected":""), (ch==3?" selected":""),
        (ch==4?" selected":""), (ch==5?" selected":""), (ch==6?" selected":""),
        channel_name(ch), channel_unit(ch), (size_t)(HIST_SAMPLES - 1),
        svg_w, svg_h, svg_w, svg_h,
        ml, svg_h - mb, svg_w - mr, svg_h - mb,
        ml, mt, ml, svg_h - mb,
        ml + 2, mt + 12, channel_unit(ch),
        4, mt + 12, maxv,
        4, svg_h - mb + 12, minv,
        ml, svg_h - 8, (int)(HIST_SAMPLES - 1),
        svg_w - mr - 10, svg_h - 8,
        pts_raw, pts_avg,
        ml + 8, mt + 14,
        ml + 8, mt + 30,
        (int)HIST_SAMPLES,
        ch,
        channel_unit(ch),
        js_raw,
        js_avg,
        svg_w, svg_h, ml, mt, mr, mb);

    if (blen < 0) blen = 0;
    if ((size_t)blen >= sizeof(body)) blen = (int)sizeof(body) - 1;

    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 Ok\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Cache-Control: no-store\r\n"
        "Content-Length: %d\r\n"
        "\r\n", blen);
    (void)write(cfd, hdr, (size_t)n);
    (void)write(cfd, body, (size_t)blen);
}

static void http_send_sse_stream(int cfd) {
    const char *hdr =
        "HTTP/1.1 200 Ok\r\n"
        "Content-Type: text/event-stream\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    (void)send(cfd, hdr, strlen(hdr), 0);

    for (;;) {
        mpu_sample_t raw, avg;
        struct timespec ts;
        unsigned meas_us = 0;
        if (shared_snapshot(g_sh, &raw, &avg, &ts, &meas_us, NULL) < 0) {
            struct timespec sl = {.tv_sec = 0, .tv_nsec = 200 * 1000 * 1000};
            nanosleep(&sl, NULL);
            continue;
        }

        char json[512];
        int jlen = snprintf(json, sizeof(json),
            "{\"meas_ms\":%.3f,"
            "\"raw\":{\"ax_ms2\":%.6f,\"ay_ms2\":%.6f,\"az_ms2\":%.6f,\"temp_c\":%.4f,\"gx_dps\":%.4f,\"gy_dps\":%.4f,\"gz_dps\":%.4f},"
            "\"avg\":{\"ax_ms2\":%.6f,\"ay_ms2\":%.6f,\"az_ms2\":%.6f,\"temp_c\":%.4f,\"gx_dps\":%.4f,\"gy_dps\":%.4f,\"gz_dps\":%.4f}}",
            (double)meas_us / 1000.0,
            accel_ms2_from_raw(raw.ch[0]), accel_ms2_from_raw(raw.ch[1]), accel_ms2_from_raw(raw.ch[2]), temp_c_from_raw(raw.ch[3]),
            gyro_dps_from_raw(raw.ch[4]), gyro_dps_from_raw(raw.ch[5]), gyro_dps_from_raw(raw.ch[6]),
            accel_ms2_from_raw(avg.ch[0]), accel_ms2_from_raw(avg.ch[1]), accel_ms2_from_raw(avg.ch[2]), temp_c_from_raw(avg.ch[3]),
            gyro_dps_from_raw(avg.ch[4]), gyro_dps_from_raw(avg.ch[5]), gyro_dps_from_raw(avg.ch[6]));
        if (jlen < 0) jlen = 0;
        if ((size_t)jlen >= sizeof(json)) jlen = (int)sizeof(json) - 1;

        char msg[700];
        int mlen = snprintf(msg, sizeof(msg), "data: %.*s\n\n", jlen, json);
        if (mlen < 0) mlen = 0;
        if ((size_t)mlen > sizeof(msg)) mlen = (int)sizeof(msg);

        ssize_t w = send(cfd, msg, (size_t)mlen, 0);
        if (w <= 0) {
            break;
        }

        struct timespec sl = {.tv_sec = 0, .tv_nsec = 200 * 1000 * 1000};
        nanosleep(&sl, NULL);
    }
}

/* PNG mínimo 1x1 (rojo). Generado para este proyecto (sin copyright). */
static const unsigned char k_png_1x1_red[] = {
    0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,
    0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,
    0x08,0x02,0x00,0x00,0x00,0x90,0x77,0x53,
    0xDE,0x00,0x00,0x00,0x0C,0x49,0x44,0x41,
    0x54,0x08,0xD7,0x63,0xF8,0xCF,0xC0,0x00,
    0x00,0x03,0x01,0x01,0x00,0x18,0xDD,0x8D,
    0xB6,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,
    0x44,0xAE,0x42,0x60,0x82
};

static void http_send_png_1x1(int cfd)
{
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 Ok\r\n"
        "Content-Type: image/png\r\n"
        "Cache-Control: no-store\r\n"
        "Content-Length: %zu\r\n\r\n",
        sizeof(k_png_1x1_red));
    (void)write(cfd, hdr, (size_t)n);
    (void)write(cfd, k_png_1x1_red, sizeof(k_png_1x1_red));
}

/**
 * @brief Convierte aceleración cruda a m/s².
 * @details Asume ACCEL_AFS=2g (16384 LSB/g).
 */
static double accel_ms2_from_raw(int16_t v) {
    // Asumimos ACCEL_AFS=2g => 16384 LSB/g.
    const double g0 = 9.80665;
    return ((double)v / 16384.0) * g0;
}

/**
 * @brief Convierte giroscopio crudo a °/s.
 * @details Asume GYRO_FS=1000dps (32.8 LSB/(°/s)).
 */
static double gyro_dps_from_raw(int16_t v) {
    // Asumimos GYRO_FS=1000dps => 32.8 LSB/(°/s).
    return ((double)v / 32.8);
}

/**
 * @brief Convierte temperatura cruda a °C.
 * @details Datasheet: Temp(°C) = (TEMP_OUT / 340) + 36.53.
 */
static double temp_c_from_raw(int16_t v) {
    // Datasheet: Temp(°C) = (TEMP_OUT / 340) + 36.53
    return ((double)v / 340.0) + 36.53;
}

/**
 * @brief Responde con el JSON de datos (raw + avg) para el dashboard.
 */
static TD3_UNUSED void http_send_json_data(int cfd, const mpu_sample_t *raw, const mpu_sample_t *avg, unsigned meas_us, const struct timespec *ts) {
    (void)ts;
    char body[1024];
    double meas_ms = (double)meas_us / 1000.0;
    int blen = snprintf(body, sizeof(body),
        "{\"meas_ms\":%.3f,"
        "\"raw\":{\"ax_ms2\":%.6f,\"ay_ms2\":%.6f,\"az_ms2\":%.6f,\"temp_c\":%.4f,\"gx_dps\":%.4f,\"gy_dps\":%.4f,\"gz_dps\":%.4f},"
        "\"avg\":{\"ax_ms2\":%.6f,\"ay_ms2\":%.6f,\"az_ms2\":%.6f,\"temp_c\":%.4f,\"gx_dps\":%.4f,\"gy_dps\":%.4f,\"gz_dps\":%.4f}}",
        meas_ms,
        accel_ms2_from_raw(raw ? raw->ch[0] : 0), accel_ms2_from_raw(raw ? raw->ch[1] : 0), accel_ms2_from_raw(raw ? raw->ch[2] : 0),
        temp_c_from_raw(raw ? raw->ch[3] : 0),
        gyro_dps_from_raw(raw ? raw->ch[4] : 0), gyro_dps_from_raw(raw ? raw->ch[5] : 0), gyro_dps_from_raw(raw ? raw->ch[6] : 0),
        accel_ms2_from_raw(avg ? avg->ch[0] : 0), accel_ms2_from_raw(avg ? avg->ch[1] : 0), accel_ms2_from_raw(avg ? avg->ch[2] : 0),
        temp_c_from_raw(avg ? avg->ch[3] : 0),
        gyro_dps_from_raw(avg ? avg->ch[4] : 0), gyro_dps_from_raw(avg ? avg->ch[5] : 0), gyro_dps_from_raw(avg ? avg->ch[6] : 0));

    if (blen < 0) blen = 0;
    if ((size_t)blen >= sizeof(body)) blen = (int)sizeof(body) - 1;

    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 Ok\r\n"
        "Content-Type: application/json; charset=UTF-8\r\n"
        "Cache-Control: no-store\r\n"
        "Content-Length: %d\r\n\r\n", blen);
    (void)write(cfd, hdr, (size_t)n);
    (void)write(cfd, body, (size_t)blen);
}

/** @brief Respuesta HTTP 400 con un mensaje simple. */
static void http_send_400(int cfd, const char *msg) {
    char body[256];
    int blen = snprintf(body, sizeof(body),
        "<html><body><h1>400</h1><p>%s</p></body></html>", msg ? msg : "Bad method");
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 400 Bad method\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %d\r\n\r\n", blen);
    (void)write(cfd, hdr, (size_t)n);
    (void)write(cfd, body, (size_t)blen);
}

/** @brief Respuesta HTTP 404 para recursos desconocidos. */
static void http_send_404(int cfd) {
    const char *body = "<html><body><h1>404</h1><p>Bad resource</p></body></html>";
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 404 Bad resource\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %zu\r\n\r\n", strlen(body));
    (void)write(cfd, hdr, (size_t)n);
    (void)write(cfd, body, strlen(body));
}

/** @brief Respuesta HTTP 500 con un mensaje simple. */
static void http_send_500(int cfd, const char *msg) {
    char body[256];
    int blen = snprintf(body, sizeof(body),
        "<html><body><h1>500</h1><p>%s</p></body></html>", msg ? msg : "Internal error");
    char hdr[256];
    int n = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 500 Internal error\r\n"
        "Content-Type: text/html; charset=UTF-8\r\n"
        "Content-Length: %d\r\n\r\n", blen);
    (void)write(cfd, hdr, (size_t)n);
    (void)write(cfd, body, (size_t)blen);
}

/** @brief Retorna 1 si `s` comienza con el prefijo `p`. */
static int starts_with(const char *s, const char *p) {
    return strncmp(s, p, strlen(p)) == 0;
}

/** @brief Saltea whitespace ASCII típico de headers HTTP. */
static const char *skip_ws(const char *s) {
    while (s && (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n')) ++s;
    return s;
}

/** @brief Detecta fin de headers HTTP (`\r\n\r\n` o `\n\n`). */
static int has_http_header_end(const char *buf) {
    if (!buf) return 0;
    return (strstr(buf, "\r\n\r\n") != NULL) || (strstr(buf, "\n\n") != NULL);
}

/**
 * @brief Parsea `Content-Length` desde headers HTTP.
 * @return Valor >= 0 (0 si no está o es inválido).
 */
static TD3_UNUSED long parse_content_length(const char *headers) {
    if (!headers) return 0;
    const char *p = strcasestr(headers, "Content-Length:");
    if (!p) return 0;
    p += strlen("Content-Length:");
    while (*p == ' ' || *p == '\t') ++p;
    char *endp = NULL;
    long v = strtol(p, &endp, 10);
    if (endp == p || v < 0) return 0;
    return v;
}

/**
 * @brief Consume el body de una request HTTP para dejar el socket limpio.
 */
static TD3_UNUSED void drain_http_body(int cfd, const char *buf, size_t buf_len, long content_len) {
    if (content_len <= 0) return;
    if (!buf) buf_len = 0;

    // Cuántos bytes del body ya están dentro de buf
    const char *end = NULL;
    if (buf) {
        end = strstr(buf, "\r\n\r\n");
        if (!end) end = strstr(buf, "\n\n");
    }

    size_t already = 0;
    if (end) {
        size_t hdr_end_len = (end[0] == '\r') ? 4 : 2;
        size_t hdr_bytes = (size_t)((end - buf) + (ptrdiff_t)hdr_end_len);
        if (buf_len > hdr_bytes) already = buf_len - hdr_bytes;
    }

    long remaining = content_len - (long)already;
    if (remaining <= 0) return;

    char tmp[256];
    while (remaining > 0) {
        ssize_t r = recv(cfd, tmp, (remaining > (long)sizeof(tmp)) ? sizeof(tmp) : (size_t)remaining, 0);
        if (r <= 0) break;
        remaining -= r;
    }
}

/**
 * @brief Loop dedicado de sensado.
 * @details
 * Lee del device, calcula el promedio con ventana `W` y publica en shared memory.
 * Esta función corre en el proceso hijo "sensor".
 */
static void sensor_loop(int devfd, int filter_window) {
    (void)filter_window;

    /* Requisito del enunciado:
     * - aplicar filtro de media con ventana configurada
     * - implementación en archivo fuente separado (filter.c)
     */
    int W = 2;
    if (g_sh) {
        int w = atomic_load(&g_sh->cfg_filter_window);
        if (w >= 0) W = w;
    }
    if (W < 0) W = 0;
    if (W > (MAX_AVG_SAMPLES - 1) / 2) W = (MAX_AVG_SAMPLES - 1) / 2;

    ma_filter_t filt;
    if (ma_init(&filt, (size_t)MAX_AVG_SAMPLES, W) < 0) {
        fprintf(stderr, "[sensor] ma_init failed\n");
        return;
    }

    const int batch = 10; // amortiza overhead del driver/lecturas
    mpu_sample_t tmp[batch];

    for (;;) {
        /* Permitir recarga en caliente via SIGUSR2 (padre actualiza shared cfg). */
        if (g_sh) {
            int newW = atomic_load(&g_sh->cfg_filter_window);
            if (newW < 0) newW = 0;
            if (newW > (MAX_AVG_SAMPLES - 1) / 2) newW = (MAX_AVG_SAMPLES - 1) / 2;
            if (newW != W) {
                W = newW;
                ma_free(&filt);
                if (ma_init(&filt, (size_t)MAX_AVG_SAMPLES, W) < 0) {
                    fprintf(stderr, "[sensor] ma_init failed after reload\n");
                    return;
                }
                td3_logf(TD3_ANSI_GREEN, "[sensor] filter window updated: W=%d\n", W);
            }
        }

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        if (device_read_samples(devfd, tmp, (size_t)batch) < 0) {
            // Si falla, no hacemos busy-loop
            struct timespec sl = {.tv_sec = 0, .tv_nsec = 50 * 1000 * 1000};
            nanosleep(&sl, NULL);
            continue;
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        long long dt_ns = (long long)(t1.tv_sec - t0.tv_sec) * 1000000000LL + (long long)(t1.tv_nsec - t0.tv_nsec);
        if (dt_ns < 0) dt_ns = 0;
        unsigned meas_us = (unsigned)((dt_ns / 1000LL) / (long long)batch);

        for (int i = 0; i < batch; ++i) {
            mpu_sample_t raw = tmp[i];
            mpu_sample_t avg = {0};

            ma_push(&filt, &raw);
            ma_get_centered(&filt, &avg);

            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            shared_publish(g_sh, &raw, &avg, &ts, meas_us);
        }
    }
}

static ssize_t recv_http_headers(int cfd, char *buf, size_t cap, int total_timeout_ms) {
    if (!buf || cap < 2) return 0;

    size_t used = 0;
    buf[0] = '\0';

    // Leer en pequeñas tandas hasta obtener fin de headers o agotar timeout.
    while (total_timeout_ms > 0 && used < (cap - 1)) {
        struct pollfd pfd;
        pfd.fd = cfd;
        pfd.events = POLLIN;

        int step = 100;
        if (step > total_timeout_ms) step = total_timeout_ms;

        int pr = poll(&pfd, 1, step);
        total_timeout_ms -= step;

        if (pr <= 0) {
            // timeout o EINTR: seguimos hasta agotar total_timeout_ms
            continue;
        }
        if (!(pfd.revents & POLLIN)) {
            continue;
        }

        ssize_t r = recv(cfd, buf + used, (cap - 1) - used, 0);
        if (r <= 0) {
            break;
        }
        used += (size_t)r;
        buf[used] = '\0';
        if (has_http_header_end(buf)) {
            break;
        }
    }

    return (ssize_t)used;
}

// Manejo de una conexión de cliente (en el hijo)
/**
 * @brief Atiende un cliente conectado.
 * @details
 * - Si detecta HTTP, responde dashboard (`/`) o JSON (`/data.json`).
 * - Si no es HTTP, hace streaming binario (raw+avg) esperando cambios de `seq`.
 */
static void handle_client(int cfd, int devfd, int filter_window,
                          const char *peer_ip, int peer_port) {
    (void)devfd;
    (void)filter_window;

    int active = g_sh ? atomic_load(&g_sh->active_clients) : -1;
    int max_active = g_sh ? atomic_load(&g_sh->cfg_max_active_conns) : g_cfg.max_active_conns;
    td3_logf(TD3_ANSI_YELLOW, "[client] %d/%d pid=%d conectado desde %s:%d\n",
             active, max_active, (int)getpid(), peer_ip ? peer_ip : "?", peer_port);

    // Leer una solicitud simple (hasta 1KB) para decidir si es HTTP o binario.
    // OJO: un navegador manda HTTP, pero puede no estar listo al instante.
    // Esperamos un poco; si no llega nada, asumimos protocolo binario.
    char req[1024];
    ssize_t r = recv_http_headers(cfd, req, sizeof(req), 1500);
    if (r < 0) r = 0;
    req[(size_t)r] = '\0';

    const char *req0 = skip_ws(req);
    int is_http = starts_with(req0, "GET ") || starts_with(req0, "HEAD ") || starts_with(req0, "POST ");

    if (is_http) {
        // Rutas:
        //   GET /        -> HTML con datos + botón vivo
        //   POST /vivo   -> HTML con datos actualizados (trigger por botón)
        //   GET /data    -> binario (14 int16)
        char method[8], path[256], proto[16];
        method[0]=path[0]=proto[0]='\0';
        sscanf(req0, "%7s %255s %15s", method, path, proto);

        /* Apéndice I (PDF):
         * - si método no es GET => 400 Bad method
         * - si recurso no es / o /Archivo-grafico => 404 Bad resource
         */
        if (strcmp(method, "GET") != 0) {
            http_send_400(cfd, "Bad method");
            return;
        }

        char base_path[256];
        strncpy(base_path, path, sizeof(base_path));
        base_path[sizeof(base_path) - 1] = '\0';
        char *q = strchr(base_path, '?');
        if (q) *q = '\0';

        if (strcmp(base_path, "/") == 0) {
            mpu_sample_t raw, avg;
            int16_t hraw[HIST_SAMPLES][MPU_CHANNELS];
            int16_t havg[HIST_SAMPLES][MPU_CHANNELS];
            unsigned hpos = 0, hcount = 0;
            int ch = channel_from_query(path);

            if (shared_snapshot_hist(g_sh, &raw, &avg, hraw, havg, &hpos, &hcount) < 0) {
                http_send_500(cfd, "No sensor data yet");
                return;
            }
            http_send_dashboard(cfd, &raw, &avg, ch, hraw, havg, hpos, hcount);
            return;
        }

        if (strcmp(base_path, "/events") == 0) {
            http_send_sse_stream(cfd);
            return;
        }

        if (strcmp(base_path, "/Archivo-grafico") == 0) {
            http_send_png_1x1(cfd);
            return;
        }

        http_send_404(cfd);
        return;
    }

    /* No-HTTP: protocolo simple request/response.
     * Por cada solicitud del cliente (1 byte cualquiera), respondemos raw(7)+avg(7) int16_t.
     */
    for (;;) {
        unsigned char reqb;
        ssize_t rr = recv(cfd, &reqb, 1, 0);
        if (rr == 0) break;
        if (rr < 0) {
            if (errno == EINTR) continue;
            break;
        }

        mpu_sample_t raw;
        mpu_sample_t avg;
        struct timespec ts;
        unsigned meas_us = 0;
        if (shared_snapshot(g_sh, &raw, &avg, &ts, &meas_us, NULL) < 0)
            continue;

        int16_t payload[MPU_CHANNELS * 2];
        for (int i = 0; i < MPU_CHANNELS; ++i) payload[i] = raw.ch[i];
        for (int i = 0; i < MPU_CHANNELS; ++i) payload[MPU_CHANNELS + i] = avg.ch[i];

        size_t off = 0;
        while (off < sizeof(payload)) {
            ssize_t w = send(cfd, (const char *)payload + off, sizeof(payload) - off, 0);
            if (w < 0) {
                if (errno == EINTR) continue;
                off = sizeof(payload);
                break;
            }
            if (w == 0) {
                off = sizeof(payload);
                break;
            }
            off += (size_t)w;
        }
    }
}

/**
 * @brief Lanza el servidor TCP concurrente.
 * @details
 * Crea memoria compartida, forkea el proceso sensor y luego acepta clientes.
 * Permite recargar config parcial con SIGUSR2.
 */
int run_server(const char *bind_addr, int port,
               const char *dev_path,
               const char *config_path)
{
    g_config_path = config_path;
    if (cfg_load(config_path, &g_cfg) < 0) {
        fprintf(stderr, "No se pudo cargar config, usando defaults.\n");
        cfg_apply_defaults(&g_cfg);
    }

    // Abrir dispositivo del driver (bloqueante)
    int devfd = open(dev_path, O_RDONLY);
    if (devfd < 0) {
        perror("open device");
        return -1;
    }
    set_cloexec(devfd);

    // Memoria compartida para que el hijo sensor publique y los hijos de clientes lean.
    g_sh = mmap(NULL, sizeof(*g_sh), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (g_sh == MAP_FAILED) {
        perror("mmap shared");
        close(devfd);
        return -1;
    }
    memset(g_sh, 0, sizeof(*g_sh));
    atomic_store(&g_sh->seq, 0U);
    atomic_store(&g_sh->cfg_max_active_conns, g_cfg.max_active_conns);
    atomic_store(&g_sh->cfg_filter_window, g_cfg.filter_window);
    atomic_store(&g_sh->active_clients, 0);

    // Hijo dedicado a sensar y actualizar la tabla compartida.
    g_sensor_pid = fork();
    if (g_sensor_pid < 0) {
        perror("fork sensor");
        munmap(g_sh, sizeof(*g_sh));
        g_sh = NULL;
        close(devfd);
        return -1;
    }
    if (g_sensor_pid == 0) {
        // En el hijo sensor no necesitamos SIGUSR2/SIGCHLD del server.
        signal(SIGUSR2, SIG_IGN);
        signal(SIGCHLD, SIG_IGN);
        td3_logf(TD3_ANSI_GREEN, "[sensor] pid=%d leyendo_mpu6050 W=%d\n",
                 (int)getpid(), g_cfg.filter_window);
        sensor_loop(devfd, g_cfg.filter_window);
        _exit(0);
    }

    // Padre: no leerá el device (lo hace el hijo sensor). Cerramos para evitar herencia a hijos clientes.
    close(devfd);
    devfd = -1;

    // Señales
    struct sigaction sa_usr2 = {0}, sa_chld = {0};
    sa_usr2.sa_handler = on_sigusr2;
    sigemptyset(&sa_usr2.sa_mask);
    /* Importante: NO usar SA_RESTART para SIGUSR2.
     * Queremos que interrupta accept() (EINTR) y así el padre aplique la recarga
     * inmediatamente, sin esperar a que llegue un cliente nuevo.
     */
    sa_usr2.sa_flags = 0;
    sigaction(SIGUSR2, &sa_usr2, NULL);

    sa_chld.sa_handler = on_sigchld;
    sigemptyset(&sa_chld.sa_mask);
    sa_chld.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa_chld, NULL);

    /* Evita que un write/send a un socket cerrado mate el proceso con SIGPIPE. */
    signal(SIGPIPE, SIG_IGN);

    // Socket escucha
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sfd < 0) { perror("socket"); close(devfd); return -1; }
    set_cloexec(sfd);

    int yes = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    addr.sin_addr.s_addr = bind_addr ? inet_addr(bind_addr) : htonl(INADDR_ANY);

    if (bind(sfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sfd); close(devfd);
        return -1;
    }
    if (listen(sfd, g_cfg.backlog) < 0) {
        perror("listen");
        close(sfd); close(devfd);
        return -1;
    }

        td3_logf(TD3_ANSI_BLUE,
                 "[parent] pid=%d escuchando en %s:%d | backlog=%d | max_active=%d | W=%d\n",
                 (int)getpid(), bind_addr ? bind_addr : "0.0.0.0", port, g_cfg.backlog,
                 g_cfg.max_active_conns, g_cfg.filter_window);
        td3_logf(TD3_ANSI_BLUE,
                 "[parent] sensor_pid=%d | dashboard: http://%s:%d/\n",
                 (int)g_sensor_pid, bind_addr ? bind_addr : "0.0.0.0", port);

    // Loop de aceptación
    for (;;) {
        if (g_reload_cfg) {
            g_reload_cfg = 0;
            server_config_t fresh;
            if (cfg_load(g_config_path, &fresh) == 0) {
                int old_max = g_cfg.max_active_conns;
                int old_w   = g_cfg.filter_window;
                /* Requisito enunciado: SIGUSR2 recarga max_active_conns y filter_window.
                 * backlog no se recarga (sólo al inicio).
                 */
                g_cfg.max_active_conns = fresh.max_active_conns;
                g_cfg.filter_window    = fresh.filter_window;
                if (g_sh) {
                    atomic_store(&g_sh->cfg_max_active_conns, g_cfg.max_active_conns);
                    atomic_store(&g_sh->cfg_filter_window, g_cfg.filter_window);
                }

                if (g_cfg.max_active_conns != old_max || g_cfg.filter_window != old_w) {
                    td3_logf(TD3_ANSI_RED,
                             "[parent] config recargada (SIGUSR2): max_active=%d->%d | W=%d->%d\n",
                             old_max, g_cfg.max_active_conns, old_w, g_cfg.filter_window);
                } else {
                    td3_logf(TD3_ANSI_RED,
                             "[parent] config recargada (SIGUSR2): sin cambios (max_active=%d | W=%d)\n",
                             g_cfg.max_active_conns, g_cfg.filter_window);
                }
            } else {
                td3_logf(TD3_ANSI_RED,
                         "[parent] config recarga fallida (SIGUSR2): se mantienen valores actuales\n");
            }
        }

        struct sockaddr_in cli; socklen_t clilen = sizeof(cli);
        int cfd = accept(sfd, (struct sockaddr*)&cli, &clilen);
        if (cfd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        set_cloexec(cfd);

        char cip[64] = "?";
        const char *pp = inet_ntop(AF_INET, &cli.sin_addr, cip, sizeof(cip));
        (void)pp;
        int cport = (int)ntohs(cli.sin_port);

        int max_active = g_sh ? atomic_load(&g_sh->cfg_max_active_conns) : g_cfg.max_active_conns;
        /* Reserva un slot ANTES del fork para evitar carrera con SIGCHLD. */
        int prev = g_sh ? atomic_fetch_add(&g_sh->active_clients, 1) : 0;
        if (g_sh && prev >= max_active) {
            atomic_fetch_sub(&g_sh->active_clients, 1);
            close(cfd);
            continue;
        }

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            if (g_sh) atomic_fetch_sub(&g_sh->active_clients, 1);
            close(cfd);
            continue;
        }

        if (pid == 0) {
            // Hijo: no necesita socket de escucha
            close(sfd);
            /* No permitir que SIGUSR2 interrumpa la comunicación del hijo. */
            signal(SIGUSR2, SIG_IGN);
            signal(SIGPIPE, SIG_IGN);
            // IMPORTANTE: no reabrir el device por conexión.
            // El driver puede ser de apertura exclusiva (open_semaphore), lo que haría
            // que el hijo falle antes de responder HTTP y el cliente vea "connection reset".
            // Usamos el fd heredado del padre.
            handle_client(cfd, devfd, g_cfg.filter_window, cip, cport);

            int active = g_sh ? atomic_load(&g_sh->active_clients) : -1;
            int max_active = g_sh ? atomic_load(&g_sh->cfg_max_active_conns) : g_cfg.max_active_conns;
            /* Nota: el decremento real de active_clients lo hace el padre en SIGCHLD.
             * Para que el log sea intuitivo, mostramos cuántos quedan tras irse.
             */
            int active_after = (active > 0) ? (active - 1) : active;
            td3_logf(TD3_ANSI_ORANGE,
                     "[client] %d/%d pid=%d se va: cliente %d/%d se desconecto (%s:%d)\n",
                     active_after, max_active, (int)getpid(), active_after, max_active, cip, cport);

            close(cfd);
            _exit(0);
        } else {
            // Padre
            td3_logf(TD3_ANSI_BLUE,
                     "[parent] cliente %d/%d conectado -> child_pid=%d (%s:%d)\n",
                     prev + 1, max_active, (int)pid, cip, cport);
            close(cfd);
        }
    }

    // (Nunca llega)
    close(sfd);
    if (devfd >= 0) close(devfd);
    return 0;
}
