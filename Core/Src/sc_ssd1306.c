/**
  ******************************************************************************
  * @file    sc_ssd1306.c
  * @brief   SSD1306 128x64 OLED 非阻塞刷新驱动。
  ******************************************************************************
  */

#include "sc_ssd1306.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "main.h"
#include "sc_font5x7.h"
#include "sc_i2c2.h"

#define SC_SSD1306_I2C_TIMEOUT_MS   20u
#define SC_SSD1306_CHUNK            32u
#define SC_SSD1306_CHUNKS_PER_PAGE  (SC_SSD1306_WIDTH / SC_SSD1306_CHUNK)
#define SC_SSD1306_RETRY_DELAY_MS   100u
#define SC_SSD1306_CMD_BUF_SIZE     32u

typedef enum {
  SC_SSD1306_TX_NONE = 0,
  SC_SSD1306_TX_PAGE_COMMAND,
  SC_SSD1306_TX_PAGE_DATA
} sc_ssd1306_tx_kind_t;

static uint8_t s_framebuffer[SC_SSD1306_PAGES][SC_SSD1306_WIDTH];
static uint32_t s_page_generation[SC_SSD1306_PAGES];
static uint8_t s_dirty_pages;
static uint8_t s_initialized;
static uint8_t s_refresh_requested;
static uint8_t s_retry_waiting;
static uint8_t s_active_page;
static uint8_t s_active_chunk;
static uint8_t s_command_sent;
static uint32_t s_active_generation;
static uint32_t s_retry_at_ms;
static uint32_t s_retry_count;
static volatile uint8_t s_tx_active;
static volatile uint8_t s_tx_done;
static sc_ssd1306_tx_kind_t s_tx_kind;
static volatile sc_ssd1306_status_t s_tx_result;
static sc_ssd1306_status_t s_last_status;
static uint8_t s_tx_buffer[SC_SSD1306_CHUNK + 1u];

static const uint8_t s_init_cmds[] = {
  0xAEu,
  0xD5u, 0x80u,
  0xA8u, 0x3Fu,
  0xD3u, 0x00u,
  0x40u,
  0x8Du, 0x14u,
  0x20u, 0x02u,
  0xA1u,
  0xC8u,
  0xDAu, 0x12u,
  0x81u, 0x7Fu,
  0xD9u, 0xF1u,
  0xDBu, 0x30u,
  0xA4u,
  0xA6u,
  0xAFu
};

static uint32_t sc_ssd1306_enter_critical(void)
{
  uint32_t primask = __get_PRIMASK();
  __disable_irq();
  return primask;
}

static void sc_ssd1306_exit_critical(uint32_t primask)
{
  if (primask == 0u)
  {
    __enable_irq();
  }
}

static bool sc_ssd1306_time_reached(uint32_t now_ms,
                                    uint32_t deadline_ms)
{
  return (int32_t)(now_ms - deadline_ms) >= 0;
}

static sc_ssd1306_status_t sc_ssd1306_map_i2c2(
  sc_i2c2_status_t status)
{
  switch (status)
  {
    case SC_I2C2_OK:
      return SC_SSD1306_OK;
    case SC_I2C2_ERR_ARGUMENT:
      return SC_SSD1306_ERR_ARGUMENT;
    case SC_I2C2_ERR_CONFIG:
      return SC_SSD1306_ERR_CONFIG;
    case SC_I2C2_ERR_NOT_READY:
      return SC_SSD1306_ERR_NOT_READY;
    case SC_I2C2_ERR_BUSY:
      return SC_SSD1306_ERR_BUSY;
    case SC_I2C2_ERR_TIMEOUT:
      return SC_SSD1306_ERR_TIMEOUT;
    case SC_I2C2_ERR_BUS:
    default:
      return SC_SSD1306_ERR_BUS;
  }
}

static uint8_t sc_ssd1306_next_dirty_page(void)
{
  uint8_t page;

  for (page = 0u; page < SC_SSD1306_PAGES; page++)
  {
    if ((s_dirty_pages & (uint8_t)(1u << page)) != 0u)
    {
      return page;
    }
  }
  return SC_SSD1306_PAGES;
}

static void sc_ssd1306_tx_complete(sc_i2c2_status_t status, void *context)
{
  (void)context;
  s_tx_result = sc_ssd1306_map_i2c2(status);
  s_tx_active = 0u;
  s_tx_done = 1u;
}

static void sc_ssd1306_handle_failure(uint32_t now_ms,
                                      sc_ssd1306_status_t status)
{
  s_tx_kind = SC_SSD1306_TX_NONE;
  s_active_page = SC_SSD1306_PAGES;
  s_active_chunk = 0u;
  s_command_sent = 0u;
  s_retry_waiting = 1u;
  s_retry_at_ms = now_ms + SC_SSD1306_RETRY_DELAY_MS;
  s_retry_count++;
  s_last_status = status;
}

