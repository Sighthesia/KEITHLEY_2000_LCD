/********************* COPYRIGHT  **********************
* File Name        : if_port.c
* Author           : Levetop Electronics
* Version          : V1.0
* Date             : 2017-9-11
* Description      : 
********************************************************/

#include "if_port.h"

// -------------------------------------------------------- 8bits 8080 driver--------------------------------------------------------------

#if STM32_FSMC_8
	  
//FSMC Init
void FSMC_Init_8(void)
{ 					
 	GPIO_InitTypeDef GPIO_InitStructure;
	FSMC_NORSRAMInitTypeDef  FSMC_NORSRAMInitStructure;
	FSMC_NORSRAMTimingInitTypeDef  readWriteTiming; 
	FSMC_NORSRAMTimingInitTypeDef  writeTiming;
	
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_FSMC,ENABLE);		 
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB|RCC_APB2Periph_GPIOD|RCC_APB2Periph_GPIOE|RCC_APB2Periph_GPIOG,ENABLE);
	
 	//PORTD 		
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_14|GPIO_Pin_15;				 	
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; 		 
 	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
 	GPIO_Init(GPIOD, &GPIO_InitStructure); 
  	 
	//PORTE  
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7|GPIO_Pin_8|GPIO_Pin_9|GPIO_Pin_10;				 
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; 		   
 	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
 	GPIO_Init(GPIOE, &GPIO_InitStructure);    	    	 

   	//PORTG12 A0	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7|GPIO_Pin_11;	 //PORTD
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; 		 //   
 	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
 	GPIO_Init(GPIOD, &GPIO_InitStructure); 

	readWriteTiming.FSMC_AddressSetupTime = 0x01;	 //
	readWriteTiming.FSMC_AddressHoldTime = 0x00;	 //	
	readWriteTiming.FSMC_DataSetupTime = 0x01;		 //
	readWriteTiming.FSMC_BusTurnAroundDuration = 0x00;
	readWriteTiming.FSMC_CLKDivision = 0x00;
	readWriteTiming.FSMC_DataLatency = 0x00;
	readWriteTiming.FSMC_AccessMode = FSMC_AccessMode_A;	 //
    
	writeTiming.FSMC_AddressSetupTime = 0x01;	 //
	writeTiming.FSMC_AddressHoldTime = 0x00;	 //		
	writeTiming.FSMC_DataSetupTime = 0x01;		 //
	writeTiming.FSMC_BusTurnAroundDuration = 0x00;
	writeTiming.FSMC_CLKDivision = 0x00;
	writeTiming.FSMC_DataLatency = 0x00;
	writeTiming.FSMC_AccessMode = FSMC_AccessMode_A;	 //

	FSMC_NORSRAMInitStructure.FSMC_Bank = FSMC_Bank1_NORSRAM1;// 
	FSMC_NORSRAMInitStructure.FSMC_DataAddressMux = FSMC_DataAddressMux_Disable; // 
	FSMC_NORSRAMInitStructure.FSMC_MemoryType =FSMC_MemoryType_NOR;// FSMC_MemoryType_SRAM;  //SRAM   
	FSMC_NORSRAMInitStructure.FSMC_MemoryDataWidth = FSMC_MemoryDataWidth_8b;//8bit   
	FSMC_NORSRAMInitStructure.FSMC_BurstAccessMode =FSMC_BurstAccessMode_Disable;// FSMC_BurstAccessMode_Disable; 
	FSMC_NORSRAMInitStructure.FSMC_WaitSignalPolarity = FSMC_WaitSignalPolarity_Low;
	FSMC_NORSRAMInitStructure.FSMC_AsynchronousWait=FSMC_AsynchronousWait_Disable;  //--------
	FSMC_NORSRAMInitStructure.FSMC_WrapMode = FSMC_WrapMode_Disable;   
	FSMC_NORSRAMInitStructure.FSMC_WaitSignalActive = FSMC_WaitSignalActive_DuringWaitState;  
	FSMC_NORSRAMInitStructure.FSMC_WriteOperation = FSMC_WriteOperation_Enable;	//
	FSMC_NORSRAMInitStructure.FSMC_WaitSignal = FSMC_WaitSignal_Enable;   
	FSMC_NORSRAMInitStructure.FSMC_ExtendedMode = FSMC_ExtendedMode_Enable; //
	FSMC_NORSRAMInitStructure.FSMC_WriteBurst = FSMC_WriteBurst_Disable; 
	FSMC_NORSRAMInitStructure.FSMC_ReadWriteTimingStruct = &readWriteTiming; //
	FSMC_NORSRAMInitStructure.FSMC_WriteTimingStruct = &writeTiming;  //

	FSMC_NORSRAMInit(&FSMC_NORSRAMInitStructure);  //

 	FSMC_NORSRAMCmd(FSMC_Bank1_NORSRAM1, ENABLE);  // 
}

