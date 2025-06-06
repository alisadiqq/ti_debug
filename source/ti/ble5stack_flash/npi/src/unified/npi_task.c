/******************************************************************************

 @file  npi_task.c

 @brief NPI is a TI RTOS Application Thread that provides a ! common
        Network Processor Interface framework.

 Group: WCS, LPC, BTS
 Target Device: cc23xx

 ******************************************************************************
 
 Copyright (c) 2015-2024, Texas Instruments Incorporated
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

// ****************************************************************************
// includes
// ****************************************************************************
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>

#include "inc/npi_util.h"
#include "inc/npi_task.h"
#include "inc/npi_data.h"
#include "inc/npi_tl.h"

#include <semaphore.h>
#include <sched.h>

// ****************************************************************************
// defines
// ****************************************************************************

#if defined(ICALL_EVENTS) && !defined(APP_EXTERNAL_CONTROL)
#define NPITASK_ICALL_EVENT                 ICALL_MSG_EVENT_ID // Event_Id_31

//! \brief ASYNC Message Received Event (no framing bytes)
#define NPITASK_FRAME_RX_EVENT              Event_Id_00

//! \brief A framed message buffer is ready to be sent to the transport layer.
#define NPITASK_TX_READY_EVENT              Event_Id_01

//! \brief ASYNC Message Received Event (no framing bytes)
#define NPITASK_SYNC_FRAME_RX_EVENT         Event_Id_02

//! \brief Last TX message has been successfully sent
#define NPITASK_TX_DONE_EVENT               Event_Id_03

//! \brief Remote Rdy received Event
#define NPITASK_REM_RDY_EVENT               Event_Id_04

//! \brief NPI assert message
#define NPITASK_ASSERT_MSG_EVENT            Event_Id_05

#define NPITASK_ALL_EVENTS                  (NPITASK_ICALL_EVENT | \
                                             NPITASK_FRAME_RX_EVENT | \
                                             NPITASK_TX_READY_EVENT | \
                                             NPITASK_SYNC_FRAME_RX_EVENT | \
                                             NPITASK_TX_DONE_EVENT | \
                                             NPITASK_REM_RDY_EVENT | \
                                             NPITASK_ASSERT_MSG_EVENT)
#else //!defined(ICALL_EVENTS) || defined(APP_EXTERNAL_CONTROL)
//! \brief ASYNC Message Received Event (no framing bytes)
#define NPITASK_FRAME_RX_EVENT              0x0008

//! \brief A framed message buffer is ready to be sent to the transport layer.
#define NPITASK_TX_READY_EVENT              0x0010

//! \brief ASYNC Message Received Event (no framing bytes)
#define NPITASK_SYNC_FRAME_RX_EVENT         0x0020

//! \brief Last TX message has been successfully sent
#define NPITASK_TX_DONE_EVENT               0x0040

//! \brief Remote Rdy received Event
#define NPITASK_REM_RDY_EVENT               0x0080

//! \brief NPI assert message
#define NPITASK_ASSERT_MSG_EVENT            0x0100
#endif //defined(ICALL_EVENTS) && !defined(APP_EXTERNAL_CONTROL)
//! \brief Task priority for NPI RTOS task
#define NPITASK_PRIORITY                    2

//! \brief NPI allows for NPI_MAX_SUBSTEM number of subsystems to be registered
//         to receive NPI messages from the host.
#define NPI_MAX_SUBSYSTEMS                  4
#define NPI_MAX_SS_ENTRY                    NPI_MAX_SUBSYSTEMS
#define NPI_MAX_ICALL_ENTRY                 NPI_MAX_SUBSYSTEMS

#define REM_RDY_ASSERTED                    0x00
#define REM_RDY_DEASSERTED                  0x01

#define NPI_ASSERT_MSG_LEN                  5

// ****************************************************************************
// typedefs
// ****************************************************************************
//! \brief When a subsystem registers with NPI Task to receive messages from
//         host, it must provide its subsystem ID along with a CallBack function
//         to handle all messages
typedef struct _npiFromHostTableEntry_t
{
    uint8_t             ssID;
    npiFromHostCBack_t  ssCB;
} _npiFromHostTableEntry_t;

//! \brief When a subsystem registers with NPI Task to receive messages from
//         ICall it must provide its subsystem ID along with a CallBack function
//         to handle all messages
typedef struct _npiFromICallTableEntry_t
{
    uint8_t              icallID;
    npiFromICallCBack_t ssCB;
} _npiFromICallTableEntry_t;

//*****************************************************************************
// globals
//*****************************************************************************
//! \brief RTOS task handle for NPI task
pthread_t npiTaskHandle;
uint8_t *npiTaskStack;

//! \brief Handle for the ASYNC TX Queue
static mqd_t      npiTxQueue;

//! \brief Handle for the ASYNC RX Queue
static mqd_t      npiRxQueue;

//! \brief Handle for the SYNC TX Queue
static mqd_t      npiSyncTxQueue;

//! \brief Handle for the SYNC RX Queue
static mqd_t      npiSyncRxQueue;

//! \brief Handle for the semaphore
sem_t *pNpiSem;

//! \brief Flag/Counter indicating a Synchronous REQ/RSP is currently being
//!        processed.
static int8_t syncTransactionInProgress = 0;

//! \brief Pointer to last tx message.  This is freed once confirmation is
//!        is received that the buffer has been transmitted
//!        (ie. NPITASK_TRANSPORT_TX_DONE_EVENT)
//!
static uint8_t *lastQueuedTxMsg;

#if !defined(ICALL_EVENTS) || defined(APP_EXTERNAL_CONTROL)
//! \brief Task pending events
static uint16_t NPITask_events = 0;

//! \brief Event flags for capturing Task-related events from ISR context
static uint16_t tlDoneISRFlag = 0;
static uint16_t remRdyISRFlag = 0;
#endif //!defined(ICALL_EVENTS) && !defined(APP_EXTERNAL_CONTROL)

//! \brief Routing table for translating incoming Host messages to the proper
//!        subsystem callback based on SSID of the message
_npiFromHostTableEntry_t HostToSSTable[NPI_MAX_SS_ENTRY];

#ifdef USE_ICALL
//! \brief Routing table for translating incoming ICall messages to the proper
//!        subsystem callback based on ICall msg source entity ID
_npiFromICallTableEntry_t ICallToSSTable[NPI_MAX_ICALL_ENTRY];
#endif //USE_ICALL

//! \brief Global flag to keep NPI from being opened twice without first closing
static uint8_t taskOpen = 0;

//! \brief Storage space for final assert NPI message
static uint8_t sendAssertMessage[NPI_ASSERT_MSG_LEN] =
{NPI_ASSERT_PAYLOAD_LEN,
 0, RPC_SYS_BLE_SNP+(NPI_MSG_TYPE_ASYNC<<5),
 NPI_ASSERT_CMD1_ID ,0};

//! \brief Save assert type locally for use in NPI assert message
static uint8_t npiTask_assertType;

/* Default NPI parameters structure */
const NPI_Params NPI_defaultParams = {
    .stackSize          = 1024,
    .bufSize            = 530,
    .mrdyGpioIndex      = 0/*IOID_UNUSED*/,
    .srdyGpioIndex      = 0/*IOID_UNUSED*/,
#if defined(NPI_USE_UART)
    .portType           = NPI_SERIAL_TYPE_UART,
    .portBoardID        = 0,                     /* CC2650_UART0 */
#elif defined(NPI_USE_SPI)
    .portType           = NPI_SERIAL_TYPE_SPI,
#if (defined(CC2650DK_5XD) ||  defined(CC2650DK_4XS )) && !defined(TI_DRIVERS_DISPLAY_INCLUDED)
    .portBoardID        = 0,                     /* CC2650_SPI0, conflicts with SRF06 display so both can't be enabled */
#elif (defined(CC2650DK_7ID) || defined(CC2650_LAUNCHXL) || \
    defined(CC2640R2_LAUNCHXL) || defined(CC26X2R1_LAUNCHXL) || defined(CC2652RB_LAUNCHXL) || \
    defined(CC13X2R1_LAUNCHXL) || (defined (CC13X2P1_LAUNCHXL) || defined (CC13X2P_2_LAUNCHXL) || defined (CC13X2P_4_LAUNCHXL) || \
    defined (CC2652PSIP_LP) || defined (CC2652RSIP_LP)) || defined (CC2652R7_LP) || defined (CC1352P7_1_LP) || \
    defined (CC1352P7_4_LP)) || defined (CC2651P3_LP) || defined (CC2651R3_LP)
    .portBoardID        = 1,                     /* CC2650_SPI1 */
