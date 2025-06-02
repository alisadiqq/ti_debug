/******************************************************************************

@file  app_l2cap_coc.c

@brief This file contains the L2CAP COC flow control application
functionality.

Group: WCS, BTS
Target Device: cc23xx

******************************************************************************

 Copyright (c) 2023-2024, Texas Instruments Incorporated
 All rights reserved.

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:

 *  Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.

 *  Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

 *  Neither the name of Texas Instruments Incorporated nor the names of
    its contributors may be used to endorse or promote products derived
    from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

******************************************************************************


*****************************************************************************/

/*
 * General L2CAP Terminology :

_________________________________________________________________________________________________________________________________________________
||  Term                               ||  Description                                                                                          ||
||_____________________________________||_______________________________________________________________________________________________________||
||  L2CAP channel                      ||  The logical connection between two endpoints in peer devices,                                        ||
||                                     ||  characterized by their Channel Identifiers (CIDs)                                                    ||
||  SDU or L2CAP SDU                   ||  Service Data Unit: a packet of data that L2CAP exchanges with                                        ||
||                                     ||  the upper layer and transports transparently over an L2CAP channel                                   ||
||                                     ||  using the procedures specified in this document.                                                     ||
||                                     ||  This is the raw payload from the host/app, and does not include L2CAP headers.                       ||
||  PDU or L2CAP PDU                   ||  Protocol Data Unit: a packet of data containing L2CAP protocol information                           ||
||                                     ||  fields, control information, and/or upper layer information data.                                    ||
||                                     ||  This packet includes L2CAP headers.                                                                  ||
||                                     ||  A single SDU may be split across multiple PDUs.                                                      ||
||  Maximum Transmission Unit (MTU)    ||  The maximum size of payload data, in octets, that the upper layer entity                             ||
||                                     ||  can accept (that is, the MTU corresponds to the maximum SDU size).                                   ||
||                                     ||  Note: This is different than ATT_MTU.                                                                ||
||  Maximum PDU Payload Size (MPS)     ||  The maximum size of payload data in octets that the L2CAP layer entity can                           ||
||                                     ||  accept (that is, the MPS corresponds to the maximum PDU payload size).                               ||
||  Credit                             ||  The number of LE-frames that the device can receive. Credits may range                               ||
||                                     ||  between 1 and 65535, and are used as a flow control mechanism between devices.                       ||
||  L2CAP Basic Header                 ||  L2CAP protocol information that is prepended to each PDU. This includes CID and length               ||
||  Protocol/Service Multiplexer (PSM) ||  A two octet field that is used to define the interpretation of L2CAP channel data.                   ||
||                                     ||  There are both dynamic and fixed PSMs. Fixed PSMs are assigned by the SIG,                           ||
||                                     ||  while dynamic PSMs may be discovered by GATT.                                                        ||
||  Fragmentation/Recombination        ||  Fragmentation is the process of breaking down L2CAP PDUs into smaller pieces                         ||
||                                     ||  for the for the controller to send out. Recombination is the process of the controller               ||
||                                     ||  reassembling fragments into complete L2CAP PDUs.                                                     ||
||                                     ||  Fragmentation/Recombination is performed by the controller and is based on the                       ||
||                                     ||  LE Data Length Extension feature. Fragmentation/Recombination operations are transparent to L2CAP.   ||
||  Segmentation/Reassemble            ||  Segmentation is the process of breaking a single L2CAP SDU up multiple L2CAP packets                 ||
||                                     ||  called SDU segments. Reassemble is the inverse of this operation on the receive side.                ||
||                                     ||  Each segment is encapsulated in a proper L2CAP header.                                               ||
||                                     ||  Both segmentation and reassembly is handled by the L2CAP layer and transparent                       ||
||                                     ||  to lower and higher layers.                                                                          ||
||_____________________________________||_______________________________________________________________________________________________________||

According to the User Guide
*/

