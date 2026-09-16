#include "LT768_Lib.h"
#include "usart.h"
unsigned char CCLK;    // LT768 Kernel clock frequency    
unsigned char MCLK;    // SDRAM clock frequency 
unsigned char SCLK;    // LCD Scan clock frequency

//---------------------------------------------------------------------------------------------------------------------------------

//Rest LT768
void LT768_HW_Reset(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	RCC_APB2PeriphClockCmd(	RCC_APB2Periph_GPIOE, ENABLE );
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOE, &GPIO_InitStructure);

	GPIO_ResetBits(GPIOE, GPIO_Pin_1);
	Delay_ms(100);
	GPIO_SetBits(GPIOE, GPIO_Pin_1 );
	Delay_ms(100);
}

//Check LT768 system
void System_Check_Temp(void)
{
	unsigned char i=0;
	unsigned char temp=0;
	unsigned char system_ok=0;
	do
	{
		if((LCD_StatusRead()&0x02)==0x00)    
		{
			Delay_ms(1);                  //If MCU speed is too fast, use if necessary
			LCD_CmdWrite(0x01);
			Delay_ms(1);                  //If MCU speed is too fast, use if necessary
			temp =LCD_DataRead();
			if((temp & 0x80)==0x80)       //Check whether the CCR register PLL is ready
			{
				system_ok=1;
				i=0;
			}
			else
			{
				Delay_ms(1); //If MCU speed is too fast, use if necessary
				LCD_CmdWrite(0x01);
				Delay_ms(1); //If MCU speed is too fast, use if necessary
				LCD_DataWrite(0x80);
			}
		}
		else
		{
			system_ok=0;
			i++;
		}
		if(system_ok==0 && i==5)
		{
			LT768_HW_Reset(); //note1
			i=0;
		}
	}while(system_ok==0);
}

void LT768_PLL_Initial(void) 
{    
	unsigned int temp = 0;
	unsigned int temp1;
	
	unsigned short lpllOD_sclk, lpllOD_cclk, lpllOD_mclk;
	unsigned short lpllR_sclk, lpllR_cclk, lpllR_mclk;
	unsigned short lpllN_sclk, lpllN_cclk, lpllN_mclk;
	
	temp = (LCD_HBPD + LCD_HFPD + LCD_HSPW + LCD_XSIZE_TFT) * (LCD_VBPD + LCD_VFPD + LCD_VSPW+LCD_YSIZE_TFT) * 60;   
	
	temp1 = (temp%1000000)/100000;
	if(temp1>5)
		 temp = temp / 1000000 + 1;
	else temp = temp / 1000000;
	
	SCLK = temp;	
	CCLK = 80;
	MCLK = 80;
	if(SCLK > 65)		SCLK = 65;

#if XI_4M 	
	
	lpllOD_sclk = 3;
	lpllOD_cclk = 2;
	lpllOD_mclk = 2;
	lpllR_sclk  = 2;
	lpllR_cclk  = 2;
	lpllR_mclk  = 2;
	lpllN_mclk  = MCLK;      
	lpllN_cclk  = CCLK;    
	lpllN_sclk  = 2*SCLK; 
	
#endif

#if XI_8M 	
	
	lpllOD_sclk = 3;
	lpllOD_cclk = 2;
	lpllOD_mclk = 2;
	lpllR_sclk  = 2;
	lpllR_cclk  = 4;
	lpllR_mclk  = 4;
	lpllN_mclk  = MCLK;      
	lpllN_cclk  = CCLK;    
	lpllN_sclk  = SCLK; 
	
#endif

#if XI_10M 	
	
	lpllOD_sclk = 3;
	lpllOD_cclk = 2;
	lpllOD_mclk = 2;
	lpllR_sclk  = 5;
	lpllR_cclk  = 5;
	lpllR_mclk  = 5;
	lpllN_mclk  = MCLK;      
	lpllN_cclk  = CCLK;    
	lpllN_sclk  = 2*SCLK; 
	
#endif

#if XI_12M 	
	
	lpllOD_sclk = 3;
	lpllOD_cclk = 2;
	lpllOD_mclk = 2;
	lpllR_sclk  = 3;
	lpllR_cclk  = 6;
	lpllR_mclk  = 6;
	lpllN_mclk  = MCLK;      
	lpllN_cclk  = CCLK;    
	lpllN_sclk  = SCLK; 
	
#endif
	
	LCD_CmdWrite(0x05);
	LCD_DataWrite((lpllOD_sclk<<6) | (lpllR_sclk<<1) | ((lpllN_sclk>>8)&0x1));
	LCD_CmdWrite(0x07);
	LCD_DataWrite((lpllOD_mclk<<6) | (lpllR_mclk<<1) | ((lpllN_mclk>>8)&0x1));
	LCD_CmdWrite(0x09);
	LCD_DataWrite((lpllOD_cclk<<6) | (lpllR_cclk<<1) | ((lpllN_cclk>>8)&0x1));

	LCD_CmdWrite(0x06);
	LCD_DataWrite(lpllN_sclk);
	LCD_CmdWrite(0x08);
	LCD_DataWrite(lpllN_mclk);
	LCD_CmdWrite(0x0a);
	LCD_DataWrite(lpllN_cclk);
      
	LCD_CmdWrite(0x00);
	Delay_us(1);
	LCD_DataWrite(0x80);

	Delay_ms(1);	//
}