#elif (defined(CC2650DK_5XD) || defined(CC2650DK_4XS)) && defined(TI_DRIVERS_DISPLAY_INCLUDED)
#error "WARNING! CC2650_SPI0, is used to drive the SmartRF06 display. Cannot use SPI0 if display is enabled."
#endif

#endif
};

//*****************************************************************************
// function prototypes
//*****************************************************************************

//! \brief Callback function registered with Transport Layer
static void NPITask_transportDoneCallBack(uint16_t sizeRx, uint16_t sizeTx);

//! \brief Callback function registered with Transport Layer
static void NPITask_RemRdyEventCB(uint8_t state);

const npiTLCallBacks transportCBs = {
  &NPITask_RemRdyEventCB,
  &NPITask_transportDoneCallBack,
};

//! \brief ASYNC TX Q Processing function.
static void NPITask_ProcessTXQ(mqd_t txQ);

//! \brief ASYNC RX Q Processing function.
static void NPITask_processRXQ(void);

//! \brief SYNC RX Q Processing function.
static void NPITask_processSyncRXQ(void);

//! \brief Function to route NPI Message to the appropriate subsystem
static uint8_t NPITask_routeHostToSS(_npiFrame_t *pNPIMsg);

#ifdef USE_ICALL
//! \brief Function to route ICall Message to the appropriate subsystem
static uint8_t NPITask_routeICallToSS(ICall_ServiceEnum src, uint8_t *pGenMsg);
#endif //USE_ICALL

//! \brief Function that transforms NPI Frame struct into a byte array. This is
//         necessary to send data over NPI Transport Layer
static uint8_t * NPITask_SerializeFrame(_npiFrame_t *pNPIMsg);

//! \brief Function that transforms byte contents of NPI RxBuf into an NPI Frame
//!        struct that can be routed.
static void NPITask_DeserializeFrame(_npiFrame_t *pMsg);

