/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <string.h>
#include "hal_board.h"
#include "font_digits.h"
#include "font_text.h"
#include "keypad.h"
#include "k2000_proto.h"
#include "lt7680_bus.h"
#include "lt7680_gfx.h"
#include "main_display.h"
#include "panel_transform.h"
#include "reading_split.h"
#include "scene.h"
#include "ui_model.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* SPI self-test: reset LT7680 then repeatedly write one register so that a
 * logic analyzer on PA5 (SCK) / PA7 (SDI) shows continuous SPI frames. It
 * deliberately skips panel init so the question "is LT7680 SPI reachable?" is
 * answered in isolation. Set to 1 to enable. */
#define LT7680_SPI_SELFTEST 0U

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */
static ui_model_t s_ui;
static keypad_t s_keypad;
static bool s_display_ready;
static bool s_ui_dirty;
static bool s_blink_visible = true;
static uint32_t s_blink_tick;

static void proto_on_event(const k2000_event_t *evt)
{
    char num[UI_MODEL_MAX_FIELD];
    char unit[UI_MODEL_MAX_UNIT];
    uint8_t num_len;
    uint8_t unit_len;
    uint8_t special;

    if (evt == 0) {
        return;
    }
    switch (evt->type) {
    case K2000_EVT_FIELD:
        if (reading_is_special(evt->field.value, evt->field.value_len,
                               &special)) {
            num_len = evt->field.value_len;
            if (num_len >= sizeof(num)) num_len = (uint8_t)(sizeof(num) - 1u);
            memcpy(num, evt->field.value, num_len);
            num[num_len] = '\0';
            unit_len = 0u;
            unit[0] = '\0';
        } else {
            reading_split(evt->field.value, evt->field.value_len, num, &num_len,
                          unit, &unit_len);
            special = 0u;
        }
        ui_model_apply_reading(&s_ui, num, num_len, unit, unit_len, special);
        s_ui_dirty = true;
        break;
    case K2000_EVT_STATUS:
        ui_model_apply_status(&s_ui, evt->status_tag, evt->status_value);
        s_ui_dirty = true;
        break;
    case K2000_EVT_CURSOR:
        ui_model_apply_cursor(&s_ui, evt->pos);
        s_ui_dirty = true;
        break;
    case K2000_EVT_BLINK_START:
        ui_model_apply_blink(&s_ui, true);
        s_ui_dirty = true;
        break;
    case K2000_EVT_BLINK_END:
        ui_model_apply_blink(&s_ui, false);
        s_ui_dirty = true;
        break;
    case K2000_EVT_SYMBOL:
        ui_model_apply_symbol(&s_ui, evt->ctrl);
        s_ui_dirty = true;
        break;
    case K2000_EVT_SEGMENT:
        ui_model_apply_segment(&s_ui, evt->ctrl);
        s_ui_dirty = true;
        break;
    case K2000_EVT_FLUSH:
        ui_model_apply_flush(&s_ui);
        s_ui_dirty = true;
        break;
    default:
        break;
    }
}

static void proto_on_unknown(uint8_t byte)
{
    (void)byte;
}

/* Reading scene (id 0). The renderer keeps the verified panel writes behind
 * the display-ready gate and uses the logical UI coordinate transform. */
static void reading_scene_enter(void)
{
}

static void reading_scene_exit(void)
{
}

static lt7680_status_t ui_set_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    uint16_t fb_x;
    uint16_t fb_y;

    panel_transform_ui_to_fb(x, y, &fb_x, &fb_y);
    return lt7680_gfx_set_pixel(fb_x, fb_y, color);
}

static lt7680_status_t ui_fill_rect(uint16_t x, uint16_t y, uint16_t w,
                                    uint16_t h, uint16_t color)
{
    lt7680_rect_t rect;

    panel_transform_ui_rect_to_fb(x, y, w, h, &rect.x, &rect.y,
                                  &rect.w, &rect.h);
    return lt7680_gfx_fill_rect(&rect, color);
}

/* Transparent text: only set pixels where a glyph bit is set; background
 * pixels are left to the caller's band clears (fill_rect). Never draws a
 * background colour, so glyphs cannot carry a coloured underbox. */
static lt7680_status_t ui_draw_text(uint16_t x, uint16_t y, const char *text,
                                    uint16_t fg)
{
    uint16_t cx = x;

    while (text != 0 && *text != '\0') {
        const uint8_t *bitmap = font_text_bitmap(*text);
        uint16_t row;
        uint16_t col;

        if (bitmap == 0) {
            bitmap = font_text_bitmap('?');
        }
        for (row = 0u; row < FONT_TEXT_HEIGHT; row++) {
            const uint8_t *bits = bitmap + row * FONT_TEXT_BYTES_PER_ROW;
            for (col = 0u; col < FONT_TEXT_WIDTH; col++) {
                if ((bits[col >> 3] &
                     (uint8_t)(0x80u >> (col & 7u))) != 0u) {
                    lt7680_status_t st =
                        ui_set_pixel((uint16_t)(cx + col),
                                     (uint16_t)(y + row), fg);
                    if (st != LT7680_OK) {
                        return st;
                    }
                }
            }
        }
        cx = (uint16_t)(cx + FONT_TEXT_WIDTH);
        text++;
    }
    return LT7680_OK;
}