void FMSC_8_CmdWrite(u8 cmd)
{
	*(vu8*) (LCD_BASE0)= (cmd);
}

void FMSC_8_DataWrite(u8 data)
{
	*(vu8*) (LCD_BASE1)= (data);
}

void FMSC_8_DataWrite_Pixel(u16 data)
{
	*(vu8*) (LCD_BASE1)= (data);
	*(vu8*) (LCD_BASE1)= (data>>8);
}

u8 FMSC_8_StatusRead(void)
{
	u8 temp = 0;
	temp = *(vu8*)(LCD_BASE0);
	return temp;
}

u16 FMSC_8_DataRead(void)
{
	u16 temp = 0;
	temp =  *(vu8*)(LCD_BASE1);
	return temp;
}


#endif


// -------------------------------------------------------- 16bits 8080 driver-------------------------------------------------------------

#if STM32_FSMC_16

void FSMC_Init_16(void)
{ 					
 	GPIO_InitTypeDef GPIO_InitStructure;
	FSMC_NORSRAMInitTypeDef  FSMC_NORSRAMInitStructure;
	FSMC_NORSRAMTimingInitTypeDef  readWriteTiming; 
	FSMC_NORSRAMTimingInitTypeDef  writeTiming;
	
	RCC_AHBPeriphClockCmd(RCC_AHBPeriph_FSMC,ENABLE);	//
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB|RCC_APB2Periph_GPIOD|RCC_APB2Periph_GPIOE|RCC_APB2Periph_GPIOG,ENABLE);//
	
 	//PORTD
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0|GPIO_Pin_1|GPIO_Pin_4|GPIO_Pin_5|GPIO_Pin_8|GPIO_Pin_9|GPIO_Pin_10|GPIO_Pin_14|GPIO_Pin_15;				 //	//PORTD复用推挽输出  
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; 		 //  
 	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
 	GPIO_Init(GPIOD, &GPIO_InitStructure); 
  	 
	//PORTE 
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7|GPIO_Pin_8|GPIO_Pin_9|GPIO_Pin_10|GPIO_Pin_11|GPIO_Pin_12|GPIO_Pin_13|GPIO_Pin_14|GPIO_Pin_15;				 //	//PORTD复用推挽输出  
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; 		 // 
 	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
 	GPIO_Init(GPIOE, &GPIO_InitStructure);    	    	 											 

   	//PORTG12 A0	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7|GPIO_Pin_11;	 //PORTD
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP; 		 //
 	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
 	GPIO_Init(GPIOD, &GPIO_InitStructure); 

	readWriteTiming.FSMC_AddressSetupTime = 0x01;	 // ADDSET 
	readWriteTiming.FSMC_AddressHoldTime = 0x00;	 // ADDHLD 	
	readWriteTiming.FSMC_DataSetupTime = 0x01;		 // 
	readWriteTiming.FSMC_BusTurnAroundDuration = 0x00;
	readWriteTiming.FSMC_CLKDivision = 0x00;
	readWriteTiming.FSMC_DataLatency = 0x00;
	readWriteTiming.FSMC_AccessMode = FSMC_AccessMode_A;	 // A 
    
	writeTiming.FSMC_AddressSetupTime = 0x01;	 // ADDSET 
	writeTiming.FSMC_AddressHoldTime = 0x00;	 // A		
	writeTiming.FSMC_DataSetupTime = 0x01;		 // 
	writeTiming.FSMC_BusTurnAroundDuration = 0x00;
	writeTiming.FSMC_CLKDivision = 0x00;
	writeTiming.FSMC_DataLatency = 0x00;
	writeTiming.FSMC_AccessMode = FSMC_AccessMode_A;	 // 
 
	FSMC_NORSRAMInitStructure.FSMC_Bank = FSMC_Bank1_NORSRAM1;//NE4
	FSMC_NORSRAMInitStructure.FSMC_DataAddressMux = FSMC_DataAddressMux_Disable; //
	FSMC_NORSRAMInitStructure.FSMC_MemoryType =FSMC_MemoryType_NOR;// FSMC_MemoryType_SRAM;  //SRAM   
	FSMC_NORSRAMInitStructure.FSMC_MemoryDataWidth = FSMC_MemoryDataWidth_16b;//16bit   
	FSMC_NORSRAMInitStructure.FSMC_BurstAccessMode =FSMC_BurstAccessMode_Disable;// FSMC_BurstAccessMode_Disable; 
	FSMC_NORSRAMInitStructure.FSMC_WaitSignalPolarity = FSMC_WaitSignalPolarity_Low;
	FSMC_NORSRAMInitStructure.FSMC_AsynchronousWait=FSMC_AsynchronousWait_Disable;  //--------
	FSMC_NORSRAMInitStructure.FSMC_WrapMode = FSMC_WrapMode_Disable;   
	FSMC_NORSRAMInitStructure.FSMC_WaitSignalActive = FSMC_WaitSignalActive_DuringWaitState;  
	FSMC_NORSRAMInitStructure.FSMC_WriteOperation = FSMC_WriteOperation_Enable;	//
	FSMC_NORSRAMInitStructure.FSMC_WaitSignal = FSMC_WaitSignal_Enable;   
	FSMC_NORSRAMInitStructure.FSMC_ExtendedMode = FSMC_ExtendedMode_Enable; //
	FSMC_NORSRAMInitStructure.FSMC_WriteBurst = FSMC_WriteBurst_Disable; 
	FSMC_NORSRAMInitStructure.FSMC_ReadWriteTimingStruct = &readWriteTiming; //
	FSMC_NORSRAMInitStructure.FSMC_WriteTimingStruct = &writeTiming;  //

	FSMC_NORSRAMInit(&FSMC_NORSRAMInitStructure);  //

 	FSMC_NORSRAMCmd(FSMC_Bank1_NORSRAM1, ENABLE);  //BANK1 
}

