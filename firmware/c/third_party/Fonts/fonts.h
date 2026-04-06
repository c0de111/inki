/**
  ******************************************************************************
  * @file    fonts.h
  * @author  MCD Application Team
  * @version V1.0.0
  * @date    18-February-2014
  * @brief   Header for fonts.c file
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; COPYRIGHT(c) 2014 STMicroelectronics</center></h2>
  *
  * Redistribution and use in source and binary forms, with or without modification,
  * are permitted provided that the following conditions are met:
  *   1. Redistributions of source code must retain the above copyright notice,
  *      this list of conditions and the following disclaimer.
  *   2. Redistributions in binary form must reproduce the above copyright notice,
  *      this list of conditions and the following disclaimer in the documentation
  *      and/or other materials provided with the distribution.
  *   3. Neither the name of STMicroelectronics nor the names of its contributors
  *      may be used to endorse or promote products derived from this software
  *      without specific prior written permission.
  *
  * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  *
  ******************************************************************************
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __FONTS_H
#define __FONTS_H

/*×îŽó×ÖÌåÎ¢ÈíÑÅºÚ24 (32x41) */
#define MAX_HEIGHT_FONT         120
#define MAX_WIDTH_FONT          90
//#define MAX_HEIGHT_FONT         410
//#define MAX_WIDTH_FONT          320
#define OFFSET_BITMAP           

#ifdef __cplusplus
 extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>

//ASCII
typedef struct _tFont
{    
  const uint8_t *table;
  uint16_t Width;
  uint16_t Height;
} sFONT;


//GB2312
typedef struct                                          // ºº×Ö×ÖÄ£ÊýŸÝœá¹¹
{
  const  char index[2];                               // ºº×ÖÄÚÂëË÷Òý
  const  char matrix[MAX_HEIGHT_FONT*MAX_WIDTH_FONT/8+2];  // µãÕóÂëÊýŸÝ
}CH_CN;


typedef struct
{    
  const CH_CN *table;
  uint16_t size;
  uint16_t ASCII_Width;
  uint16_t Width;
  uint16_t Height;
  
}cFONT;

extern sFONT font_ubuntu_mono_18pt_italic; // Font Size: 34x47px

extern sFONT font_ubuntu_mono_18pt_medium; // Font Size: 27x47px

extern sFONT font_ubuntu_mono_36pt_bold; // Font Size: 54x94px
extern sFONT font_ubuntu_mono_28pt_bold; // Font Size: 42x73px
extern sFONT font_ubuntu_mono_26pt_bold; // Font Size: 39x67px
extern sFONT font_ubuntu_mono_24pt_bold; // Font Size: 36x62px
extern sFONT font_ubuntu_mono_22pt_bold; // Font Size: 34x57px 
extern sFONT font_ubuntu_mono_20pt_bold; // Font Size: 30x52px
extern sFONT font_ubuntu_mono_18pt_bold; // Font Size: 27x47px
extern sFONT font_ubuntu_mono_16pt_bold; // Font Size: 25x42px
extern sFONT font_ubuntu_mono_14pt_bold; // Font Size: 21x36px
extern sFONT font_ubuntu_mono_12pt_bold; // Font Size: 18x31px
extern sFONT font_ubuntu_mono_11pt_bold; // Font Size: 17x29px
extern sFONT font_ubuntu_mono_10pt_bold; // Font Size: 16x26px
extern sFONT font_ubuntu_mono_9pt_bold; // Font Size: 15x24px
extern sFONT font_ubuntu_mono_8pt_bold; // Font Size: 12x20px
extern sFONT font_ubuntu_mono_7pt_bold; // Font Size: 11x19px
extern sFONT font_ubuntu_mono_6pt_bold; // Font Size: 9x16px

extern sFONT font_ubuntu_mono_36pt; // Font Size: 54x94px
extern sFONT font_ubuntu_mono_28pt; // Font Size: 42x73px
extern sFONT font_ubuntu_mono_26pt; // Font Size: 39x67px
extern sFONT font_ubuntu_mono_24pt; // Font Size: 36x62px
extern sFONT font_ubuntu_mono_22pt; // Font Size: 34x57px 
extern sFONT font_ubuntu_mono_20pt; // Font Size: 30x52px
extern sFONT font_ubuntu_mono_18pt; // Font Size: 27x47px
extern sFONT font_ubuntu_mono_16pt; // Font Size: 25x42px
extern sFONT font_ubuntu_mono_14pt; // Font Size: 21x36px
extern sFONT font_ubuntu_mono_12pt; // Font Size: 18x31px
extern sFONT font_ubuntu_mono_11pt; // Font Size: 17x29px
extern sFONT font_ubuntu_mono_10pt; // Font Size: 16x26px
extern sFONT font_ubuntu_mono_9pt; // Font Size: 15x24px
extern sFONT font_ubuntu_mono_8pt; // Font Size: 12x20px
extern sFONT font_ubuntu_mono_7pt; // Font Size: 11x19px
extern sFONT font_ubuntu_mono_6pt; // Font Size: 9x16px
extern sFONT Font36;
extern sFONT Font24;
extern sFONT Font20;
extern sFONT Font16;
extern sFONT Font12;
extern sFONT Font8;

extern cFONT Font12CN;
extern cFONT Font24CN;
#ifdef __cplusplus
}
#endif
  
#endif /* __FONTS_H */
 

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
