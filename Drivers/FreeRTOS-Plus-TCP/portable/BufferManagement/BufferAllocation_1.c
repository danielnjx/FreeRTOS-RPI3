/*
 * FreeRTOS+TCP Labs Build 160112 (C) 2016 Real Time Engineers ltd.
 * Authors include Hein Tibosch and Richard Barry
 *
 *******************************************************************************
 ***** NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ***
 ***                                                                         ***
 ***                                                                         ***
 ***   FREERTOS+TCP IS STILL IN THE LAB (mainly because the FTP and HTTP     ***
 ***   demos have a dependency on FreeRTOS+FAT, which is only in the Labs    ***
 ***   download):                                                            ***
 ***                                                                         ***
 ***   FreeRTOS+TCP is functional and has been used in commercial products   ***
 ***   for some time.  Be aware however that we are still refining its       ***
 ***   design, the source code does not yet quite conform to the strict      ***
 ***   coding and style standards mandated by Real Time Engineers ltd., and  ***
 ***   the documentation and testing is not necessarily complete.            ***
 ***                                                                         ***
 ***   PLEASE REPORT EXPERIENCES USING THE SUPPORT RESOURCES FOUND ON THE    ***
 ***   URL: http://www.FreeRTOS.org/contact  Active early adopters may, at   ***
 ***   the sole discretion of Real Time Engineers Ltd., be offered versions  ***
 ***   under a license other than that described below.                      ***
 ***                                                                         ***
 ***                                                                         ***
 ***** NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ******* NOTE ***
 *******************************************************************************
 *
 * FreeRTOS+TCP can be used under two different free open source licenses.  The
 * license that applies is dependent on the processor on which FreeRTOS+TCP is
 * executed, as follows:
 *
 * If FreeRTOS+TCP is executed on one of the processors listed under the Special 
 * License Arrangements heading of the FreeRTOS+TCP license information web 
 * page, then it can be used under the terms of the FreeRTOS Open Source 
 * License.  If FreeRTOS+TCP is used on any other processor, then it can be used
 * under the terms of the GNU General Public License V2.  Links to the relevant
 * licenses follow:
 * 
 * The FreeRTOS+TCP License Information Page: http://www.FreeRTOS.org/tcp_license 
 * The FreeRTOS Open Source License: http://www.FreeRTOS.org/license
 * The GNU General Public License Version 2: http://www.FreeRTOS.org/gpl-2.0.txt
 *
 * FreeRTOS+TCP is distributed in the hope that it will be useful.  You cannot
 * use FreeRTOS+TCP unless you agree that you use the software 'as is'.
 * FreeRTOS+TCP is provided WITHOUT ANY WARRANTY; without even the implied
 * warranties of NON-INFRINGEMENT, MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE. Real Time Engineers Ltd. disclaims all conditions and terms, be they
 * implied, expressed, or statutory.
 *
 * 1 tab == 4 spaces!
 *
 * http://www.FreeRTOS.org
 * http://www.FreeRTOS.org/plus
 * http://www.FreeRTOS.org/labs
 *
 */

/******************************************************************************
 *
 * See the following web page for essential buffer allocation scheme usage and
 * configuration details:
 * http://www.FreeRTOS.org/FreeRTOS-Plus/FreeRTOS_Plus_TCP/Embedded_Ethernet_Buffer_Management.html
 *
 ******************************************************************************/


/* Standard includes. */
#include <stdint.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_UDP_IP.h"
#include "NetworkInterface.h"
#include "NetworkBufferManagement.h"

/* For an Ethernet interrupt to be able to obtain a network buffer there must
be at least this number of buffers available. */
#define ipINTERRUPT_BUFFER_GET_THRESHOLD	( 3 )

/* A list of free (available) NetworkBufferDescriptor_t structures. */
static xList xFreeBuffersList;

/* Some statistics about the use of buffers. */
static size_t uxMinimumFreeNetworkBuffers;

/* Declares the pool of NetworkBufferDescriptor_t structures that are available to the
system.  All the network buffers referenced from xFreeBuffersList exist in this
array.  The array is not accessed directly except during initialisation, when
the xFreeBuffersList is filled (as all the buffers are free when the system is
booted). */
static NetworkBufferDescriptor_t xNetworkBuffers[ ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS ];

/* This constant is defined as true to let FreeRTOS_TCP_IP.c know that the network buffers
have constant size, large enough to hold the biggest ethernet packet. No resizing will
be done. */
const portBASE_TYPE xBufferAllocFixedSize = pdTRUE;

/* The semaphore used to obtain network buffers. */
static xSemaphoreHandle xNetworkBufferSemaphore = NULL;

