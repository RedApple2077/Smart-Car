/**
  ******************************************************************************
  * @file    sc_ssd1306.h
  * @brief   SSD1306 128x64 OLED 驱动（I2C2，7 位地址 0x3C）。
  *          接线（文档 4.6）：
  *            OLED GND -> 系统 GND
  *            OLED VCC -> 核心板 3.3V（不接 5V）
  *            OLED SCL -> PB10 / I2C2_SCL
  *            OLED SDA -> PB11 / I2C2_SDA
  *          I2C2 基线 100 kHz；OLED 与 MPU6050 共用总线，地址分别为
  *          0x3C 和 0x68。OLED 是辅助设备，NACK/超时只关闭显示，
  *          不改变控制状态（文档 8.4）。
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SC_SSD1306_H
#define __SC_SSD1306_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

/* Exported constants --------------------------------------------------------*/
#define SC_SSD1306_I2C_ADDRESS 0x3Cu   /* 7 位地址；模块背面 0x78 为 8 位写地址 */
#define SC_SSD1306_WIDTH       128u
#define SC_SSD1306_HEIGHT      64u
#define SC_SSD1306_PAGES       8u
#define SC_SSD1306_LINE_CHARS  21u

/* Exported types ------------------------------------------------------------*/
typedef enum {
  SC_SSD1306_OK = 0,
  SC_SSD1306_ERR_ARGUMENT = 1,
  SC_SSD1306_ERR_NOT_READY = 2,
  SC_SSD1306_ERR_TIMEOUT = 3,
  SC_SSD1306_ERR_BUS = 4
} sc_ssd1306_status_t;

/* Exported functions prototypes ---------------------------------------------*/
/**
  * @brief  初始化 SSD1306：检查 0x3C、发送初始化命令并清屏。
  * @retval SC_SSD1306_OK；NACK/总线错误/超时返回对应错误码。失败只影响显示。
  */
sc_ssd1306_status_t sc_ssd1306_init(void);

/**
  * @brief  清空显示缓冲；需再调用 sc_ssd1306_refresh() 才会写到屏幕。
  * @retval SC_SSD1306_OK
  */
sc_ssd1306_status_t sc_ssd1306_clear(void);

/**
  * @brief  把一行文本写入显示缓冲（0..7 行，最多 21 个字符）。
  *         只标记发生变化的页，之后由 sc_ssd1306_refresh() 发送。
  * @param  row  0..7
  * @param  text 以 '\0' 结尾的 ASCII 文本
  * @retval SC_SSD1306_OK 或 SC_SSD1306_ERR_ARGUMENT
  */
sc_ssd1306_status_t sc_ssd1306_write_line(uint8_t row, const char *text);

/**
  * @brief  把发生变化的页面通过 I2C2 写到 OLED。
  * @retval SC_SSD1306_OK、SC_SSD1306_ERR_TIMEOUT、SC_SSD1306_ERR_BUS
  *         或 SC_SSD1306_ERR_NOT_READY。
  *         失败时保留未发送页的脏标记，下一次可重试。
  */
sc_ssd1306_status_t sc_ssd1306_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* __SC_SSD1306_H */