static void sc_ssd1306_finish_page(void)
{
  if (s_active_generation == s_page_generation[s_active_page])
  {
    s_dirty_pages &= (uint8_t)~(1u << s_active_page);
  }

  s_active_page = SC_SSD1306_PAGES;
  s_active_chunk = 0u;
  s_command_sent = 0u;
  s_tx_kind = SC_SSD1306_TX_NONE;

  if (s_dirty_pages == 0u)
  {
    s_refresh_requested = 0u;
  }
}

static void sc_ssd1306_service_completion(uint32_t now_ms)
{
  uint32_t primask;
  uint8_t done = 0u;
  sc_ssd1306_status_t result = SC_SSD1306_OK;

  primask = sc_ssd1306_enter_critical();
  if (s_tx_done != 0u)
  {
    done = 1u;
    s_tx_done = 0u;
    result = s_tx_result;
  }
  sc_ssd1306_exit_critical(primask);

  if (done == 0u)
  {
    return;
  }

  if (result != SC_SSD1306_OK)
  {
    sc_ssd1306_handle_failure(now_ms, result);
    return;
  }

  s_last_status = SC_SSD1306_OK;
  if (s_tx_kind == SC_SSD1306_TX_PAGE_COMMAND)
  {
    s_active_chunk = 0u;
    s_command_sent = 1u;
    s_tx_kind = SC_SSD1306_TX_NONE;
  }
  else if (s_tx_kind == SC_SSD1306_TX_PAGE_DATA)
  {
    s_active_chunk++;
    s_tx_kind = SC_SSD1306_TX_NONE;
    if (s_active_chunk >= SC_SSD1306_CHUNKS_PER_PAGE)
    {
      sc_ssd1306_finish_page();
    }
  }
  else
  {
    s_tx_kind = SC_SSD1306_TX_NONE;
  }
}

static sc_ssd1306_status_t sc_ssd1306_start_tx(uint16_t len,
                                               sc_ssd1306_tx_kind_t kind)
{
  sc_i2c2_status_t status;

  s_tx_kind = kind;
  s_tx_active = 1u;
  status = sc_i2c2_master_transmit_async(
    SC_SSD1306_I2C_ADDRESS, s_tx_buffer, len,
    sc_ssd1306_tx_complete, NULL);
  if (status != SC_I2C2_OK)
  {
    s_tx_active = 0u;
    s_tx_kind = SC_SSD1306_TX_NONE;
  }
  return sc_ssd1306_map_i2c2(status);
}

static sc_ssd1306_status_t sc_ssd1306_start_page_command(void)
{
  s_tx_buffer[0] = 0x00u;
  s_tx_buffer[1] = (uint8_t)(0xB0u | s_active_page);
  s_tx_buffer[2] = 0x00u;
  s_tx_buffer[3] = 0x10u;
  return sc_ssd1306_start_tx(4u, SC_SSD1306_TX_PAGE_COMMAND);
}

static sc_ssd1306_status_t sc_ssd1306_start_page_data(void)
{
  uint16_t offset =
    (uint16_t)(s_active_chunk * SC_SSD1306_CHUNK);

  s_tx_buffer[0] = 0x40u;
  memcpy(&s_tx_buffer[1],
         &s_framebuffer[s_active_page][offset],
         SC_SSD1306_CHUNK);
  return sc_ssd1306_start_tx((uint16_t)(SC_SSD1306_CHUNK + 1u),
                             SC_SSD1306_TX_PAGE_DATA);
}

static void sc_ssd1306_start_next_transaction(uint32_t now_ms)
{
  sc_ssd1306_status_t status = SC_SSD1306_OK;

  if (s_refresh_requested == 0u)
  {
    return;
  }
  if (s_dirty_pages == 0u)
  {
    s_refresh_requested = 0u;
    return;
  }
  if (s_retry_waiting != 0u)
  {
    if (!sc_ssd1306_time_reached(now_ms, s_retry_at_ms))
    {
      return;
    }
    s_retry_waiting = 0u;
  }

  if (s_active_page >= SC_SSD1306_PAGES)
  {
    s_active_page = sc_ssd1306_next_dirty_page();
    if (s_active_page >= SC_SSD1306_PAGES)
    {
      s_refresh_requested = 0u;
      return;
    }
    s_active_generation = s_page_generation[s_active_page];
    s_active_chunk = 0u;
    s_command_sent = 0u;
    s_tx_kind = SC_SSD1306_TX_NONE;
  }

  if (s_tx_kind == SC_SSD1306_TX_NONE)
  {
    if (s_command_sent == 0u)
    {
      status = sc_ssd1306_start_page_command();
    }
    else
    {
      status = sc_ssd1306_start_page_data();
    }
  }

  if (status != SC_SSD1306_OK)
  {
    sc_ssd1306_handle_failure(now_ms, status);
  }
}