/*
 * L2CAP COC BASIC OPERATION OVERVIEW
 *
 * LE Credit Based Flow Control Mode is used for LE L2CAP connection-oriented
 * channels with flow control using a credit based scheme for L2CAP data (i.e.
 * not signaling packets).
 * The number of credits (K-frames) that can be received by a device on an
 * L2CAP channel is determined during connection establishment. K-frames shall
 * only be sent on an L2CAP channel if the device has a credit count greater than
 * zero for that L2CAP channel. For each K-frame sent the device decreases the
 * credit count for that L2CAP channel by one. The peer device may return credits
 * for an L2CAP channel at any time by sending an L2CAP_FLOW_CONTROL_CREDIT_IND
 * packet. When a credit packet is received by a device it shall
 * increment the credit count for that L2CAP channel by the value of the Credits
 * field in this packet. The number of credits returned for an L2CAP channel may
 * exceed the initial credits provided in the L2CAP_LE_CREDIT_BASED_CONNECTION_REQ
 * or L2CAP_LE_CREDIT_BASED_CONNECTION_RSP packet.
 * The device sending the L2CAP_FLOW_CONTROL_CREDIT_IND
 * packet shall ensure that the number of credits returned for an L2CAP channel
 * does not cause the credit count to exceed 65535. The device receiving the
 * credit packet shall disconnect the L2CAP channel if the credit count exceeds
 * 65535. The device shall also disconnect the L2CAP channel if it receives a
 * K-frame on an L2CAP channel from the peer device that has a credit count of
 * zero. If a device receives an L2CAP_FLOW_CONTROL_CREDIT_IND packet
 * with credit value set to zero, the packet shall be ignored. A device shall not
 * send credit values of zero in L2CAP_FLOW_CONTROL_CREDIT_IND
 * packets.
 * If an L2CAP_LE_CREDIT_BASED_CONNECTION_REQ packet is received
 * and there is insufficient authentication between the two devices, the connection
 * shall be rejected with a result value of “Connection refused - insufficient
 * authentication. If an L2CAP_LE_CREDIT_BASED_CONNECTION_REQ
 * packet is received and there is insufficient authorization between the two
 * devices, the connection shall be rejected with a result value of “Connection
 * refused - insufficient authorization. If an L2CAP_LE_CREDIT_BASED_-
 * CONNECTION_REQ packet is received and the encryption key size is too
 * short, the connection shall be rejected with a result value of “Connection
 * refused insufficient encryption key size.
 */

/*********************************************************************
 * INCLUDES
 */

#if defined (BLE_V41_FEATURES) && (BLE_V41_FEATURES & L2CAP_COC_CFG)

#include "l2cap.h"
#include <string.h>
#include <icall.h>
#include <ti/drivers/GPIO.h>
#include "ti_ble_config.h"
#include <ti/bleapp/ble_app_util/inc/bleapputil_api.h>
#include <ti/bleapp/menu_module/menu_module.h>
#include <app_main.h>

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */
/*********************************************************************
 * TYPEDEFS
 */
/// @brief Application data format (src/dst CIDs are relative to sender of request).
typedef struct
{
  uint16 connHandle;   //!< The connection handle
  uint16 CID;          //!< L2CAP CID - characterizes given logical connection between this and the peer device
  uint16 peerCID;      //!< L2CAP peer CID - characterizes given logical connection at the peer device
  uint16 PSM;          //!< PSM - Protocol/Service Multiplexer ID
  uint16 peerPSM;      //!< peer PSM - peer Protocol/Service Multiplexer ID
  uint8 taskId;        //!< Task registered with PSM
} gL2CAPCOC_AppData_t;

/*********************************************************************
 * GLOBAL VARIABLES
 */
gL2CAPCOC_AppData_t gL2CAPCOC_AppData;

/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * LOCAL FUNCTIONS
 */
void L2CAPCOC_dataHandler(uint32 event, BLEAppUtil_msgHdr_t *pMsgData);
void L2CAPCOC_signalHandler(uint32 event, BLEAppUtil_msgHdr_t *pMsgData);
void L2CAPCOC_connEventHandler(uint32 event, BLEAppUtil_msgHdr_t *pMsgData);

