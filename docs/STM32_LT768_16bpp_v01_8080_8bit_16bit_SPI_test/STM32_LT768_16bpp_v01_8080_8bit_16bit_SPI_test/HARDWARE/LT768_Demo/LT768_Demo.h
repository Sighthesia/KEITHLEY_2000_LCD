/******************** COPYRIGHT  ********************
* File Name        : LT738_Demo.h
* Author           : Levetop Electronics
* Version          : V1.0
* Date             : 2019-01-23
* Description      : LT738的测试程序
****************************************************/

#ifndef _LT738_Demo_h
#define _LT738_Demo_h
#include "delay.h"
#include "LT738_Lib.h"
#define Picture_1_Addr	0									//图片1在FLASH的地址
#define layer1_start_addr 0									//图层1的起始地址
#define layer2_start_addr LCD_XSIZE_TFT*LCD_YSIZE_TFT*2		//图层2的起始地址
#define layer3_start_addr LCD_XSIZE_TFT*LCD_YSIZE_TFT*2*2	//图层3的起始地址

void Show(void);

#endif
