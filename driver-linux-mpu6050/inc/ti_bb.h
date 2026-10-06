/**
 * @file ti_bb.h
 * @brief Definiciones específicas AM335x/BeagleBone Black (I2C2 + clocks + IRQ bits).
 */

#ifndef TI_BB_H
#define TI_BB_H

/**
 * @brief Direcciones/offsets del SoC y registros del controlador I2C.
 * @details Referencia: https://www.ti.com/lit/ug/spruh73q/spruh73q.pdf
 */

/* Clock Module: I2C2 clock control */
#define CM_PER_I2C2_CLKCTRL                 0x44E00044

/* CM_PER_I2C2_CLKCTRL fields */
#define CM_PER_I2C2_CLKCTRL_MODULEMODE_ENABLE   0x2U          /* bits [1:0] */
#define CM_PER_I2C2_CLKCTRL_IDLEST_SHIFT        16
#define CM_PER_I2C2_CLKCTRL_IDLEST_MASK         (0x3U << CM_PER_I2C2_CLKCTRL_IDLEST_SHIFT)
#define CM_PER_I2C2_CLKCTRL_IDLEST_FUNC         0x0U

/* Control Module: pin multiplexing for I2C2 */
#define CONTROL_MODULE_CONF_I2C2_SDA        0x44E10978  /* P9_20 */
#define CONTROL_MODULE_CONF_I2C2_SCL        0x44E1097C  /* P9_19 */
#define CONTROL_MODULE_CONF_I2C2_PRESET     0x33        /* mux + pull settings */

/* I2C2 controller base and register offsets (AM335x TRM) */
#define MODULE_I2C_BASE_ADDR                0x4819C000
#define MODULE_I2C_SIZE                     0x1000

#define MODULE_I2C_REVN_LO_OFFSET           0x00
#define MODULE_I2C_REVN_HI_OFFSET           0x04
#define MODULE_I2C_SYSC_OFFSET              0x10
#define MODULE_I2C_IRQSTATUS_RAW_OFFSET     0x24
#define MODULE_I2C_IRQSTATUS_OFFSET         0x28
#define MODULE_I2C_IRQENABLE_SET_OFFSET     0x2C
#define MODULE_I2C_IRQENABLE_CLR_OFFSET     0x30
#define MODULE_I2C_WE_OFFSET                0x34
#define MODULE_I2C_DMARXENABLE_SET_OFFSET   0x38
#define MODULE_I2C_DMATXENABLE_SET_OFFSET   0x3C
#define MODULE_I2C_DMARXENABLE_CLR_OFFSET   0x40
#define MODULE_I2C_DMATXENABLE_CLR_OFFSET   0x44
#define MODULE_I2C_DMARXWAKE_EN_OFFSET      0x48
#define MODULE_I2C_DMATXWAKE_EN_OFFSET      0x4C
#define MODULE_I2C_SYSS_OFFSET              0x90
#define MODULE_I2C_BUF_OFFSET               0x94
#define MODULE_I2C_CNT_OFFSET               0x98
#define MODULE_I2C_DATA_OFFSET              0x9C
#define MODULE_I2C_CON_OFFSET               0xA4
#define MODULE_I2C_OA_OFFSET                0xA8
#define MODULE_I2C_SA_OFFSET                0xAC
#define MODULE_I2C_PSC_OFFSET               0xB0
#define MODULE_I2C_SCLL_OFFSET              0xB4
#define MODULE_I2C_SCLH_OFFSET              0xB8
#define MODULE_I2C_SYSTEST_OFFSET           0xBC
#define MODULE_I2C_BUFSTAT_OFFSET           0xC0
#define MODULE_I2C_OA1_OFFSET               0xC4
#define MODULE_I2C_OA2_OFFSET               0xC8
#define MODULE_I2C_OA3_OFFSET               0xCC
#define MODULE_I2C_ACTOA_OFFSET             0xD0
#define MODULE_I2C_SBLOCK_OFFSET            0xD4

/* Timing presets for ~100kHz with 12MHz module clock */
#define PRESCALER_VALUE_100k                0x03
#define I2C_SCLL_100K                       0x0000003B
#define I2C_SCLH_100K                       0x00000037

/* I2C_CON bit positions and masks */
#define I2C_CON_STT_BIT     0
#define I2C_CON_STP_BIT     1
#define I2C_CON_TRX_BIT     9
#define I2C_CON_MST_BIT     10
#define I2C_CON_I2C_EN_BIT  15

#define I2C_CON_STT         (1U << I2C_CON_STT_BIT)
#define I2C_CON_STP         (1U << I2C_CON_STP_BIT)
#define I2C_CON_TRX         (1U << I2C_CON_TRX_BIT)
#define I2C_CON_MST         (1U << I2C_CON_MST_BIT)
#define I2C_CON_I2C_EN      (1U << I2C_CON_I2C_EN_BIT)

/* I2C_CNT helper constants */
#define I2C_CNT_ONE_BYTE    0x01U
#define I2C_CNT_TWO_BYTES   0x02U

/* Clock-ready polling parameters */
#define I2C2_CLK_READY_ATTEMPTS  1000
#define I2C2_CLK_POLL_DELAY_US   10

/* I2C controller interrupt bits (IRQSTATUS / IRQENABLE) */
#define AL_INT_ENABLED   (1U)
#define NACK_INT_ENABLED (1U << 1)
#define ARDY_INT_ENABLED (1U << 2)
#define RRDY_INT_ENABLED (1U << 3)
#define XRDY_INT_ENABLED (1U << 4)

#define ALL_INTERRUPTS 		(RRDY_INT_ENABLED | XRDY_INT_ENABLED | ARDY_INT_ENABLED | BF_INT_ENABLED | BB_INT_ENABLED | NACK_INT_ENABLED)
#define GC_INT_ENABLED   (1U << 5)
#define STC_INT_ENABLED  (1U << 6)
#define AERR_INT_ENABLED (1U << 7)
#define BF_INT_ENABLED   (1U << 8)
#define AAS_INT_ENABLED  (1U << 9)
#define XUDF_INT_ENABLED (1U << 10)
#define ROVR_INT_ENABLED (1U << 11)
#define BB_INT_ENABLED   (1U << 12)
#define RDR_INT_ENABLED  (1U << 13)
#define XDR_INT_ENABLED  (1U << 14)

/* Convenience masks used by driver */
#define I2C_WR_INTERRUPTS (XRDY_INT_ENABLED | ARDY_INT_ENABLED | NACK_INT_ENABLED | \
                           AL_INT_ENABLED | BB_INT_ENABLED | BF_INT_ENABLED | AERR_INT_ENABLED)
#define I2C_RX_INTERRUPTS (RRDY_INT_ENABLED | ARDY_INT_ENABLED | NACK_INT_ENABLED | \
                           AL_INT_ENABLED | BB_INT_ENABLED | BF_INT_ENABLED | AERR_INT_ENABLED)

#endif /* TI_BB_H */
