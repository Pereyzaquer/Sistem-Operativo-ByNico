/**
 * @file mpu6050_driver.c
 * @brief Driver de kernel Linux para el sensor MPU6050 en BeagleBone Black (AM335x).
 * @details
 * Este módulo expone un char device (`/dev/mpu6050`) y se registra como platform driver
 * (match por Device Tree `compatible="i2c_mpu6050"`).
 *
 * A diferencia de un driver I2C típico (i2c_client), esta implementación accede al
 * controlador I2C2 por MMIO + interrupciones, administrando el protocolo I2C en bajo nivel.
 */

#define MY_VERSION 11

/* Core Linux kernel headers for this driver */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/semaphore.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/random.h>
#include <linux/errno.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/atomic.h>
#include <linux/vmalloc.h>
#include <linux/gpio/consumer.h>

/* Project headers (SoC + sensor + shared structs/macros) */
#include "ti_bb.h"
#include "mpu6050.h"
#include "extras.h"
#include "mpu6050_ioctl.h"

/* Module metadata */
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
MODULE_AUTHOR("Nicope");
MODULE_DESCRIPTION("MPU6050 Driver");

/* Forward declarations required before use */
static const struct of_device_id mpu6050_of_match[];

static int mpu6050_probe(struct platform_device *pdev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
static void mpu6050_remove(struct platform_device *pdev);
#else
static int mpu6050_remove(struct platform_device *pdev);
#endif

static int dev_open(struct inode *inode, struct file *file);
static ssize_t dev_read(struct file *file, char __user *user_buff, size_t size, loff_t *offset);
static ssize_t dev_write(struct file *file, const char __user *user_buff, size_t size, loff_t *offset);
static int dev_release(struct inode *inode, struct file *file);
static long dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int8_t sleep_mode_off(void);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
static int dev_uevent(const struct device *dev, struct kobj_uevent_env *env);
#else
static int dev_uevent(struct device *dev, struct kobj_uevent_env *env);
#endif
static irqreturn_t mpu6050_interrupt_handler(int irq, void *dev_id);

/**
 * @brief Variables globales utilziadas
 */
static struct driver_data driver = {0};

/* Variables to synchronize I2C operations inside the interrupt handler. */
static volatile bool tx_data_ready = false;
static volatile bool rx_data_ready = false;
static volatile bool access_ready = false;
static volatile bool access_error = false;
static volatile bool not_ack = false;
static volatile bool bus_free = false;
static volatile bool bus_busy = false;
static volatile bool last_read_valid = false;
static volatile bool last_write_valid = false;
static volatile bool timeout_error = false;

/* The single-byte read result from the I2C DATA register */
static volatile uint8_t read_value = 0;

/* For sending register address + data during a write, or register address for read */
static uint8_t data_to_transmit[2];
static uint8_t byte_to_transmit = 0;
static uint8_t total_bytes_to_transmit = 0;

/* Probe completion tracking: open() can wait briefly for probe() to finish.
 * This prevents returning -ENODEV just because userspace raced probe.
 */
static DECLARE_COMPLETION(mpu6050_probe_done);
static int mpu6050_probe_status = -ENODEV;

/* Shadow copy of I2C_CON to avoid ioread32() from I2C_CON at runtime.
 * On some AM335x/BBB scenarios (clock-gated / bus-error), reads can trigger an abort.
 */
static uint32_t i2c_con_shadow;

/**
 * @brief Operaciones del archivo de carácter.
 * @details Tabla de callbacks para open/read/write/release del dispositivo `/dev/mpu6050`.
 */
static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = dev_open,
    .read = dev_read,
    .write = dev_write,
    .unlocked_ioctl = dev_ioctl,
    .release = dev_release,
};

/**
 * @brief Estructura del platform driver del MPU6050.
 * @details Expone `probe` y `remove` y la tabla de compatibilidad DT.
 */
static struct platform_driver mpu6050_platform_driver = {
    .driver = {
        .name = DEVICE_NAME,
        .owner = THIS_MODULE,
        .probe_type = PROBE_PREFER_ASYNCHRONOUS,
        .of_match_table = of_match_ptr(mpu6050_of_match),
    },
    .probe = mpu6050_probe,
    .remove = mpu6050_remove,
};

/**
 * @brief Tabla de compatibilidad para Device Tree.
 * @details Permite asociar el driver al nodo con `compatible`.
 */
static const struct of_device_id mpu6050_of_match[] = {
    {.compatible = COMPATIBLE},
    {},
};

/* Forward declarations of functions used by probe/open */
static int i2c2_config(void);
static int8_t set_mpu_range(void);
static int mpu_write_reg(uint8_t reg_addr, uint8_t val);
static uint8_t mpu_read_reg(uint8_t reg_addr);
/* Low-level I2C helpers used by mpu_*_reg (must be declared before first use) */
static void load_cnt(uint32_t value);
static void set_master_transmiter(void);
static void set_master_receiver(void);
static void enable_irqs(uint32_t interrupt_mask);
static void disable_irqs(uint32_t interrupt_mask);
static void stt_condition_stt_stp(void);
static void stt_condition_stt_(void);
/* FIFO helpers forward declarations */
static int configure_fifo(void);
static void sensor_reset(void);
static int disable_fifo_data(void);
static int read_fifo_count(uint16_t *fifo_count);
static void print_raw_frame(const uint8_t *frame, size_t size);

/* Ensure I2C2 module clock is enabled before touching I2C2 registers.
 * Without this, accesses to I2C2 registers can trigger an external abort
 * on AM335x when the module is clock-gated.
 */
static int ensure_i2c2_clock_ready(void);

 /**
  * @brief Estructura de datos del driver
  */
// Estructura global del driver
MODULE_DEVICE_TABLE(of, mpu6050_of_match);

// Cola de espera para sincronizar operaciones I2C en el manejador de interrupciones
DECLARE_WAIT_QUEUE_HEAD(wait_queue);

// Semáforo para controlar acceso exclusivo al dispositivo
static struct semaphore open_semaphore;

/* Debug: track who currently holds the open semaphore */
static pid_t open_owner_pid = -1;
static char open_owner_comm[TASK_COMM_LEN];

/* Recuperatorio: contador de lecturas exitosas desde que se instaló el módulo. */
static atomic64_t g_read_count = ATOMIC64_INIT(0);

/* Configuración por Device Tree (con defaults si no está presente). */
static u32 g_dt_sample_rate_hz = 200;         /* Hz */
static u32 g_dt_dlpf_cfg = DLPF_CFG_260HZ;    /* 0..7 (CONFIG.DLPF_CFG) */
static u32 g_dt_gyro_fs = GYRO_FS_1000DPS;    /* 0..3 (GYRO_CONFIG.FS_SEL) */
static u32 g_dt_accel_fs = ACCEL_AFS_2G;      /* 0..3 (ACCEL_CONFIG.AFS_SEL) */

/* Optional: MPU6050 Data Ready interrupt (INT pin) via GPIO.
 * If wired and described in DT as `drdy-gpios`, read() can sleep until HW interrupt.
 */
static DECLARE_WAIT_QUEUE_HEAD(fifo_waitq);
static atomic_t g_drdy_pending = ATOMIC_INIT(0);
static struct gpio_desc *g_drdy_gpiod;
static int g_drdy_irq = -1;
static bool g_drdy_irq_requested;

struct mpu6050_file_ctx {
    size_t stash_len;
    uint8_t stash[FRAME_LEN];
};

static irqreturn_t mpu6050_drdy_interrupt_handler(int irq, void *dev_id)
{
    (void)irq;
    (void)dev_id;
    atomic_set(&g_drdy_pending, 1);
    wake_up_interruptible(&fifo_waitq);
    return IRQ_HANDLED;
}

static void mpu6050_load_dt_config(struct device *dev)
{
    struct device_node *np = dev->of_node;
    u32 val;

    if (!np)
        return;

    if (!of_property_read_u32(np, "mpu6050,sample-rate-hz", &val) && val > 0)
        g_dt_sample_rate_hz = val;

    if (!of_property_read_u32(np, "mpu6050,dlpf-cfg", &val) && val <= 7)
        g_dt_dlpf_cfg = val;

    if (!of_property_read_u32(np, "mpu6050,gyro-fs", &val) && val <= 3)
        g_dt_gyro_fs = val;

    if (!of_property_read_u32(np, "mpu6050,accel-fs", &val) && val <= 3)
        g_dt_accel_fs = val;

    dev_info(dev,
             "[mpu6050] DT config: sample-rate-hz=%u dlpf-cfg=%u gyro-fs=%u accel-fs=%u\n",
             g_dt_sample_rate_hz, g_dt_dlpf_cfg, g_dt_gyro_fs, g_dt_accel_fs);
}

/* Start of main functions */

/**
 * @brief Probe del dispositivo MPU6050. Los recursos que se piden aca se van en mpu6050_remove
 * @param pdev Dispositivo de plataforma detectado por DT.
 * @return 0 si tuvo éxito, negativo en error.
 */