static lt7680_status_t ui_draw_digits(uint16_t x, uint16_t y, const char *text,
                                      uint16_t fg)
{
    uint16_t cx = x;

    while (text != 0 && *text != '\0') {
        const uint8_t *bitmap = font_digit_bitmap(*text);
        uint16_t row;
        uint16_t col;

        if (bitmap == 0) {
            return LT7680_ERR_PARAM;
        }
        for (row = 0u; row < FONT_DIGIT_HEIGHT; row++) {
            const uint8_t *bits = bitmap + row * FONT_DIGIT_BYTES_PER_ROW;
            for (col = 0u; col < FONT_DIGIT_WIDTH; col++) {
                if ((bits[col >> 3] &
                     (uint8_t)(0x80u >> (col & 7u))) != 0u) {
                    lt7680_status_t st =
                        ui_set_pixel((uint16_t)(cx + col),
                                     (uint16_t)(y + row), fg);
                    if (st != LT7680_OK) {
                        return st;
                    }
                }
            }
        }
        cx = (uint16_t)(cx + FONT_DIGIT_WIDTH);
        text++;
    }
    return LT7680_OK;
}

static lt7680_status_t ui_draw_separators(void)
{
    /* Static decoration (Q5-a): one full-width 1px line under the merged
     * top band. Redrawn with every frame because the per-frame band clears
     * would otherwise cover it; visually it never changes. */
    return ui_fill_rect(0u, MAIN_DISPLAY_SEP_Y_TOP_BAND,
                        MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_SEP_H,
                        MAIN_DISPLAY_SEP_COLOR);
}

static void reading_scene_render(void)
{
    main_display_frame_t frame;
    uint8_t i;
    uint16_t sx;

    if (!s_display_ready) {
        return;
    }
    /* Static decorations are part of the frame: drawn once the display is
     * enabled, independent of host-data dirtiness, so the base UI is never
     * blank while waiting for the first message. */
    (void)ui_draw_separators();
    if (!s_ui_dirty) {
        return;
    }
    s_ui_dirty = false;
    main_display_format(&s_ui, &frame);

    (void)ui_fill_rect(0u, MAIN_DISPLAY_TOP_BAND_Y,
                       MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_TOP_BAND_H,
                       0x0000u);
    (void)ui_fill_rect(0u, MAIN_DISPLAY_READING_Y,
                       MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_READING_H,
                       0x0000u);
    (void)ui_fill_rect(0u, MAIN_DISPLAY_CURSOR_Y,
                       MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_CURSOR_H,
                       0x0000u);
    /* The band clears above cover the separator; redraw it before the
     * dynamic content so the frame is always complete. */
    (void)ui_draw_separators();
    /* Top band: unit/range left (or "Range ?" while empty), status right. */
    if (frame.unit_placeholder) {
        (void)ui_draw_text(frame.unit_x, frame.unit_y,
                           MAIN_DISPLAY_RANGE_PLACEHOLDER,
                           MAIN_DISPLAY_PLACEHOLDER_COLOR);
    } else if (frame.unit_len > 0u) {
        (void)ui_draw_text(frame.unit_x, frame.unit_y, frame.unit,
                           0xFFFFu);
    }
    sx = frame.status_x;
    for (i = 0u; i < frame.status_count; i++) {
        (void)ui_draw_text(sx, frame.status_y, frame.status_text[i],
                           0xFFFFu);
        sx = (uint16_t)(sx + (uint8_t)strlen(frame.status_text[i]) *
                                  FONT_TEXT_WIDTH +
                        MAIN_DISPLAY_STATUS_LABEL_GAP);
    }
    if (frame.no_data) {
        /* No-data state: seven '?' slots, one per right-aligned reading slot,
         * vertically centred in the reading band (placeholder grey). */
        for (i = 0u; i < MAIN_DISPLAY_NO_DATA_SLOTS; i++) {
            (void)ui_draw_text((uint16_t)(frame.no_data_x +
                                          (uint16_t)i * FONT_DIGIT_WIDTH +
                                          (FONT_DIGIT_WIDTH -
                                           FONT_TEXT_WIDTH) / 2u),
                               frame.no_data_y, "?",
                               MAIN_DISPLAY_PLACEHOLDER_COLOR);
        }
    } else if (frame.special != 0u) {
        (void)ui_draw_text(frame.start_x, frame.reading_y, frame.value,
                           frame.value_color);
    } else {
        (void)ui_draw_digits(frame.start_x, frame.reading_y, frame.value,
                             frame.value_color);
    }
    if (frame.cursor_visible && s_blink_visible) {
        (void)ui_fill_rect(frame.cursor_x, frame.cursor_y, FONT_DIGIT_WIDTH,
                           MAIN_DISPLAY_CURSOR_H, frame.value_color);
    }
    /* Footer spec line: integration-rate dependent bandwidth/Read rate. */
    if (frame.footer_spec_len > 0u) {
        (void)ui_draw_text(frame.footer_spec_x, frame.footer_spec_y,
                           frame.footer_spec, 0xFFFFu);
    }
}

