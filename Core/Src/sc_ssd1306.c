/**
  ******************************************************************************
  * @file    sc_ssd1306.c
  * @brief   SSD1306 128x64 OLED 驱动实现（I2C2，地址 0x3C）。
  *          固定 1024 字节显示缓冲 + 页脏标记；只发送变化的页。
  *          I2C 访问带超时，OLED 故障不改变任何控制状态。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include <string.h>

#include "sc_ssd1306.h"
#include "sc_font5x7.h"
#include "main.h"

/* External variables --------------------------------------------------------*/
extern I2C_HandleTypeDef hi2c2;

/* Private constants ---------------------------------------------------------*/
#define SC_SSD1306_I2C_ADDR_8BIT   ((uint16_t)(SC_SSD1306_I2C_ADDRESS << 1))
#define SC_SSD1306_I2C_TIMEOUT_MS  20u
#define SC_SSD1306_CHUNK           32u
#define SC_SSD1306_CMD_BUF_SIZE    32u

/* Private variables ---------------------------------------------------------*/
static uint8_t s_framebuffer[SC_SSD1306_PAGES][SC_SSD1306_WIDTH];
static uint8_t s_dirty_pages;

/* SSD1306 初始化命令流；首个控制字节 0x00 由发送函数补上。 */
static const uint8_t s_init_cmds[] = {
  0xAEu,              /* display off */
  0xD5u, 0x80u,       /* clock divide ratio */
  0xA8u, 0x3Fu,       /* multiplex 64 */
  0xD3u, 0x00u,       /* display offset 0 */
  0x40u,              /* start line 0 */
  0x8Du, 0x14u,       /* charge pump on */
  0x20u, 0x02u,       /* page addressing mode (per-page refresh) */
  0xA1u,              /* segment remap */
  0xC8u,              /* COM scan dec */
  0xDAu, 0x12u,       /* COM pins for 128x64 */
  0x81u, 0x7Fu,       /* contrast */
  0xD9u, 0xF1u,       /* pre-charge */
  0xDBu, 0x30u,       /* VCOMH */
  0xA4u,              /* display from RAM */
  0xA6u,              /* normal display */
  0xAFu               /* display on */
};

/* Private functions ---------------------------------------------------------*/
static sc_ssd1306_status_t sc_ssd1306_map_hal(HAL_StatusTypeDef status)
{
  switch (status)
  {
    case HAL_OK:
      return SC_SSD1306_OK;
    case HAL_TIMEOUT:
      return SC_SSD1306_ERR_TIMEOUT;
    case HAL_BUSY:
      return SC_SSD1306_ERR_NOT_READY;
    case HAL_ERROR:
    default:
      return SC_SSD1306_ERR_BUS;
  }
}

static sc_ssd1306_status_t sc_ssd1306_write_commands(const uint8_t *cmds,
                                                      uint16_t len)
{
  uint8_t buf[SC_SSD1306_CMD_BUF_SIZE];
  HAL_StatusTypeDef hal_status;

  if ((cmds == NULL) || (len == 0u) ||
      (((uint32_t)len + 1u) > (uint32_t)sizeof(buf)))
  {
    return SC_SSD1306_ERR_ARGUMENT;
  }

  buf[0] = 0x00u;  /* control byte: command stream */
  memcpy(&buf[1], cmds, len);

  hal_status = HAL_I2C_Master_Transmit(&hi2c2, SC_SSD1306_I2C_ADDR_8BIT,
                                       buf, (uint16_t)(len + 1u),
                                       SC_SSD1306_I2C_TIMEOUT_MS);
  return sc_ssd1306_map_hal(hal_status);
}