#if !defined( ipconfigBUFFER_ALLOC_LOCK )
	#define ipconfigBUFFER_ALLOC_INIT( ) do {} while (0)
	#define ipconfigBUFFER_ALLOC_LOCK_FROM_ISR()		\
		unsigned portBASE_TYPE uxSavedInterruptStatus = portSET_INTERRUPT_MASK_FROM_ISR(); \
		{

	#define ipconfigBUFFER_ALLOC_UNLOCK_FROM_ISR()		\
			portCLEAR_INTERRUPT_MASK_FROM_ISR( uxSavedInterruptStatus ); \
		}

	#define ipconfigBUFFER_ALLOC_LOCK()					taskENTER_CRITICAL()
	#define ipconfigBUFFER_ALLOC_UNLOCK()				taskEXIT_CRITICAL()
#else
	/* user can define hist own lock/unlock routines */
#endif /* ipconfigBUFFER_ALLOC_LOCK */

/*-----------------------------------------------------------*/

#if( ipconfigIP_TASK_KEEPS_MESSAGE_BUFFER != 0 )

	/* when this options is used, the IP-task will keep 1 message buffer permanently
	for its own use. */
	extern NetworkBufferDescriptor_t *pxIpTaskMessageBuffer;

#endif

#if ipconfigTCP_IP_SANITY

/* HT: SANITY code will be removed as soon as the library is stable
 * and and ready to become public
 * Function below gives information about the use of buffers */
#define WARN_LOW		( 2 )
#define WARN_HIGH		( ( 5 * ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS ) / 10 )

static char cIsLow = pdFALSE;

/*-----------------------------------------------------------*/

portBASE_TYPE prvIsFreeBuffer( const NetworkBufferDescriptor_t *pxDescr )
{
	return ( bIsValidNetworkDescriptor( pxDescr ) != 0 ) &&
		( listIS_CONTAINED_WITHIN( &xFreeBuffersList, &( pxDescr->xBufferListItem ) ) != 0 );
}
/*-----------------------------------------------------------*/

static void showWarnings( void )
{
	unsigned portBASE_TYPE uxCount = uxGetNumberOfFreeNetworkBuffers( );
	if( ( ( cIsLow == 0 ) && ( uxCount <= WARN_LOW ) ) || ( ( cIsLow != 0 ) && ( uxCount >= WARN_HIGH ) ) )
	{
		cIsLow = !cIsLow;
		FreeRTOS_debug_printf( ( "*** Warning *** %s %lu buffers left\n", cIsLow ? "only" : "now", uxCount ) );
	}
	else
	{
		return;
	}
}
/*-----------------------------------------------------------*/

unsigned portBASE_TYPE bIsValidNetworkDescriptor( const NetworkBufferDescriptor_t * pxDesc )
{
	uint32_t offset = ( uint32_t ) ( ((const char *)pxDesc) - ((const char *)xNetworkBuffers) );
	if( ( offset >= sizeof( xNetworkBuffers ) ) ||
		( ( offset % sizeof( xNetworkBuffers[0] ) ) != 0 ) )
		return pdFALSE;
	return (unsigned portBASE_TYPE) (pxDesc - xNetworkBuffers) + 1;
}

#else

unsigned portBASE_TYPE bIsValidNetworkDescriptor (const NetworkBufferDescriptor_t * pxDesc);

unsigned portBASE_TYPE bIsValidNetworkDescriptor (const NetworkBufferDescriptor_t * pxDesc)
{
	(void)pxDesc;
	return pdTRUE;
}
/*-----------------------------------------------------------*/

static void showWarnings( void )
{
}

#endif /* ipconfigTCP_IP_SANITY */