static int mpu6050_probe(struct platform_device *pdev)
{
    int aux= 0;
    struct device *dev = &pdev->dev;

    /* Carga configuración desde Device Tree (si existe). */
    mpu6050_load_dt_config(dev);

    /* Reset probe state so open() can wait for this attempt. */
    driver.hw_initialized = false;
    mpu6050_probe_status = -EINPROGRESS;
    reinit_completion(&mpu6050_probe_done);

    pr_info("[mpu6050] Probe MPU6050 device\n");

    // Mapeo registro de memorias de la Beagle Bone ===================================
    /* Map CONTROL_MODULE_CONF_I2C2_SDA */
    driver.conf_i2c2_sda = ioremap(CONTROL_MODULE_CONF_I2C2_SDA, sizeof(u32));
    if (!driver.conf_i2c2_sda)
    {
        dev_err(dev, "[mpu6050] Failed to ioremap CONTROL_MODULE_CONF_I2C2_SDA\n");
        mpu6050_probe_status = -ENOMEM;
        complete_all(&mpu6050_probe_done);
        return -ENOMEM;
    }
    pr_info("[mpu6050] Mapped CONTROL_MODULE_CONF_I2C2_SDA at virtual address %p\n", driver.conf_i2c2_sda);

    /* Map CONTROL_MODULE_CONF_I2C2_SCL */
    driver.conf_i2c2_scl = ioremap(CONTROL_MODULE_CONF_I2C2_SCL, sizeof(u32));
    if (!driver.conf_i2c2_scl)
    {
        dev_err(dev, "[mpu6050] Failed to ioremap CONTROL_MODULE_CONF_I2C2_SCL\n");
        iounmap(driver.conf_i2c2_sda);
        mpu6050_probe_status = -ENOMEM;
        complete_all(&mpu6050_probe_done);
        return -ENOMEM;
    }
    pr_info("[mpu6050] Mapped CONTROL_MODULE_CONF_I2C2_SCL at virtual address %p\n", driver.conf_i2c2_scl);

    /* Map CM_PER_I2C2_CLKCTRL */
    driver.per_cm_i2c2_clkctrl = ioremap(CM_PER_I2C2_CLKCTRL, sizeof(u32));
    if (!driver.per_cm_i2c2_clkctrl)
    {
        dev_err(dev, "[mpu6050] Failed to ioremap CM_PER_I2C2_CLKCTRL\n");
        iounmap(driver.conf_i2c2_sda);
        iounmap(driver.conf_i2c2_scl);
        mpu6050_probe_status = -ENOMEM;
        complete_all(&mpu6050_probe_done);
        return -ENOMEM;
    }
    pr_info("[mpu6050] Mapped CM_PER_I2C2_CLKCTRL at virtual address %p\n", driver.per_cm_i2c2_clkctrl);

    /* Map I2C Module Base Address */
    driver.i2c_module = ioremap(MODULE_I2C_BASE_ADDR, MODULE_I2C_SIZE);
    if (!driver.i2c_module)
    {
        dev_err(dev, "[mpu6050] Failed to ioremap MODULE_I2C_BASE_ADDR\n");
        iounmap(driver.conf_i2c2_sda);
        iounmap(driver.conf_i2c2_scl);
        iounmap(driver.per_cm_i2c2_clkctrl);
        mpu6050_probe_status = -ENOMEM;
        complete_all(&mpu6050_probe_done);
        return -ENOMEM;
    }
    pr_info("[mpu6050] Mapped MODULE_I2C_BASE_ADDR at virtual address %p\n", driver.i2c_module);

    // Termino de mapear las regiones de memoria ------------------------------------------
    
    // Solicito una IRQ ===================================================================
    /* Get the IRQ number */
    driver.irq = platform_get_irq(pdev, 0);
    if (driver.irq < 0)
    {
        dev_err(dev, "[mpu6050] Error getting IRQ: %d\n", driver.irq);
        /* Unmap previously mapped regions */
        iounmap(driver.conf_i2c2_sda);
        iounmap(driver.conf_i2c2_scl);
        iounmap(driver.per_cm_i2c2_clkctrl);
        iounmap(driver.i2c_module);

        mpu6050_probe_status = driver.irq;
        complete_all(&mpu6050_probe_done);
        return driver.irq;
    }
    pr_info("[mpu6050] platform_get_irq OK: %d\n", driver.irq);

    /* Request the IRQ */
    aux = request_irq(driver.irq, mpu6050_interrupt_handler,
                      IRQF_TRIGGER_RISING | IRQF_NO_SUSPEND | IRQF_ONESHOT,
                      DEVICE_NAME, &driver);
    if (aux)
    {
        dev_err(dev, "[mpu6050] Error requesting IRQ: %d\n", aux);
        /* Unmap previously mapped regions */
        iounmap(driver.conf_i2c2_sda);
        iounmap(driver.conf_i2c2_scl);
        iounmap(driver.per_cm_i2c2_clkctrl);
        iounmap(driver.i2c_module);

        mpu6050_probe_status = aux;
        complete_all(&mpu6050_probe_done);
        return aux;
    }
    driver.irq_requested = true;    // funciona como flag para liberar la IRQ en el remove
    pr_info("[mpu6050] request_irq OK\n");

    /* Optional DRDY GPIO interrupt (MPU6050 INT pin wired to a GPIO).
     * DT binding: provide `drdy-gpios = <...>;` on this node.
     */
    g_drdy_irq_requested = false;
    g_drdy_irq = -1;
    g_drdy_gpiod = devm_gpiod_get_optional(dev, "drdy", GPIOD_IN);
    if (IS_ERR(g_drdy_gpiod)) {
        dev_warn(dev, "[mpu6050] drdy-gpios present but failed to acquire (%ld); falling back to polling\n",
                 PTR_ERR(g_drdy_gpiod));
        g_drdy_gpiod = NULL;
    }
    if (g_drdy_gpiod) {
        g_drdy_irq = gpiod_to_irq(g_drdy_gpiod);
        if (g_drdy_irq < 0) {
            dev_warn(dev, "[mpu6050] drdy-gpios acquired but gpiod_to_irq failed (%d); falling back to polling\n",
                     g_drdy_irq);
            g_drdy_irq = -1;
        } else {
            aux = request_irq(g_drdy_irq, mpu6050_drdy_interrupt_handler,
                              IRQF_TRIGGER_RISING | IRQF_NO_SUSPEND,
                              "mpu6050_drdy", NULL);
            if (aux) {
                dev_warn(dev, "[mpu6050] request_irq(DRDY) failed (%d); falling back to polling\n", aux);
                g_drdy_irq = -1;
            } else {
                g_drdy_irq_requested = true;
                dev_info(dev, "[mpu6050] DRDY GPIO IRQ enabled: %d\n", g_drdy_irq);
            }
        }
    }

    /* Configure and enable I2C2 controller so we can access the sensor */
    aux = i2c2_config();
    if (aux < 0) {
        dev_err(dev, "[mpu6050] i2c2_config failed: %d\n", aux);
        free_irq(driver.irq, &driver);
        driver.irq_requested = false;
        iounmap(driver.conf_i2c2_sda);
        iounmap(driver.conf_i2c2_scl);
        iounmap(driver.per_cm_i2c2_clkctrl);
        iounmap(driver.i2c_module);

        mpu6050_probe_status = aux;
        complete_all(&mpu6050_probe_done);
        return aux;
    }

    /* Soft reset the MPU6050: set DEVICE_RESET (bit 7) in PWR_MGMT_1 */
    // Esto te pide el datasheet

    aux = mpu_write_reg(MPU6050_PWR_MGMT_1_ADDRESS, PWR_MGMT_1_DEVICE_RESET);
    // Wait for sensor reset to be completed
    if (msleep_interruptible(100))
    {
        pr_warn("[mpu6050] Sleep interrupted during reset\n");
    }
    if (aux < 0) {
        dev_err(dev, "[mpu6050] Failed to write DEVICE_RESET\n");
        free_irq(driver.irq, &driver);
        driver.irq_requested = false;
        iounmap(driver.conf_i2c2_sda);
        iounmap(driver.conf_i2c2_scl);
        iounmap(driver.per_cm_i2c2_clkctrl);
        iounmap(driver.i2c_module);

        mpu6050_probe_status = -EIO;
        complete_all(&mpu6050_probe_done);
        return -EIO;
    }

    // Dice que hay un delay de 100ms despues del reset
    msleep(100);

    /* Take device out of sleep and select PLL with X gyro as clock source */
    if (mpu_write_reg(MPU6050_PWR_MGMT_1_ADDRESS, PWR_MGMT_1_CLKSEL_PLL_X) < 0) {
        dev_err(dev, "[mpu6050] Failed to set PWR_MGMT_1 to PLL X (0x01)\n");
        free_irq(driver.irq, &driver);
        driver.irq_requested = false;
        iounmap(driver.conf_i2c2_sda);
        iounmap(driver.conf_i2c2_scl);
        iounmap(driver.per_cm_i2c2_clkctrl);
        iounmap(driver.i2c_module);

        mpu6050_probe_status = -EIO;
        complete_all(&mpu6050_probe_done);
        return -EIO;
    }

    /* Ensure sensor is awake (SLEEP=0). Keep this in probe(), not in open(),
     * to avoid doing I2C transactions during open() which previously caused
     * external aborts on AM335x when I2C was clock-gated.
     */
    aux = sleep_mode_off();
    if (aux < 0) {
        dev_err(dev, "[mpu6050] Failed to exit sleep mode\n");
        free_irq(driver.irq, &driver);
        driver.irq_requested = false;
        iounmap(driver.conf_i2c2_sda);
        iounmap(driver.conf_i2c2_scl);
        iounmap(driver.per_cm_i2c2_clkctrl);
        iounmap(driver.i2c_module);

        mpu6050_probe_status = -EIO;
        complete_all(&mpu6050_probe_done);
        return -EIO;
    }

    /* Configure sensor registers (CONFIG, GYRO_CONFIG, ACCEL_CONFIG, etc.) */
    aux = set_mpu_range();
    if (aux < 0) {
        dev_err(dev, "[mpu6050] set_mpu_range failed: %d\n", aux);
        free_irq(driver.irq, &driver);
        driver.irq_requested = false;
        iounmap(driver.conf_i2c2_sda);
        iounmap(driver.conf_i2c2_scl);
        iounmap(driver.per_cm_i2c2_clkctrl);
        iounmap(driver.i2c_module);

        mpu6050_probe_status = aux;
        complete_all(&mpu6050_probe_done);
        return aux;
    }

    /* Mark hardware as initialized for dev_open() checks */
    driver.hw_initialized = true;

    /* Enable MPU6050 Data Ready interrupt generation (INT pin).
     * If DRDY GPIO IRQ is not wired, this has no effect on read() waking.
     */
    (void)mpu_write_reg(MPU6050_INT_ENABLE_ADDRESS, 0x01);
    (void)mpu_read_reg(MPU6050_INT_STATUS_ADDRESS);

    mpu6050_probe_status = 0;
    complete_all(&mpu6050_probe_done);

    pr_info("[mpu6050] Probe successful\n");
    return 0;
}

