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
* Name:         spi.c
* Author:       Nivis LLC, Dorin Muica, Avram Ionut
* Date:         July 2009
* Description:  This source file is provided ...
*               This file holds definitions of the ...
* Changes:
* Revisions:
***************************************************************************************************/
#include "uart2.h"
#include "typedef.h"
#include "ISA100/sfc.h"
#include "CommonAPI/DAQ_Comm.h"
#include "../LibInterface/ITC_Interface.h"
#include "itc.h"
#include "global.h"
#include "string.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////


#if ( UART2_MODE != NONE )

#define STX 0xF1
#define CHX 0xF2

// General data

static uint8 g_ucRxComplete;

static uint8  g_aucTxQueue[3*UART2_BUFFER_SIZE];
uint16 g_unTxQueueSize;


static uint8 g_aucTx[UART2_BUFFER_SIZE + API_MSG_CRC_SIZE];
static uint8 g_ucTxIdx = 0;
static uint8 g_ucTxLen = 0;

uint32 g_ulTimestamp;
uint16 g_usTxTimeout;
uint16 g_usRxTimeout;

static uint8 g_aucRx[UART2_BUFFER_SIZE + API_MSG_CRC_SIZE];
static uint8 g_ucRxIdx;

uint8 g_ucWaitForCts;

void UART2_sendPendingBuff( void );
static void UART2_Interrupt(void);

////////////////////////////////////////////////////////////////////////////////////
// Name: GpioUart2Init
// Description: UART2 gpio initialisation
// Parameters: none
// Return: none
///////////////////////////////////////////////////////////////////////////////////
#define FN_MASK      3UL
#define FN_ALT       1  

#ifdef WAKEUP_ON_EXT_PIN
    void GpioUart2Init(void)
    {
        register uint32_t tmpReg;
        GPIO.PuSelLo |= (/*GPIO_UART2_RTS_bit |*/ GPIO_UART2_RX_bit);  // Pull-up select: UP type
        GPIO.PuEnLo  |= (/*GPIO_UART2_RTS_bit |*/ GPIO_UART2_RX_bit);  // Pull-up enable
        GPIO.InputDataSelLo &= ~(/*GPIO_UART2_RTS_bit |*/ GPIO_UART2_RX_bit); // read from pads
        GPIO.DirResetLo = (/*GPIO_UART2_RTS_bit |*/ GPIO_UART2_RX_bit); // inputs
        GPIO.DirSetLo = (/*GPIO_UART2_CTS_bit |*/ GPIO_UART2_TX_bit);  // outputs
        
        tmpReg = GPIO.FuncSel1 & ~(/*(FN_MASK << GPIO_UART2_CTS_fnpos) | (FN_MASK << GPIO_UART2_RTS_fnpos)\
        |*/ (FN_MASK << GPIO_UART2_RX_fnpos) | (FN_MASK << GPIO_UART2_TX_fnpos));
        GPIO.FuncSel1 = tmpReg | (/*(FN_ALT << GPIO_UART2_CTS_fnpos) | (FN_ALT << GPIO_UART2_RTS_fnpos)\
        |*/ (FN_ALT << GPIO_UART2_RX_fnpos) | (FN_ALT << GPIO_UART2_TX_fnpos));
    }
#else
    void GpioUart2Init(void)
    {
        //5 wires with hardware flow control
        register uint32_t tmpReg;
        GPIO.PuSelLo |= (GPIO_UART2_RTS_bit | GPIO_UART2_RX_bit);  // Pull-up select: UP type
        GPIO.PuEnLo  |= (GPIO_UART2_RTS_bit | GPIO_UART2_RX_bit);  // Pull-up enable
        GPIO.InputDataSelLo &= ~(GPIO_UART2_RTS_bit | GPIO_UART2_RX_bit); // read from pads
        GPIO.DirResetLo = (GPIO_UART2_RTS_bit | GPIO_UART2_RX_bit); // inputs
        GPIO.DirSetLo = (GPIO_UART2_CTS_bit | GPIO_UART2_TX_bit);  // outputs
        
        tmpReg = GPIO.FuncSel1 & ~((FN_MASK << GPIO_UART2_CTS_fnpos) | (FN_MASK << GPIO_UART2_RTS_fnpos)\
        | (FN_MASK << GPIO_UART2_RX_fnpos) | (FN_MASK << GPIO_UART2_TX_fnpos));
        GPIO.FuncSel1 = tmpReg | ((FN_ALT << GPIO_UART2_CTS_fnpos) | (FN_ALT << GPIO_UART2_RTS_fnpos)\
        | (FN_ALT << GPIO_UART2_RX_fnpos) | (FN_ALT << GPIO_UART2_TX_fnpos));
    }
