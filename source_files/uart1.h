/*
* Copyright (C) 2013 Nivis LLC.
* Email:   opensource@nivis.com
* Website: http://www.nivis.com
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, version 3 of the License.
* 
* Redistribution and use in source and binary forms must retain this
* copyright notice.

* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*/

/***************************************************************************************************
* Name:         uart1.h
* Author:       Marius Vilvoi
* Date:         October 2007
* Description:  This header file is provided ...
*               This file holds definitions of the ...
* Changes:
* Revisions:
****************************************************************************************************/
#ifndef _NIVIS_UART1_H_
#define _NIVIS_UART1_H_

#ifdef BACKBONE_SUPPORT

  #include "MC1322x.h"

  // BAUD RATES
  #define UART_BAUD_115200      767L
  #define UART_BAUD_57600       383L
  #define UART_BAUD_38400       256L
  #define UART_BAUD_19200       128L
  #define UART_BAUD_9600         64L

  // BUFFER LENGHT
  #define UART1_PACKET_LENGTH       (120+16) //120
  
  #define UART1_RX_INT_EN()    { UART1_UCON &=~ 0x4000; }
  #define UART1_RX_INT_DI()    { UART1_UCON |= 0x4000; }
  
  #define UART1_TX_INT_EN()    { UART1_UCON &=~ 0x2000; }
  #define UART1_TX_INT_DI()    { UART1_UCON |= 0x2000; }

  #define UART1_RXTX_INT_EN()    { UART1_UCON &= ~0x6000; }
  #define UART1_RXTX_INT_DI()    { UART1_UCON |=  0x6000; }

  #define UART1_TX_EN()    { UART1_UCON |= 0x0001; }  // TX : 1 = enabled; 0 = disabled
  #define UART1_TX_DI()    { UART1_UCON &=~0x0001; }  //
  #define UART1_RX_EN()    { UART1_UCON |= 0x0002; }  // RX : 1 = enabled; 0 = disabled
  #define UART1_RX_DI()    { UART1_UCON &=~0x0002; }  //

  #define UART1_RXTX_DI()    { UART1_UCON &=~0x0003; }  //
  #define UART1_RXTX_EN()    { UART1_UCON |= 0x0003; }  //
  
  void UART1_Init(void);
  unsigned char UART1_SendMsg(const unsigned char * p_pucBuff, unsigned char p_ucLen);
  
  extern  unsigned char g_ucBufferReadyReq;
  extern unsigned int   g_unLastSTXTime;

#else  // BACKBONE_SUPPORT
  
  #define UART1_Init()
  #define UART1_SendMsg(...) 0
  
#endif // BACKBONE_SUPPORT
#endif /* _NIVIS_UART1_H_ */

