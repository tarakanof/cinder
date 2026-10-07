#pragma once

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/i2c_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_KNOB_15_MD50ET_SPI_HOST            (SPI2_HOST)

#define BSP_KNOB_15_MD50ET_PIN_LCD_CS          (GPIO_NUM_12)
#define BSP_KNOB_15_MD50ET_PIN_LCD_PCLK        (GPIO_NUM_10)
#define BSP_KNOB_15_MD50ET_PIN_LCD_DATA0       (GPIO_NUM_13)
#define BSP_KNOB_15_MD50ET_PIN_LCD_DATA1       (GPIO_NUM_11)
#define BSP_KNOB_15_MD50ET_PIN_LCD_DATA2       (GPIO_NUM_14)
#define BSP_KNOB_15_MD50ET_PIN_LCD_DATA3       (GPIO_NUM_9)
#define BSP_KNOB_15_MD50ET_PIN_LCD_RST         (GPIO_NUM_8)
#define BSP_KNOB_15_MD50ET_PIN_BK_LIGHT        (GPIO_NUM_17)  /* high before LCD init (docs/llm.md) */
#define BSP_KNOB_15_MD50ET_BK_LIGHT_ON_LEVEL   (1)

#define BSP_KNOB_15_MD50ET_H_RES               (472)
#define BSP_KNOB_15_MD50ET_V_RES               (466)

#define BSP_KNOB_15_MD50ET_TOUCH_I2C_HOST      (I2C_NUM_0)
#define BSP_KNOB_15_MD50ET_PIN_TOUCH_SCL       (GPIO_NUM_3)
#define BSP_KNOB_15_MD50ET_PIN_TOUCH_SDA       (GPIO_NUM_1)
#define BSP_KNOB_15_MD50ET_PIN_TOUCH_RST       (GPIO_NUM_2)
#define BSP_KNOB_15_MD50ET_PIN_TOUCH_INT       (GPIO_NUM_4)

#define BSP_KNOB_15_MD50ET_PIN_ENCODER_A       (GPIO_NUM_6)
#define BSP_KNOB_15_MD50ET_PIN_ENCODER_B       (GPIO_NUM_5)
#define BSP_KNOB_15_MD50ET_PIN_BUTTON          (GPIO_NUM_0)

#ifdef __cplusplus
}
#endif