/**
 * @brief Platform driver remove
 *      Despagina y libera recursos asignados en el probe
 *      Libera el IRQ si fue solicitada
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
static void mpu6050_remove(struct platform_device *pdev)
#else
static int mpu6050_remove(struct platform_device *pdev)
#endif
{
    (void)pdev;
    pr_info("[mpu6050] Device removed start\n");

    if (g_drdy_irq_requested && g_drdy_irq >= 0) {
        free_irq(g_drdy_irq, NULL);
        g_drdy_irq_requested = false;
        pr_info("[mpu6050] DRDY IRQ %d freed\n", g_drdy_irq);
        g_drdy_irq = -1;
    }

    // Libero la IRQ si fue solicitada
    if (driver.irq_requested)
    {
        free_irq(driver.irq, &driver);
        driver.irq_requested = false;
        pr_info("[mpu6050] IRQ %d freed\n", driver.irq);
    }

    /* Mark hardware as not initialized */
    driver.hw_initialized = false;

    // Desmapeo de las regiones de memoria
    if (driver.i2c_module)
    {
        iounmap(driver.i2c_module);
        driver.i2c_module = NULL;
        pr_info("[mpu6050] Unmapped MODULE_I2C_BASE_ADDR\n");
    }

    if (driver.per_cm_i2c2_clkctrl)
    {
        iounmap(driver.per_cm_i2c2_clkctrl);
        driver.per_cm_i2c2_clkctrl = NULL;
        pr_info("[mpu6050] Unmapped CM_PER_I2C2_CLKCTRL\n");
    }

    if (driver.conf_i2c2_scl)
    {
        iounmap(driver.conf_i2c2_scl);
        driver.conf_i2c2_scl = NULL;
        pr_info("[mpu6050] Unmapped CONTROL_MODULE_CONF_I2C2_SCL\n");
    }

    if (driver.conf_i2c2_sda)
    {
        iounmap(driver.conf_i2c2_sda);
        driver.conf_i2c2_sda = NULL;
        pr_info("[mpu6050] Unmapped CONTROL_MODULE_CONF_I2C2_SDA\n");
    }

    pr_info("[mpu6050] Device removed complete\n");
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 4, 0)
    return 0;
#endif
}

/**
 * @brief Manejador de interrupción del controlador I2C.
 *      Handles I2C IRQSTATUS bits and sets driver-level flags used to
 *      synchronize read/write sequences with wait queues.
 * @param irq Número de IRQ.
 * @param dev_id Puntero a `driver_data` pasado al registrar la IRQ.
 * @return `IRQ_HANDLED` al procesar la interrupción.
 */
static irqreturn_t mpu6050_interrupt_handler(int irq, void *dev_id)
{
    (void)irq;
    struct driver_data *data = (struct driver_data *)dev_id;
    uint32_t status;

    pr_debug("[mpu6050] IRQ Interrupt Handled\n");

    /* Read the IRQ status */
    status = ioread32(data->i2c_module + MODULE_I2C_IRQSTATUS_OFFSET);
    pr_debug("[mpu6050] IRQ Status: 0x%08X\n", status);

    /* Clear IRQ status */
    iowrite32(status, data->i2c_module + MODULE_I2C_IRQSTATUS_OFFSET);

    /* Address ready (ARDY) */
    if (status & ARDY_INT_ENABLED) {
        pr_debug("[mpu6050] ARDY interrupt - address phase done\n");
        access_ready = true;
    }

    /* NACK */
    if (status & NACK_INT_ENABLED) {
        pr_err("[mpu6050] NACK received during I2C transaction\n");
        not_ack = true;
    }

    /* Bus busy/free */
    if (status & BB_INT_ENABLED) {
        pr_debug("[mpu6050] Bus is busy\n");
        bus_busy = true;
    }
    if (status & BF_INT_ENABLED) {
        pr_debug("[mpu6050] Bus is free\n");
        bus_free = true;
    }

    /* Read ready */
    if (status & RRDY_INT_ENABLED) {
        if (!rx_data_ready) {
            read_value = ioread32(data->i2c_module + MODULE_I2C_DATA_OFFSET) & 0xFF;
            pr_debug("[mpu6050] Read data: 0x%02X\n", read_value);
            rx_data_ready = true;
        }
    }

    /* Transmit ready */
    if (status & XRDY_INT_ENABLED) {
        if (byte_to_transmit < total_bytes_to_transmit) {
            iowrite32(data_to_transmit[byte_to_transmit++],
                      data->i2c_module + MODULE_I2C_DATA_OFFSET);
            pr_debug("[mpu6050] Transmitted byte: 0x%02X\n",
                     data_to_transmit[byte_to_transmit - 1]);

            if (byte_to_transmit == total_bytes_to_transmit) {
                pr_debug("[mpu6050] All data transmitted\n");
                tx_data_ready = true;
            }
        }
    }

    /* Arbitration lost */
    if (status & AERR_INT_ENABLED) {
        pr_err("[mpu6050] Arbitration lost\n");
        access_error = true;
    }

    /* Wake up any waiter */
    wake_up_interruptible(&wait_queue);

    return IRQ_HANDLED;
}

/**
 * @brief Inicialización del módulo: registra el dispositivo de carácter y el platform driver
 *
 * - Reserva el rango de números mayor/menor con alloc_chrdev_region y crea
 *   la clase y el nodo de dispositivo para el espacio de usuario.
 * - Registra `mpu6050_platform_driver` para que el kernel invoque
 *   `mpu6050_probe` cuando exista un dispositivo compatible en el Device Tree.
 *
 * Flujo de error:
 * - Si algún paso falla, libera en orden los recursos ya reservados
 *   (device, class, cdev y dev_t) antes de retornar.
 *
 * @return 0 en éxito, código de error negativo en fallo
 */
