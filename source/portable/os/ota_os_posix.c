/*
 * FreeRTOS OTA V1.2.0
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://aws.amazon.com/freertos
 * http://www.FreeRTOS.org
 */

/* Standard Includes.*/
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

/* Posix includes. */
#include <sys/types.h>
#include <mqueue.h>

/* OTA OS POSIX Interface Includes.*/
#include "ota_os_posix.h"

/* OTA Library include. */
#include "ota.h"
#include "ota_private.h"

/* OTA Event queue attributes.*/
#define OTA_QUEUE_NAME    "/otaqueue"
#define MAX_MESSAGES      10
#define MAX_MSG_SIZE      sizeof( OtaEventMsg_t )

/* OTA Event queue attributes.*/
static mqd_t otaEventQueue;

/* OTA Timer.*/
timer_t otaTimer;

extern int errno;

OtaOsStatus_t Posix_OtaInitEvent( OtaEventContext_t * pEventCtx )
{
    ( void ) pEventCtx;

    OtaOsStatus_t otaErrRet = OtaOsSuccess;
    struct mq_attr attr;

    /* Unlink the event queue.*/
    mq_unlink( OTA_QUEUE_NAME );

    /* Initialize queue attributes.*/
    attr.mq_flags = 0;
    attr.mq_maxmsg = MAX_MESSAGES;
    attr.mq_msgsize = MAX_MSG_SIZE;
    attr.mq_curmsgs = 0;

    /* Open the event queue.*/
    otaEventQueue = mq_open( OTA_QUEUE_NAME, O_CREAT | O_RDWR, S_IRWXU, &attr );

    if( otaEventQueue == -1 )
    {
        otaErrRet = OtaOsEventQueueCreateFailed;

        LogError( ( "Failed to create OTA Event Queue: "
                    "mq_open returned error: "
                    "otaErrRet=%i "
                    ",errno=%s",
                    otaErrRet,
                    strerror( errno ) ) );
    }
    else
    {
        LogDebug( ( "OTA Event Queue created." ) );
    }

    return otaErrRet;
}

OtaOsStatus_t Posix_OtaSendEvent( OtaEventContext_t * pEventCtx,
                                  const void * pEventMsg,
                                  unsigned int timeout )
{
    ( void ) pEventCtx;
    ( void ) timeout;

    OtaOsStatus_t otaErrRet = OtaOsSuccess;

    /* Send the event to OTA event queue.*/
    if( mq_send( otaEventQueue, pEventMsg, MAX_MSG_SIZE, 0 ) == -1 )
    {
        otaErrRet = OtaOsEventQueueSendFailed;

        LogError( ( "Failed to send event to OTA Event Queue: "
                    "mq_send returned error: "
                    "otaErrRet=%i "
                    ",errno=%s",
                    otaErrRet,
                    strerror( errno ) ) );
    }
    else
    {
        LogDebug( ( "OTA Event Sent." ) );
    }

    return otaErrRet;
}

OtaOsStatus_t Posix_OtaReceiveEvent( OtaEventContext_t * pContext,
                                     void * pEventMsg,
                                     uint32_t timeout )
{
    ( void ) pContext;
    ( void ) timeout;

    OtaOsStatus_t otaErrRet = OtaOsSuccess;

    char * pDst = pEventMsg;
    char buff[ MAX_MSG_SIZE ];

    /* Receive the next event from OTA event queue.*/
    if( mq_receive( otaEventQueue, buff, sizeof( buff ), NULL ) == -1 )
    {
        otaErrRet = OtaOsEventQueueReceiveFailed;

        LogError( ( "Failed to receive OTA Event: "
                    "mq_reqeive returned error: "
                    "otaErrRet=%i "
                    ",errno=%s",
                    otaErrRet,
                    strerror( errno ) ) );
    }
    else
    {
        LogDebug( ( "OTA Event received." ) );

        /* copy the data from local buffer.*/
        memcpy( pDst, buff, MAX_MSG_SIZE );
    }

    return otaErrRet;
}