// -----------------------------------------------------------------------------
//! \brief      NPI main event processing loop.
//!
//! \return     void
// -----------------------------------------------------------------------------
void *NPITask_Fxn(void *arg)
{
  _npiFrame_t temp;
#if defined(ICALL_EVENTS) && defined(APP_EXTERNAL_CONTROL)
  ICall_ServiceEnum stackid;
  ICall_EntityID dest;
  uint8_t *pMsg;

  /* Register the calling context in the icall */
  ICall_SyncHandle syncEvent_dummy;
  ICall_EntityID icall_entity_dummy;
  ICall_registerApp(&icall_entity_dummy, &syncEvent_dummy);
#endif // defined(ICALL_EVENTS) && defined(APP_EXTERNAL_CONTROL)

  /* Forever loop */
  for (;;)
  {
      sem_wait(pNpiSem);
#if !defined(ICALL_EVENTS) || defined(APP_EXTERNAL_CONTROL)
      _npiCSKey_t key;
      // Capture the ISR events flags now within a critical section.
      // We do this to avoid possible race conditions where the ISR is
      // modifying the event mask while the task is read/writing it.
      key = NPIUtil_EnterCS();

      NPITask_events = NPITask_events | tlDoneISRFlag |
               remRdyISRFlag;

      tlDoneISRFlag = 0;
      remRdyISRFlag = 0;
      NPIUtil_ExitCS(key);
#endif //defined(ICALL_EVENTS) && !defined(APP_EXTERNAL_CONTROL)

      // First check and Send NPI assert message
      if (NPITask_events & NPITASK_ASSERT_MSG_EVENT)
      {
        NPITask_sendAssertMsg(npiTask_assertType);
      }

      // Remote RDY event
      if (NPITask_events & NPITASK_REM_RDY_EVENT)
      {
#if !defined(ICALL_EVENTS) || defined(APP_EXTERNAL_CONTROL)
        key = NPIUtil_EnterCS();
        NPITask_events &= ~NPITASK_REM_RDY_EVENT;
        NPIUtil_ExitCS(key);

#endif //defined(ICALL_EVENTS) && !defined(APP_EXTERNAL_CONTROL)
#if (NPI_FLOW_CTRL == 1)
        NPITL_handleRemRdyEvent();
#endif // NPI_FLOW_CTRL = 1
      }
      // TX Frame has been successfully sent
      if (NPITask_events & NPITASK_TX_DONE_EVENT)
      {
        //Deallocate most recent message being transmitted.
        NPIUtil_free(lastQueuedTxMsg);
        lastQueuedTxMsg = NULL;
#if !defined(ICALL_EVENTS) || defined(APP_EXTERNAL_CONTROL)
        key = NPIUtil_EnterCS();
        NPITask_events &= ~NPITASK_TX_DONE_EVENT;
        NPIUtil_ExitCS(key);

#endif //defined(ICALL_EVENTS) && !defined(APP_EXTERNAL_CONTROL)
      }
      // Frame is ready to send to the Host
      if (NPITask_events & NPITASK_TX_READY_EVENT)
      {
        // Cannot send if NPI Tl is already busy.
        if (!NPITL_checkNpiBusy())
        {
          // Check for outstanding SYNC REQ/RSP transactions.  If so,
          // this ASYNC message must remain Q'd while we wait for the
          // SYNC RSP.
          if ((mq_peek(npiSyncTxQueue, (char *)&temp, sizeof(_npiFrame_t), 0) == 0) &&
              syncTransactionInProgress >= 0)
          {
            // Prioritize Synchronous traffic
            NPITask_ProcessTXQ(npiSyncTxQueue);
          }
          else if (!(NPITask_events & NPITASK_SYNC_FRAME_RX_EVENT) &&
                 syncTransactionInProgress == 0 &&
                 (mq_peek(npiTxQueue, (char *)&temp, sizeof(_npiFrame_t), 0) == 0))
          {
            // No outstanding SYNC REQ/RSP transactions, process
            // ASYNC messages.
            NPITask_ProcessTXQ(npiTxQueue);
          }
        }

        // The TX READY event flag can be cleared here regardless
        // of the state of the TX queues. The TX done call back
        // will always check the state of the queues and reset
        // the event flag if it discovers more messages to send
        // In this control flow, either a TX message was sent,
        // there is a pending sync tx message to be sent which
        // is blocking any async tx message(s), or NPI was already
        // sending a frame (aka busy). In either case,
        // the TX event will get set again when NPI is done sending (or
        // no longer busy) or after the blocking synchronous message
        // has been sent
#if !defined(ICALL_EVENTS) || defined(APP_EXTERNAL_CONTROL)
        key = NPIUtil_EnterCS();
        NPITask_events &= ~NPITASK_TX_READY_EVENT;
        NPIUtil_ExitCS(key);

#endif //defined(ICALL_EVENTS) && !defined(APP_EXTERNAL_CONTROL)
      }
#ifdef USE_ICALL
      // ICall Message Event
      if (ICall_fetchServiceMsg(&stackid, &dest, (void **) &pMsg)
        == ICALL_ERRNO_SUCCESS)
      {
        // Route the ICall message to the appropriate subsystem
        if (NPITask_routeICallToSS(stackid,pMsg) != NPI_SUCCESS)
        {
          // Unable to route message. Subsystem not registered.
          // Free message
          ICall_freeMsg(pMsg);
        }
      }
#endif //USE_ICALL
      // Synchronous Frame received from Host
      if (NPITask_events & NPITASK_SYNC_FRAME_RX_EVENT &&
          syncTransactionInProgress <= 0)
      {
        // Process Queue and clear event flag
        NPITask_processSyncRXQ();
#if !defined(ICALL_EVENTS) || defined(APP_EXTERNAL_CONTROL)
        key = NPIUtil_EnterCS();
        NPITask_events &= ~NPITASK_SYNC_FRAME_RX_EVENT;
        NPIUtil_ExitCS(key);

#endif //defined(ICALL_EVENTS) && !defined(APP_EXTERNAL_CONTROL)
        if (mq_peek(npiSyncRxQueue, (char *)&temp, sizeof(_npiFrame_t), 0) == 0)
        {
          // Queue is not empty so reset flag to process remaining
          // frame(s)
          key = NPIUtil_EnterCS();
          NPITask_events |= NPITASK_SYNC_FRAME_RX_EVENT;
          sem_post(pNpiSem);
          NPIUtil_ExitCS(key);

        }
      }

      // A complete frame (msg) has been received and is ready for handling
      if (NPITask_events & NPITASK_FRAME_RX_EVENT &&
          syncTransactionInProgress == 0)
      {
        // Process the ASYNC message and clear event flag
        NPITask_processRXQ();
#if !defined(ICALL_EVENTS) || defined(APP_EXTERNAL_CONTROL)
        key = NPIUtil_EnterCS();
        NPITask_events &= ~NPITASK_FRAME_RX_EVENT;
        NPIUtil_ExitCS(key);
#endif // !defined(ICALL_EVENTS) || defined(APP_EXTERNAL_CONTROL)
        if (mq_peek(npiRxQueue, (char *)&temp, sizeof(_npiFrame_t), 0) == 0)
        {
#if !defined(ICALL_EVENTS) || defined(APP_EXTERNAL_CONTROL)
          key = NPIUtil_EnterCS();
          // Q is not empty reset flag and process next message
          NPITask_events |= NPITASK_FRAME_RX_EVENT;
          sem_post(pNpiSem);
          NPIUtil_ExitCS(key);
#endif //!defined(ICALL_EVENTS) || defined(APP_EXTERNAL_CONTROL)
        }
      }
    }
}