static int __init mpu6050_init(void)
{
    int ret;

    pr_info("[mpu6050] Initializing MPU6050 driver - V%d\n", MY_VERSION);

    /* Initialize semaphores used by file ops */
    sema_init(&open_semaphore, 1);
    open_owner_pid = -1;
    open_owner_comm[0] = '\0';

    /* Allocate a device number */
    ret = alloc_chrdev_region(&driver.dev_num, MENOR, CANT_DISP, DEVICE_NAME);
    if (ret < 0)
    {
        pr_err("[mpu6050] Failed to allocate device number\n");
        return ret;
    }

    /* Create cdev structure */
    driver.c_dev = cdev_alloc();
    if (!driver.c_dev)
    {
        pr_err("[mpu6050] Failed to allocate cdev structure\n");
        unregister_chrdev_region(driver.dev_num, CANT_DISP);
        return -ENOMEM;
    }

    pr_alert("[mpu6050] Numero MAYOR asignado %d, 0x%X \n", MAJOR(driver.dev_num), MAJOR(driver.dev_num));

    //Le asigno las file operation al cdev
    cdev_init(driver.c_dev, &fops); // No cuenta con codigo de error

    /* Add cdev to kernel */
    ret = cdev_add(driver.c_dev, driver.dev_num, CANT_DISP);
    if (ret < 0)
    {
        pr_err("[mpu6050] Failed to add cdev to kernel\n");
        kfree(driver.c_dev);
        unregister_chrdev_region(driver.dev_num, CANT_DISP);
        return ret;
    }

    /* Create class */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
    driver.class_ptr = class_create(CLASS_NAME);
#else
    driver.class_ptr = class_create(THIS_MODULE, CLASS_NAME); // Aca se agrega al /dev o es necesario para esto
#endif
    if (IS_ERR(driver.class_ptr))
    {
        pr_err("[mpu6050] Failed to create class\n");
        cdev_del(driver.c_dev);
        unregister_chrdev_region(driver.dev_num, CANT_DISP);
        return PTR_ERR(driver.class_ptr);
    }

    //Se ejecuta cuando device_create salga bien
    driver.class_ptr->dev_uevent = dev_uevent;

    /* Create device */
    driver.device_ptr = device_create(driver.class_ptr, NULL, driver.dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(driver.device_ptr))
    {
        pr_err("[mpu6050] Failed to create device\n");
        class_destroy(driver.class_ptr);
        cdev_del(driver.c_dev);
        unregister_chrdev_region(driver.dev_num, CANT_DISP);
        return PTR_ERR(driver.device_ptr);
    }

    //Inicio de plataform driver
    /* Register the platform driver */
    ret = platform_driver_register(&mpu6050_platform_driver);
    if (ret != 0)
    {
        pr_err("[mpu6050] Failed to register platform driver\n");
        device_destroy(driver.class_ptr, driver.dev_num);
        class_destroy(driver.class_ptr);
        cdev_del(driver.c_dev);
        unregister_chrdev_region(driver.dev_num, CANT_DISP);
        return ret;
    }

    pr_info("[mpu6050] Driver initialized successfully\n");
    return 0;
}

/**
 * @brief Module exit: unregister platform driver and cleanup char device
 */
static void __exit mpu6050_exit(void)
{
    platform_driver_unregister(&mpu6050_platform_driver);
    device_destroy(driver.class_ptr, driver.dev_num);
    class_destroy(driver.class_ptr);
    cdev_del(driver.c_dev);
    unregister_chrdev_region(driver.dev_num, CANT_DISP);
    pr_info("[mpu6050] Driver exited\n");
}

/**
 * @brief uevent hook to set device node permissions
 * @param dev pointer to device
 * @param env uevent environment
 * @return 0
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 4, 0)
static int dev_uevent(const struct device *dev, struct kobj_uevent_env *env)
#else
static int dev_uevent(struct device *dev, struct kobj_uevent_env *env)
#endif
{
    (void)dev;
    add_uevent_var(env, "DEVMODE=%#o", 0666);   //Le doy permisos de lectura y escritura a todos
    return 0;
}


/**
 * @brief configura y habilita el controlador I2C2 para comunicarse con el MPU6050 (pagina 1253 https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf)
 *  - Configura los pines SDA y SCL (SDA -> Pin 20, SCL -> Pin 19)
 *  - Habilita el clock del modulo I2C2
 *  - Configura el prescaler y los tiempos de SCLH y SCLL para 100kHz
 *  - Habilita el modulo I2C2
 *  - Verifica la comunicacion leyendo el registro WHO_AM_I del MPU6050
 * @return 0 on success, negative error code on failure
 */
static int i2c2_config(void)
{
    uint32_t value;
    uint8_t reg_value = 0;

    pr_info("[mpu6050] Habilito configuraciones de I2C2\n");

    // Leo el valor actual de CM_PER_I2C2_CLKCTRL
    value = ioread32(driver.per_cm_i2c2_clkctrl);

    pr_info("[mpu6050] CM_PER_I2C2_CLKCTRL read: phys=0x%08X virt=%p val=0x%08X\n",
            (unsigned int)CM_PER_I2C2_CLKCTRL,
            driver.per_cm_i2c2_clkctrl,
            (unsigned int)value);

    //pr_info("[mpu6050] No es error de escritura\n");

    value |= CM_PER_I2C2_CLKCTRL_MODULEMODE_ENABLE; // MODULEMODE = ENABLE

    pr_info("[mpu6050] CM_PER_I2C2_CLKCTRL write: phys=0x%08X virt=%p val=0x%08X\n",
            (unsigned int)CM_PER_I2C2_CLKCTRL,
            driver.per_cm_i2c2_clkctrl,
            (unsigned int)value);

    iowrite32(value, driver.per_cm_i2c2_clkctrl);
    pr_debug("[mpu6050] Enabled I2C2 Clock");

    /* Esperar a que el módulo esté funcional (IDLEST[17:16] == 0) */
    {
        int attempts = I2C2_CLK_READY_ATTEMPTS; /* ~10ms max con udelay(10) */
        do {
            value = ioread32(driver.per_cm_i2c2_clkctrl);
            if (((value & CM_PER_I2C2_CLKCTRL_IDLEST_MASK) >> CM_PER_I2C2_CLKCTRL_IDLEST_SHIFT) == CM_PER_I2C2_CLKCTRL_IDLEST_FUNC)
                break;
            udelay(I2C2_CLK_POLL_DELAY_US);
        } while (--attempts);

        if (((value & CM_PER_I2C2_CLKCTRL_IDLEST_MASK) >> CM_PER_I2C2_CLKCTRL_IDLEST_SHIFT) != CM_PER_I2C2_CLKCTRL_IDLEST_FUNC) {
            pr_err("[mpu6050] I2C2 clock did not become functional (IDLEST=%u)\n",
                   (unsigned int)((value & CM_PER_I2C2_CLKCTRL_IDLEST_MASK) >> CM_PER_I2C2_CLKCTRL_IDLEST_SHIFT));
            return -ETIMEDOUT;
        }
        
    }

    // COnfiguracion de pines I2C2 SDA y SCL (CONTROL_MODULERegisters)
    // Valor del preset 110011, es lo mismo para ambos pines
    // Bits 5 --> Input enable value for the PAD
    // Bit 4 --> Pull Up/down select (1 -> pullup)
    // Bit 3 --> Habilito el pull up/down
    // Bits 2:0 --> Pad functional signal mux select (No se bien su funcion)

    iowrite32(CONTROL_MODULE_CONF_I2C2_PRESET, driver.conf_i2c2_sda);
    pr_debug("[mpu6050] Configured I2C2 SDA \n");

    iowrite32(CONTROL_MODULE_CONF_I2C2_PRESET, driver.conf_i2c2_scl);
    pr_debug("[mpu6050] Configured I2C2 SCL \n");

    // Configuracion del modulo I2C2 
    // I2C Registers --> Pagina 4601
    
    // Se debe colocar un prescalado de 0x3 para obtener 100khz
    iowrite32(PRESCALER_VALUE_100k, driver.i2c_module + MODULE_I2C_PSC_OFFSET);
    pr_debug("[mpu6050] Set I2C2_PSC to 3 (Module clock ~12 MHz)\n");

    // Configuro los tiempos de SCLH y SCLL para 100kHz
    // SCLH tiempo alto de reloj
    // SCLL tiempo bajo de reloj
    // Los tiempo de alto y bajo no pueden ser los mismos   
    iowrite32(I2C_SCLH_100K, driver.i2c_module + MODULE_I2C_SCLH_OFFSET);
    pr_debug("[mpu6050] Set I2C2_SCLH to %d\n", I2C_SCLH_100K);

    iowrite32(I2C_SCLL_100K, driver.i2c_module + MODULE_I2C_SCLL_OFFSET);
    pr_debug("[mpu6050] Set I2C2_SCLL to %d\n", I2C_SCLL_100K);

    // Leo el registro I2C_CON y habilito el modulo I2C
    // Bit 15 --> I2C Module Enable
    value = ioread32(driver.i2c_module + MODULE_I2C_CON_OFFSET);
    value |= I2C_CON_I2C_EN;
    iowrite32(value, driver.i2c_module + MODULE_I2C_CON_OFFSET);
    i2c_con_shadow = value;
    pr_debug("[mpu6050] Set I2C2_CON \n");

    // Configura el address del esclavo I2C (MPU6050)
    // Primero intento con 0x68
    iowrite32(MPU6050_I2C_ADDRESS_1, driver.i2c_module + MODULE_I2C_SA_OFFSET);

    /* Detect address: try 0x68 first, then 0x69 if needed */
    reg_value = mpu_read_reg(MPU6050_WHO_AM_I_REGISTER_ADDRESS);
    if (!last_read_valid)
    {
        pr_warn("[mpu6050] WHO_AM_I failed at 0x68, trying 0x69\n");
        /* Switch I2C slave address to 0x69 and retry */
        iowrite32(MPU6050_I2C_ADDRESS_2, driver.i2c_module + MODULE_I2C_SA_OFFSET);
        pr_debug("[mpu6050] Switched I2C2_SA to 0x69\n");
        reg_value = mpu_read_reg(MPU6050_WHO_AM_I_REGISTER_ADDRESS);
        if (!last_read_valid)
        {
            pr_err("[mpu6050] Failed to read WHO_AM_I at both 0x68 and 0x69\n");
            return -EIO;
        }
        else
        {
            pr_info("[mpu6050] Detected MPU6050 at address 0x69\n");
        }
    }
    else
    {
        pr_info("[mpu6050] Detected MPU6050 at address 0x68\n");
    }

    pr_debug("[mpu6050] WHO_AM_I register value: 0x%02X\n", reg_value);
    pr_info("[mpu6050] i2c2_config Sin errores\n");
    return 0;
}