static bStatus_t L2CAPCOC_openCoc(uint16_t connHandle);
static bStatus_t L2CAPCOC_closeCoc(uint16_t connHandle);

// Events handlers struct, contains the handlers and event masks
// of the L2CAP data packets
BLEAppUtil_EventHandler_t DSL2CAPdataHandler =
{
    .handlerType    = BLEAPPUTIL_L2CAP_DATA_TYPE,
    .pEventHandler  = L2CAPCOC_dataHandler,
    .eventMask      = 0,
};

// Events handlers struct, contains the handlers and event masks
// of the L2CAP signal packets
BLEAppUtil_EventHandler_t DSL2CAPsignalHandler =
{
    .handlerType    = BLEAPPUTIL_L2CAP_SIGNAL_TYPE,
    .pEventHandler  = L2CAPCOC_signalHandler,
    .eventMask      = BLEAPPUTIL_L2CAP_CHANNEL_ESTABLISHED_EVT       |
                      BLEAPPUTIL_L2CAP_CHANNEL_TERMINATED_EVT        |
                      BLEAPPUTIL_L2CAP_OUT_OF_CREDIT_EVT             |
                      BLEAPPUTIL_L2CAP_PEER_CREDIT_THRESHOLD_EVT
};

// Events handlers struct, contains the handlers and event masks
// of the GAP connection notifications
BLEAppUtil_EventHandler_t DSL2CAPconnectionHandler =
{
    .handlerType    = BLEAPPUTIL_GAP_CONN_TYPE,
    .pEventHandler  = L2CAPCOC_connEventHandler,
    .eventMask      = BLEAPPUTIL_LINK_ESTABLISHED_EVENT |
                      BLEAPPUTIL_LINK_TERMINATED_EVENT |
                      BLEAPPUTIL_LINK_PARAM_UPDATE_EVENT |
                      BLEAPPUTIL_LINK_PARAM_UPDATE_REQ_EVENT
};

/*********************************************************************
 * @fn      L2CAPCOC_start
 *
 * @brief   This function is called after stack initialization,
 *          the purpose of this function is to initialize and
 *          register the L2CAP COC profile.
 *
 * @return  SUCCESS or stack call status
 */
bStatus_t L2CAPCOC_start( void )
{
  bStatus_t status = SUCCESS;

  status = BLEAppUtil_registerEventHandler(&DSL2CAPdataHandler);
  if(status != SUCCESS)
  {
      // Return status value
      return(status);
  }

  status = BLEAppUtil_registerEventHandler(&DSL2CAPsignalHandler);
  if(status != SUCCESS)
  {
      // Return status value
      return(status);
  }

  status = BLEAppUtil_registerEventHandler(&DSL2CAPconnectionHandler);
  if(status != SUCCESS)
  {
      // Return status value
      return(status);
  }

  // Set LEDs
  GPIO_write( CONFIG_GPIO_LED_RED, CONFIG_LED_OFF );
  GPIO_write( CONFIG_GPIO_LED_GREEN, CONFIG_LED_ON );

  return ( SUCCESS );
}

/*********************************************************************
 * @fn      L2CAPCOC_connEventHandler
 *
 * @brief   The purpose of this function is to handle connection related
 *          events that rise from the GAP and were registered in
 *          @ref BLEAppUtil_registerEventHandler
 *
 * @param   event - message event.
 * @param   pMsgData - pointer to message data.
 *
 * @return  none
 */
void L2CAPCOC_connEventHandler(uint32 event, BLEAppUtil_msgHdr_t *pMsgData)
{
  if (!pMsgData)
  {
    // Caller needs to figure out by himself that pMsg is NULL
    return;
  }

  switch(event)
  {
    case BLEAPPUTIL_LINK_ESTABLISHED_EVENT:
    {
      L2CAPCOC_openCoc(((gapEstLinkReqEvent_t *)pMsgData)->connectionHandle);
      break;
    }

    case BLEAPPUTIL_LINK_TERMINATED_EVENT:
    {
      /* Disconnect and De-register requests were supposed to be handled implicitly by
       * getting those requests from the peer device.
       * Added Here just to make sure we cleanup all app data. */

      L2CAPCOC_closeCoc(((gapEstLinkReqEvent_t *)pMsgData)->connectionHandle);
      break;
    }

    default:
    {
      break;
    }
  }
}

