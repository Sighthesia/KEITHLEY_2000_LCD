/********************* COPYRIGHT  ***********************
* File Name       : LT7x8x.c
* Author          : Levetop Electronics
* Version         : V1.0
* Date            : 2019-1-23
* Description     : 
*********************************************************/

#include "LT768.h"

//==============================================================================
void LCD_RegisterWrite(unsigned char Cmd,unsigned char Data)
{
	LCD_CmdWrite(Cmd);
	LCD_DataWrite(Data);
}  
//---------------------//
unsigned char LCD_RegisterRead(unsigned char Cmd)
{
	unsigned char temp;
	
	LCD_CmdWrite(Cmd);
	temp=LCD_DataRead();
	return temp;
}


void TFT_16bit(void)
{
/*  00b: 24-bits output.
    01b: 18-bits output, unused pins are set as GPIO.
    10b: 16-bits output, unused pins are set as GPIO.
    11b: LVDS, all 24-bits unused output pins are set as GPIO.*/
	unsigned char temp;
	LCD_CmdWrite(0x01);
	temp = LCD_DataRead();
	temp |= cSetb4;
      temp &= cClrb3;
	LCD_DataWrite(temp);  
}


void Host_Bus_16bit(void)
{
/*  Parallel Host Data Bus Width Selection
    0: 8-bit Parallel Host Data Bus.
    1: 16-bit Parallel Host Data Bus.*/
	unsigned char temp;
	LCD_CmdWrite(0x01);
	temp = LCD_DataRead();
	temp |= cSetb0;
	LCD_DataWrite(temp);
}

void RGB_16b_16bpp(void)
{
	unsigned char temp;
	LCD_CmdWrite(0x02);
	temp = LCD_DataRead();
	temp &= cClrb7;
	temp |= cSetb6;
	LCD_DataWrite(temp);
}

void MemWrite_Left_Right_Top_Down(void)
{
	unsigned char temp;
	LCD_CmdWrite(0x02);
	temp = LCD_DataRead();
	temp &= cClrb2;
	temp &= cClrb1;
	LCD_DataWrite(temp);
}

void Graphic_Mode(void)
{
	unsigned char temp;
	LCD_CmdWrite(0x03);
	temp = LCD_DataRead();
    temp &= cClrb2;
	LCD_DataWrite(temp);
}

void Memory_Select_SDRAM(void)
{
	unsigned char temp;
	LCD_CmdWrite(0x03);
	temp = LCD_DataRead();
    temp &= cClrb1;
    temp &= cClrb0;	// B
	LCD_DataWrite(temp);
}

void PCLK_Falling(void)
{
/*
PCLK Inversion
0: PDAT, DE, HSYNC etc. Drive(/ change) at PCLK falling edge.
1: PDAT, DE, HSYNC etc. Drive(/ change) at PCLK rising edge.
*/
	unsigned char temp;
	LCD_CmdWrite(0x12);
	temp = LCD_DataRead();
    temp |= cSetb7;
	LCD_DataWrite(temp);
}

void VSCAN_T_to_B(void)
{
/*	
Vertical Scan direction
0 : From Top to Bottom
1 : From bottom to Top
PIP window will be disabled when VDIR set as 1.
*/
	unsigned char temp;
	
	LCD_CmdWrite(0x12);
	temp = LCD_DataRead();
	temp &= cClrb3;
	LCD_DataWrite(temp);
}

void PDATA_Set_RGB(void)
{
/*	
parallel PDATA[23:0] Output Sequence
000b : RGB.
001b : RBG.
010b : GRB.
011b : GBR.
100b : BRG.
101b : BGR.
*/
	unsigned char temp;
	
	LCD_CmdWrite(0x12);
	temp = LCD_DataRead();
    temp &=0xf8;
	LCD_DataWrite(temp);
}

void HSYNC_Low_Active(void)
{
/*	
HSYNC Polarity
0 : Low active.
1 : High active.
*/
	unsigned char temp;
	
	LCD_CmdWrite(0x13);
	temp = LCD_DataRead();
	temp &= cClrb7;
	LCD_DataWrite(temp);
}