// File operations main functions ================================================================================================
/**
 * @brief Abre el dispositivo de carácter.
 * @param inode Inodo del dispositivo.
 * @param file Estructura de archivo de usuario.
 * @return 0 si tuvo éxito, negativo si HW no inicializado o semáforo ocupado.
 */
static int dev_open(struct inode *inode, struct file *file)
{
    (void)inode;
    if (file)
        file->private_data = NULL;

    /*
     * Exclusive open:
     * If the semaphore is already held, do NOT block forever (userspace will
     * look like it's frozen). Instead, fail fast with -EBUSY and report the
     * current owner so we can debug who kept /dev/mpu6050 open.
     */
    pr_debug("[mpu6050] Start mpu6050 - OPEN (pid=%d comm=%s)\n", current->pid, current->comm);
    /* Try to acquire semaphore without sleeping; fail fast if busy */
    if (down_interruptible(&open_semaphore))
        return -ERESTARTSYS;
    open_owner_pid = current->pid;
    get_task_comm(open_owner_comm, current);
    pr_debug("[mpu6050] Semaphore acquired\n");

    /* Verify that probe() completed hardware initialization.
     * Userspace can race module insertion; wait briefly for probe().
     */
    if (!driver.hw_initialized) {
        if (!completion_done(&mpu6050_probe_done)) {
            unsigned long waited = wait_for_completion_timeout(&mpu6050_probe_done, TIMEOUT_JIFFIES);
            if (waited == 0) {
                pr_err("[mpu6050] probe() did not complete within timeout\n");
            }
        }
        if (!driver.hw_initialized) {
            pr_err("[mpu6050] Hardware not initialized by probe (status=%d)\n", mpu6050_probe_status);
            open_owner_pid = -1;
            open_owner_comm[0] = '\0';
            up(&open_semaphore);
            return -ENODEV;
        }
    }

    /* IMPORTANT: do not touch I2C hardware in open().
     * On BBB/AM335x, the I2C module can be clock-gated by runtime PM when idle;
     * performing I2C transactions here has caused external aborts.
     * Hardware initialization/wake-up is handled in probe() and on demand
     * in mpu_read_reg()/mpu_write_reg().
     */
    /* Open should not perform hardware reset or reconfiguration.
     * Hardware initialization is done once in probe(). Here we only
     * initialize per-open software structures and keep the semaphore held
     * until release().
     */
    pr_info("[mpu6050] Device successfully opened (software init only)\n");

    if (file) {
        struct mpu6050_file_ctx *ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
        if (!ctx) {
            open_owner_pid = -1;
            open_owner_comm[0] = '\0';
            up(&open_semaphore);
            return -ENOMEM;
        }
        file->private_data = ctx;
    }
    return 0;
}

/**
 * @brief Lee datos del FIFO del MPU6050 y los copia a espacio de usuario.
 * @param file Archivo de usuario asociado al dispositivo.
 * @param user_buff Buffer destino en espacio de usuario.
 * @param size Cantidad de bytes a leer (se espera 1024).
 * @param offset Desplazamiento (no utilizado).
 * @return Cantidad de bytes leídos o código de error negativo.
 */
static ssize_t dev_read(struct file *file, char __user *user_buff, size_t size, loff_t *offset)
{
    (void)file;
    (void)offset;
    struct mpu6050_file_ctx *ctx = file ? (struct mpu6050_file_ctx *)file->private_data : NULL;
    uint8_t *out = NULL;
    uint8_t *tmp = NULL;
    size_t out_off = 0;
    size_t need;
    size_t rounded;
    size_t tmp_off = 0;
    uint16_t fifo_count = 0;

    if (size == 0)
        return 0;
    if (!user_buff)
        return -EINVAL;

    /* Buffer de salida exacto (lo que pidió userspace). */
    out = kvmalloc(size, GFP_KERNEL);
    if (!out)
        return -ENOMEM;

    /* Primero: servir bytes remanentes de la lectura anterior (mantiene alineación a 14 bytes). */
    if (ctx && ctx->stash_len) {
        size_t take = min(size, ctx->stash_len);
        memcpy(out, ctx->stash, take);
        if (take < ctx->stash_len)
            memmove(ctx->stash, ctx->stash + take, ctx->stash_len - take);
        ctx->stash_len -= take;
        out_off += take;
        if (out_off == size)
            goto done_copy;
    }

    need = size - out_off;
    rounded = need;
    if (ctx)
        rounded = roundup(need, (size_t)FRAME_LEN);
    if (rounded == 0)
        goto done_copy;

    tmp = kvmalloc(rounded, GFP_KERNEL);
    if (!tmp) {
        kvfree(out);
        return -ENOMEM;
    }

    if (configure_fifo() < 0) {
        pr_err("[mpu6050] Failed to configure FIFO before reading\n");
        kvfree(tmp);
        kvfree(out);
        return -EIO;
    }

    pr_info("[mpu6050] Reading %zu bytes (rounded=%zu) from FIFO\n", need, rounded);

    while (tmp_off < rounded) {
        size_t available_bytes;
        size_t chunk;

        if (read_fifo_count(&fifo_count) < 0) {
            pr_err("[mpu6050] Failed to read FIFO_COUNT\n");
            sensor_reset();
            kvfree(tmp);
            kvfree(out);
            return -EIO;
        }

        if (fifo_count > MPU6050_FIFO_READ_SIZE) {
            pr_err("[mpu6050] FIFO overflow detected (count=%u), resetting sensor\n", fifo_count);
            sensor_reset();
            kvfree(tmp);
            kvfree(out);
            return -EIO;
        }

        /* Espera bloqueante hasta que entre al menos 1 frame (ideal: IRQ DRDY). */
        if (fifo_count < FRAME_LEN) {
            if (g_drdy_irq_requested) {
                int w = wait_event_interruptible(fifo_waitq, atomic_xchg(&g_drdy_pending, 0));
                if (w)
                    return (kvfree(tmp), kvfree(out), -ERESTARTSYS);
            } else {
                if (msleep_interruptible(1))
                    return (kvfree(tmp), kvfree(out), -ERESTARTSYS);
            }
            continue;
        }

        available_bytes = (size_t)(fifo_count / FRAME_LEN) * (size_t)FRAME_LEN;
        chunk = min(rounded - tmp_off, available_bytes);
        if (chunk == 0)
            continue;

        for (size_t i = 0; i < chunk; ++i) {
            uint8_t val = mpu_read_reg(MPU6050_FIFO_R_W_ADDRESS);
            if (!last_read_valid) {
                pr_err("[mpu6050] Failed to read FIFO byte at offset %zu\n", tmp_off + i);
                sensor_reset();
                kvfree(tmp);
                kvfree(out);
                return -EIO;
            }
            tmp[tmp_off + i] = val;
        }

        if (tmp_off == 0)
            print_raw_frame(tmp, min((size_t)20, chunk));

        tmp_off += chunk;
    }

    memcpy(out + out_off, tmp, need);
    if (ctx) {
        size_t leftover = rounded - need;
        if (leftover > 0) {
            if (leftover > sizeof(ctx->stash))
                leftover = sizeof(ctx->stash);
            memcpy(ctx->stash, tmp + need, leftover);
            ctx->stash_len = leftover;
        }
    }

    (void)disable_fifo_data();

done_copy:
    if (copy_to_user(user_buff, out, size)) {
        pr_err("[mpu6050] Failed to copy data to user space\n");
        kvfree(tmp);
        kvfree(out);
        return -EFAULT;
    }

    kvfree(tmp);
    kvfree(out);
    atomic64_inc(&g_read_count);
    return (ssize_t)size;
}

/**
 * @brief Operación de escritura (no utilizada, eco del tamaño).
 * @param file Archivo de usuario.
 * @param user_buff Buffer de usuario.
 * @param size Cantidad de bytes.
 * @param offset Desplazamiento.
 * @return Cantidad de bytes recibidos.
 */
static ssize_t dev_write(struct file *file, const char __user *user_buff, size_t size, loff_t *offset)
{
    (void)file;
    (void)user_buff;
    (void)offset;
    pr_info("[mpu6050] Write operation of %zu bytes\n", size);
    return size;
}

/**
 * @brief Cierra el dispositivo de carácter. Cierra todo lo que se abrio en open
 * @param inode Inodo del dispositivo.
 * @param file Estructura de archivo de usuario.
 * @return 0.
 */