// -----------------------------------------------------------------------------
// Exported Functions

// -----------------------------------------------------------------------------
//! \brief      Initialize a NPI_Params struct with default values
//!
//! \param[in]  portType  NPI_SERIAL_TYPE_[UART,SPI]
//! \param[in]  params    Pointer to NPI params to be initialized
//!
//! \return     uint8_t   Status NPI_SUCCESS, or NPI_TASK_INVALID_PARAMS
// -----------------------------------------------------------------------------
uint8_t NPITask_Params_init(uint8_t portType, NPI_Params *params)
{
  if (params != NULL)
  {
    *params = NPI_defaultParams;

#if defined(NPI_USE_UART)
    UART2_Params_init(&params->portParams.uartParams);
    params->portParams.uartParams.readMode = UART2_Mode_CALLBACK;
    params->portParams.uartParams.writeMode = UART2_Mode_CALLBACK;
#elif defined(NPI_USE_SPI)
    SPI_Params_init(&params->portParams.spiParams);
    params->portParams.spiParams.mode = SPI_SLAVE;
    params->portParams.spiParams.bitRate = 8000000;
    params->portParams.spiParams.frameFormat = SPI_POL1_PHA1;
#endif //NPI_USE_UART

    return NPI_SUCCESS;
  }

  return NPI_TASK_INVALID_PARAMS;
}

// -----------------------------------------------------------------------------
//! \brief      Task creation function for NPI
//!
//! \param[in]  params    Pointer to NPI params which will be used to
//!                       initialize the NPI Task
//!
//! \return     uint8_t   Status NPI_SUCCESS, or NPI_TASK_FAILURE
// -----------------------------------------------------------------------------
uint8_t NPITask_open(NPI_Params *params)
{
    NPITL_Params transportParams;
    NPI_Params npiParams;

    // Check to see if NPI has already been opened
    if (taskOpen)
    {
      return NPI_TASK_FAILURE;
    }

    taskOpen = 1;

    // If params are NULL use defaults.
    if (params == NULL) {
#if defined(NPI_USE_UART)
        NPITask_Params_init(NPI_SERIAL_TYPE_UART,&npiParams);
#elif defined(NPI_USE_SPI)
        NPITask_Params_init(NPI_SERIAL_TYPE_SPI,&npiParams);
#endif // NPI_USE_UART

        params = &npiParams;
    }

    // Initialize globals
#if !defined(ICALL_EVENTS) || defined(APP_EXTERNAL_CONTROL)
    NPITask_events = 0;
#endif // !defined(ICALL_EVENTS) || defined(APP_EXTERNAL_CONTROL)
    lastQueuedTxMsg = NULL;

    // create Semaphore instance
    pNpiSem = (sem_t *)NPIUtil_malloc(sizeof(sem_t));
    sem_init(pNpiSem, 0, 0);

    // create Queue instances
    NPIUtil_createPQueue(&npiTxQueue,     "NPI Tx Queue",      NPI_TX_QUEUE_SIZE,      sizeof(_npiFrame_t), O_NONBLOCK);
    NPIUtil_createPQueue(&npiRxQueue,     "NPI Rx Queue",      NPI_RX_QUEUE_SIZE,      sizeof(_npiFrame_t), O_NONBLOCK);
    NPIUtil_createPQueue(&npiSyncRxQueue, "NPI Sync Tx Queue", NPI_SYNC_TX_QUEUE_SIZE, sizeof(_npiFrame_t), O_NONBLOCK);
    NPIUtil_createPQueue(&npiSyncTxQueue, "NPI Sync Rx Queue", NPI_SYNC_RX_QUEUE_SIZE, sizeof(_npiFrame_t), O_NONBLOCK);

    // Initialize Transport Layer
    transportParams.npiTLBufSize = params->bufSize;
    transportParams.mrdyGpioIndex = params->mrdyGpioIndex;
    transportParams.srdyGpioIndex = params->srdyGpioIndex;
    transportParams.portType = params->portType;
    transportParams.portBoardID = params->portBoardID;
    transportParams.portParams = params->portParams;
    transportParams.npiCallBacks = transportCBs;

    if(NPITL_openTL(&transportParams)!=NPI_SUCCESS)
    {
        return NPI_TASK_FAILURE;
    }

   // Clear Routing Tables
    memset(HostToSSTable, 0, sizeof(_npiFromHostTableEntry_t)*NPI_MAX_SS_ENTRY);
#ifdef USE_ICALL
    memset(ICallToSSTable, 0, sizeof(_npiFromICallTableEntry_t)*NPI_MAX_ICALL_ENTRY);
#endif //USE_ICALL

    // Configure and create the NPI task.
    npiTaskStack = NPIUtil_malloc(params->stackSize);
    if (npiTaskStack == NULL)
    {
      return NPI_TASK_FAILURE;
    }
    int ret = NPIUtil_createPTask(&npiTaskHandle, NPITask_Fxn, NPITASK_PRIORITY, npiTaskStack, params->stackSize);
    if (ret != 0)
    {
      return NPI_TASK_FAILURE;
    }

    return NPI_SUCCESS;
}