static const scene_t s_reading_scene = {
    reading_scene_enter,
    reading_scene_exit,
    reading_scene_render,
};

static void update_blink(void)
{
    uint32_t now = HAL_GetTick();

    if (!s_ui.blink) {
        s_blink_visible = true;
        s_blink_tick = now;
        return;
    }
    if ((now - s_blink_tick) >= 250u) {
        s_blink_tick = now;
        s_blink_visible = !s_blink_visible;
        s_ui_dirty = true;
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  ui_model_init(&s_ui);
  keypad_init(&s_keypad);
  scene_mgr_init();
  scene_mgr_register(0, &s_reading_scene);
    scene_mgr_enter(0);
    s_ui_dirty = true;

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
/* USER CODE BEGIN 2 */
#if LT7680_SPI_SELFTEST
  hal_board_init();
  hal_uart_send_text("\r\nSPI SELFTEST\r\n");
  (void)lt7680_reset();
  for (;;) {
    /* Write reg 0x01 = 0x08, then read it back, then read status. Returning
     * data on PA6 (MISO) proves LT7680 is alive and receiving. */
    uint8_t rd = 0u;
    (void)lt7680_write_reg(0x01u, 0x08u);
    (void)lt7680_read_reg(0x01u, &rd);
    (void)lt7680_wait_ready(10u);
    (void)lt7680_delay_ms(1u);
  }
#else
  {
#if PANEL_LANDSCAPE
    const lt7680_panel_t panel = {960u, 320u, 16u};
#else
    const lt7680_panel_t panel = {320u, 960u, 16u};
#endif
    const k2000_proto_cb_t proto_cb = {proto_on_event, proto_on_unknown};
    lt7680_status_t st;
    uint8_t status = 0u;

    hal_board_init();
    k2000_proto_init(&proto_cb);
    panel_transform_init(MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_UI_HEIGHT,
                         panel.width, panel.height);
    hal_uart_send_text("\r\nK2000 TFT build12 clean-demo\r\n");
    hal_uart_send_text("\r\nLT7680 SELF-TEST\r\n");

    st = lt7680_reset();
    if (st != LT7680_OK) {
      hal_uart_send_text("FAIL reset=");
      hal_uart_send_hex8((uint8_t)st);
      hal_uart_send_text("\r\n");
    } else {
      st = lt7680_read_status(&status);
      if (st != LT7680_OK) {
        hal_uart_send_text("FAIL status-read=");
        hal_uart_send_hex8((uint8_t)st);
        hal_uart_send_text("\r\n");
      } else {
        hal_uart_send_text("STATUS=0x");
        hal_uart_send_hex8(status);
        hal_uart_send_text("\r\n");
        st = lt7680_wait_ready(1000u);
        if (st != LT7680_OK) {
          hal_uart_send_text("FAIL ready=");
          hal_uart_send_hex8((uint8_t)st);
          hal_uart_send_text("\r\n");
        } else {
          /* Blank the display right after reset so the LT7680's default
           * register state (colour-bar test pattern / display on) never
           * flashes during the ~200ms panel init or gfx init. 0x08 = init
           * display ctrl value (bit3 scan dir set, bits7/6/5/4/0-2 clear). */
          (void)lt7680_write_reg(0x12u, 0x08u);
          hal_panel_init();
          st = lt7680_gfx_init(&panel);
          if (st != LT7680_OK) {
            hal_uart_send_text("FAIL init=");
            hal_uart_send_hex8((uint8_t)st);
            hal_uart_send_text("\r\n");
          } else {
            /* Keep the display blank while SDRAM is cleared. Without this
             * clear, REG[12h]=0x48 exposes stale/uninitialized canvas pixels
             * as sparse RGB corruption. */
            st = lt7680_gfx_clear(0x0000u);
            if (st != LT7680_OK) {
              hal_uart_send_text("FAIL clear=");
              hal_uart_send_hex8((uint8_t)st);
              hal_uart_send_text("\r\n");
            } else {
              /* Enable the normal canvas output without the internal test
               * pattern. The post-reset blank (0x08) remains active until
               * the framebuffer contains a known black image. */
              (void)lt7680_write_reg(0x12u, 0x48u);
              hal_uart_send_text("PASS display enabled, waiting for reading\r\n");
              s_display_ready = true;
            }
          }
        }
      }
    }
  }
#endif /* LT7680_SPI_SELFTEST */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
  {
    int ch = hal_uart_receive_byte();
    if (ch >= 0) {
      k2000_proto_feed((uint8_t)ch);
    }

    /* Scan the key matrix, debounce, and passthrough press/release codes to
     * the host. Local-key interpretation (DISPLAY/TREND scene switching) is
     * deferred to the trend milestone (ADR-0002). */
    {
      int code = keypad_scan(&s_keypad, hal_keypad_read_code(),
                             HAL_GetTick());
      if (code != 0) {
        uint8_t b = (uint8_t)code;
        hal_uart_send(&b, 1);
      }
    }

    update_blink();
    scene_mgr_render();
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