static int dev_release(struct inode *inode, struct file *file)
{
    pr_info("[mpu6050] Releasing device (software cleanup)\n");

    if (file && file->private_data) {
        kfree(file->private_data);
        file->private_data = NULL;
    }

    /* Do not reset or shut down hardware here. Release only software resources. */
    open_owner_pid = -1;
    open_owner_comm[0] = '\0';
    up(&open_semaphore);
    pr_debug("[mpu6050] Semaphore released\n");

    pr_info("[mpu6050] Device successfully released\n");
    return 0;
}

/**
 * @brief IOCTLs del driver (recuperatorio: contador de lecturas)
 */
static long dev_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    (void)file;

    if (_IOC_TYPE(cmd) != MPU6050_IOCTL_MAGIC)
        return -ENOTTY;

    switch (cmd) {
    case MPU6050_IOCTL_GET_READ_COUNT: {
        mpu6050_u64 val = (mpu6050_u64)atomic64_read(&g_read_count);
        if (copy_to_user((void __user *)arg, &val, sizeof(val)))
            return -EFAULT;
        return 0;
    }
    case MPU6050_IOCTL_RESET_READ_COUNT:
        atomic64_set(&g_read_count, 0);
        return 0;
    default:
        return -ENOTTY;
    }
}
//================================================================================================================================

/**
 * @brief Escribe un byte en un registro del sensor.
 * @param reg_addr Dirección del registro del MPU6050.
 * @param val Valor a escribir.
 * @return 0 si tuvo éxito, negativo en error.
 */
static int mpu_write_reg(uint8_t reg_addr, uint8_t val)
{
    if (!driver.i2c_module || !driver.per_cm_i2c2_clkctrl)
        return -ENODEV;
    if (ensure_i2c2_clock_ready() < 0)
        return -EIO;

    /* Clear global flags */
    tx_data_ready = false;
    rx_data_ready = false;
    access_ready = false;
    access_error = false;
    not_ack = false;
    bus_free = false;
    last_write_valid = false;
    bus_busy = false;
    timeout_error = false;

    pr_debug("[mpu6050] Writing 0x%02X to register 0x%02X\n", val, reg_addr);

    /* Clear any stale pending IRQ status bits from a previous transaction */
    iowrite32(0xFFFF, driver.i2c_module + MODULE_I2C_IRQSTATUS_OFFSET);

    set_master_transmiter();
    load_cnt(I2C_CNT_TWO_BYTES); /* Write 2 bytes: register address + data */

    enable_irqs(ALL_INTERRUPTS);

    /* Prepare data: register address and value */
    data_to_transmit[0] = reg_addr;
    data_to_transmit[1] = val;
    byte_to_transmit = 0;
    total_bytes_to_transmit = 2;

    /* Send START + STOP condition */
    stt_condition_stt_stp();

    /* Wait for completion (ARDY + BF) or NACK, but do not hang forever */
    do
    {
        int ret = wait_event_interruptible_timeout(
            wait_queue,
            ((access_ready && bus_free) || not_ack),
            TIMEOUT_JIFFIES);

        if (ret == 0)
        {
            pr_err("[mpu6050] Timeout waiting write completion\n");
            timeout_error = true;
            break;
        }
        else if (ret < 0)
        {
            pr_err("[mpu6050] Interrupted while waiting write completion: %d\n", ret);
            timeout_error = true;
            break;
        }
    } while ((!access_ready || !bus_free) && !not_ack);

    if (timeout_error)
    {
        uint32_t raw = ioread32(driver.i2c_module + MODULE_I2C_IRQSTATUS_RAW_OFFSET);
        uint32_t con = ioread32(driver.i2c_module + MODULE_I2C_CON_OFFSET);
        uint32_t cnt = ioread32(driver.i2c_module + MODULE_I2C_CNT_OFFSET);
        pr_err("[mpu6050] write timeout debug: IRQSTATUS_RAW=0x%08X CON=0x%08X CNT=0x%08X\n", raw, con, cnt);
        disable_irqs(ALL_INTERRUPTS);
        return -ETIMEDOUT;
    }

    if (not_ack)
    {
        pr_err("[mpu6050] NACK received during write\n");
        not_ack = false;
        disable_irqs(ALL_INTERRUPTS);
        return -EIO;
    }

    disable_irqs(ALL_INTERRUPTS);

    last_write_valid = true;
    pr_debug("[mpu6050] Write successful\n");
    return 0;
}

/**
 * @brief Lee un registro del sensor MPU6050 a través del bus I2C
 * @param reg_addr Dirección del registro a leer
 * @return Valor leído del registro, o 0 en caso de error (ver `last_read_valid`)
 */
static uint8_t mpu_read_reg(uint8_t reg_addr)
{
    if (!driver.i2c_module || !driver.per_cm_i2c2_clkctrl) {
        last_read_valid = false;
        return 0;
    }
    if (ensure_i2c2_clock_ready() < 0) {
        last_read_valid = false;
        return 0;
    }

    /* Clear global flags before starting */
    tx_data_ready = false;
    rx_data_ready = false;
    access_ready = false;
    not_ack = false;
    bus_free = false;
    last_read_valid = false;
    bus_busy = false;
    timeout_error = false;
    access_error = false;

    pr_debug("[mpu6050] Starting read of register 0x%02X\n", reg_addr);

    /* Clear any stale pending IRQ status bits from a previous transaction */
    iowrite32(0xFFFF, driver.i2c_module + MODULE_I2C_IRQSTATUS_OFFSET);

    /* Configure Master mode, set up data count, set direction to WRITE for sending register address */
    // Pongo el BBB en modo maestro y lo preparao para transmitir
    set_master_transmiter();
    load_cnt(I2C_CNT_ONE_BYTE);     // Escribo un byte: registro

    // Habilito las interrupciones de escritura
    enable_irqs(I2C_WR_INTERRUPTS);

    // Preparo los datos a transmitir: la dirección del registro
    data_to_transmit[0] = reg_addr;
    byte_to_transmit = 0;
    total_bytes_to_transmit = 1;

    // Configuro las condiciones de START y STOP para la fase de dirección (WRITE)
    stt_condition_stt_();

    // Espero a que la fase de dirección finalice o falle
    int ret;
    do
    {
        ret = wait_event_interruptible_timeout(wait_queue, (access_ready || not_ack), TIMEOUT_JIFFIES);

        if (ret == 0)
        {
            pr_err("[mpu6050] Timeout occurred while waiting for address phase or NACK\n");
            timeout_error = true;
            break;
        }
        else if (ret < 0)
        {
            pr_err("[mpu6050] Error waiting for event: %d\n", ret);
            timeout_error = true;
            break;
        }
    } while (!access_ready && !not_ack);

    disable_irqs(I2C_WR_INTERRUPTS);
    // Compruebo errores de timeout o NACK
    if (timeout_error)
    {
        pr_err("[mpu6050] Error time out\n");
        return 0;
    }
    else if (not_ack)
    {
        pr_err("[mpu6050] NACK received during address phase\n");
        not_ack = false;
        disable_irqs(I2C_RX_INTERRUPTS);
        return 0;
    }

    // Ya escribi el registro que quiero leer, ahora paso a modo receptor para leer el dato
    set_master_receiver();
    stt_condition_stt_stp();
    enable_irqs(I2C_RX_INTERRUPTS);

    access_ready = false;
    rx_data_ready = false;
    // Espero a que la fase de lectura finalice o falle (con timeout)
    do
    {
        ret = wait_event_interruptible_timeout(
            wait_queue,
            ((access_ready && bus_free) || not_ack || access_error),
            TIMEOUT_JIFFIES);

        if (ret == 0)
        {
            pr_err("[mpu6050] Timeout occurred while waiting for read phase\n");
            timeout_error = true;
            break;
        }
        else if (ret < 0)
        {
            pr_err("[mpu6050] Error waiting for read phase: %d\n", ret);
            timeout_error = true;
            break;
        }
    } while ((!access_ready || !bus_free) && !not_ack && !access_error);

    disable_irqs(I2C_RX_INTERRUPTS);

    if (timeout_error)
    {
        last_read_valid = false;
        return 0;
    }

    if (access_error)
    {
        pr_err("[mpu6050] I2C access error during read\n");
        access_error = false;
        last_read_valid = false;
        return 0;
    }

    // Valido si la lectura fue exitosa
    last_read_valid = !not_ack;
    if (not_ack)
    {
        pr_err("[mpu6050] NACK during read phase\n");
        not_ack = false;
        return 0;
    }

    pr_debug("[mpu6050] Completed read: 0x%02X -> 0x%02X\n", reg_addr, read_value);
    return read_value;
}

/**
 * @brief Modifico el valor del registro I2C_CNT con el valor pasado por parámetro
 *      Este registro indica la cantidad de bytes a transferir en la operación I2C
 * @param value Cantidad de bytes a transferir.
 */