void VSYNC_Low_Active(void)
{
/*	
VSYNC Polarity
0 : Low active.
1 : High active.
*/
	unsigned char temp;
	
	LCD_CmdWrite(0x13);
	temp = LCD_DataRead();
	temp &= cClrb6;	
	LCD_DataWrite(temp);
}

void DE_High_Active(void)
{
/*	
DE Polarity
0 : High active.
1 : Low active.
*/
	unsigned char temp;
	
	LCD_CmdWrite(0x13);
	temp = LCD_DataRead();
	temp &= cClrb5;
	LCD_DataWrite(temp);
}


void LCD_HorizontalWidth_VerticalHeight(unsigned short WX,unsigned short HY)
{
/*
[14h] Horizontal Display Width Setting Bit[7:0]
[15h] Horizontal Display Width Fine Tuning (HDWFT) [3:0]
The register specifies the LCD panel horizontal display width in
the unit of 8 pixels resolution.
Horizontal display width(pixels) = (HDWR + 1) * 8 + HDWFTR

[1Ah] Vertical Display Height Bit[7:0]
Vertical Display Height(Line) = VDHR + 1
[1Bh] Vertical Display Height Bit[10:8]
Vertical Display Height(Line) = VDHR + 1
*/
	unsigned char temp;
	if(WX<8)
    {
	LCD_CmdWrite(0x14);
	LCD_DataWrite(0x00);
    
	LCD_CmdWrite(0x15);
	LCD_DataWrite(WX);
    
    temp=HY-1;
	LCD_CmdWrite(0x1A);
	LCD_DataWrite(temp);
	    
	temp=(HY-1)>>8;
	LCD_CmdWrite(0x1B);
	LCD_DataWrite(temp);
	}
	else
	{
    temp=(WX/8)-1;
	LCD_CmdWrite(0x14);
	LCD_DataWrite(temp);
    
    temp=WX%8;
	LCD_CmdWrite(0x15);
	LCD_DataWrite(temp);
    
    temp=HY-1;
	LCD_CmdWrite(0x1A);
	LCD_DataWrite(temp);
	    
	temp=(HY-1)>>8;
	LCD_CmdWrite(0x1B);
	LCD_DataWrite(temp);
	}
}

