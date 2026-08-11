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

/* Task 5 demo: after the color-bars acceptance, clear the test pattern and
 * draw the JetBrains Mono big digits to the real panel, printing the draw
 * time over UART. Set to 0 to restore the plain color-bars behaviour. */
#define FONT_DIGIT_DEMO 0

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
#if FONT_DIGIT_DEMO
static void uart_print_u32(uint32_t value);
static void dump_reg(const char *label, uint8_t reg)
{
    uint8_t v = 0u;
    if (lt7680_read_reg(reg, &v) == LT7680_OK) {
        hal_uart_send_text(label);
        hal_uart_send_hex8(v);
    } else {
        hal_uart_send_text(label);
        hal_uart_send_text("ERR");
    }
    hal_uart_send_text("\r\n");
}
static void dump_reg16(const char *label, uint8_t reg)
{
    uint8_t lo = 0u, hi = 0u;
    if (lt7680_read_reg(reg, &lo) == LT7680_OK &&
        lt7680_read_reg((uint8_t)(reg + 1u), &hi) == LT7680_OK) {
        hal_uart_send_text(label);
        hal_uart_send_hex8(hi);
        hal_uart_send_hex8(lo);
    } else {
        hal_uart_send_text(label);
        hal_uart_send_text("ERR");
    }
    hal_uart_send_text("\r\n");
}
#endif

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

static lt7680_status_t ui_draw_text(uint16_t x, uint16_t y, const char *text,
                                    uint16_t fg, uint16_t bg)
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
                uint16_t color = (bits[col >> 3] &
                                  (uint8_t)(0x80u >> (col & 7u))) != 0u ? fg : bg;
                lt7680_status_t st = ui_set_pixel((uint16_t)(cx + col),
                                                   (uint16_t)(y + row), color);
                if (st != LT7680_OK) {
                    return st;
                }
            }
        }
        cx = (uint16_t)(cx + FONT_TEXT_WIDTH);
        text++;
    }
    return LT7680_OK;
}

static lt7680_status_t ui_draw_digits(uint16_t x, uint16_t y, const char *text,
                                      uint16_t fg, uint16_t bg)
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
                uint16_t color = (bits[col >> 3] &
                                  (uint8_t)(0x80u >> (col & 7u))) != 0u ? fg : bg;
                lt7680_status_t st = ui_set_pixel((uint16_t)(cx + col),
                                                   (uint16_t)(y + row), color);
                if (st != LT7680_OK) {
                    return st;
                }
            }
        }
        cx = (uint16_t)(cx + FONT_DIGIT_WIDTH);
        text++;
    }
    return LT7680_OK;
}

static lt7680_status_t ui_draw_placeholders(uint16_t x, uint16_t y,
                                             uint8_t count, uint16_t color)
{
    uint8_t i;

    for (i = 0u; i < count; i++) {
        lt7680_status_t st = ui_fill_rect(
            (uint16_t)(x + (uint16_t)i * FONT_DIGIT_WIDTH),
            (uint16_t)(y + FONT_DIGIT_HEIGHT - 8u), FONT_DIGIT_WIDTH, 4u,
            color);
        if (st != LT7680_OK) {
            return st;
        }
    }
    return LT7680_OK;
}