static void load_cnt(uint32_t value)
{
    uint32_t old_value;

    if (!driver.i2c_module || !driver.per_cm_i2c2_clkctrl)
        return;
    if (ensure_i2c2_clock_ready() < 0)
        return;

    old_value = ioread32(driver.i2c_module + MODULE_I2C_CNT_OFFSET);
    iowrite32(value, driver.i2c_module + MODULE_I2C_CNT_OFFSET);

    pr_debug("[mpu6050] load_dcount: I2C_CNT updated from 0x%08X to 0x%08X\n", old_value, value);
}

static int ensure_i2c2_clock_ready(void)
{
    uint32_t value;
    int attempts;

    if (!driver.per_cm_i2c2_clkctrl)
        return -ENODEV;

    value = ioread32(driver.per_cm_i2c2_clkctrl);
    value |= CM_PER_I2C2_CLKCTRL_MODULEMODE_ENABLE;
    iowrite32(value, driver.per_cm_i2c2_clkctrl);

    attempts = I2C2_CLK_READY_ATTEMPTS;
    do {
        value = ioread32(driver.per_cm_i2c2_clkctrl);
        if (((value & CM_PER_I2C2_CLKCTRL_IDLEST_MASK) >> CM_PER_I2C2_CLKCTRL_IDLEST_SHIFT) == CM_PER_I2C2_CLKCTRL_IDLEST_FUNC)
            return 0;
        udelay(I2C2_CLK_POLL_DELAY_US);
    } while (--attempts);

    pr_err("[mpu6050] I2C2 clock not functional (IDLEST=%u)\n",
           (unsigned int)((value & CM_PER_I2C2_CLKCTRL_IDLEST_MASK) >> CM_PER_I2C2_CLKCTRL_IDLEST_SHIFT));
    return -ETIMEDOUT;
}

/**
 *  @brief Configura el controlador I2C en modo transmisor (escritor)
 * MST = 1, TRX = 0, Operating Modes = Master receiver.
 * MST = 1, TRX = 1, Operating Modes = Master transmitter. (Este seria este caso)
 */
static void set_master_transmiter(void)
{
    uint32_t old_value, new_value;

    old_value = i2c_con_shadow;
    new_value = old_value | I2C_CON_TRX | I2C_CON_MST;   // TRX=1, MST=1
    iowrite32(new_value, driver.i2c_module + MODULE_I2C_CON_OFFSET);
    i2c_con_shadow = new_value;

    pr_debug("[mpu6050] set_trx_write: I2C_CON updated from 0x%08X to 0x%08X\n", old_value, new_value);
}

/**
 *  @brief Configura el controlador I2C en modo transmisor (escritor)
 * MST = 1, TRX = 0, Operating Modes = Master receiver. (Este seria este caso)
 * MST = 1, TRX = 1, Operating Modes = Master transmitter.
 */
static void set_master_receiver(void)
{
    uint32_t old_value, new_value;

    old_value = i2c_con_shadow;
    new_value = (old_value & ~I2C_CON_TRX) | I2C_CON_MST;   // TRX=0, MST=1
    iowrite32(new_value, driver.i2c_module + MODULE_I2C_CON_OFFSET);
    i2c_con_shadow = new_value;

    pr_debug("[mpu6050] set_trx_write: I2C_CON updated from 0x%08X to 0x%08X\n", old_value, new_value);
}

/**
 * @brief Habilita las interrupciones del controlador I2C según el interrupt_mask
 * @param interrupt_mask Máscara de interrupciones a habilitar
 */
static void enable_irqs(uint32_t interrupt_mask)
{
    iowrite32(interrupt_mask, driver.i2c_module + MODULE_I2C_IRQENABLE_SET_OFFSET);

    pr_debug("[mpu6050] enable_irqs: Enabled interrupts 0x%08X\n", interrupt_mask);
}

/**
 * @brief Deshabilita las interrupciones del controlador I2C según el interrupt_mask
 * @param interrupt_mask Máscara de interrupciones a deshabilitar
 */
static void disable_irqs(uint32_t interrupt_mask)
{
    uint32_t old_value;

    old_value = ioread32(driver.i2c_module + MODULE_I2C_IRQENABLE_CLR_OFFSET);
    iowrite32(interrupt_mask, driver.i2c_module + MODULE_I2C_IRQENABLE_CLR_OFFSET);

    pr_debug("[mpu6050] disable_irqs: Disabled interrupts 0x%08X (previously 0x%08X)\n", interrupt_mask, old_value);
}

/**
 * @brief Seteo la condición de START y STOP en el registro I2C_CON
 * STT = Start Condition    (bit 0)
 * STP = Stop Condition     (bit 1)
 * STT = 1, STP = 0, Conditions = Start, Bus Activities = S-A-D.
 * STT = 0, STP = 1, Conditions = Stop, Bus Activities = P.
 * STT = 1, STP = 1, Conditions = Start-Stop (DCOUNT=n), Bus    (Utilizamos el valor de load_cnt() para definir n)
 */
static void stt_condition_stt_stp(void)
{
    uint32_t old_value, new_value;

    /*
     * IMPORTANT: STT/STP are self-clearing in HW. Do not keep them latched
     * in i2c_con_shadow, otherwise future writes to I2C_CON can accidentally
     * retrigger transfers at the wrong time.
     */
    old_value = i2c_con_shadow & ~(I2C_CON_STT | I2C_CON_STP);
    new_value = old_value | I2C_CON_STT | I2C_CON_STP; /* STT=1, STP=1 */
    iowrite32(new_value, driver.i2c_module + MODULE_I2C_CON_OFFSET);
    i2c_con_shadow = old_value;

    pr_debug("[mpu6050] set_start_stop_condition: I2C_CON updated from 0x%08X to 0x%08X\n", old_value, new_value);
}

/**
 * @brief Seteo la condicion para escribir un registro (START sin STOP)
 *      Esto se hace para enviar la dirección del registro a leer antes de hacer la lectura
 */
static void stt_condition_stt_(void)  // frx = for read
{
    uint32_t old_value, new_value;

    /* Do not latch STT/STP in the shadow (see stt_condition_stt_stp). */
    old_value = i2c_con_shadow & ~(I2C_CON_STT | I2C_CON_STP);
    new_value = (old_value | I2C_CON_STT) & ~I2C_CON_STP; /* STT=1, STP=0 */
    iowrite32(new_value, driver.i2c_module + MODULE_I2C_CON_OFFSET);
    i2c_con_shadow = old_value;

    pr_debug("[mpu6050] set_start_stop_condition: I2C_CON updated from 0x%08X to 0x%08X\n", old_value, new_value);
}

/**
 * @brief Configura filtros y rangos del sensor (DLFP/Gyro/Accel)
 *      Configura el rango del acelerómetro del MPU6050 a +/- 2g
 *      Modifica el registro ACCEL_CONFIG (0x1C) para establecer el rango
 *      de medición del acelerómetro a +/- 2g.
 *
 * @return 0 on success, -EIO on failure
 */
static int8_t set_mpu_range(void)
{
    uint8_t reg_value;
    u32 gyro_rate_hz;
    u32 smplrt_div;

    /* ================= CONFIG ================= */
    reg_value = mpu_read_reg(MPU6050_CONFIG_ADDRESS);
    if (!last_read_valid)
        return -EIO;

    reg_value &= ~CONFIG_DLPF_MASK;
    /* Config por DT: DLPF_CFG 0..7 */
    reg_value |= ((uint8_t)g_dt_dlpf_cfg << CONFIG_DLPF_POS);

    if (mpu_write_reg(MPU6050_CONFIG_ADDRESS, reg_value) < 0)
        return -EIO;

    /* ================= SMPLRT_DIV =================
     * SampleRate = GyroRate / (1 + SMPLRT_DIV)
     * Con DLPF_CFG=0/7: GyroRate=8kHz; caso contrario: GyroRate=1kHz.
     */
    gyro_rate_hz = (g_dt_dlpf_cfg == 0 || g_dt_dlpf_cfg == 7) ? 8000U : 1000U;
    if (g_dt_sample_rate_hz == 0)
        g_dt_sample_rate_hz = 200;
    if (g_dt_sample_rate_hz >= gyro_rate_hz) {
        smplrt_div = 0;
    } else {
        /* ceil(gyro_rate/sample_rate) - 1, acotado a 0..255 */
        smplrt_div = ((gyro_rate_hz + g_dt_sample_rate_hz - 1) / g_dt_sample_rate_hz) - 1;
        smplrt_div = min_t(u32, smplrt_div, 255U);
    }
    if (mpu_write_reg(MPU6050_SMPLRT_DIV_ADDRESS, (uint8_t)smplrt_div) < 0)
        return -EIO;

    /* ================= GYRO_CONFIG ================= */
    reg_value = mpu_read_reg(MPU6050_GYRO_CONFIG_ADDRESS);
    if (!last_read_valid)
        return -EIO;

    reg_value &= ~GYRO_CONFIG_FS_MASK;
    reg_value |= ((uint8_t)g_dt_gyro_fs << GYRO_CONFIG_FS_POS);

    if (mpu_write_reg(MPU6050_GYRO_CONFIG_ADDRESS, reg_value) < 0)
        return -EIO;

    /* ================= ACCEL_CONFIG ================= */
    reg_value = mpu_read_reg(MPU6050_ACCEL_CONFIG_ADDRESS);
    if (!last_read_valid)
        return -EIO;

    reg_value &= ~ACCEL_CONFIG_AFS_MASK;
    reg_value |= ((uint8_t)g_dt_accel_fs << ACCEL_CONFIG_AFS_POS);

    if (mpu_write_reg(MPU6050_ACCEL_CONFIG_ADDRESS, reg_value) < 0)
        return -EIO;

    /* ================= Verificación ================= */
    reg_value = mpu_read_reg(MPU6050_GYRO_CONFIG_ADDRESS);
    if (!last_read_valid ||
        ((reg_value & GYRO_CONFIG_FS_MASK) >> GYRO_CONFIG_FS_POS) != g_dt_gyro_fs)
        return -EIO;

    reg_value = mpu_read_reg(MPU6050_ACCEL_CONFIG_ADDRESS);
    if (!last_read_valid ||
        ((reg_value & ACCEL_CONFIG_AFS_MASK) >> ACCEL_CONFIG_AFS_POS) != g_dt_accel_fs)
        return -EIO;

    pr_debug("[mpu6050] CONFIG, SMPLRT_DIV, GYRO and ACCEL ranges set (dlpf=%u rate=%uHz div=%u gyro-fs=%u accel-fs=%u)\n",
             g_dt_dlpf_cfg, g_dt_sample_rate_hz, smplrt_div, g_dt_gyro_fs, g_dt_accel_fs);
    return 0;
}