static sc_ssd1306_status_t sc_ssd1306_write_page(uint8_t page)
{
  uint8_t buf[SC_SSD1306_CHUNK + 1u];
  uint16_t offset = 0u;
  sc_ssd1306_status_t status;
  HAL_StatusTypeDef hal_status;

  while (offset < SC_SSD1306_WIDTH)
  {
    uint16_t chunk = (uint16_t)(SC_SSD1306_WIDTH - offset);

    if (chunk > SC_SSD1306_CHUNK)
    {
      chunk = SC_SSD1306_CHUNK;
    }

    buf[0] = 0x40u;  /* control byte: data stream */
    memcpy(&buf[1], &s_framebuffer[page][offset], chunk);

    hal_status = HAL_I2C_Master_Transmit(&hi2c2, SC_SSD1306_I2C_ADDR_8BIT,
                                         buf, (uint16_t)(chunk + 1u),
                                         SC_SSD1306_I2C_TIMEOUT_MS);
    status = sc_ssd1306_map_hal(hal_status);
    if (status != SC_SSD1306_OK)
    {
      return status;
    }

    offset = (uint16_t)(offset + chunk);
  }

  return SC_SSD1306_OK;
}

/* Exported functions --------------------------------------------------------*/
sc_ssd1306_status_t sc_ssd1306_init(void)
{
  HAL_StatusTypeDef hal_status;
  sc_ssd1306_status_t status;

  /* SSD1306 上电后需要稳定时间；只允许在初始化路径等待。 */
  HAL_Delay(100u);

  hal_status = HAL_I2C_IsDeviceReady(&hi2c2, SC_SSD1306_I2C_ADDR_8BIT, 3u,
                                     SC_SSD1306_I2C_TIMEOUT_MS);
  if (hal_status != HAL_OK)
  {
    return sc_ssd1306_map_hal(hal_status);
  }

  status = sc_ssd1306_write_commands(s_init_cmds,
                                     (uint16_t)sizeof(s_init_cmds));
  if (status != SC_SSD1306_OK)
  {
    return status;
  }

  (void)sc_ssd1306_clear();
  return sc_ssd1306_refresh();
}

sc_ssd1306_status_t sc_ssd1306_clear(void)
{
  memset(s_framebuffer, 0, sizeof(s_framebuffer));
  s_dirty_pages = 0xFFu;
  return SC_SSD1306_OK;
}

sc_ssd1306_status_t sc_ssd1306_write_line(uint8_t row, const char *text)
{
  uint8_t page[SC_SSD1306_WIDTH];
  uint16_t col = 0u;
  size_t i;
  uint8_t k;

  if ((row >= SC_SSD1306_PAGES) || (text == NULL))
  {
    return SC_SSD1306_ERR_ARGUMENT;
  }

  memset(page, 0, sizeof(page));

  for (i = 0u; (text[i] != '\0') && (i < SC_SSD1306_LINE_CHARS); i++)
  {
    uint8_t c = (uint8_t)text[i];
    const uint8_t *glyph;

    if ((c < 0x20u) || (c > 0x7Eu))
    {
      c = (uint8_t)'?';
    }

    glyph = sc_font5x7[c - 0x20u];
    for (k = 0u; k < 5u; k++)
    {
      page[col + k] = glyph[k];
    }
    col = (uint16_t)(col + 6u);
  }

  if (memcmp(page, s_framebuffer[row], sizeof(page)) == 0)
  {
    return SC_SSD1306_OK;
  }

  memcpy(s_framebuffer[row], page, sizeof(page));
  s_dirty_pages |= (uint8_t)(1u << row);
  return SC_SSD1306_OK;
}

sc_ssd1306_status_t sc_ssd1306_refresh(void)
{
  uint8_t page;
  uint8_t cmds[3];
  sc_ssd1306_status_t status;

  if (s_dirty_pages == 0u)
  {
    return SC_SSD1306_OK;
  }

  for (page = 0u; page < SC_SSD1306_PAGES; page++)
  {
    if ((s_dirty_pages & (uint8_t)(1u << page)) == 0u)
    {
      continue;
    }

    /* 页模式寻址：页号、列低 4 位、列高 4 位。 */
    cmds[0] = (uint8_t)(0xB0u | page);
    cmds[1] = 0x00u;
    cmds[2] = 0x10u;

    status = sc_ssd1306_write_commands(cmds, 3u);
    if (status != SC_SSD1306_OK)
    {
      return status;
    }

    status = sc_ssd1306_write_page(page);
    if (status != SC_SSD1306_OK)
    {
      return status;
    }

    s_dirty_pages &= (uint8_t)~(1u << page);
  }

  return SC_SSD1306_OK;
}