static void reading_scene_render(void)
{
    main_display_frame_t frame;
    uint8_t i;
    uint16_t sx;

    if (!s_display_ready || !s_ui_dirty) {
        return;
    }
    s_ui_dirty = false;
    main_display_format(&s_ui, &frame);

    (void)ui_fill_rect(0u, MAIN_DISPLAY_STATUS_BAR_Y,
                       MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_STATUS_BAR_H,
                       0x0000u);
    (void)ui_fill_rect(0u, MAIN_DISPLAY_UNIT_ROW_Y,
                       MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_UNIT_ROW_H,
                       0x0000u);
    (void)ui_fill_rect(0u, MAIN_DISPLAY_READING_Y,
                       MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_READING_H,
                       0x0000u);
    (void)ui_fill_rect(0u, MAIN_DISPLAY_CURSOR_Y,
                       MAIN_DISPLAY_UI_WIDTH, MAIN_DISPLAY_CURSOR_H,
                       0x0000u);
    sx = 0u;
    for (i = 0u; i < frame.status_count; i++) {
        (void)ui_draw_text(sx, frame.status_y, frame.status_text[i],
                           0xFFFFu, 0x0000u);
        sx = (uint16_t)(sx + (uint8_t)strlen(frame.status_text[i]) *
                                  FONT_TEXT_WIDTH +
                        MAIN_DISPLAY_STATUS_LABEL_GAP);
    }
    if (frame.unit_len > 0u) {
        (void)ui_draw_text(frame.unit_x, frame.unit_y, frame.unit,
                           0xFFFFu, 0x0000u);
    }
    if (frame.placeholder) {
        (void)ui_draw_placeholders(frame.start_x, frame.reading_y,
                                    frame.value_len, 0xFFFFu);
    } else if (frame.special != 0u) {
        (void)ui_draw_text(frame.start_x, frame.reading_y, frame.value,
                           frame.value_color, 0x0000u);
    } else {
        (void)ui_draw_digits(frame.start_x, frame.reading_y, frame.value,
                             frame.value_color, 0x0000u);
    }
    if (frame.cursor_visible && s_blink_visible) {
        (void)ui_fill_rect(frame.cursor_x, frame.cursor_y, FONT_DIGIT_WIDTH,
                           MAIN_DISPLAY_CURSOR_H, frame.value_color);
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
    hal_uart_send_text("\r\nK2000 TFT build11 no-boot-bars\r\n");
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
#if FONT_DIGIT_DEMO
    if (s_display_ready) {
    {
      /* Task 5 demo. Bit5 of REG[12h] enables the color-bar test pattern,
       * which overrides the SDRAM image; clear it so the big digits drawn
       * below are visible. 7 digits only fit the 960-wide landscape layout
       * (Panel_Landscape=1); on the verified 320-wide portrait we render 6. */
      uint8_t disp = 0u;
      uint32_t t0;
      uint32_t t1;
      uint16_t demo_len;
      uint16_t demo_w;
      uint16_t ux0;
      uint16_t uy0;
      static const char demo_digits[] =
#if PANEL_LANDSCAPE
          "1234567";
#else
          "123456";
#endif
      /* Clear bit5 (color-bar test pattern) only if the read succeeded, so
       * a failed read cannot write 0x12=0 and blank the display.  Then wipe
       * the frame buffer: the canvas defaults to an 8bpp block window, so
       * turning the test pattern off alone would show a single-row sliver. */
      if (lt7680_read_reg(0x12u, &disp) == LT7680_OK) {
        /* Keep the display blank while the 614400-byte framebuffer clear and
         * per-pixel draw run. Otherwise the panel visibly shows each partial
         * burst and the old/new image appears as two refreshes. */
(void)lt7680_write_reg(0x12u, (uint8_t)(disp & ~0x60u));
      }
      t1 = HAL_GetTick();
      (void)lt7680_gfx_clear(0x0000u);
      hal_uart_send_text("clear-ms=");
      uart_print_u32(HAL_GetTick() - t1);
      hal_uart_send_text("\r\n");

      /* Telemetry: confirm the windows and test-pattern state read back as
       * configured.  If MIW/CVSIMWTH/AW_* read back 0 the register write
       * path is at fault; if R12 still has bit5 set the test pattern never
       * turned off. */
      hal_uart_send_text("R12=0x");
      hal_uart_send_hex8(disp);
      hal_uart_send_text("\r\n");
      dump_reg16("MIW=", 0x24u);
      dump_reg16("CVSW=", 0x54u);
      dump_reg16("AW_W=", 0x5Au);
      dump_reg16("AW_H=", 0x5Cu);
      dump_reg("AWCOL=", 0x5Eu);
      dump_reg("P10=", 0x10u);
      dump_reg("P5E=", 0x5Eu);

      /* Read back a sample of canvas pixels after the clear via MRWDP. If
       * every sample is 0000 the GE fill reached the whole canvas and the
       * striped background must be a display-side artefact; any non-zero
       * sample pins down (x,y) where memory was NOT cleared. */
      {
        static const uint16_t sx[] = {0u, 159u, 319u};
        static const uint16_t sy[] = {0u, 200u, 400u, 600u, 800u, 959u};
        uint8_t i;
        for (i = 0u; i < 3u; i++) {
          uint8_t j;
          for (j = 0u; j < 6u; j++) {
            uint16_t px = 0u;
            if (lt7680_gfx_peek_pixel(sx[i], sy[j], &px) == LT7680_OK) {
              hal_uart_send_text("PX(");
              uart_print_u32(sx[i]);
              hal_uart_send_text(",");
              uart_print_u32(sy[j]);
              hal_uart_send_text(")=");
              hal_uart_send_hex8((uint8_t)(px >> 8));
              hal_uart_send_hex8((uint8_t)(px & 0xFFu));
              hal_uart_send_text("\r\n");
            }
          }
        }
      }

demo_len = (uint16_t)(sizeof(demo_digits) - 1u);
      demo_w = (uint16_t)(demo_len * FONT_DIGIT_WIDTH);
      /* The RGB panel transposes the framebuffer (fb row -> screen column, no
       * reversal). Render the landscape 960x320 UI directly into the 320x960
       * framebuffer as its transpose: fb_x = uy, fb_y = ux. Centring in the
       * UI space then lands the glyph block centred on the physical panel. */
      ux0 = (uint16_t)((panel.height - demo_w) / 2u);
      uy0 = (uint16_t)((panel.width - FONT_DIGIT_HEIGHT) / 2u);
      t0 = HAL_GetTick();
      {
        const char *p = demo_digits;
        while (*p != '\0') {
          const uint8_t *bmp = font_digit_bitmap(*p);
          if (bmp != 0) {
            uint16_t dy;
            for (dy = 0u; dy < FONT_DIGIT_HEIGHT; dy++) {
              const uint8_t *row =
                  bmp + (uint16_t)(dy * FONT_DIGIT_BYTES_PER_ROW);
              uint16_t dx;
              for (dx = 0u; dx < FONT_DIGIT_WIDTH; dx++) {
                if ((row[dx >> 3] & (0x80u >> (dx & 7u))) != 0u) {
                    uint16_t ux = (uint16_t)(ux0 + dx);
                    uint16_t uy = (uint16_t)(uy0 + dy);
                    uint16_t fb_x;
                    uint16_t fb_y;
                    panel_transform_ui_to_fb(ux, uy, &fb_x, &fb_y);
                    if (fb_x < panel.width && fb_y < panel.height) {
                      (void)lt7680_gfx_set_pixel(fb_x, fb_y, 0xFFFFu);
                    }
                }
              }
            }
          }
          ux0 += FONT_DIGIT_WIDTH;
          p++;
        }
      }
      (void)lt7680_write_reg(0x12u, 0x48u);
      (void)lt7680_read_reg(0x12u, &disp);
      hal_uart_send_text("R12-final=0x");
      hal_uart_send_hex8(disp);
      hal_uart_send_text("\r\n");
      hal_uart_send_text("digits-ms=");
      uart_print_u32(HAL_GetTick() - t0);
      hal_uart_send_text("\r\n");
    }
    }
#endif /* FONT_DIGIT_DEMO */
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

#if FONT_DIGIT_DEMO
static void uart_print_u32(uint32_t value)
{
    char buf[10];
    uint8_t i = (uint8_t)sizeof(buf);
    do {
        buf[--i] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0u);
    hal_uart_send((const uint8_t *)&buf[i], (uint16_t)(sizeof(buf) - i));
}
#endif

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