/* ===================== FIFO helpers and read path ===================== */

/**
 * @brief Realiza un reset suave del sensor MPU6050.
 * @details Lee PWR_MGMT_1, setea el bit de reset y espera el tiempo recomendado.
 */
static void sensor_reset(void)
{
    uint8_t value = mpu_read_reg(MPU6050_PWR_MGMT_1_ADDRESS);
    if (!last_read_valid) {
        pr_err("[mpu6050] failed to read MPU6050_PWR_MGMT_1_ADDRESS\n");
        return;
    }
    if (mpu_write_reg(MPU6050_PWR_MGMT_1_ADDRESS, value | PWR_MGMT_1_DEVICE_RESET) < 0) {
        pr_err("[mpu6050] failed to write MPU6050_PWR_MGMT_1_ADDRESS\n");
        return;
    }

    if (msleep_interruptible(MPU6050_RESET_DELAY_MS))
        pr_warn("[mpu6050] Sleep interrupted during reset\n");

    pr_debug("[mpu6050] Reset done!\n");
}

/**
 * @brief Lee el contador de bytes disponibles en el FIFO.
 * @param fifo_count Puntero de salida para almacenar la cantidad de bytes.
 * @return 0 si tuvo éxito, -EIO en caso de error de lectura.
 */
static int read_fifo_count(uint16_t *fifo_count)
{
    uint8_t fifo_count_high = mpu_read_reg(MPU6050_FIFO_COUNT_H_ADDRESS);
    if (!last_read_valid) {
        pr_err("[mpu6050] Failed to read FIFO_COUNTH\n");
        return -EIO;
    }
    uint8_t fifo_count_low  = mpu_read_reg(MPU6050_FIFO_COUNT_L_ADDRESS);
    if (!last_read_valid) {
        pr_err("[mpu6050] Failed to read FIFO_COUNTL\n");
        return -EIO;
    }
    *fifo_count = ((fifo_count_high << 8) | fifo_count_low);
    return 0;
}

/**
 * @brief Deshabilita las fuentes de datos del FIFO escribiendo 0 en FIFO_EN.
 * @return 0 si tuvo éxito, -EIO en error de escritura.
 */
static int disable_fifo_data(void)
{
    if (mpu_write_reg(MPU6050_FIFO_EN_ADDRESS, 0) < 0) {
        pr_err("[mpu6050] Failed to set MPU6050_FIFO_EN_ADDRESS\n");
        return -EIO;
    }
    return 0;
}

/**
 * @brief Resetea y configura el FIFO para habilitar TEMP, ACCEL y GYRO.
 * @return 0 si tuvo éxito, -EIO si alguna operación I2C falla.
 */
static int configure_fifo(void)
{
    uint8_t reg_value;

    pr_info("[mpu6050] Configuring FIFO\n");

    /* Read USER_CTRL, then reset FIFO */
    pr_info("[mpu6050] FIFO step: read USER_CTRL\n");
    reg_value = mpu_read_reg(MPU6050_USER_CTRL_ADDRESS);
    if (!last_read_valid) {
        pr_err("[mpu6050] Failed to read USER_CTRL register\n");
        return -EIO;
    }

    pr_info("[mpu6050] FIFO step: write USER_CTRL FIFO_RST\n");
    reg_value |= MPU6050_USER_CTRL_FIFO_RST_BIT; /* FIFO_RST */
    if (mpu_write_reg(MPU6050_USER_CTRL_ADDRESS, reg_value) < 0) {
        pr_err("[mpu6050] Failed to reset FIFO\n");
        return -EIO;
    }
    pr_debug("[mpu6050] FIFO reset\n");

    /* Program FIFO_EN sources before enabling FIFO globally */
    pr_info("[mpu6050] FIFO step: write FIFO_EN sources\n");
    reg_value = MPU6050_FIFO_EN_TEMP_ACCEL_GYRO; /* TEMP + ACCEL + GYRO */
    if (mpu_write_reg(MPU6050_FIFO_EN_ADDRESS, reg_value) < 0) {
        pr_err("[mpu6050] Failed to configure FIFO_EN register\n");
        return -EIO;
    }

    /* Enable FIFO globally (USER_CTRL.FIFO_EN), leaving FIFO_RST cleared */
    pr_info("[mpu6050] FIFO step: write USER_CTRL FIFO_EN\n");
    reg_value = mpu_read_reg(MPU6050_USER_CTRL_ADDRESS);
    if (!last_read_valid) {
        pr_err("[mpu6050] Failed to re-read USER_CTRL register\n");
        return -EIO;
    }
    reg_value &= ~MPU6050_USER_CTRL_FIFO_RST_BIT; /* Clear FIFO_RST */
    reg_value |= MPU6050_USER_CTRL_FIFO_EN_BIT;  /* Set FIFO_EN */
    if (mpu_write_reg(MPU6050_USER_CTRL_ADDRESS, reg_value) < 0) {
        pr_err("[mpu6050] Failed to enable FIFO\n");
        return -EIO;
    }
    pr_debug("[mpu6050] FIFO enabled in USER_CTRL register\n");

    pr_info("[mpu6050] FIFO configured successfully\n");
    return 0;
}

/**
 * @brief Imprime una cantidad fija de bytes de una trama cruda (debug).
 * @param frame Buffer con los datos a imprimir.
 * @param size Cantidad de bytes a imprimir.
 */
static void print_raw_frame(const uint8_t *frame, size_t size)
{
    pr_info("[mpu6050] Raw Frame: ");
    for (size_t i = 0; i < size; i++)
        pr_info("%02X ", frame[i]);
    pr_info("\n");
}

/**
 * @brief Desactiva el modo de suspensión del MPU6050.
 * @return 0 si tuvo éxito, -EIO en caso de error.
 */
static int8_t sleep_mode_off(void)
{
    uint8_t reg_value = 0;

    /* Read the PWR_MGMT_1 register to check if the device is in sleep mode */
    reg_value = mpu_read_reg(MPU6050_PWR_MGMT_1_ADDRESS);
    if (!last_read_valid)
    {
        pr_err("[mpu6050] Failed to read PWR_MGMT_1 register\n");
        return -EIO;
    }

    pr_debug("[mpu6050] Current PWR_MGMT_1 register value: 0x%02X\n", reg_value);

    /* Check if the device is already awake */
    if ((reg_value >> 6) == 0)
    {
        pr_debug("[mpu6050] Device is already not in sleep mode\n");
        return 0;
    }

    /* Wake up the device by clearing the sleep mode bit */
    reg_value &= ~0x40; // Clear SLEEP bit (bit 6)
    if (mpu_write_reg(MPU6050_PWR_MGMT_1_ADDRESS, reg_value) < 0)
    {
        pr_err("[mpu6050] Failed to clear SLEEP bit\n");
        return -EIO;
    }

    /* Datasheet indicates wake-up time; use 150ms to be safe */
    msleep(150);

    /* Verify that sleep mode was successfully turned off */
    reg_value = mpu_read_reg(MPU6050_PWR_MGMT_1_ADDRESS);
    if (!last_read_valid)
    {
        pr_err("[mpu6050] Failed to verify PWR_MGMT_1 register after write\n");
        return -EIO;
    }

    if ((reg_value >> 6) == 1)
    {
        pr_err("[mpu6050] Failed to disable sleep mode\n");
        return -EIO;
    }

    pr_debug("[mpu6050] Sleep mode successfully turned off\n");
    return 0;
}

module_init(mpu6050_init);
module_exit(mpu6050_exit);