void LT768_SDRAM_initail(unsigned char mclk)
{
	unsigned short sdram_itv;

	LCD_RegisterWrite(0xe0,0x20);      
	LCD_RegisterWrite(0xe1,0x03);	//CAS:2=0x02¡ACAS:3=0x03
	sdram_itv = (64000000 / 8192) / (1000/mclk) ;
	sdram_itv-=2;

	LCD_RegisterWrite(0xe2,sdram_itv);
	LCD_RegisterWrite(0xe3,sdram_itv >>8);
	LCD_RegisterWrite(0xe4,0x01);
	Check_SDRAM_Ready();
	Delay_ms(1);
}


void Set_LCD_Panel(void)
{
	//**[01h]**//   
	TFT_16bit();
	
	#if STM32_FSMC_8
	Host_Bus_8bit();    //Host bus 8bit
	#else
	Host_Bus_16bit();	//Host bus 16bit
	#endif
      
	//**[02h]**//
	RGB_16b_16bpp();
	MemWrite_Left_Right_Top_Down();	
	//MemWrite_Down_Top_Left_Right();
      
	//**[03h]**//
	Graphic_Mode();
	Memory_Select_SDRAM();
     
	PCLK_Falling();	       	//REG[12h]:Falling edge 
	//PCLK_Rising();
	
	VSCAN_T_to_B();	        //REG[12h]:From top to bottom
	//VSCAN_B_to_T();				//From bottom to top
	
	PDATA_Set_RGB();        //REG[12h]:Select RGB output
	//PDATA_Set_RBG();
	//PDATA_Set_GRB();
	//PDATA_Set_GBR();
	//PDATA_Set_BRG();
	//PDATA_Set_BGR();

	HSYNC_Low_Active();     //REG[13h]:		  
	//HSYNC_High_Active();
	
	VSYNC_Low_Active();     //REG[13h]:			
	//VSYNC_High_Active();
	
	DE_High_Active();       //REG[13h]:	
	//DE_Low_Active();
 
	LCD_HorizontalWidth_VerticalHeight(LCD_XSIZE_TFT ,LCD_YSIZE_TFT);	
	LCD_Horizontal_Non_Display(LCD_HBPD);	                            
	LCD_HSYNC_Start_Position(LCD_HFPD);	                              
	LCD_HSYNC_Pulse_Width(LCD_HSPW);		                            	
	LCD_Vertical_Non_Display(LCD_VBPD);	                                
	LCD_VSYNC_Start_Position(LCD_VFPD);	                              
	LCD_VSYNC_Pulse_Width(LCD_VSPW);		                            	

	Memory_XY_Mode();	//Block mode (X-Y coordination addressing)
	Memory_16bpp_Mode();	
}

void LT768_initial(void)
{
	LT768_PLL_Initial();
	printf("pllok\r\n");
	LT768_SDRAM_initail(MCLK);
	printf("sramok\r\n");
	Set_LCD_Panel();
	printf("lcdok\r\n");
}


void LT768_Init(void)
{
	Delay_ms(100);                    //Delay for LT768 power on
	LT768_HW_Reset();                 //LT768 rest
	System_Check_Temp();	            //Check LT768 system
	Delay_ms(100);
	while(LCD_StatusRead()&0x02);	    //Initial_Display_test	and  set SW2 pin2 = 1
	LT768_initial();
}

void LT768_DrawSquare_Fill
(
 unsigned short X1                // X1's  position
,unsigned short Y1                // Y1's  position
,unsigned short X2                // X2's  position
,unsigned short Y2                // Y2's  position
,unsigned long ForegroundColor    // background color
)
{
	Foreground_color_65k(ForegroundColor);
	Square_Start_XY(X1,Y1);
	Square_End_XY(X2,Y2);
	Start_Square_Fill();
	Check_2D_Busy();
}


void LT768_PWM1_Init
(
 unsigned char on_off                       // 0£ºDisable PWM0    1£ºEnable PWM0
,unsigned char Clock_Divided                // Frequency division range of PWM clock 0~3(1,1/2,1/4,1/8)
,unsigned char Prescalar                    // Clock frequency division range 1~256
,unsigned short Count_Buffer                // Set the output period of PWM
,unsigned short Compare_Buffer              // Set duty cycle
)
{
	Select_PWM1();
	Set_PWM_Prescaler_1_to_256(Prescalar);
 
	if(Clock_Divided ==0)	Select_PWM1_Clock_Divided_By_1();
//	if(Clock_Divided ==1)	Select_PWM1_Clock_Divided_By_2();
//	if(Clock_Divided ==2)	Select_PWM1_Clock_Divided_By_4();
//	if(Clock_Divided ==3)	Select_PWM1_Clock_Divided_By_8();

	Set_Timer1_Count_Buffer(Count_Buffer); 
	Set_Timer1_Compare_Buffer(Compare_Buffer); 

	if (on_off == 1)	Start_PWM1(); 
//	if (on_off == 0)	Stop_PWM1();
}