// -----------------------------------------------------------------------------
//! \brief      NPI Task close and tear down. Cannot be used with ICall because
//!             ICall service cannot be un-enrolled
//!
//! \return     uint8_t   Status NPI_SUCCESS, or NPI_TASK_FAILURE
// -----------------------------------------------------------------------------
uint8_t NPITask_close(void)
{
#ifdef USE_ICALL
    return NPI_TASK_FAILURE;
#else
    if (!taskOpen)
    {
      return NPI_TASK_FAILURE;
    }

    // Close Tranpsort Layer
    NPITL_closeTL();

    // Delete RTOS allocated structures
    mq_close(npiTxQueue);
    mq_close(npiRxQueue);
    mq_close(npiSyncRxQueue);
    mq_close(npiSyncTxQueue);

    // Delete semaphore
    sem_destroy(pNpiSem);
    NPIUtil_free((uint8_t *)pNpiSem);

    // Free any message buffers for in-flight messages
    if (lastQueuedTxMsg != NULL)
    {
      NPIUtil_free(lastQueuedTxMsg);
    }

    // Delete NPI task
    NPIUtil_free(npiTaskStack);
    pthread_exit(0);
    taskOpen = 0;

    return NPI_SUCCESS;
#endif //USE_ICALL
}

// -----------------------------------------------------------------------------
//! \brief      API for application task to send a message to the Host.
//!             NOTE: It's assumed all message traffic to the stack will use
//!             other (ICALL) APIs/Interfaces.
//!
//! \param[in]  pNpiFrame    Pointer to "unframed" message buffer.
//!
//! \return     uint8_t Status NPI_SUCCESS, or NPI_SS_NOT_FOUND
// -----------------------------------------------------------------------------
uint8_t NPITask_sendToHost(_npiFrame_t *pNpiFrame)
{
    uint8_t MsgType = NPI_GET_MSG_TYPE(pNpiFrame);
    uint8_t status = NPI_SUCCESS;
    _npiCSKey_t key;

    // Must block task pre-emption so that the higher priority NPI task
    // does not clear the NPITask_events flag before pNpiFrame is properly enqueued.

    switch (MsgType)
    {
        // Enqueue to appropriate NPI Task Q and post corresponding event.
        case NPI_MSG_TYPE_SYNCRSP:
        case NPI_MSG_TYPE_SYNCREQ:
        {
            mq_send(npiSyncTxQueue, (char*)pNpiFrame, sizeof(_npiFrame_t), 1);
            key = NPIUtil_EnterCS();
            NPITask_events |= NPITASK_TX_READY_EVENT;
            sem_post(pNpiSem);
            NPIUtil_ExitCS(key);
        }
        break;
        case NPI_MSG_TYPE_ASYNC:
        {
            mq_send(npiTxQueue, (char*)pNpiFrame, sizeof(_npiFrame_t), 1);
            key = NPIUtil_EnterCS();
            NPITask_events |= NPITASK_TX_READY_EVENT;
            sem_post(pNpiSem);
            NPIUtil_ExitCS(key);
        }
        break;
        default:
            status = NPI_INVALID_PKT;
        break;
    }

    return status;
}

// -----------------------------------------------------------------------------
//! \brief      API for subsystems to register for NPI messages received with
//!             the specific ssID. All NPI messages will be passed to callback
//!             provided
//!
//! \param[in]  ssID    The subsystem ID of NPI messages that should be routed
//!                     to pCB
//! \param[in]  pCB     The call back function that will receive NPI messages
//!
//! \return     uint8_t Status NPI_SUCCESS, or NPI_ROUTING_FULL
// -----------------------------------------------------------------------------
uint8_t NPITask_regSSFromHostCB(uint8_t ssID, npiFromHostCBack_t pCB)
{
    uint8_t i;

    for (i = 0; i < NPI_MAX_SS_ENTRY; i++)
    {
        if (HostToSSTable[i].ssCB == NULL)
        {
            HostToSSTable[i].ssID = ssID;
            HostToSSTable[i].ssCB = pCB;
            return NPI_SUCCESS;
        }
    }

    return NPI_ROUTING_FULL;
}

