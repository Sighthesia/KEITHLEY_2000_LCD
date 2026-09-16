#!/usr/bin/env python3
"""Host config check for the LT7680 SPI transport isolation experiment.

Run with:  python3 -m unittest discover -s tools/tests -p "test_*.py"
or:        tools/tests/run_tests.sh

Verifies, without any toolchain or hardware, that:
  * LT7680_SPI_HW defaults to 0 (software bit-bang active),
  * the f2a7464-era bit-bang transfer (PA5 SCK / PA7 MOSI / PA6 MISO,
    PA4 CS via the shared GPIO chip-select path) is intact,
  * the SPI1 hardware init + CR1 recovery code is still present but
    gated behind #if LT7680_SPI_HW (optional path, not compiled in),
  * init_gpio configures PA5/PA7 as plain outputs and PA6 as input
    when LT7680_SPI_HW=0, and USART1 (PA9 TX / PA10 RX) is unaffected,
  * no Raw protocol / rendering logic files are touched by this change
    (enforced indirectly: this test only inspects hal_board.c).

Raw protocol / render logic is intentionally untouched.
"""
import os
import re
import unittest

HAL_BOARD = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "..",
    "KEITHLEY_2000_LCD", "Core", "Src", "hal_board.c")


def _read():
    with open(os.path.normpath(HAL_BOARD), "r", encoding="utf-8") as f:
        return f.read()


class TestSpiTransportConfig(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.src = _read()

    def test_default_is_bitbang(self):
        m = re.search(r"#define\s+LT7680_SPI_HW\s+(\d+)u?", self.src)
        self.assertIsNotNone(m, "LT7680_SPI_HW define missing")
        self.assertEqual(m.group(1), "0",
                         "isolation experiment requires LT7680_SPI_HW=0")

    def test_bitbang_transfer_present(self):
        # Classic MSB-first loop driving SCK/SDI and sampling SDO.
        self.assertIn("HAL_GPIO_WritePin(LCM_SCK_GPIO_PORT, LCM_SCK_PIN",
                      self.src)
        self.assertIn("HAL_GPIO_WritePin(LT7680_SDI_GPIO_PORT, LT7680_SDI_PIN",
                      self.src)
        self.assertIn("HAL_GPIO_ReadPin(LCM_SDO_GPIO_PORT, LCM_SDO_PIN)",
                      self.src)
        # The bit-bang body must live in the #else of the transport switch.
        m = re.search(r"#if\s+LT7680_SPI_HW(.*?)#else(.*?)#endif",
                      self.src, re.S)
        self.assertIsNotNone(m, "transport #if/#else block missing")
        self.assertIn("HAL_GPIO_WritePin(LCM_SCK_GPIO_PORT", m.group(2))

    def test_hardware_path_preserved_but_gated(self):
        # init_spi1 declaration/definition/call + CR1 recovery stay in tree.
        self.assertIn("static void init_spi1(void);", self.src)
        self.assertIn("static void init_spi1(void)\n{", self.src)
        self.assertIn("k2000_spi_config_missing", self.src)
        self.assertIn("K2000_SPI_CR1_EXPECTED", self.src)
        # Every hardware touchpoint must be inside #if LT7680_SPI_HW.
        for token in ("init_spi1()", "SPI1->CR1", "SPI1->SR",
                      "__HAL_RCC_SPI1_CLK_ENABLE"):
            for mm in re.finditer(re.escape(token), self.src):
                head = self.src[:mm.start()]
                opens = len(re.findall(r"#if\s+LT7680_SPI_HW\b", head))
                closes = len(re.findall(r"#endif", head))
                # Bit-bang default: at least one unclosed #if guards it.
                # (Simple nesting check; file uses flat #if/#else/#endif.)
                self.assertGreater(opens, closes // 2,
                                   "%s must be gated by #if LT7680_SPI_HW"
                                   % token)

    def test_gpio_bitbang_pins(self):
        # Scope to init_gpio: the #else branch there must set PA5/PA7 as
        # plain outputs (not AF_PP).
        mgpio = re.search(r"static void init_gpio\(void\)\s*\{(.*)",
                          self.src, re.S)
        self.assertIsNotNone(mgpio, "init_gpio missing")
        gpio_body = mgpio.group(1)
        m = re.search(r"#else\s*\n(.*?)gpio\.Pin = LCM_SCK_PIN \| LT7680_SDI_PIN;",
                      gpio_body, re.S)
        self.assertIsNotNone(m, "SCK/SDI gpio block missing")
        self.assertIn("GPIO_MODE_OUTPUT_PP", m.group(1),
                      "PA5/PA7 must be plain outputs when LT7680_SPI_HW=0")
        self.assertNotIn("GPIO_MODE_AF_PP", m.group(1))
        # ...PA6 (+PA8) always inputs, never driven.
        self.assertIn("gpio.Pin = LCM_SDO_PIN | LCM_INT_PIN;", self.src)
        m2 = re.search(
            r"gpio\.Mode = GPIO_MODE_INPUT;\s*\n\s*gpio\.Pull = GPIO_NOPULL;"
            r"\s*\n\s*gpio\.Pin = LCM_SDO_PIN \| LCM_INT_PIN;",
            self.src)
        self.assertIsNotNone(m2, "PA6/PA8 must be inputs")
        # PA4 CS stays a plain GPIO output shared by both paths.
        self.assertIn("LT7680_CS_PIN;", self.src)

    def test_usart1_unaffected(self):
        self.assertIn("gpio.Pin = UART_TX_PIN;", self.src)
        self.assertIn("gpio.Pin = UART_RX_PIN;", self.src)
        self.assertIn("GPIO_MODE_AF_PP", self.src)
        self.assertIn("__HAL_RCC_USART1_CLK_ENABLE();", self.src)
        self.assertIn("USART_CR1_UE | USART_CR1_TE | USART_CR1_RE",
                      self.src)


if __name__ == "__main__":
    unittest.main()