void FMSC_16_CmdWrite(u8 cmd)
{
	*(vu16*) (LCD_BASE0)= (cmd);
}

void FMSC_16_DataWrite(u8 data)
{
	*(vu16*) (LCD_BASE1)= (data);
}

void FMSC_16_DataWrite_Pixel(u16 data)
{
	*(vu16*) (LCD_BASE1)= (data);
}

u8 FMSC_16_StatusRead(void)
{
	u8 temp = 0;
	temp = *(vu16*)(LCD_BASE0);
	return temp;
}

u16 FMSC_16_DataRead(void)
{
	u16 temp = 0;
	temp =  *(vu16*)(LCD_BASE1);
	return temp;
}
#endif

// -------------------------------------------------------- SPI -------------------------------------------------------------

#if STM32_SPI

void SPI2_Init(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;
	SPI_InitTypeDef  SPI_InitStructure;
 
	RCC_APB2PeriphClockCmd(	RCC_APB2Periph_GPIOB, ENABLE );//
	RCC_APB1PeriphClockCmd(	RCC_APB1Periph_SPI2,  ENABLE );//
 
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;  //PB13/14/15
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);//
 	GPIO_SetBits(GPIOB,GPIO_Pin_13|GPIO_Pin_14|GPIO_Pin_15);  //PB13/14/15
	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_12;  // PB12 
 	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;  //
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
 	GPIO_Init(GPIOB, &GPIO_InitStructure);
 	GPIO_SetBits(GPIOB,GPIO_Pin_12);

	SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;  //
	SPI_InitStructure.SPI_Mode = SPI_Mode_Master;		//
	SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;		//
	SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;		//
	SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;	//
	SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;		//
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;		//
	SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;	//
	SPI_InitStructure.SPI_CRCPolynomial = 7;	//
	SPI_Init(SPI2, &SPI_InitStructure);  //
 
	SPI_Cmd(SPI2, ENABLE); //	
}