#ifdef USE_ICALL
// -----------------------------------------------------------------------------
//! \brief      API for subsystems to register for ICall messages received from
//!             the specific source entity ID. All ICall messages will be passed
//!             to the callback provided
//!
//! \param[in]  icallID Source entity ID whose messages should be sent to pCB
//!             pCB     The call back function that will receive ICall messages
//!
//! \return     uint8_t Status NPI_SUCCESS, or NPI_ROUTING_FULL
// -----------------------------------------------------------------------------
uint8_t NPITask_regSSFromICallCB(uint8_t icallID, npiFromICallCBack_t pCB)
{
    uint8_t i;

    for (i = 0; i < NPI_MAX_ICALL_ENTRY; i++)
    {
        if (ICallToSSTable[i].ssCB == NULL)
        {
            ICallToSSTable[i].icallID = icallID;
            ICallToSSTable[i].ssCB = pCB;

            return NPI_SUCCESS;
        }
    }

    return NPI_ROUTING_FULL;
}
#endif //USE_ICALL

// -----------------------------------------------------------------------------
// Routing Functions

// -----------------------------------------------------------------------------
//! \brief      Function to route NPI Message to the appropriate subsystem
//!
//! \param[in]  pNPIMsg Pointer to message that will be routed
//!
//! \return     uint8_t Status NPI_SUCCESS, or NPI_SS_NOT_FOUND
// -----------------------------------------------------------------------------
static uint8_t NPITask_routeHostToSS(_npiFrame_t *pNPIMsg)
{
    uint8_t i;
    uint8_t ssIDtoRoute = NPI_GET_SS_ID(pNPIMsg);

    for (i = 0; i < NPI_MAX_SS_ENTRY; i++)
    {
        if (ssIDtoRoute == HostToSSTable[i].ssID)
        {
            HostToSSTable[i].ssCB(pNPIMsg);

            return NPI_SUCCESS;
        }
    }

    return NPI_SS_NOT_FOUND;
}

#ifdef USE_ICALL
// -----------------------------------------------------------------------------
//! \brief      Function to route ICall Message to the appropriate subsystem
//!
//! \param[in]  src     ICall Message source Entity ID
//! \param[in]  pGenMsg Pointer to generic ICall message that will be routed
//!
//! \return     uint8_t Status NPI_SUCCESS, or NPI_SS_NOT_FOUND
// -----------------------------------------------------------------------------
static uint8_t NPITask_routeICallToSS(ICall_ServiceEnum src, uint8_t *pGenMsg)
{
    uint8_t i;

    for (i = 0; i < NPI_MAX_ICALL_ENTRY; i++)
    {
        if (src == ICallToSSTable[i].icallID)
        {
            ICallToSSTable[i].ssCB(pGenMsg);
            return NPI_SUCCESS;
        }
    }
    return NPI_SS_NOT_FOUND;
}
#endif //USE_ICALL

// -----------------------------------------------------------------------------
//! \brief      API to de-allocate an NPI frame
//!
//! \param[in]  frame   Pointer to NPI frame to be de-allocated
//!
//! \return     void
// -----------------------------------------------------------------------------
void NPITask_freeFrameData(_npiFrame_t *npiFrame)
{
  if ((npiFrame->dataLen) && (NULL != npiFrame->pData))
  {
    NPIUtil_free(npiFrame->pData);
  }
}

// -----------------------------------------------------------------------------
// Serialize and Deserialize functions

// -----------------------------------------------------------------------------
//! \brief      Function creates a byte array from an NPI Frame struct
//!
//!             *** Whoever calls this function is responsible for freeing the
//!             the created byte array
//!
//! \param[in]  pNPIMsg     Pointer to message that will be serialized
//!
//! \return     uint8_t*    Equivalent byte array of the NPI Frame
// -----------------------------------------------------------------------------
static uint8_t * NPITask_SerializeFrame(_npiFrame_t *pNPIMsg)
{
    uint8_t *pSerMsg;

    // Allocate byte array for the entire message
    pSerMsg = NPIUtil_malloc(pNPIMsg->dataLen + NPI_MSG_HDR_LENGTH);

    if (pSerMsg != NULL)
    {
        // Packet Format [ Len1 ][ Len0 ][ Cmd0 ][ Cmd 1 ][ Data Payload ]
        // Fill in Header
        pSerMsg[0] = (uint8)(pNPIMsg->dataLen & 0xFF);
        pSerMsg[1] = (uint8)(pNPIMsg->dataLen >> 8);
        pSerMsg[2] = pNPIMsg->cmd0;
        pSerMsg[3] = pNPIMsg->cmd1;

        // Copy Data Payload
        memcpy(&pSerMsg[4],pNPIMsg->pData,pNPIMsg->dataLen);
    }
    else
    {
      HAL_ASSERT(HAL_ASSERT_CAUSE_OUT_OF_MEMORY);
    }

    return pSerMsg;
}