/*********************************************************************
 * @fn      L2CAPCOC_dataHandler
 *
 * @brief   Handles the data received on the L2CAP channel
 *
 * @param   event - event to handle
 * @param   pMsgData - data to handle
 *
 * @return  none
 */
void L2CAPCOC_dataHandler(uint32 event, BLEAppUtil_msgHdr_t *pMsgData)
{
  uint16 i = 0;

  if (!pMsgData)
  {
    // Caller needs to figure out by himself that pMsg is NULL
    return;
  }

  // Toggle LEDs to indicate that data was received
  GPIO_toggle( CONFIG_GPIO_LED_RED );
  GPIO_toggle( CONFIG_GPIO_LED_GREEN );

  l2capDataEvent_t *pDataPkt = (l2capDataEvent_t *)pMsgData;

  MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE2, 0,
                    "L2CAP: connHandle: "
                    MENU_MODULE_COLOR_YELLOW "%d " MENU_MODULE_COLOR_RESET
                    " CID " MENU_MODULE_COLOR_YELLOW "0x%x " MENU_MODULE_COLOR_RESET
                    "length: " MENU_MODULE_COLOR_YELLOW "%d " MENU_MODULE_COLOR_RESET,
                    pDataPkt->pkt.connHandle,
                    pDataPkt->pkt.CID,
                    pDataPkt->pkt.len);
//  MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE3, 0,
//                    "Incoming Data: " MENU_MODULE_COLOR_YELLOW "%s" MENU_MODULE_COLOR_RESET,
//                    pDataPkt->pkt.pPayload);

  // Change upper case to lower case and vice versa
  for ( i = 0; i < pDataPkt->pkt.len; i++ )
  {
    if ( pDataPkt->pkt.pPayload[i] >= 'a' && pDataPkt->pkt.pPayload[i] <= 'z' )
    {
      pDataPkt->pkt.pPayload[i] = pDataPkt->pkt.pPayload[i] - 32;
    }
    else if ( pDataPkt->pkt.pPayload[i] >= 'A' && pDataPkt->pkt.pPayload[i] <= 'Z' )
    {
      pDataPkt->pkt.pPayload[i] = pDataPkt->pkt.pPayload[i] + 32;
    }
  }

//  MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE4, 0,
//                    "Outgoing Data: " MENU_MODULE_COLOR_YELLOW "%s" MENU_MODULE_COLOR_RESET,
//                    pDataPkt->pkt.pPayload);

  // The app takes the received data, changes the payload according to ( Upper-Case <---->lower-case )
  // and sends it back to the peer device (loopback implementation).
  // It is possible to expand this function to support multiple COC CID's
  pDataPkt->pkt.connHandle = gL2CAPCOC_AppData.connHandle;
  pDataPkt->pkt.CID = gL2CAPCOC_AppData.CID;
  L2CAP_SendSDU( &pDataPkt->pkt );
}

/*********************************************************************
 * @fn      L2CAPCOC_signalHandler
 *
 * @brief   Handles the signals received on the L2CAP channel
 *
 * @param   event - event to handle
 * @param   pMsgData - request/response data to handle
 *
 * @return  none
 */
