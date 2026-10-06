/**
 * @file mpu6050.h
 * @brief Constantes del MPU6050: direcciones I2C, registros y campos de configuración.
 */

#ifndef MPU6050_H
#define MPU6050_H

/** @name Identificadores del driver */
/** @{ */
#define DEVICE_NAME "mpu6050"
#define CLASS_NAME  "i2cMpu6050"
#define COMPATIBLE  "i2c_mpu6050"
/** @} */

/** @name Parámetros de char device */
/** @{ */
#define MENOR       0
#define CANT_DISP   1
/** @} */

/** @brief Buffer chico usado por helpers del char device. */
#define CHAT_BUFFER_SIZE 256

/** @name Direcciones I2C del sensor */
/** @{ */
#define MPU6050_I2C_ADDRESS_1 0x68
#define MPU6050_I2C_ADDRESS_2 0x69
/** @} */

/** @name Mapa de registros MPU6050 */
/** @{ */
#define MPU6050_WHO_AM_I_REGISTER_ADDRESS 0x75
#define MPU6050_PWR_MGMT_1_ADDRESS        0x6B
#define MPU6050_CONFIG_ADDRESS            0x1A
#define MPU6050_SMPLRT_DIV_ADDRESS        0x19
#define MPU6050_GYRO_CONFIG_ADDRESS       0x1B
#define MPU6050_ACCEL_CONFIG_ADDRESS      0x1C
#define MPU6050_INT_ENABLE_ADDRESS        0x38
#define MPU6050_INT_STATUS_ADDRESS        0x3A
#define MPU6050_FIFO_EN_ADDRESS           0x23
#define MPU6050_USER_CTRL_ADDRESS         0x6A
#define MPU6050_FIFO_R_W_ADDRESS          0x74
#define MPU6050_FIFO_COUNT_H_ADDRESS      0x72
/** Per MPU6050 register map: FIFO_COUNTL is 0x73 (0x71 is I2C_SLV0_REG) */
#define MPU6050_FIFO_COUNT_L_ADDRESS      0x73
#define MPU6050_TEMP_H_ADDRESS            0x41
#define MPU6050_TEMP_L_ADDRESS            0x42
#define MPU6050_ACCEL_X_H_ADDRESS         0x3B
#define MPU6050_ACCEL_X_L_ADDRESS         0x3C
#define MPU6050_ACCEL_Y_H_ADDRESS         0x3D
#define MPU6050_ACCEL_Y_L_ADDRESS         0x3E
#define MPU6050_ACCEL_Z_H_ADDRESS         0x3F
#define MPU6050_ACCEL_Z_L_ADDRESS         0x40
/** @} */

/** @name Campos de CONFIG */
/** @{ */
#define CONFIG_DLPF_MASK      0x07
#define CONFIG_DLPF_POS       0
/** DLPF_CFG: 0/7 => gyro internal rate 8kHz, 1..6 => 1kHz */
#define DLPF_CFG_260HZ        0
#define DLPF_CFG_44HZ         3
/** @} */

/** @name Campos de GYRO_CONFIG */
/** @{ */
#define GYRO_CONFIG_FS_MASK   (0x3 << 3)
#define GYRO_CONFIG_FS_POS    3
/** Full-scale range selections for FS_SEL[4:3] */
#define GYRO_FS_250DPS        0
#define GYRO_FS_500DPS        1
#define GYRO_FS_1000DPS       2
#define GYRO_FS_2000DPS       3
/** @} */

/** @name Campos de ACCEL_CONFIG */
/** @{ */
#define ACCEL_CONFIG_AFS_MASK (0x3 << 3)
#define ACCEL_CONFIG_AFS_POS  3
/** Full-scale range selections for AFS_SEL[4:3] */
#define ACCEL_AFS_2G          0
#define ACCEL_AFS_4G          1
#define ACCEL_AFS_8G          2
#define ACCEL_AFS_16G         3
/** @} */

/** @name Bits de PWR_MGMT_1 */
/** @{ */
#define PWR_MGMT_1_SLEEP_BIT_POS 6
#define PWR_MGMT_1_SLEEP_MASK    (1U << PWR_MGMT_1_SLEEP_BIT_POS)
#define PWR_MGMT_1_SLEEP_ENABLED  1U
#define PWR_MGMT_1_SLEEP_DISABLED 0U
/** Additional PWR_MGMT_1 helpers */
#define PWR_MGMT_1_DEVICE_RESET   (1U << 7)
#define PWR_MGMT_1_CLKSEL_PLL_X   0x01U
/** @} */

/** @name Definiciones alternativas (legacy) */
/** @{ */
#define ACCEL_CONFIG_AFS_MASK_R  (0x18U)
#define ACCEL_CONFIG_AFS_POS_R   3
#define ACCEL_AFS_16G_R          (0x3U)
/** @} */

/** @name Helpers de tiempo */
/** @{ */
#ifndef HZ
#define HZ 100
#endif
#define TIMEOUT_JIFFIES          (2 * HZ)
/** @} */

/** @name FIFO: helpers y tamaños */
/** @{ */
#define MPU6050_USER_CTRL_FIFO_RST_BIT  (1U << 2)
#define MPU6050_USER_CTRL_FIFO_EN_BIT   (1U << 6)
#define MPU6050_FIFO_EN_TEMP_ACCEL_GYRO 0xF8U

#define MPU6050_RESET_DELAY_MS    100
#define MPU6050_FIFO_READ_SIZE    1024U
#define MPU6050_DEBUG_PRINT_BYTES 20U
/** @} */

#endif /* MPU6050_H */
