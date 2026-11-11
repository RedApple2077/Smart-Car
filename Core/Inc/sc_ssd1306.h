/**
  ******************************************************************************
  * @file    sc_ssd1306.h
  * @brief   SSD1306 128x64 OLED 非阻塞刷新驱动。
  ******************************************************************************
  */

#ifndef __SC_SSD1306_H
#define __SC_SSD1306_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define SC_SSD1306_I2C_ADDRESS      0x3Cu
#define SC_SSD1306_WIDTH            128u
#define SC_SSD1306_HEIGHT           64u
#define SC_SSD1306_PAGES            8u
#define SC_SSD1306_LINE_CHARS       21u

typedef enum {
  SC_SSD1306_OK = 0,
  SC_SSD1306_ERR_ARGUMENT,
  SC_SSD1306_ERR_CONFIG,
  SC_SSD1306_ERR_NOT_READY,
  SC_SSD1306_ERR_BUSY,
  SC_SSD1306_ERR_TIMEOUT,
  SC_SSD1306_ERR_BUS
} sc_ssd1306_status_t;

typedef enum {
  SC_SSD1306_STATE_UNINITIALIZED = 0,
  SC_SSD1306_STATE_IDLE,
  SC_SSD1306_STATE_REFRESHING,
  SC_SSD1306_STATE_RETRY_WAIT
} sc_ssd1306_state_t;

typedef struct {
  sc_ssd1306_state_t state;
  uint8_t dirty_pages;
  uint8_t refresh_pending;
  uint32_t retry_count;
  sc_ssd1306_status_t last_status;
} sc_ssd1306_status_info_t;

/**
  * @brief  启动期初始化；成功后把全屏清空请求交给 service() 异步发送。
  *         初始化失败后，refresh/service 不再访问总线。
  */
sc_ssd1306_status_t sc_ssd1306_init(void);

/**
  * @brief  清空 1024 B 显存并标记所有页为脏。
  */
sc_ssd1306_status_t sc_ssd1306_clear(void);

/**
  * @brief  向显存写入一行 5x7 ASCII 文本（最多 21 个字符）。
  */
sc_ssd1306_status_t sc_ssd1306_write_line(uint8_t row, const char *text);

/**
  * @brief  请求刷新；成功表示请求已接受，不表示屏幕已刷新完成。
  */
sc_ssd1306_status_t sc_ssd1306_refresh(void);

/**
  * @brief  主循环服务函数，每次最多启动一笔 I2C 事务。
  */
void sc_ssd1306_service(uint32_t now_ms);

/**
  * @brief  获取刷新状态快照。
  */
sc_ssd1306_status_t sc_ssd1306_get_status(
  sc_ssd1306_status_info_t *status);

#ifdef __cplusplus
}
#endif

#endif /* __SC_SSD1306_H */