OtaOsStatus_t Posix_OtaDeinitEvent( OtaEventContext_t * pContext )
{
    ( void ) pContext;

    OtaOsStatus_t otaErrRet = OtaOsSuccess;

    /* Remove the event queue.*/
    if( mq_unlink( OTA_QUEUE_NAME ) == -1 )
    {
        otaErrRet = OtaOsEventQueueDeleteFailed;

        LogError( ( "Failed to delete OTA Event queue: "
                    "mq_unlink returned error: "
                    "otaErrRet=%i "
                    ",errno=%s",
                    otaErrRet,
                    strerror( errno ) ) );
    }
    else
    {
        LogDebug( ( "OTA Event queue deleted." ) );
    }

    return otaErrRet;
}

static void timerCallback( union sigval arg )
{
    OtaEventMsg_t xEventMsg = { 0 };

    xEventMsg.eventId = OtaAgentEventRequestTimer;

    /* Send job document received event. */
    OTA_SignalEvent( &xEventMsg );
}

OtaOsStatus_t Posix_OtaStartTimer( OtaTimerContext_t * pTimerCtx,
                                   const char * const pTimerName,
                                   const uint32_t timeout,
                                   void ( * callback )( void * ) )
{
    ( void ) pTimerCtx;

    OtaOsStatus_t otaErrRet = OtaOsSuccess;

    /* Create the timer structures. */
    struct sigevent sgEvent;
    struct itimerspec timerAttr;

    /* clear everything in the structures. */
    memset( &sgEvent, 0, sizeof( struct sigevent ) );
    memset( &timerAttr, 0, sizeof( struct itimerspec ) );

    /* Set attributes. */
    sgEvent.sigev_notify = SIGEV_THREAD;
    sgEvent.sigev_value.sival_ptr = &otaTimer;
    sgEvent.sigev_notify_function = timerCallback;

    /* Set timeout attributes.*/
    timerAttr.it_value.tv_sec = timeout;
    timerAttr.it_interval.tv_sec = timerAttr.it_value.tv_sec;

    /* Create timer.*/
    if( timer_create( CLOCK_REALTIME, &sgEvent, &otaTimer ) == -1 )
    {
        otaErrRet = OtaOsTimerCreateFailed;

        LogError( ( "Failed to create OTA timer: "
                    "timer_create returned error: "
                    "otaErrRet=%i "
                    ",errno=%s",
                    otaErrRet,
                    strerror( errno ) ) );
    }
    else
    {
        /* Set timeout.*/
        if( timer_settime( otaTimer, 0, &timerAttr, NULL ) == -1 )
        {
            otaErrRet = OtaOsTimerStartFailed;

            LogError( ( "Failed to set OTA timer timeout: "
                        "timer_settime returned error: "
                        "otaErrRet=%i "
                        ",errno=%s",
                        otaErrRet,
                        strerror( errno ) ) );
        }
        else
        {
            LogInfo( ( "OTA Timer started." ) );
        }
    }

    return otaErrRet;
}

OtaOsStatus_t Posix_OtaStopTimer( OtaTimerContext_t * pTimerCtx )
{
    ( void ) pTimerCtx;

    OtaOsStatus_t otaErrRet = OtaOsSuccess;

    struct itimerspec trigger;

    trigger.it_value.tv_sec = 0;

    /* Stop the timer*/
    if( timer_settime( otaTimer, 0, &trigger, NULL ) == -1 )
    {
        otaErrRet = OtaOsTimerStopFailed;

        LogError( ( "Failed to stop OTA timer: "
                    "timer_settime returned error: "
                    "otaErrRet=%i "
                    ",errno=%s",
                    otaErrRet,
                    strerror( errno ) ) );
    }
    else
    {
        LogInfo( ( "OTA Timer stopped." ) );
    }

    return otaErrRet;
}

OtaOsStatus_t ota_DeleteTimer( OtaTimerContext_t * pTimerCtx )
{
    ( void ) pTimerCtx;

    OtaOsStatus_t otaErrRet = OtaOsSuccess;

    /* Delete the timer*/
    if( timer_delete( otaTimer ) == -1 )
    {
        otaErrRet = OtaOsTimerDeleteFailed;

        LogError( ( "Failed to delete OTA timer: "
                    "timer_delete returned error: "
                    "otaErrRet=%i "
                    ",errno=%s",
                    otaErrRet,
                    strerror( errno ) ) );
    }
    else
    {
        LogInfo( ( "OTA Timer deleted." ) );
    }

    return otaErrRet;
}

void * STDC_Malloc( size_t size )
{
    /* Use standard C malloc.*/
    return malloc( size );
}

void STDC_Free( void * ptr )
{
    /* Use standard C free.*/
    free( ptr );
}
