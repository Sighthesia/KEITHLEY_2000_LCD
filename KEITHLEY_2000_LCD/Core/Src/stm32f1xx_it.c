/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f1xx_it.c
  * @brief   Interrupt Service Routines.
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
#include "stm32f1xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "hal_board.h"

void fault_report_c(const unsigned long *stack, unsigned long exc_lr);
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M3 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */
  __asm volatile (
      "tst lr, #4        \n"
      "ite eq            \n"
      "mrseq r0, msp     \n"
      "mrsne r0, psp     \n"
      "mov r1, lr        \n"
      "b fault_report_c  \n");
  while (1) { }
  /* USER CODE END HardFault_IRQn 0 */
}
void fault_report_c(const unsigned long *stack, unsigned long exc_lr)
{
  /* Fault black box: a derailment that executes data as code faults
   * within a few instructions. The stacked frame preserves the faulting
   * PC and the CALLER's LR -- the true origin of the wild branch. */
  hal_uart_send_text("[FAULT] pc=");
  hal_uart_send_hex8((uint8_t)(stack[6] >> 24));
  hal_uart_send_hex8((uint8_t)(stack[6] >> 16));
  hal_uart_send_hex8((uint8_t)(stack[6] >> 8));
  hal_uart_send_hex8((uint8_t)stack[6]);
  hal_uart_send_text(" lr=");
  hal_uart_send_hex8((uint8_t)(stack[5] >> 24));
  hal_uart_send_hex8((uint8_t)(stack[5] >> 16));
  hal_uart_send_hex8((uint8_t)(stack[5] >> 8));
  hal_uart_send_hex8((uint8_t)stack[5]);
  hal_uart_send_text(" exc=");
  hal_uart_send_hex8((uint8_t)(exc_lr >> 24));
  hal_uart_send_hex8((uint8_t)(exc_lr >> 16));
  hal_uart_send_hex8((uint8_t)(exc_lr >> 8));
  hal_uart_send_hex8((uint8_t)exc_lr);
  hal_uart_send_text("\r\n");
  while (1) { }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */
  __asm volatile (
      "tst lr, #4        \n"
      "ite eq            \n"
      "mrseq r0, msp     \n"
      "mrsne r0, psp     \n"
      "mov r1, lr        \n"
      "b fault_report_c  \n");
  while (1) { }
  /* USER CODE END MemoryManagement_IRQn 0 */
}

/**
  * @brief This function handles Prefetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */
  __asm volatile (
      "tst lr, #4        \n"
      "ite eq            \n"
      "mrseq r0, msp     \n"
      "mrsne r0, psp     \n"
      "mov r1, lr        \n"
      "b fault_report_c  \n");
  while (1) { }
  /* USER CODE END BusFault_IRQn 0 */
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */
  __asm volatile (
      "tst lr, #4        \n"
      "ite eq            \n"
      "mrseq r0, msp     \n"
      "mrsne r0, psp     \n"
      "mov r1, lr        \n"
      "b fault_report_c  \n");
  while (1) { }
  /* USER CODE END UsageFault_IRQn 0 */
}

/**
  * @brief This function handles System service call via SWI instruction.
  */
void SVC_Handler(void)
{
  /* USER CODE BEGIN SVCall_IRQn 0 */

  /* USER CODE END SVCall_IRQn 0 */
  /* USER CODE BEGIN SVCall_IRQn 1 */

  /* USER CODE END SVCall_IRQn 1 */
}

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
  * @brief This function handles Pendable request for system service.
  */
void PendSV_Handler(void)
{
  /* USER CODE BEGIN PendSV_IRQn 0 */

  /* USER CODE END PendSV_IRQn 0 */
  /* USER CODE BEGIN PendSV_IRQn 1 */

  /* USER CODE END PendSV_IRQn 1 */
}

/**
  * @brief This function handles System tick timer.
  */
static void __attribute__((used, noinline)) systick_handler_c(
    const uint32_t *stack)
{
  extern volatile uint32_t s_irq_pc;
  extern volatile uint32_t s_irq_pc_ring[16];
  extern volatile uint8_t s_irq_pc_idx;
  uint32_t interrupted_pc = stack[6];

  /* The DWT PC sample inside this ISR only observes the ISR itself. The
   * exception frame preserves the preempted instruction, including a main-loop
   * dead wait, which is the address SWD must report after a freeze. */
  s_irq_pc = interrupted_pc;
  s_irq_pc_ring[s_irq_pc_idx & 0x0Fu] = interrupted_pc;
  s_irq_pc_idx++;
  HAL_IncTick();
}

void SysTick_Handler(void) __attribute__((naked));
void SysTick_Handler(void)
{
  __asm volatile (
      "mrs r0, msp        \n"
      "b systick_handler_c \n");
}

/******************************************************************************/
/* STM32F1xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f1xx.s).                    */
/******************************************************************************/

/* USER CODE BEGIN 1 */

void USART1_IRQHandler(void)
{
  hal_uart_rx_irq();
}

/* USER CODE END 1 */