#endif

void UART2_setBaudrate( uint32 p_ulUBRCNT )
{
    ITC_DisableInterrupt(gUart2Int_c);
    
  //configure the uart parameters 
    UART2_UCON    = 0x0000;                                                       // disable UART 1 for setting baud rate
    UART2_UBRCNT  = p_ulUBRCNT;                                                   // set desired baud rate
    UART2_URXCON  = 1;                                                            // set buffer level
    UART2_UTXCON  = 31;                                                           // 31 = ok // set buffer level

  #ifndef WAKEUP_ON_EXT_PIN
    //only for 5 wires with hardware flow control
    UART2_UCON_FCE = 1; //51000158 enable hardware protocol 5 wires
    UART2_UCTS = 0x1E;
  #endif    
    
    (void)UART2_USTAT;
    
    UART2_TX_INT_DI();
    UART2_RXTX_EN();    
    
    ITC_EnableInterrupt(gUart2Int_c);
 }

////////////////////////////////////////////////////////////////////////////////////
// Name: UART2_Init
// Description: UART2 Initialisation
// Parameters: none
// Return: none
///////////////////////////////////////////////////////////////////////////////////
void UART2_Init(void)
{
  // Assign interrupt to the Freescale driver interrupt
    IntAssignHandler(gUart2Int_c, (IntHandlerFunc_t)UART2_Interrupt);
    ITC_SetPriority (gUart2Int_c, gItcNormalPriority_c);

  // assign coresponding pins to SPI peripheral function
    GpioUart2Init();
 
    //UART2_setBaudrate( ( (UART_BAUD_9600 ) << 16 ) + 9999 );
    UART2_setBaudrate( ( (UART_BAUD_38400 ) << 16 ) + 9999 );  
    
    g_ucTxIdx = 0;
    g_ucTxLen = 0;
    
    g_ucRxComplete = FALSE;
    g_usTxTimeout = 0;
    g_usRxTimeout = 0;
    g_ucRxIdx = 0xFF;    
}

///////////////////////////////////////////////////////////////////////////////////
// Name: UART2_UpdateSpeed
// Description: update the baud of uart
// Parameters: p_NewSpeed: Requested speed.
// Return: true if success
///////////////////////////////////////////////////////////////////////////////////
uint8 UART2_UpdateSpeed(/*enum SPI_SPEED p_NewSpeed*/uint8 p_NewSpeed)
{
    switch( p_NewSpeed )
    {
    case UART_SPEED_9_6K: UART2_setBaudrate( ( (UART_BAUD_9600 ) << 16 ) + 9999 ); break;
    case UART_SPEED_19k:  UART2_setBaudrate( ( (UART_BAUD_19200 ) << 16 ) + 9999 ); break;
    case UART_SPEED_38k:  UART2_setBaudrate( ( (UART_BAUD_38400 ) << 16 ) + 9999 ); break;
    case UART_SPEED_115k: UART2_setBaudrate( ( (UART_BAUD_115200 ) << 16 ) + 9999 ); break;
    default:
          return SFC_INVALID_ARGUMENT;
    }
    return SFC_SUCCESS;
}