void L2CAPCOC_signalHandler(uint32 event, BLEAppUtil_msgHdr_t *pMsgData)
{
  if (!pMsgData)
  {
    // Caller needs to figure out by himself that pMsg is NULL
    return;
  }

  /*Handle the l2cap event */
  switch (event)
  {
    case BLEAPPUTIL_L2CAP_CHANNEL_ESTABLISHED_EVT:
    {
      l2capChannelEstEvt_t *pConnEvt = &((l2capSignalEvent_t *)pMsgData)->cmd.channelEstEvt;
      gL2CAPCOC_AppData.CID        = pConnEvt->CID;
      gL2CAPCOC_AppData.peerCID    = pConnEvt->info.peerCID;

      MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE1, 0,
                        "L2CAP: COC established "
                        "PSM "               MENU_MODULE_COLOR_YELLOW "%d " MENU_MODULE_COLOR_RESET
                        "Peer PSM "          MENU_MODULE_COLOR_YELLOW "%d " MENU_MODULE_COLOR_RESET
                        "CID "               MENU_MODULE_COLOR_YELLOW "0x%x " MENU_MODULE_COLOR_RESET
                        "Peer CID "          MENU_MODULE_COLOR_YELLOW "0x%x " MENU_MODULE_COLOR_RESET
                        "mtu "               MENU_MODULE_COLOR_YELLOW "0x%d " MENU_MODULE_COLOR_RESET
                        "mps "               MENU_MODULE_COLOR_YELLOW "0x%d " MENU_MODULE_COLOR_RESET,
                        gL2CAPCOC_AppData.PSM,
                        gL2CAPCOC_AppData.peerPSM,
                        gL2CAPCOC_AppData.CID,
                        gL2CAPCOC_AppData.peerCID,
                        pConnEvt->info.mtu,
                        pConnEvt->info.mps);

      /* Send the first packet to the peer device.
       * This packet will be echoed and sent back and forward until the connection will be destroyed.*/
      if (L2CAP_CONN_ESTABLISH_ROLE == L2CAPCOC_INITIATOR)
      {
          l2capPacket_t packet;
          packet.connHandle = gL2CAPCOC_AppData.connHandle;
          packet.CID = gL2CAPCOC_AppData.CID;
          packet.len = L2CAP_MAX_MTU;
          packet.pPayload = L2CAP_bm_alloc( L2CAP_MAX_MTU );

          memset (packet.pPayload, 0x41 /*"A"*/, L2CAP_MAX_MTU);

          bStatus_t  status = L2CAP_SendSDU( &packet );
          MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE2, 0,
                            "L2CAP: Data Sent "
                            "status "               MENU_MODULE_COLOR_YELLOW "%d " MENU_MODULE_COLOR_RESET
                            "L2CAP_MAX_MTU "        MENU_MODULE_COLOR_YELLOW "%d " MENU_MODULE_COLOR_RESET,
                            status,
                            L2CAP_MAX_MTU);

      }
      return;
    }
    case BLEAPPUTIL_L2CAP_PEER_CREDIT_THRESHOLD_EVT:
    {
      l2capCreditEvt_t *pCreditEvt = &((l2capSignalEvent_t *)pMsgData)->cmd.creditEvt;
      uint16_t connHandle = ((l2capSignalEvent_t *)pMsgData)->connHandle;

      // Send credits to the peer device
      L2CAP_FlowCtrlCredit(connHandle, pCreditEvt->CID, L2CAP_NOF_CREDITS);
      return;
    }
    case BLEAPPUTIL_L2CAP_OUT_OF_CREDIT_EVT:
      /* Do nothing */
    case BLEAPPUTIL_L2CAP_CHANNEL_TERMINATED_EVT:
    case BLEAPPUTIL_L2CAP_SEND_SDU_DONE_EVT:
    case BLEAPPUTIL_L2CAP_NUM_CTRL_DATA_PKT_EVT:
      /* No further actions required - we have handled the events. */
      break;
    default:
      break;
  }
}

