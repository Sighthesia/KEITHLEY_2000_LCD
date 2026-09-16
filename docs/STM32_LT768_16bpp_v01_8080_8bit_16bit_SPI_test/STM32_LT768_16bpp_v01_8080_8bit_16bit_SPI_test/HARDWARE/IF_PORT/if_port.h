/********************* COPYRIGHT  **********************
* File Name        : if_port.h
* Author           : Levetop Electronics
* Version          : V1.0
* Date             : 2017-9-11
* Description      : 选择不同的驱动接口
********************************************************/

#ifndef _if_port_h
#define _if_port_h
#include "sys.h"      // STM32的头文件

#define STM32_FSMC_8	0		// 使用STM32的外设FSMC驱动LT768(STM32硬件模拟8080总线(8位))
#define STM32_FSMC_16	1		// 使用STM32的外设FSMC驱动LT738(STM32硬件模拟8080总线(16位))
#define STM32_SPI		0		// 使用STM32的SPI驱动LT738

//选择8位8080总线驱动
#if STM32_FSMC_8
#define LCD_BASE0        	  ((u32)0x60000000)
#define LCD_BASE1        	  ((u32)0x60010000)
#endif

//选择16位8080总线驱动
#if STM32_FSMC_16
#define LCD_BASE0        	  ((u32)0x60000000)
#define LCD_BASE1        	  ((u32)0x60020000)
#endif

//选择SPI驱动
#if STM32_SPI
#define SS_RESET     (GPIOB->ODR &= ~(1<<12))
#define SS_SET       (GPIOB->ODR |= (1<<12))
#endif

void Parallel_Init(void);

void LCD_CmdWrite(u8 cmd);
void LCD_DataWrite(u8 data);
void LCD_DataWrite_Pixel(u16 data);
u8 LCD_StatusRead(void);
u16 LCD_DataRead(void);
	 
void Delay_us(u16 time);     // 延时函数us级
void Delay_ms(u16 time);     // 延时函数ms级



#endif