///////////////////////////////////////////////////////////////////////////////////
// Name: UART2_WriteBuff
// Description: Start the uart sending message process
// Parameters:
//     const ApiMessageFrame * p_pMsgHeader -  message header
//     const uint8*  p_pMsgPayload - message payload
// Return: operation result
///////////////////////////////////////////////////////////////////////////////////
SC UART2_WriteBuff( const ApiMessageFrame * p_pMsgHeader,
                  const uint8*  p_pMsgPayload)
{
    SC oResult = SUCCESS;
    if( p_pMsgHeader )
    {
        uint8 pMsgSize = sizeof(ApiMessageFrame) + p_pMsgHeader->MessageSize;
        if ( g_unTxQueueSize + pMsgSize + 1 < sizeof(g_aucTxQueue) ) 
        {
            uint8 * pTxQueue = g_aucTxQueue + g_unTxQueueSize;
            *(pTxQueue++) = pMsgSize;
            
            memcpy( pTxQueue, p_pMsgHeader, sizeof(ApiMessageFrame) ); 
            pTxQueue += sizeof(ApiMessageFrame);
            
            memcpy( pTxQueue, p_pMsgPayload,p_pMsgHeader->MessageSize ); 
            
            g_unTxQueueSize += pMsgSize+1;
        }
        else
        {
            oResult = OUT_OF_MEMORY;
        }
    }
    
    UART2_sendPendingBuff();    
    return oResult;
}

///////////////////////////////////////////////////////////////////////////////////
// Name: UART2_getNextSendMessage
// Description: gets next message from the queue
// Parameters: none
// Return: pointer to data or NULL if no data
///////////////////////////////////////////////////////////////////////////////////
uint8 * UART2_getNextSendMessage( void )
{
    static uint8 ucPreviousReqMsgID = 0;
    
    if( g_unTxQueueSize )
    {
        uint8 * pQueueMsg = g_aucTxQueue;
        for( ;pQueueMsg < g_aucTxQueue + g_unTxQueueSize;  pQueueMsg += pQueueMsg[0] + 1 )
        {
            if( ((ApiMessageFrame*)(pQueueMsg+1))->MessageHeader.m_bIsRsp ) // response messages first
            {
                return pQueueMsg;
            }
        }

        if( ((ApiMessageFrame*)g_aucTxQueue)->MessageID == ucPreviousReqMsgID )
        {
            g_ucExternalCommStatus = EXTERNAL_COMM_NOTOK;
        }        

        ucPreviousReqMsgID = ((ApiMessageFrame*)g_aucTxQueue)->MessageID;

        return g_aucTxQueue; // request message
    }
    
    return NULL;
}

///////////////////////////////////////////////////////////////////////////////////
// Name: UART2_removeMsg
// Description: erase message from the queue
// Parameters: pointer to messege
// Return: none
///////////////////////////////////////////////////////////////////////////////////
void UART2_removeMsg( uint8 * p_pMsg )
{
    uint8  ucPkLen = 1 + (uint16)(p_pMsg[0]);
    if( p_pMsg+ucPkLen <= g_aucTxQueue+g_unTxQueueSize )
    {
        g_unTxQueueSize -= ucPkLen;
        memmove( p_pMsg, p_pMsg+ucPkLen, g_aucTxQueue+g_unTxQueueSize-p_pMsg );  // remove from queue
    }
    else // corrupted queue
    {
        g_unTxQueueSize = 0;
    }
}