void LCD_Horizontal_Non_Display(unsigned short WX)
{
/*
[16h] Horizontal Non-Display Period(HNDR) Bit[4:0]
This register specifies the horizontal non-display period. Also
called back porch.
Horizontal non-display period(pixels) = (HNDR + 1) * 8 + HNDFTR

[17h] Horizontal Non-Display Period Fine Tuning(HNDFT) [3:0]
This register specifies the fine tuning for horizontal non-display
period; it is used to support the SYNC mode panel. Each level of
this modulation is 1-pixel.
Horizontal non-display period(pixels) = (HNDR + 1) * 8 + HNDFTR
*/
	unsigned char temp;
	if(WX<8)
	{
	LCD_CmdWrite(0x16);
	LCD_DataWrite(0x00);
    
	LCD_CmdWrite(0x17);
	LCD_DataWrite(WX);
	}
	else
	{
    temp=(WX/8)-1;
	LCD_CmdWrite(0x16);
	LCD_DataWrite(temp);
    
    temp=WX%8;
	LCD_CmdWrite(0x17);
	LCD_DataWrite(temp);
	}	
}
//[18h]=========================================================================
void LCD_HSYNC_Start_Position(unsigned short WX)
{
/*
[18h] HSYNC Start Position[4:0]
The starting position from the end of display area to the
beginning of HSYNC. Each level of this modulation is 8-pixel.
Also called front porch.
HSYNC Start Position(pixels) = (HSTR + 1)x8
*/
	unsigned char temp;
	if(WX<8)
	{
	LCD_CmdWrite(0x18);
	LCD_DataWrite(0x00);
	}
	else
	{
    temp=(WX/8)-1;
	LCD_CmdWrite(0x18);
	LCD_DataWrite(temp);	
	}
}
//[19h]=========================================================================
void LCD_HSYNC_Pulse_Width(unsigned short WX)
{
/*
[19h] HSYNC Pulse Width(HPW) [4:0]
The period width of HSYNC.
HSYNC Pulse Width(pixels) = (HPW + 1)x8
*/
	unsigned char temp;
	if(WX<8)
	{
	LCD_CmdWrite(0x19);
	LCD_DataWrite(0x00);
	}
	else
	{
    temp=(WX/8)-1;
	LCD_CmdWrite(0x19);
	LCD_DataWrite(temp);	
	}
}
//[1Ch][1Dh]=========================================================================
void LCD_Vertical_Non_Display(unsigned short HY)
{
/*
[1Ch] Vertical Non-Display Period Bit[7:0]
Vertical Non-Display Period(Line) = (VNDR + 1)

[1Dh] Vertical Non-Display Period Bit[9:8]
Vertical Non-Display Period(Line) = (VNDR + 1)
*/
	unsigned char temp;
    temp=HY-1;
	LCD_CmdWrite(0x1C);
	LCD_DataWrite(temp);

	LCD_CmdWrite(0x1D);
	LCD_DataWrite(temp>>8);
}
//[1Eh]=========================================================================
void LCD_VSYNC_Start_Position(unsigned short HY)
{
/*
[1Eh] VSYNC Start Position[7:0]
The starting position from the end of display area to the beginning of VSYNC.
VSYNC Start Position(Line) = (VSTR + 1)
*/
	unsigned char temp;
    temp=HY-1;
	LCD_CmdWrite(0x1E);
	LCD_DataWrite(temp);
}
//[1Fh]=========================================================================
void LCD_VSYNC_Pulse_Width(unsigned short HY)
{
/*
[1Fh] VSYNC Pulse Width[5:0]
The pulse width of VSYNC in lines.
VSYNC Pulse Width(Line) = (VPWR + 1)
*/
	unsigned char temp;
      temp=HY-1;
	LCD_CmdWrite(0x1F);
	LCD_DataWrite(temp);
}

void Memory_XY_Mode(void)	
{
/*
Canvas addressing mode
0: Block mode (X-Y coordination addressing)
1: linear mode
*/
	unsigned char temp;

	LCD_CmdWrite(0x5E);
	temp = LCD_DataRead();
	temp &= cClrb2;
	LCD_DataWrite(temp);
}

void Memory_16bpp_Mode(void)	
{
/*
Canvas image¡¦s color depth & memory R/W data width
In Block Mode:
00: 8bpp
01: 16bpp
1x: 24bpp
In Linear Mode:
X0: 8-bits memory data read/write.
X1: 16-bits memory data read/write
*/
	unsigned char temp;

	LCD_CmdWrite(0x5E);
	temp = LCD_DataRead();
	temp &= cClrb1;
	temp |= cSetb0;
	LCD_DataWrite(temp);
}


//Input data format:R5G6B5 
void Foreground_color_65k(unsigned short temp)
{
    LCD_CmdWrite(0xD2);
 	LCD_DataWrite(temp>>8);
 
    LCD_CmdWrite(0xD3);
 	LCD_DataWrite(temp>>3);
  
    LCD_CmdWrite(0xD4);
 	LCD_DataWrite(temp<<3);
}

void Square_Start_XY(unsigned short WX,unsigned short HY)
{
/*
[68h] Draw Line/Square/Triangle Start X-coordination [7:0]
[69h] Draw Line/Square/Triangle Start X-coordination [12:8]
[6Ah] Draw Line/Square/Triangle Start Y-coordination [7:0]
[6Bh] Draw Line/Square/Triangle Start Y-coordination [12:8]
*/
	LCD_CmdWrite(0x68);
	LCD_DataWrite(WX);

	LCD_CmdWrite(0x69);
	LCD_DataWrite(WX>>8);

	LCD_CmdWrite(0x6A);
	LCD_DataWrite(HY);

	LCD_CmdWrite(0x6B);
	LCD_DataWrite(HY>>8);
}