// -----------------------------------------------------------------------------
//! \brief      Function creates an NPI Frame struct from RxBuf contents
//!
//!             *** Whoever calls this function is responsible for freeing the
//!             the created NPI Frame struct
//!
//! \return     _npiFrame_t*  Equivalent NPI Frame of RxBuf
// -----------------------------------------------------------------------------
static void NPITask_DeserializeFrame(_npiFrame_t *pMsg)
{
    uint16_t datalen;
    uint8_t ch;

    // Function assumes the following packet structure is in RxBuf:
    // [ Len1 ][ Len0 ][ Cmd0 ][ Cmd 1 ][ Data Payload ]
    // It also assumes that npiRxBufHead points to the Len1 byte
    NPITL_readTL(&ch, 1);
    datalen = ch;
    NPITL_readTL(&ch, 1);
    datalen += (ch << 8);

    // Allocate memory for NPI Frame

    pMsg->dataLen = datalen;
    // Assign CMD0 and CMD1 bytes
    NPITL_readTL(&(pMsg->cmd0), 1);
    NPITL_readTL(&(pMsg->cmd1), 1);

    if (datalen)
    {
        pMsg->pData = NPIUtil_malloc(datalen);
        if (pMsg->pData != NULL)
        {
            // Copy Data payload
            NPITL_readTL(pMsg->pData, pMsg->dataLen);
        }
    }
    else
    {
      pMsg->pData = NULL;
    }
}

// -----------------------------------------------------------------------------
// "Processor" functions

// -----------------------------------------------------------------------------
//! \brief      Dequeue next message in the ASYNC TX Queue and send to serial
//!             interface.
//!
//! \param[in]  txQ    queue handle to be processed
//!
//! \return     void
// -----------------------------------------------------------------------------
static void NPITask_ProcessTXQ(mqd_t txQ)
{
    _npiFrame_t npiFrame;

    if (mq_receive(txQ, (char*)&npiFrame, sizeof(_npiFrame_t), NULL) == sizeof(_npiFrame_t))
    {
        // Serialize NPI Frame to be sent over Transport Layer
        if(lastQueuedTxMsg != NULL)
        {
          NPIUtil_free(lastQueuedTxMsg);
          lastQueuedTxMsg = NULL;
        }
        lastQueuedTxMsg = NPITask_SerializeFrame(&npiFrame);

        if (lastQueuedTxMsg != NULL)
        {
          // Write byte array over Transport Layer
          // We have already checked if TL is busy so we assume write succeeds
          NPITL_writeTL(lastQueuedTxMsg, npiFrame.dataLen + NPI_MSG_HDR_LENGTH);
          // If the message is a synchronous response or request
          if (NPI_GET_MSG_TYPE((&npiFrame)) == NPI_MSG_TYPE_SYNCREQ ||
              NPI_GET_MSG_TYPE((&npiFrame)) == NPI_MSG_TYPE_SYNCRSP)
          {
              // Decrement the outstanding Sync REQ/RSP flag.
              syncTransactionInProgress--;
          }
        }
        NPITask_freeFrameData(&npiFrame);
    }
}

// -----------------------------------------------------------------------------
//! \brief      Dequeue next message in the RX Queue and process it.
//!
//! \return     void
// -----------------------------------------------------------------------------
static void NPITask_processRXQ(void)
{
    _npiFrame_t npiFrame;

    if (mq_receive(npiRxQueue, (char*)&npiFrame, sizeof(_npiFrame_t), NULL) == sizeof(_npiFrame_t))
    {
        // Route to SS based on ID in message

        if (NPI_SUCCESS != NPITask_routeHostToSS(&npiFrame))
        {
            // No subsystem registered to handle message. Free NPI Frame
          NPITask_freeFrameData(&npiFrame);
        }
    }
}

// -----------------------------------------------------------------------------
//! \brief      Dequeue next message in the RX Queue and process it.
//!
//! \return     void
// -----------------------------------------------------------------------------
static void NPITask_processSyncRXQ(void)
{
    _npiFrame_t npiFrame;

    // Sync transaction of 0 means no synchronous transaction is in progress
    // A value of less than zero means that we have sent a sync message and
    // are waiting on the reply
    if (syncTransactionInProgress <= 0)
    {
        if (mq_receive(npiSyncRxQueue, (char*)&npiFrame, sizeof(_npiFrame_t), NULL) == sizeof(_npiFrame_t))
        {
            // Route to SS based on ID in message
            if (NPI_SUCCESS == NPITask_routeHostToSS(&npiFrame))
            {
              // Increment the outstanding Sync REQ/RSP flag.
              syncTransactionInProgress++;
            }
            else
            {
              NPITask_freeFrameData(&npiFrame);
            }
        }
    }
}

// -----------------------------------------------------------------------------
// Call Back Functions