#define SET_MT_RTS_ACTIVE()    GPIO_DATA_RESET0 = GET_GPIO_AS_BIT( MT_RTS )
#define SET_MT_RTS_INACTIVE()  GPIO_DATA_SET0 = GET_GPIO_AS_BIT( MT_RTS )
#define GET_SP_CTS_ACTIVE()    (GPIO_DATA0_P21==0)
///////////////////////////////////////////////////////////////////////////////////
// Name: UART2_sendPendingBuff
// Description: used to start the uart sending message process in UART2_WriteBuff
// Parameters: none
// Return: none
// Obs: called once so inline make sense
///////////////////////////////////////////////////////////////////////////////////
void UART2_sendPendingBuff( void )
{
    if ( g_ucTxLen || !UART2_UTXCON  ) // TX on progress
        return;
    
    uint8 * pPk = UART2_getNextSendMessage();
    if( !pPk )
        return;

    uint8 ucPkLen = pPk[0];
            
    if( ucPkLen < sizeof(ApiMessageFrame) ) // corrupted queue
    {
        g_unTxQueueSize = 0;
        return;
    }
    if( ucPkLen >= sizeof(g_aucTx)-2 ) // too long message, discard it 
    {
        UART2_removeMsg( pPk );
        return;
    }
    
#ifdef WAKEUP_ON_EXT_PIN
    SET_MT_RTS_ACTIVE();
                            
    if( !GET_SP_CTS_ACTIVE() ) //condition YOKOGAWA
    {
        if( !g_ucWaitForCts )
        {
            g_ucWaitForCts = 1;
            g_usTxTimeout = MSEC_100; //waiting acq board to be ready
        }                
        return;
    }
    g_ucWaitForCts = 0;
#endif
                
    memcpy( g_aucTx, pPk+1, ucPkLen );
    if( ((ApiMessageFrame*)(pPk+1))->MessageHeader.m_bIsRsp ) // response, remove from queue
    {       
        UART2_removeMsg( pPk );
    }
                
    uint16 unCRC = ComputeCCITTCRC(g_aucTx, ucPkLen);
    g_aucTx[ ucPkLen+0 ] = (unCRC >> 8); // CRC -> HI byte
    g_aucTx[ ucPkLen+1 ] = unCRC & 0x00FF; // CRC -> LO byte
     
    g_ucTxIdx = 0;
    g_ucTxLen = ucPkLen+2;

    g_usTxTimeout = MSEC_1/10 + MSEC_1; // that time is activated at end of TX 
    
    // Send STX as the first byte to start the process.
    UART2_UDATA = STX;
    UART2_TX_INT_EN();
                
#ifdef WAKEUP_ON_EXT_PIN                 
    SET_MT_RTS_INACTIVE();
#endif
}


///////////////////////////////////////////////////////////////////////////////////
// Name: UART2_ReadBuff
// Description: Get the data received.
// Parameters:
//    unsigned char* p_pMsg = data array
//    unsigned char p_ucMsgLen  = data array max size
// Returns: 0 if no data, Size of data if there was data
// Note: format is address + data
///////////////////////////////////////////////////////////////////////////////////
uint8 UART2_ReadBuff(unsigned char* p_pMsg,
                    unsigned char p_ucMsgMaxSize)
{
    uint8 ucResultSize = 0;

        if ( g_ucRxComplete ) 
        {
            // As long as DataRxFlag is TRUE, the buffer does not
            // need protection from interrupts
        
            // Process results of DMA reception for full packet.
            // From STX find CRC and process.  CRC should be at STX+headersize(5)+(Size@STX+4)+CRC(2)
            uint8 ucPacketSize = g_ucRxIdx;    
            if( ucPacketSize > 2 ) // valid size
            {
                if (0 == ComputeCCITTCRC(g_aucRx, ucPacketSize)) 
                {
                    ucPacketSize -= 2; // CRC[2]
                    if (ucPacketSize < p_ucMsgMaxSize) 
                    {
                        if(    ((ApiMessageFrame*)g_aucRx)->MessageHeader.m_bIsRsp  // it is response
                            && ((ApiMessageFrame*)g_aucRx)->MessageID == ((ApiMessageFrame*)(g_aucTxQueue+1))->MessageID
                            && !((ApiMessageFrame*)(g_aucTxQueue+1))->MessageHeader.m_bIsRsp   //it is waited response  
                              )
                        {
                              UART2_removeMsg( g_aucTxQueue );
                              g_ucExternalCommStatus = EXTERNAL_COMM_OK;
                        }
                        memcpy(p_pMsg, g_aucRx, ucPacketSize);
                        ucResultSize = ucPacketSize;
                    }
                }
            }            
            g_ucRxIdx = 0xFF;
            g_ucRxComplete = FALSE;
        }
        
    return ucResultSize;
}