sc_ssd1306_status_t sc_ssd1306_init(void)
{
  uint8_t command_buffer[SC_SSD1306_CMD_BUF_SIZE];
  uint16_t command_len =
    (uint16_t)(sizeof(s_init_cmds) + 1u);
  sc_i2c2_status_t i2c_status;
  sc_ssd1306_status_t status;

  if (s_initialized != 0u)
  {
    return SC_SSD1306_OK;
  }

  i2c_status = sc_i2c2_init();
  if (i2c_status != SC_I2C2_OK)
  {
    s_last_status = sc_ssd1306_map_i2c2(i2c_status);
    return s_last_status;
  }

  HAL_Delay(100u);
  i2c_status = sc_i2c2_is_device_ready(
    SC_SSD1306_I2C_ADDRESS, 3u, SC_SSD1306_I2C_TIMEOUT_MS);
  if (i2c_status != SC_I2C2_OK)
  {
    s_last_status = sc_ssd1306_map_i2c2(i2c_status);
    return s_last_status;
  }

  command_buffer[0] = 0x00u;
  memcpy(&command_buffer[1], s_init_cmds, sizeof(s_init_cmds));
  i2c_status = sc_i2c2_master_transmit_blocking(
    SC_SSD1306_I2C_ADDRESS,
    command_buffer,
    command_len,
    SC_SSD1306_I2C_TIMEOUT_MS);
  status = sc_ssd1306_map_i2c2(i2c_status);
  if (status != SC_SSD1306_OK)
  {
    s_last_status = status;
    return status;
  }

  memset(s_framebuffer, 0, sizeof(s_framebuffer));
  memset(s_page_generation, 0, sizeof(s_page_generation));
  s_dirty_pages = 0xFFu;
  s_refresh_requested = 1u;
  s_retry_waiting = 0u;
  s_active_page = SC_SSD1306_PAGES;
  s_active_chunk = 0u;
  s_command_sent = 0u;
  s_active_generation = 0u;
  s_tx_active = 0u;
  s_tx_done = 0u;
  s_tx_kind = SC_SSD1306_TX_NONE;
  s_retry_count = 0u;
  s_last_status = SC_SSD1306_OK;
  s_initialized = 1u;
  return SC_SSD1306_OK;
}

sc_ssd1306_status_t sc_ssd1306_clear(void)
{
  uint8_t page;

  memset(s_framebuffer, 0, sizeof(s_framebuffer));
  for (page = 0u; page < SC_SSD1306_PAGES; page++)
  {
    s_page_generation[page]++;
  }
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
  for (i = 0u;
       (text[i] != '\0') && (i < SC_SSD1306_LINE_CHARS);
       i++)
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
  s_page_generation[row]++;
  s_dirty_pages |= (uint8_t)(1u << row);
  return SC_SSD1306_OK;
}

sc_ssd1306_status_t sc_ssd1306_refresh(void)
{
  if (s_initialized == 0u)
  {
    return SC_SSD1306_ERR_NOT_READY;
  }

  s_refresh_requested = 1u;
  if (s_dirty_pages == 0u)
  {
    s_refresh_requested = 0u;
  }
  return SC_SSD1306_OK;
}

void sc_ssd1306_service(uint32_t now_ms)
{
  if (s_initialized == 0u)
  {
    return;
  }

  sc_ssd1306_service_completion(now_ms);
  if (sc_i2c2_is_busy() != 0u)
  {
    return;
  }
  sc_ssd1306_start_next_transaction(now_ms);
}

sc_ssd1306_status_t sc_ssd1306_get_status(
  sc_ssd1306_status_info_t *status)
{
  if (status == NULL)
  {
    return SC_SSD1306_ERR_ARGUMENT;
  }

  if (s_initialized == 0u)
  {
    status->state = SC_SSD1306_STATE_UNINITIALIZED;
  }
  else if (s_retry_waiting != 0u)
  {
    status->state = SC_SSD1306_STATE_RETRY_WAIT;
  }
  else if ((s_tx_active != 0u) || (s_refresh_requested != 0u))
  {
    status->state = SC_SSD1306_STATE_REFRESHING;
  }
  else
  {
    status->state = SC_SSD1306_STATE_IDLE;
  }
  status->dirty_pages = s_dirty_pages;
  status->refresh_pending = s_refresh_requested;
  status->retry_count = s_retry_count;
  status->last_status = s_last_status;
  return SC_SSD1306_OK;
}