u16 SPI2_ReadWriteByte(u16 TxData)
{		
	u16 retry=0;				 
	while((SPI2->SR&1<<1)==0)		//	
	{
		retry++;
		if(retry>=0XFFFE)return 0; 	//
	}			  
	SPI2->DR=TxData;	 	  		//Send a byte 
	retry=0;
	while((SPI2->SR&1<<0)==0) 		//Wait for one byte  
	{
		retry++;
		if(retry>=0XFFFE)return 0;	//
	}	  						    
	return SPI2->DR;          		//				    
}

void SPI_CmdWrite(u8 cmd)
{
	SS_RESET; 	      
	SPI2_ReadWriteByte(0x00);
	SPI2_ReadWriteByte(cmd);
	SS_SET;
}

void SPI_DataWrite(u8 data)
{
	SS_RESET; 	      
	SPI2_ReadWriteByte(0x80);
	SPI2_ReadWriteByte(data);
	SS_SET;
}

void SPI_DataWrite_Pixel(u16 data)
{
	SS_RESET; 	      
	SPI2_ReadWriteByte(0x80);
	SPI2_ReadWriteByte(data);
	SS_SET;
	
	SS_RESET;
	SPI2_ReadWriteByte(0x80);
	SPI2_ReadWriteByte(data>>8);
	SS_SET;
}

u8 SPI_StatusRead(void)
{
	u8 temp = 0;
	SS_RESET; 	      
	SPI2_ReadWriteByte(0x40);
	temp = SPI2_ReadWriteByte(0xff);
	SS_SET;
	return temp;
}

u16 SPI_DataRead(void)
{
	u16 temp = 0;
	SS_RESET; 	      
	SPI2_ReadWriteByte(0xc0);
	temp = SPI2_ReadWriteByte(0xff);
	SS_SET;
	return temp;
}
#endif

//-----------------------------------------------------------------------------------------------------------------------------------

void LCD_CmdWrite(u8 cmd)
{
	#if STM32_FSMC_8
	FMSC_8_CmdWrite(cmd);
	#endif
	
	#if STM32_FSMC_16
	FMSC_16_CmdWrite(cmd);
	#endif
	
	#if STM32_SPI
	SPI_CmdWrite(cmd);
	#endif
}

void LCD_DataWrite(u8 data)
{
	#if STM32_FSMC_8
	FMSC_8_DataWrite(data);
	#endif
	
	#if STM32_FSMC_16
	FMSC_16_DataWrite(data);
	#endif
	
	#if STM32_SPI
	SPI_DataWrite(data);
	#endif
}

void LCD_DataWrite_Pixel(u16 data)
{
	#if STM32_FSMC_8
	FMSC_8_DataWrite_Pixel(data);
	#endif
	
	#if STM32_FSMC_16
	FMSC_16_DataWrite_Pixel(data);
	#endif
	
	#if STM32_SPI
	SPI_DataWrite_Pixel(data);
	#endif
}


u8 LCD_StatusRead(void)
{
	u8 temp = 0;
	
	#if STM32_FSMC_8
	temp = FMSC_8_StatusRead();
	#endif
	
	#if STM32_FSMC_16
	temp = FMSC_16_StatusRead();
	#endif
	
	#if STM32_SPI
	temp = SPI_StatusRead();
	#endif
	
	return temp;
}

u16 LCD_DataRead(void)
{
	u16 temp = 0;
	
	#if STM32_FSMC_8
	temp = FMSC_8_DataRead();
	#endif
	
	#if STM32_FSMC_16
	temp = FMSC_16_DataRead();
	#endif
	
	#if STM32_SPI
	temp = SPI_DataRead();
	#endif
	
	return temp;
}
	  
	 
void Delay_us(u16 time)
{    
   u16 i=0;  
   while(time--)
   {
      i=12;        //
      while(i--) ;    
   }
}

void Delay_ms(u16 time)
{    
   u16 i=0;  
   while(time--)
   {
      i=12000;    //
      while(i--) ;    
   }
}


void Parallel_Init(void)
{	
	#if STM32_FSMC_8
		FSMC_Init_8();
	#endif
	
	#if STM32_FSMC_16
		FSMC_Init_16();
	#endif
	
	#if STM32_SPI
		SPI2_Init();
	#endif
}
