// -----------------------------------------------------------------------------
//! \brief      Transaction Done callback provided to Transport Layer for
//!
//! \param[in]  uint16_t    Number of bytes received.
//! \param[in]  uint16_t    Number of bytes transmitted.
//!
//! \return     void
// -----------------------------------------------------------------------------
volatile uint8_t txcomplete = 0;
volatile uint8_t rxcomplete = 0;
static void NPITask_transportDoneCallBack(uint16_t sizeRx, uint16_t sizeTx)
{
    _npiFrame_t npiFrame;
    _npiCSKey_t key;

    if (sizeRx != 0)
    {
        rxcomplete++;
        // De-serialize byte array into NPI frame struct
        NPITask_DeserializeFrame(&npiFrame);

        switch (NPI_GET_MSG_TYPE((&npiFrame)))
        {
            // Enqueue to appropriate NPI Task Q and post corresponding event.
            case NPI_MSG_TYPE_SYNCREQ:
            case NPI_MSG_TYPE_SYNCRSP:
            {
                mq_send(npiSyncRxQueue, (char*)&npiFrame, sizeof(_npiFrame_t), 1);
                key = NPIUtil_EnterCS();
                tlDoneISRFlag |= NPITASK_SYNC_FRAME_RX_EVENT;
                sem_post(pNpiSem);
                NPIUtil_ExitCS(key);

                break;
            }
            case NPI_MSG_TYPE_ASYNC:
            {
                mq_send(npiRxQueue, (char*)&npiFrame, sizeof(_npiFrame_t), 1);
                key = NPIUtil_EnterCS();
                tlDoneISRFlag |= NPITASK_FRAME_RX_EVENT;
                sem_post(pNpiSem);
                NPIUtil_ExitCS(key);

                break;
            }
            default:
            {
                // Unexpected Msg Type. Free Msg.
                NPITask_freeFrameData(&npiFrame);
                break;
            }
        }
    }

    if (sizeTx && lastQueuedTxMsg)
    {
      txcomplete++;
      key = NPIUtil_EnterCS();
      tlDoneISRFlag |= NPITASK_TX_DONE_EVENT;
      sem_post(pNpiSem);
      NPIUtil_ExitCS(key);
    }

    // Check to see if there pending messages waiting to be sent
     // If there are then notify NPI Task by setting TX READY event flag
  _npiFrame_t temp;
    if ((mq_peek(npiSyncTxQueue, (char *)&temp, sizeof(_npiFrame_t), 0) == 0) ||
        (mq_peek(npiTxQueue,     (char *)&temp, sizeof(_npiFrame_t), 0) == 0))
    {
      key = NPIUtil_EnterCS();
      tlDoneISRFlag |= NPITASK_TX_READY_EVENT;
      sem_post(pNpiSem);
      NPIUtil_ExitCS(key);
    }
}

// -----------------------------------------------------------------------------
//! \brief      RX Callback provided to Transport Layer for REM RDY Event
//!
//! \return     void
// -----------------------------------------------------------------------------
static void NPITask_RemRdyEventCB(uint8_t state)
{
  _npiCSKey_t key;

  if (state == REM_RDY_ASSERTED)
  {
    key = NPIUtil_EnterCS();
    remRdyISRFlag = NPITASK_REM_RDY_EVENT;
    sem_post(pNpiSem);
    NPIUtil_ExitCS(key);
  }
#ifdef NPI_CENTRAL
  else if (state == REM_RDY_DEASSERTED)
  {
    // There could be pending TX messages that are waiting for Remote Ready
    // signal to be deasserted so that NPI is no longer busy
    _npiFrame_t temp;
    if ((mq_peek(npiSyncTxQueue, (char *)&temp, sizeof(_npiFrame_t), 0) == 0) ||
        (mq_peek(npiTxQueue, (char *)&temp, sizeof(_npiFrame_t), 0) == 0))
    {
        key = NPIUtil_EnterCS();
        remRdyISRFlag |= NPITASK_TX_READY_EVENT;
        sem_post(pNpiSem);
        NPIUtil_ExitCS(key);
    }
  }
#endif //NPI_CENTRAL
}

// -----------------------------------------------------------------------------
//! \brief   Send a final out of memory message if there is an allocation
//!          failure to Host.
// -----------------------------------------------------------------------------
void NPITask_sendAssertMsg(uint8_t assertMsg)
{
  // Disable all task switching and interrupts
  Hwi_disable();
  Swi_disable();
  TaskP_disableScheduler();

  // Setup and send final assert message via NPI
  sendAssertMessage[4] = assertMsg;
  NPITL_writeBypassSafeTL(&sendAssertMessage[0], NPI_ASSERT_MSG_LEN);

  // Spinlock doesn't work when power savings enabled
#ifndef POWER_SAVING
  HAL_ASSERT_SPINLOCK;
#endif
}

// -----------------------------------------------------------------------------
//! \brief   change the NPI header of the pre-defined Assert NPI message
//!          currently hardcoded set to {RPC_SYS_BLE_SNP, NPI_ASSERT_CMD1_ID}.
// -----------------------------------------------------------------------------
void NPITask_chgAssertHdr(uint8_t npi_cmd0, uint8_t npi_cmd1)
{
  _npiCSKey_t key;

  key = NPIUtil_EnterCS();

  sendAssertMessage[2] = npi_cmd0;
  sendAssertMessage[3] = npi_cmd1;

  NPIUtil_ExitCS(key);
}

// -----------------------------------------------------------------------------
//! \brief   Trigger a final NPI message for certain assert events.
// -----------------------------------------------------------------------------
#if defined(ICALL_EVENTS) && !defined(APP_EXTERNAL_CONTROL)
void NPIData_postAssertNpiMsgEvent(uint8_t assertType)
{
  npiTask_assertType = assertType;
  if (syncEvent == NULL)
  {
    //NPI still not opened
    return;
  }
  Event_post(syncEvent, NPITASK_ASSERT_MSG_EVENT);
}
#else // !defined(ICALL_EVENTS) || defined(APP_EXTERNAL_CONTROL)
void NPIData_postAssertNpiMsgEvent(uint8_t assertType)
{
  npiTask_assertType = assertType;

  _npiCSKey_t key;
  key = NPIUtil_EnterCS();
  NPITask_events |= NPITASK_ASSERT_MSG_EVENT;
  sem_post(pNpiSem);
  NPIUtil_ExitCS(key);
}
#endif