void Square_End_XY(unsigned short WX,unsigned short HY)
{
/*
[6Ch] Draw Line/Square/Triangle End X-coordination [7:0]
[6Dh] Draw Line/Square/Triangle End X-coordination [12:8]
[6Eh] Draw Line/Square/Triangle End Y-coordination [7:0]
[6Fh] Draw Line/Square/Triangle End Y-coordination [12:8]
*/
	LCD_CmdWrite(0x6C);
	LCD_DataWrite(WX);

	LCD_CmdWrite(0x6D);
	LCD_DataWrite(WX>>8);

	LCD_CmdWrite(0x6E);
	LCD_DataWrite(HY);

	LCD_CmdWrite(0x6F);
	LCD_DataWrite(HY>>8);
}

void Check_Busy_Draw(void)
{
	unsigned char temp;
	do
	{
		temp=LCD_StatusRead();
	}
	while(temp&0x08);

}
void Start_Square_Fill(void)
{
	LCD_CmdWrite(0x76);
	LCD_DataWrite(0xE0);//B1110_XXXX
	Check_Busy_Draw();
}

void Check_2D_Busy(void)
{
	do
	{
		
	}
	while( LCD_StatusRead()&0x08 );
}


void Select_PWM1(void)
{
	unsigned char temp;
	
	LCD_CmdWrite(0x85);
	temp = LCD_DataRead();
	temp |= cSetb3;
	temp &= cClrb2;
	LCD_DataWrite(temp);
}

//[84h]=========================================================================
void Set_PWM_Prescaler_1_to_256(unsigned short WX)
{
/*
PWM Prescaler Register
These 8 bits determine prescaler value for Timer 0 and 1.
Time base is ¡§Core_Freq / (Prescaler + 1)¡¨
*/
	WX=WX-1;
	LCD_CmdWrite(0x84);
	LCD_DataWrite(WX);
}

//[85h]=========================================================================
void Select_PWM1_Clock_Divided_By_1(void)
{
/*
Select MUX input for PWM Timer 1.
00 = 1; 01 = 1/2; 10 = 1/4 ; 11 = 1/8;
*/
	unsigned char temp;
	
	LCD_CmdWrite(0x85);
	temp = LCD_DataRead();
	temp &= cClrb7;
	temp &= cClrb6;
	LCD_DataWrite(temp);
}

//[8Eh][8Fh]=========================================================================
void Set_Timer1_Count_Buffer(unsigned short WX)
{
/*
Timer 0 count buffer register
Count buffer register total has 16 bits.
When timer counter equal to 0 will cause PWM timer reload Count buffer register if reload_en bit set as enable.
It may read back timer counter¡¦s real time value when PWM timer start.
*/
	LCD_CmdWrite(0x8E);
	LCD_DataWrite(WX);
	LCD_CmdWrite(0x8F);
	LCD_DataWrite(WX>>8);
}

//[8Ch][8Dh]=========================================================================
void Set_Timer1_Compare_Buffer(unsigned short WX)
{
/*
Timer 0 compare buffer register
Compare buffer register total has 16 bits.
When timer counter equal or less than compare buffer register will cause PWM out
high level if inv_on bit is off.
*/
	LCD_CmdWrite(0x8C);
	LCD_DataWrite(WX);
	LCD_CmdWrite(0x8D);
	LCD_DataWrite(WX>>8);
}

void Start_PWM1(void)
{
/*
PWM Timer 1 start/stop
Determine start/stop for Timer 1. 
0 = Stop 
1 = Start for Timer 1
*/
	unsigned char temp;
	LCD_CmdWrite(0x86);
	temp = LCD_DataRead();
	temp |= cSetb4;
	LCD_DataWrite(temp);
}

void Check_SDRAM_Ready(void)
{
/*	0: SDRAM is not ready for access
	1: SDRAM is ready for access		*/	
	unsigned char temp; 	
	do
	{
		temp=LCD_StatusRead();
	}
	while( (temp&0x04) == 0x00 );
}
void Display_ON(void)
{
/*	
Display ON/OFF
0b: Display Off.
1b: Display On.
*/
	unsigned char temp;
	
	LCD_CmdWrite(0x12);
	temp = LCD_DataRead();
	temp |= cSetb6;
	LCD_DataWrite(temp);
}