///////////////////////////////////////////////////////////////////////////////////
// Name: UART irq handler
// Description: 
// Parameters: none
// Return: none
///////////////////////////////////////////////////////////////////////////////////
static void UART2_Interrupt(void)
{
    static uint8 s_ucPrevRxChar;
    volatile unsigned long ulDEBStatus = UART2_USTAT;  //Dummy read to clear flags
      
    // RX
    while ( UART2_URXCON )
    {
        unsigned char ucRxChar = UART2_UDATA;
        g_usRxTimeout = MSEC_5;
        
        if( !g_ucRxComplete )
        {
              if( ucRxChar == STX ) 
              {          
                  g_ucRxIdx = 0;  
              }
              else  if( (ucRxChar != CHX) && (g_ucRxIdx < sizeof(g_aucRx)) )
              {
                  if( s_ucPrevRxChar == CHX )
                  {
                      g_aucRx[ g_ucRxIdx++ ] = ~ucRxChar;
                  }
                  else
                  {
                      g_aucRx[ g_ucRxIdx++ ] = ucRxChar;
                  }
                  
                  if( (g_ucRxIdx == ((uint16)(g_aucRx[3]) + 6) ) ) // end of packet
                  {
                       g_ucRxComplete = TRUE;
                  }
              }
              s_ucPrevRxChar = ucRxChar;
        }
    }

    // TX
    while( UART2_UTXCON )
    {
        if( g_ucTxIdx < g_ucTxLen )
        {              
            // Send next byte in packet.
            unsigned char ucTxChar = g_aucTx[g_ucTxIdx];  
            if( (ucTxChar == STX) || (ucTxChar == CHX) )
            {
                g_aucTx[g_ucTxIdx] = ~ucTxChar;
                ucTxChar = CHX;    
            }
            else
            {
                g_ucTxIdx ++;
            }
            UART2_UDATA = ucTxChar;
        }
        else
        {
            g_ucTxLen = 0;

            UART2_TX_INT_DI();
            break;
        }
    }
}

///////////////////////////////////////////////////////////////////////////////////
// Name: UART2_IsBusy
// Description: Returns true if communications are in process, and false if not.
// Parameters: none
// Return: TRUE/FALSE
///////////////////////////////////////////////////////////////////////////////////
Boolean UART2_IsBusy (void)
{
    return (((g_usTxTimeout!=0) || (g_usRxTimeout!=0))?TRUE:FALSE);
}

///////////////////////////////////////////////////////////////////////////////////
// Name: UART2_Timeouts
// Description: adjust the timeouts by the nr of ticks generated by RTC 
// Parameters: none
// Return: none
///////////////////////////////////////////////////////////////////////////////////
void UART2_Timeouts (void)
{
    uint32 ulNow = RTC_COUNT;
    uint16 unInterval = (uint16)(ulNow - g_ulTimestamp);
    
    g_ulTimestamp = ulNow;

    if (CRM_STATUS_EXT_WU_EVT != 0) 
    {
        g_usRxTimeout = MSEC_5;
        CRM_STATUS_EXT_WU_EVT = 0xF;
    }
    else
    {
        g_usRxTimeout -= unInterval;
        if ( g_usRxTimeout & 0x8000 ) // negative value means overflow
        {
            g_usRxTimeout = 0;
        }
    }
    
    if( !g_ucTxLen && (UART2_UTXCON >= 32) ) // nothing to send
    {
        if( g_ucTxIdx ) // end of packet, start counting over g_usTxTimeout
        {
            g_ucTxIdx = 0;
        }
        else
        {
            g_usTxTimeout -= unInterval;
            if( g_usTxTimeout & 0x8000 )
            {
                g_usTxTimeout = 0;
            }
            
#ifdef WAKEUP_ON_EXT_PIN
            if( g_usTxTimeout ) // timeout still on progress
            {
                if (g_ucWaitForCts) // is waiting for CTS
                {
                    UART2_sendPendingBuff();
                }
            }
            else
            {
                g_ucWaitForCts = 0;
                SET_MT_RTS_INACTIVE();
            }
#endif
        }
    }

    if( g_ucRxComplete ) // speed up the response
    {
        g_usRxTimeout = 0;
        
        DAQ_RxHandler();
        DAQ_TxHandler();
    }
}

#endif // UART2_MODE != NONE