/*********************************************************************
* @fn      L2CAPCOC_openCoc
*
* @brief   Opens a communication oriented channel between peers
*
* @param   connHandle - connection handle
*
* @return  status - 0 = success, 1 = failed
*/
static bStatus_t L2CAPCOC_openCoc(uint16_t connHandle)
{
  uint8_t ret = SUCCESS;
  l2capPsm_t psm;
  l2capPsmInfo_t psmInfo;
  MenuModule_clearLines(APP_MENU_PROFILE_STATUS_LINE, APP_MENU_PROFILE_STATUS_LINE4);

  if (L2CAP_PsmInfo(L2CAP_PSM_ID, &psmInfo) == INVALIDPARAMETER)
  {
    l2capUserCfg_t l2capUserConfig;
    L2CAP_GetUserConfig(&l2capUserConfig);

    // Prepare the PSM parameters
    psm.psm                 = L2CAP_PSM_ID;
    psm.mtu                 = L2CAP_MAX_MTU;
    psm.mps                 = L2CAP_MAX_MPS;
    psm.initPeerCredits     = L2CAP_NOF_CREDITS;
    psm.peerCreditThreshold = L2CAP_CREDITS_THRESHOLD;
    psm.maxNumChannels      = l2capUserConfig.maxNumCoChannels;
    psm.taskId              = ICall_getLocalMsgEntityId(ICALL_SERVICE_CLASS_BLE_MSG, BLEAppUtil_getSelfEntity());
    psm.pfnVerifySecCB      = NULL;

    // Register PSM with L2CAP task
    ret = L2CAP_RegisterPsm(&psm);

    MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0,
                      "Register PSM " MENU_MODULE_COLOR_YELLOW "%d " "%s" MENU_MODULE_COLOR_RESET,
                      psm.psm, ret ? "FAILED" : "SUCCEDED");

    if (ret == SUCCESS)
    {
      if (L2CAP_CONN_ESTABLISH_ROLE == L2CAPCOC_INITIATOR)
      {
        // Send the connection request to the peer
        ret = L2CAP_ConnectReq(connHandle, psm.psm, L2CAP_PEER_PSM_ID);
        MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE1, 0,
                      "Connection request sent to PSM ID " MENU_MODULE_COLOR_YELLOW "%d ",
                       L2CAP_PEER_PSM_ID );
      }
      /* Store the l2cap app data */
      gL2CAPCOC_AppData.connHandle = connHandle;
      gL2CAPCOC_AppData.PSM        = psm.psm;
      gL2CAPCOC_AppData.peerPSM    = L2CAP_PEER_PSM_ID;
      gL2CAPCOC_AppData.taskId     = psm.taskId;
    }
  }

  return ret;
}

/*********************************************************************
* @fn      L2CAPCOC_closeCoc
*
* @brief   Close a communication channel between peers
*
* @param   connHandle - connection handle
*
* @return  status - 0 = success, 1 = failed
*/
static bStatus_t L2CAPCOC_closeCoc(uint16_t connHandle)
{
  bStatus_t ret = SUCCESS;

  ret = L2CAP_DisconnectReq(connHandle, gL2CAPCOC_AppData.CID);

  MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE1, 0,
                    "L2CAP: COC disconnected "
                    "PSM "               MENU_MODULE_COLOR_YELLOW "%d " MENU_MODULE_COLOR_RESET
                    "Peer PSM "          MENU_MODULE_COLOR_YELLOW "%d " MENU_MODULE_COLOR_RESET
                    "CID "               MENU_MODULE_COLOR_YELLOW "0x%x " MENU_MODULE_COLOR_RESET
                    "Peer CID "          MENU_MODULE_COLOR_YELLOW "0x%x " MENU_MODULE_COLOR_RESET,
                    gL2CAPCOC_AppData.PSM,
                    gL2CAPCOC_AppData.peerPSM,
                    gL2CAPCOC_AppData.CID,
                    gL2CAPCOC_AppData.peerCID);

  ret |=  L2CAP_DeregisterPsm( gL2CAPCOC_AppData.taskId, gL2CAPCOC_AppData.PSM);

  MenuModule_printf(APP_MENU_PROFILE_STATUS_LINE, 0,
                    "De-Register PSM " MENU_MODULE_COLOR_YELLOW "%d " MENU_MODULE_COLOR_RESET,
                    gL2CAPCOC_AppData.PSM);

  memset(&gL2CAPCOC_AppData, 0, sizeof(gL2CAPCOC_AppData));

  return ret;
}

#endif //(BLE_V41_FEATURES) && (BLE_V41_FEATURES & L2CAP_COC_CFG)
