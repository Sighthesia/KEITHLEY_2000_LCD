#include "sys.h"
#include "delay.h"
#include "usart.h"
#include "LT768_Lib.h"

int main(void)
{	
	SystemInit();						// STM32 system clock init
	delay_init();						//
	uart_init(115200);	
	Parallel_Init();					      // LT768 Interface init
  
//---------------------------------------------------------------------------------------------
//The code from LT768.c and LT768_Lib.c
//---------------------------------------------------------------------------------------------      
	LT768_Init();						// LT768 init
	LT768_PWM1_Init(1,0,200,100,100);	            // Backlight on
	Display_ON();						// Set LT7381/LT768x output RGB signal 
	

      while(1)
	{
		//Fill the screen with red, green and blue with rectangular function
		LT768_DrawSquare_Fill(0,0,LCD_XSIZE_TFT,LCD_YSIZE_TFT,Red);
		delay_ms(1000);
		
		LT768_DrawSquare_Fill(0,0,LCD_XSIZE_TFT,LCD_YSIZE_TFT,Green);
		delay_ms(1000);
		
		LT768_DrawSquare_Fill(0,0,LCD_XSIZE_TFT,LCD_YSIZE_TFT,Blue);
		delay_ms(1000);		
      }
//---------------------------------------------------------------------------------------------
}

