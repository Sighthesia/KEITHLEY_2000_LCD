/******************** COPYRIGHT  ********************
* File Name        : LT768_Lib.h
* Author           : Levetop Electronics
* Version          : V1.0
* Date             : 2019-01-23
* Description      : LT768的完整库
****************************************************/

#ifndef _LT768_Lib_H
#define _LT768_Lib_H
#include "LT768.h"
#include "if_port.h"

//外部晶振
#define XI_4M            0
#define XI_8M            0
#define XI_10M    	     1
#define XI_12M           0

//分辨率
#define LCD_XSIZE_TFT	1024
#define LCD_YSIZE_TFT	600
//参数
#define LCD_VBPD		20
#define LCD_VFPD		12
#define LCD_VSPW		3
#define LCD_HBPD		140
#define LCD_HFPD		160
#define LCD_HSPW		20

#define color256_black   0x00
#define color256_white   0xff
#define color256_red     0xe0
#define color256_green   0x1c
#define color256_blue    0x03
#define color256_yellow  color256_red|color256_green
#define color256_cyan    color256_green|color256_blue
#define color256_purple  color256_red|color256_blue

/* LCD color */
#define White          0xFFFF
#define Black          0x0000
#define Grey           0xF7DE
#define Blue           0x001F
#define Blue2          0x051F
#define Red            0xF800
#define Magenta        0xF81F
#define Green          0x07E0
#define Cyan           0x7FFF
#define Yellow         0xFFE0

/* 初始化LT768x */
void LT768_Init(void);

void LT768_PWM1_Init
(
 unsigned char on_off                       // 0：禁止PWM0    1：使能PWM0
,unsigned char Clock_Divided                // PWM时钟分频  取值范围 0~3(1,1/2,1/4,1/8)
,unsigned char Prescalar                    // 时钟分频     取值范围 1~256
,unsigned short Count_Buffer                // 设置PWM的输出周期
,unsigned short Compare_Buffer              // 设置占空比
);
void LT768_DrawSquare_Fill
(
 unsigned short X1                // X1位置
,unsigned short Y1                // Y1位置
,unsigned short X2                // X2位置
,unsigned short Y2                // Y2位置
,unsigned long ForegroundColor    // 背景颜色
);



#endif

