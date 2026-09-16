#include "LT738_Demo.h"
#include "LT738_Lib.h"

/*
MCU外接flash内容：
{0x00000000, 0x0012C000}, //Picture-16bpp.bin
{0x0012C000, 0x00041400}, //Font_16.bin
{0x0016D400, 0x00092D00}, //Font_24.bin
{0x00200100, 0x00105000}, //Font_32.bin
{0x00305100, 0x0024B400}, //Font_48.bin
*/

void Show(void)
{
	Select_Main_Window_16bpp();						//选择显示颜色深度
	Main_Image_Start_Address(layer1_start_addr);	//设定主窗口起始地址（SDRAM中显示到屏幕的内容的起始地址）
	Main_Image_Width(LCD_XSIZE_TFT);				//设定主窗口的宽度
	Main_Window_Start_XY(0,0);						//设定主窗口显示的起始坐标
	Canvas_Image_Start_address(layer1_start_addr);	//设定写入到SDRAM的起始地址
	Canvas_image_width(LCD_XSIZE_TFT);				//设定写入SDRAM的宽度（必须为4的倍数）
    Active_Window_XY(0,0);							//设定工作窗口的坐标
	Active_Window_WH(LCD_XSIZE_TFT,LCD_YSIZE_TFT);	//设定工作窗口的宽度（必须为4的倍数）和高度
	
	while(1)
	{
		//用矩形函数，全屏填充红、绿、蓝三色
		LT738_DrawSquare_Fill(0,0,LCD_XSIZE_TFT,LCD_YSIZE_TFT,Red);
		delay_ms(1000);
		
		LT738_DrawSquare_Fill(50,50,LCD_XSIZE_TFT-50,LCD_YSIZE_TFT-50,Green);
		delay_ms(1000);
		
		LT738_DrawSquare_Fill(100,100,LCD_XSIZE_TFT-100,LCD_YSIZE_TFT-100,Blue);
		delay_ms(1000);
		
		//MCU读取flash，起始地址为0的一张屏幕分辨率大小的图片
		Memory_Write_Flash(0,0,LCD_XSIZE_TFT,LCD_YSIZE_TFT,Picture_1_Addr);
		
		delay_ms(1000);
		delay_ms(1000);
		
		//MCU读取flash，显示字库
		LT738_DrawSquare_Fill(0,0,LCD_XSIZE_TFT,LCD_YSIZE_TFT,White);
		LT738_Select_Outside_Font_Init(1,0,0x0012C000,layer2_start_addr,layer1_start_addr,0x00041400,16,0,0,0,0);
		LT738_Print_Outside_Font_String(100,100,White,Blue,(u8*)"乐升半导体16*16字体");
		LT738_Select_Outside_Font_Init(1,0,0x0016D400,layer2_start_addr,layer1_start_addr,0x00092D00,24,0,0,0,0);
		LT738_Print_Outside_Font_String(100,166,Blue,Green,(u8*)"乐升半导体24*24字体");
		LT738_Select_Outside_Font_Init(1,0,0x00200100,layer2_start_addr,layer1_start_addr,0x00105000,32,0,0,0,0);
		LT738_Print_Outside_Font_String(100,240,Green,Magenta,(u8*)"乐升半导体32*32字体");
		LT738_Print_OutsideFont_Giant(1,0,0x00305100,layer3_start_addr,layer1_start_addr,1024,48,1,100,322,color256_yellow,color256_blue,1,1,(u8*)"乐升半导体４８＊４８字体");
		
		delay_ms(1000);
		delay_ms(1000);
	}
}