portBASE_TYPE xNetworkBuffersInitialise( void )
{
portBASE_TYPE xReturn, x;

	/* Only initialise the buffers and their associated kernel objects if they
	have not been initialised before. */
	if( xNetworkBufferSemaphore == NULL )
	{
		/* In case alternative locking is used, the mutexes can be initialised here */
		ipconfigBUFFER_ALLOC_INIT( );

		xNetworkBufferSemaphore = xSemaphoreCreateCounting( ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS, ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS );
		configASSERT( xNetworkBufferSemaphore );

		if( xNetworkBufferSemaphore != NULL )
		{
			vListInitialise( &xFreeBuffersList );

			/* Initialise all the network buffers.  The buffer storage comes
			from the network interface, and different hardware has different
			requirements. */
			vNetworkInterfaceAllocateRAMToBuffers( xNetworkBuffers );
			for( x = 0; x < ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS; x++ )
			{
				/* Initialise and set the owner of the buffer list items. */
				vListInitialiseItem( &( xNetworkBuffers[ x ].xBufferListItem ) );
				listSET_LIST_ITEM_OWNER( &( xNetworkBuffers[ x ].xBufferListItem ), &xNetworkBuffers[ x ] );

				/* Currently, all buffers are available for use. */
				vListInsert( &xFreeBuffersList, &( xNetworkBuffers[ x ].xBufferListItem ) );
			}
			
			uxMinimumFreeNetworkBuffers = ipconfigNUM_NETWORK_BUFFER_DESCRIPTORS;
		}
	}

	if( xNetworkBufferSemaphore == NULL )
	{
		xReturn = pdFAIL;
	}
	else
	{
		xReturn = pdPASS;
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

NetworkBufferDescriptor_t *pxGetNetworkBufferWithDescriptor( size_t xRequestedSizeBytes, portTickType xBlockTimeTicks )
{
NetworkBufferDescriptor_t *pxReturn = NULL;
portBASE_TYPE xInvalid = pdFALSE;
size_t uxCount;

	/* The current implementation only has a single size memory block, so
	the requested size parameter is not used (yet). */
	( void ) xRequestedSizeBytes;
	if( xNetworkBufferSemaphore != NULL )
	{
		/* If there is a semaphore available, there is a network buffer available. */
		if( xSemaphoreTake( xNetworkBufferSemaphore, xBlockTimeTicks ) == pdPASS )
		{
			/* Protect the structure as it is accessed from tasks and interrupts. */
			ipconfigBUFFER_ALLOC_LOCK();
			{
				pxReturn = ( NetworkBufferDescriptor_t * ) listGET_OWNER_OF_HEAD_ENTRY( &xFreeBuffersList );
				if( bIsValidNetworkDescriptor(pxReturn) &&
					listIS_CONTAINED_WITHIN( &xFreeBuffersList, &( pxReturn->xBufferListItem ) ) )
				{
					vListRemove( &( pxReturn->xBufferListItem ) );
				}
				else
				{
					xInvalid = pdTRUE;
				}
			}
			ipconfigBUFFER_ALLOC_UNLOCK();

			if( xInvalid )
			{
				FreeRTOS_debug_printf( ( "pxGetNetworkBufferWithDescriptor: INVALID BUFFER: %p (valid %lu)\n",
					pxReturn, bIsValidNetworkDescriptor( pxReturn ) ) );
				pxReturn = NULL;
			}
			else
			{
				{
					/* Reading unsigned portBASE_TYPE, no critical section needed. */
					uxCount = listCURRENT_LIST_LENGTH( &xFreeBuffersList );
		
					if( uxMinimumFreeNetworkBuffers > uxCount )
					{
						uxMinimumFreeNetworkBuffers = uxCount;
					}		
				}
				pxReturn->xDataLength = xRequestedSizeBytes;

				#if( ipconfigTCP_IP_SANITY != 0 )
				{
					showWarnings();
				}
				#endif /* ipconfigTCP_IP_SANITY */

				#if( ipconfigUSE_LINKED_RX_MESSAGES != 0 )
				{
					/* make sure the buffer is not linked */
					pxReturn->pxNextBuffer = NULL;
				}
				#endif /* ipconfigUSE_LINKED_RX_MESSAGES */

				if( xTCPWindowLoggingLevel > 3 )
				{
					FreeRTOS_debug_printf( ( "BUF_GET[%ld]: %p (%p)\n",
						bIsValidNetworkDescriptor( pxReturn ),
						pxReturn, pxReturn->pucEthernetBuffer ) );
				}
			}
			iptraceNETWORK_BUFFER_OBTAINED( pxReturn );
		}
		else
		{
			iptraceFAILED_TO_OBTAIN_NETWORK_BUFFER();
		}
	}

	return pxReturn;
}
/*-----------------------------------------------------------*/

NetworkBufferDescriptor_t *pxNetworkBufferGetFromISR( size_t xRequestedSizeBytes )
{
NetworkBufferDescriptor_t *pxReturn = NULL;

	/* The current implementation only has a single size memory block, so
	the requested size parameter is not used (yet). */
	( void ) xRequestedSizeBytes;

	/* If there is a semaphore available then there is a buffer available, but,
	as this is called from an interrupt, only take a buffer if there are at
	least ipINTERRUPT_BUFFER_GET_THRESHOLD buffers remaining.  This prevents,
	to a certain degree at least, a rapidly executing interrupt exhausting
	buffer and in so doing preventing tasks from continuing. */
	if( uxQueueMessagesWaitingFromISR( ( xQueueHandle ) xNetworkBufferSemaphore ) > ipINTERRUPT_BUFFER_GET_THRESHOLD )
	{
		if( xSemaphoreTakeFromISR( xNetworkBufferSemaphore, NULL ) == pdPASS )
		{
			/* Protect the structure as it is accessed from tasks and interrupts. */
			ipconfigBUFFER_ALLOC_LOCK_FROM_ISR();
			{
				pxReturn = ( NetworkBufferDescriptor_t * ) listGET_OWNER_OF_HEAD_ENTRY( &xFreeBuffersList );
				vListRemove( &( pxReturn->xBufferListItem ) );
			}
			ipconfigBUFFER_ALLOC_UNLOCK_FROM_ISR();

			iptraceNETWORK_BUFFER_OBTAINED_FROM_ISR( pxReturn );
		}
	}

	if( pxReturn == NULL )
	{
		iptraceFAILED_TO_OBTAIN_NETWORK_BUFFER_FROM_ISR();
	}

	return pxReturn;
}
/*-----------------------------------------------------------*/

portBASE_TYPE vNetworkBufferReleaseFromISR( NetworkBufferDescriptor_t * const pxNetworkBuffer )
{
portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;

	/* Ensure the buffer is returned to the list of free buffers before the
	counting semaphore is 'given' to say a buffer is available. */
	ipconfigBUFFER_ALLOC_LOCK_FROM_ISR();
	{
		vListInsertEnd( &xFreeBuffersList, &( pxNetworkBuffer->xBufferListItem ) );
	}
	ipconfigBUFFER_ALLOC_UNLOCK_FROM_ISR();

	xSemaphoreGiveFromISR( xNetworkBufferSemaphore, &xHigherPriorityTaskWoken );
	iptraceNETWORK_BUFFER_RELEASED( pxNetworkBuffer );

	return xHigherPriorityTaskWoken;
}
/*-----------------------------------------------------------*/

void vReleaseNetworkBufferAndDescriptor( NetworkBufferDescriptor_t * const pxNetworkBuffer )
{
portBASE_TYPE xListItemAlreadyInFreeList;

#if( ipconfigIP_TASK_KEEPS_MESSAGE_BUFFER != 0 )
	if( pxNetworkBuffer == pxIpTaskMessageBuffer )
		return;
#endif

	if( !bIsValidNetworkDescriptor ( pxNetworkBuffer ) )
	{
		FreeRTOS_debug_printf( ( "vReleaseNetworkBufferAndDescriptor: Invalid buffer %p\n", pxNetworkBuffer ) );
		return ;
	}
	/* Ensure the buffer is returned to the list of free buffers before the
	counting semaphore is 'given' to say a buffer is available. */
	ipconfigBUFFER_ALLOC_LOCK();
	{
		{
			xListItemAlreadyInFreeList = listIS_CONTAINED_WITHIN( &xFreeBuffersList, &( pxNetworkBuffer->xBufferListItem ) );

			if( xListItemAlreadyInFreeList == pdFALSE )
			{
				vListInsertEnd( &xFreeBuffersList, &( pxNetworkBuffer->xBufferListItem ) );
			}
		}
	}
	ipconfigBUFFER_ALLOC_UNLOCK();

	if( xListItemAlreadyInFreeList )
	{
		FreeRTOS_debug_printf( ( "vReleaseNetworkBufferAndDescriptor: %p ALREADY RELEASED (now %lu)\n",
			pxNetworkBuffer, uxGetNumberOfFreeNetworkBuffers( ) ) );
	}
	if( !xListItemAlreadyInFreeList )
	{
		xSemaphoreGive( xNetworkBufferSemaphore );
		showWarnings();
		if( xTCPWindowLoggingLevel > 3 )
			FreeRTOS_debug_printf( ( "BUF_PUT[%ld]: %p (%p) (now %lu)\n",
				bIsValidNetworkDescriptor( pxNetworkBuffer ),
				pxNetworkBuffer, pxNetworkBuffer->pucEthernetBuffer,
				uxGetNumberOfFreeNetworkBuffers( ) ) );
	}
	iptraceNETWORK_BUFFER_RELEASED( pxNetworkBuffer );
}
/*-----------------------------------------------------------*/

unsigned portBASE_TYPE uxGetMinimumFreeNetworkBuffers( void )
{
	return uxMinimumFreeNetworkBuffers;
}
/*-----------------------------------------------------------*/

unsigned portBASE_TYPE uxGetNumberOfFreeNetworkBuffers( void )
{
	return listCURRENT_LIST_LENGTH( &xFreeBuffersList );
}

/*#endif */ /* ipconfigINCLUDE_TEST_CODE */
