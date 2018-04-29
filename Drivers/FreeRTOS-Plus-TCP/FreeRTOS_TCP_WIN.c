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

/*
 * FreeRTOS_TCP_WIN.c
 * Module which handles the TCP windowing schemes for FreeRTOS+TCP.  Many
 * functions have two versions - one for FreeRTOS+TCP (full) and one for
 * FreeRTOS+TCP (lite).
 *
 * In this module all ports and IP addresses and sequence numbers are
 * being stored in host byte-order.
 */

/* Standard includes. */
#include <stdint.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_UDP_IP.h"
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IP_Private.h"
#include "NetworkBufferManagement.h"
#include "FreeRTOS_TCP_WIN.h"

/* Constants used for Smoothed Round Trip Time (SRTT). */
#define	winSRTT_INCREMENT_NEW 		2
#define winSRTT_INCREMENT_CURRENT 	6
#define	winSRTT_DECREMENT_NEW 		1
#define winSRTT_DECREMENT_CURRENT 	7
#define winSRTT_CAP_mS				50

#if( ipconfigUSE_TCP_WIN == 1 )

	#define xTCPWindowRxNew( pxWindow, ulSequenceNumber, lCount ) xTCPWindowNew( pxWindow, ulSequenceNumber, lCount, pdTRUE )

	#define xTCPWindowTxNew( pxWindow, ulSequenceNumber, lCount ) xTCPWindowNew( pxWindow, ulSequenceNumber, lCount, pdFALSE )

	/* The code to send a single Selective ACK (SACK):
	 * NOP (0x01), NOP (0x01), SACK (0x05), LEN (0x0a),
	 * followed by a lower and a higher sequence number,
	 * where LEN is 2 + 2*4 = 10 bytes. */
	#if( ipconfigBYTE_ORDER == pdFREERTOS_BIG_ENDIAN )
		#define OPTION_CODE_SINGLE_SACK		( 0x0101050aUL )
	#else
		#define OPTION_CODE_SINGLE_SACK		( 0x0a050101UL )
	#endif

	/* Normal retransmission:
	 * A packet will be retransmitted after a Retransmit Time-Out (RTO).
	 * Fast retransmission:
	 * When 3 packets with a higher sequence number have been acknowledged
	 * by the peer, it is very unlikely a current packet will ever arrive.
	 * It will be retransmitted far before the RTO.
	 */
	#define	DUPLICATE_ACKS_BEFORE_FAST_RETRANSMIT		( 3 )

	/* If there have been several retransmissions (4), decrease the
	 * size of the transmission window to at most 2 times MSS.
	 */
	#define MAX_TRANSMIT_COUNT_USING_LARGE_WINDOW		( 4 )

#endif /* configUSE_TCP_WIN */
/*-----------------------------------------------------------*/

extern void vListInsertGeneric( xList * const pxList, xListItem * const pxNewListItem, xMiniListItem * const pxWhere );

/*
 * All TCP sockets share a pool of segment descriptors (TCPSegment_t)
 * Available descriptors are stored in the 'xSegmentList'
 * When a socket owns a descriptor, it will either be stored in
 * 'xTxSegments' or 'xRxSegments'
 * As soon as a package has been confirmed, the descriptor will be returned
 * to the segment pool
 */
#if( ipconfigUSE_TCP_WIN == 1 )
	static portBASE_TYPE prvCreateSectors( void );
#endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * Find a segment with a given sequence number in the list of received
 * segments: 'pxWindow->xRxSegments'.
 */
#if( ipconfigUSE_TCP_WIN == 1 )
	static TCPSegment_t *xTCPWindowRxFind( TCPWindow_t *pxWindow, unsigned int ulSequenceNumber );
#endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * Allocate a new segment
 * The socket will borrow all segments from a common pool: 'xSegmentList',
 * which is a list of 'TCPSegment_t'
 */
#if( ipconfigUSE_TCP_WIN == 1 )
	static TCPSegment_t *xTCPWindowNew( TCPWindow_t *pxWindow, unsigned int ulSequenceNumber, int lCount, portBASE_TYPE xIsForRx );
#endif /* ipconfigUSE_TCP_WIN == 1 */

/* When the peer has a close request (FIN flag), the driver will check if
 * there are missing packets in the Rx-queue
 * It will accept the closure of the connection if both conditions are true:
 * - the Rx-queue is empty
 * - we've ACK'd the highest Rx sequence number seen
 */
#if( ipconfigUSE_TCP_WIN == 1 )
	portBASE_TYPE xTCPWindowRxEmpty( TCPWindow_t *pxWindow );
#endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * Detaches and returns the head of a queue
 */
#if( ipconfigUSE_TCP_WIN == 1 )
	static TCPSegment_t *xTCPWindowGetHead( xList *pxList );
#endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * Returns the head of a queue but it won't be detached
 */
#if( ipconfigUSE_TCP_WIN == 1 )
	static TCPSegment_t *xTCPWindowPeekHead( xList *pxList );
#endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 *  Free entry pxSegment because it's not used anymore
 *	The ownership will be passed back to the segment pool
 */
#if( ipconfigUSE_TCP_WIN == 1 )
	static void vTCPWindowFree( TCPSegment_t *pxSegment );
#endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * A segment has been received with sequence number 'ulSequenceNumber', where
 * 'ulCurrentSequenceNumber == ulSequenceNumber', which means that exactly this
 * segment was expected.  xTCPWindowRxConfirm() will check if there is already
 * another segment with a sequence number between (ulSequenceNumber) and
 * (ulSequenceNumber+xLength).  Normally none will be found, because the next Rx
 * segment should have a sequence number equal to '(ulSequenceNumber+xLength)'.
 */
#if( ipconfigUSE_TCP_WIN == 1 )
	static TCPSegment_t *xTCPWindowRxConfirm( TCPWindow_t *pxWindow, unsigned int ulSequenceNumber, unsigned int ulLength );
#endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * FreeRTOS+TCP stores data in circular buffers.  Calculate the next position to
 * store.
 */
#if( ipconfigUSE_TCP_WIN == 1 )
	static int lTCPIncrementTxPosition( int lPosition, int lMax, int lCount );
#endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * This function will look if there is new transmission data.  It will return
 * true if there is data to be sent.
 */
#if( ipconfigUSE_TCP_WIN == 1 )
	static portBASE_TYPE prvTCPWindowTxHasSpace( TCPWindow_t *pxWindow, unsigned int ulWindowSize );
#endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * An acknowledge was received.  See if some outstanding data may be removed
 * from the transmission queue(s).
 */
#if( ipconfigUSE_TCP_WIN == 1 )
	static unsigned int prvTCPWindowTxCheckAck( TCPWindow_t *pxWindow, unsigned int ulFirst, unsigned int ulLast );
#endif /* ipconfigUSE_TCP_WIN == 1 */

/*
 * A higher Tx block has been acknowledged.  Now iterate through the xWaitQueue
 * to find a possible condition for a FAST retransmission.
 */
#if( ipconfigUSE_TCP_WIN == 1 )
	static unsigned int prvTCPWindowFastRetransmit( TCPWindow_t *pxWindow, unsigned int ulFirst );
#endif /* ipconfigUSE_TCP_WIN == 1 */

/*-----------------------------------------------------------*/

/* TCP segement pool. */
#if( ipconfigUSE_TCP_WIN == 1 )
	static TCPSegment_t *xTCPSegments = NULL;
#endif /* ipconfigUSE_TCP_WIN == 1 */

/* List of free TCP segments. */
#if( ipconfigUSE_TCP_WIN == 1 )
	static xList xSegmentList;
#endif

/* Logging verbosity level. */
portBASE_TYPE xTCPWindowLoggingLevel = 0;

/* Some 32-bit arithmetic: comparing sequence numbers */
static portINLINE portBASE_TYPE xSequenceLessThanOrEqual( unsigned int a, unsigned int b )
{
	/* Test if a <= b
	Return true if the unsigned subtraction of (b-a) doesn't generate an
	arithmetic overflow. */
	return ( ( b - a ) & 0x80000000U ) == 0;
}
/*-----------------------------------------------------------*/

static portINLINE portBASE_TYPE xSequenceLessThan( unsigned int a, unsigned int b )
{
	/* Test if a < b */
	return ( ( b - a - 1 ) & 0x80000000U ) == 0;
}
/*-----------------------------------------------------------*/

static portINLINE portBASE_TYPE xSequenceGreaterThan( unsigned int a, unsigned int b )
{
	/* Test if a > b */
	return ( ( a - b - 1 ) & 0x80000000U ) == 0;
}
/*-----------------------------------------------------------*/

static portINLINE portBASE_TYPE xSequenceGreaterThanOrEqual( unsigned int a, unsigned int b )
{
	/* Test if a >= b */
	return ( ( a - b ) & 0x80000000U ) == 0;
}
/*-----------------------------------------------------------*/

static portINLINE void vListInsertFifo( xList * const pxList, xListItem * const pxNewListItem )
{
	vListInsertGeneric( pxList, pxNewListItem, &pxList->xListEnd );
}
/*-----------------------------------------------------------*/

static portINLINE void vTCPTimerSet( TcpTimer_t *pxTimer )
{
	pxTimer->ulBorn = xTaskGetTickCount ( );
}
/*-----------------------------------------------------------*/

static portINLINE unsigned int ulTimerGetAge( TcpTimer_t *pxTimer )
{
	return ( ( xTaskGetTickCount() - pxTimer->ulBorn ) * portTICK_PERIOD_MS );
}
/*-----------------------------------------------------------*/

/* _HT_ GCC (using the settings that I'm using) checks for every public function if it is
preceded by a prototype. Later this prototype will be located in list.h? */

extern void vListInsertGeneric( xList * const pxList, xListItem * const pxNewListItem, xMiniListItem * const pxWhere );

void vListInsertGeneric( xList * const pxList, xListItem * const pxNewListItem, xMiniListItem * const pxWhere )
{
	/* Insert a new list item into pxList, it does not sort the list,
	but it puts the item just before xListEnd, so it will be the last item
	returned by listGET_HEAD_ENTRY() */
	pxNewListItem->pxNext = (struct xLIST_ITEM * configLIST_VOLATILE)pxWhere;
	pxNewListItem->pxPrevious = pxWhere->pxPrevious;
	pxWhere->pxPrevious->pxNext = pxNewListItem;
	pxWhere->pxPrevious = pxNewListItem;

	/* Remember which list the item is in. */
	pxNewListItem->pvContainer = ( void * ) pxList;

	( pxList->uxNumberOfItems )++;
}
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	static portBASE_TYPE prvCreateSectors( void )
	{
	portBASE_TYPE xIndex, xReturn;

		/* Allocate space for 'xTCPSegments' and store them in 'xSegmentList'. */

		vListInitialise( &xSegmentList );
		xTCPSegments = ( TCPSegment_t * ) pvPortMallocLarge( ipconfigTCP_WIN_SEG_COUNT * sizeof( xTCPSegments[ 0 ] ) );

		if( xTCPSegments == NULL )
		{
			FreeRTOS_debug_printf( ( "prvCreateSectors: malloc %lu failed\n",
				ipconfigTCP_WIN_SEG_COUNT * sizeof( xTCPSegments[ 0 ] ) ) );

			xReturn = pdFAIL;
		}
		else
		{
			/* Clear the allocated space. */
			memset( xTCPSegments, '\0', ipconfigTCP_WIN_SEG_COUNT * sizeof( xTCPSegments[ 0 ] ) );

			for( xIndex = 0; xIndex < ipconfigTCP_WIN_SEG_COUNT; xIndex++ )
			{
				/* Could call vListInitialiseItem here but all data has been
				nulled already.  Set the owner to a segment descriptor. */
				listSET_LIST_ITEM_OWNER( &( xTCPSegments[ xIndex ].xListItem ), ( void* ) &( xTCPSegments[ xIndex ] ) );
				listSET_LIST_ITEM_OWNER( &( xTCPSegments[ xIndex ].xQueueItem ), ( void* ) &( xTCPSegments[ xIndex ] ) );

				/* And add it to the pool of available segments */
				vListInsertFifo( &xSegmentList, &( xTCPSegments[xIndex].xListItem ) );
			}

			xReturn = pdPASS;
		}

		return xReturn;
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	static TCPSegment_t *xTCPWindowRxFind( TCPWindow_t *pxWindow, unsigned int ulSequenceNumber )
	{
	const xListItem *pxIterator;
	const xMiniListItem* pxEnd;
	TCPSegment_t *pxSegment, *pxReturn = NULL;

		/* Find a segment with a given sequence number in the list of received
		segments. */

		pxEnd = ( const xMiniListItem* )listGET_END_MARKER( &pxWindow->xRxSegments );

		for( pxIterator  = ( const xListItem * ) listGET_NEXT( pxEnd );
			 pxIterator != ( const xListItem * ) pxEnd;
			 pxIterator  = ( const xListItem * ) listGET_NEXT( pxIterator ) )
		{
			pxSegment = ( TCPSegment_t * ) listGET_LIST_ITEM_OWNER( pxIterator );

			if( pxSegment->ulSequenceNumber == ulSequenceNumber )
			{
				pxReturn = pxSegment;
				break;
			}
		}

		return pxReturn;
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	static TCPSegment_t *xTCPWindowNew( TCPWindow_t *pxWindow, unsigned int ulSequenceNumber, int lCount, portBASE_TYPE xIsForRx )
	{
	TCPSegment_t *pxSegment;
	xListItem * pxItem;

		/* Allocate a new segment.  The socket will borrow all segments from a
		common pool: 'xSegmentList', which is a list of 'TCPSegment_t' */
		if( listLIST_IS_EMPTY( &xSegmentList ) != pdFALSE )
		{
			/* If the TCP-stack runs out of segments, you might consider
			increasing 'ipconfigTCP_WIN_SEG_COUNT'. */
			FreeRTOS_debug_printf( ( "xTCPWindow%cxNew: Error: all segments occupied\n", xIsForRx ? 'R' : 'T' ) );
			pxSegment = NULL;
		}
		else
		{
			/* Pop the item at the head of the list.  Semaphore protection is
			not required as only the IP task will call these functions.  */
			pxItem = ( xListItem * ) listGET_HEAD_ENTRY( &xSegmentList );
			pxSegment = ( TCPSegment_t * ) listGET_LIST_ITEM_OWNER( pxItem );

			configASSERT( pxItem != NULL );
			configASSERT( pxSegment != NULL );

			/* Remove the item from xSegmentList. */
			vListRemove( pxItem );

			/* Add it to either the connections' Rx or Tx queue. */
			vListInsertFifo( xIsForRx ? &pxWindow->xRxSegments : &pxWindow->xTxSegments, pxItem );

			/* And set the segment's timer to zero */
			vTCPTimerSet( &pxSegment->xTransmitTimer );

			pxSegment->u.ulFlags = 0;
			pxSegment->u.bits.bIsForRx = ( xIsForRx != 0 );
			pxSegment->lMaxLength = lCount;
			pxSegment->lDataLength = lCount;
			pxSegment->ulSequenceNumber = ulSequenceNumber;
		}

		return pxSegment;
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	portBASE_TYPE xTCPWindowRxEmpty( TCPWindow_t *pxWindow )
	{
	portBASE_TYPE xReturn;

		/* When the peer has a close request (FIN flag), the driver will check
		if there are missing packets in the Rx-queue.  It will accept the
		closure of the connection if both conditions are true:
		  - the Rx-queue is empty
		  - the highest Rx sequence number has been ACK'ed */
		if( listLIST_IS_EMPTY( ( &pxWindow->xRxSegments ) ) == pdFALSE )
		{
			/* Rx data has been stored while earlier packets were missing. */
			xReturn = pdFALSE;
		}
		else if( xSequenceGreaterThanOrEqual( pxWindow->rx.ulCurrentSequenceNumber, pxWindow->rx.ulHighestSequenceNumber ) != pdFALSE )
		{
			/* No Rx packets are being stored and the highest sequence number
			that has been received has been ACKed. */
			xReturn = pdTRUE;
		}
		else
		{
			FreeRTOS_debug_printf( ( "xTCPWindowRxEmpty: cur %lu highest %lu (empty)\n",
				( pxWindow->rx.ulCurrentSequenceNumber - pxWindow->rx.ulFirstSequenceNumber ),
				( pxWindow->rx.ulHighestSequenceNumber - pxWindow->rx.ulFirstSequenceNumber ) ) );
			xReturn = pdFALSE;
		}

		return xReturn;
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	static TCPSegment_t *xTCPWindowGetHead( xList *pxList )
	{
	TCPSegment_t *pxSegment;
	xListItem * pxItem;

		/* Detaches and returns the head of a queue. */
		if( listLIST_IS_EMPTY( pxList ) != pdFALSE )
		{
			pxSegment = NULL;
		}
		else
		{
			pxItem = ( xListItem * ) listGET_HEAD_ENTRY( pxList );
			pxSegment = ( TCPSegment_t * ) listGET_LIST_ITEM_OWNER( pxItem );

			vListRemove( pxItem );
		}

		return pxSegment;
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	static TCPSegment_t *xTCPWindowPeekHead( xList *pxList )
	{
	xListItem *pxItem;
	TCPSegment_t *pxReturn;

		/* Returns the head of a queue but it won't be detached. */
		if( listLIST_IS_EMPTY( pxList ) != pdFALSE )
		{
			pxReturn = NULL;
		}
		else
		{
			pxItem = ( xListItem * ) listGET_HEAD_ENTRY( pxList );
			pxReturn = ( TCPSegment_t * ) listGET_LIST_ITEM_OWNER( pxItem );
		}

		return pxReturn;
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	static void vTCPWindowFree( TCPSegment_t *pxSegment )
	{
		/*  Free entry pxSegment because it's not used any more.  The ownership
		will be passed back to the segment pool.

		Unlink it from one of the queues, if any. */
		if( listLIST_ITEM_CONTAINER( &( pxSegment->xQueueItem ) ) != NULL )
		{
			vListRemove( &( pxSegment->xQueueItem ) );
		}

		pxSegment->ulSequenceNumber = 0;
		pxSegment->lDataLength = 0;
		pxSegment->u.ulFlags = 0;

		/* Take it out of xRxSegments/xTxSegments */
		if( listLIST_ITEM_CONTAINER( &( pxSegment->xListItem ) ) != NULL )
		{
			vListRemove( &( pxSegment->xListItem ) );
		}

		/* Return it to xSegmentList */
		vListInsertFifo( &xSegmentList, &( pxSegment->xListItem ) );
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	void vTCPWindowDestroy( TCPWindow_t *pxWindow )
	{
	xList * pxSegments;
	portBASE_TYPE xRound;
	TCPSegment_t *pxSegment;

		/*  Destroy a window.  A TCP window doesn't serve any more.  Return all
		owned segments to the pool.  In order to save code, it will make 2 rounds,
		one to remove the segments from xRxSegments, and a second round to clear
		xTxSegments*/
		for( xRound = 0; xRound < 2; xRound++ )
		{
			if( xRound != 0 )
			{
				pxSegments = &( pxWindow->xRxSegments );
			}
			else
			{
				pxSegments = &( pxWindow->xTxSegments );
			}

			if( listLIST_IS_INITIALISED( pxSegments ) != pdFALSE )
			{
				while( listCURRENT_LIST_LENGTH( pxSegments ) > 0U )
				{
					pxSegment = ( TCPSegment_t * ) listGET_OWNER_OF_HEAD_ENTRY( pxSegments );
					vTCPWindowFree( pxSegment );
				}
			}
		}
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

void vTCPWindowCreate( TCPWindow_t *pxWindow, unsigned int ulRxWindowLength,
	unsigned int ulTxWindowLength, unsigned int ulAckNumber, unsigned int ulSequenceNumber, unsigned int ulMSS )
{
	/* Create and initialize a window. */

	#if( ipconfigUSE_TCP_WIN == 1 )
	{
		if( xTCPSegments == NULL )
		{
			prvCreateSectors();
		}

		vListInitialise( &pxWindow->xTxSegments );
		vListInitialise( &pxWindow->xRxSegments );

		vListInitialise( &pxWindow->xPriorityQueue );			/* Priority queue: segments which must be sent immediately */
		vListInitialise( &pxWindow->xTxQueue   );			/* Transmit queue: segments queued for transmission */
		vListInitialise( &pxWindow->xWaitQueue );			/* Waiting queue:  outstanding segments */
	}
	#endif /* ipconfigUSE_TCP_WIN == 1 */

	if( xTCPWindowLoggingLevel != 0 )
	{
		FreeRTOS_debug_printf( ( "vTCPWindowCreate: for WinLen = Rx/Tx: %lu/%lu\n",
			ulRxWindowLength, ulTxWindowLength ) );
	}

	pxWindow->xSize.ulRxWindowLength = ulRxWindowLength;
	pxWindow->xSize.ulTxWindowLength = ulTxWindowLength;

	vTCPWindowInit( pxWindow, ulAckNumber, ulSequenceNumber, ulMSS );
}
/*-----------------------------------------------------------*/

void vTCPWindowInit( TCPWindow_t *pxWindow, unsigned int ulAckNumber, unsigned int ulSequenceNumber, unsigned int ulMSS )
{
const int l500ms = 500;

	pxWindow->u.ulFlags = 0;
	pxWindow->u.bits.bHasInit = pdTRUE;

	if( ulMSS != 0 )
	{
		if( pxWindow->usMSSInit != 0 )
		{
			pxWindow->usMSSInit = ( unsigned short ) ulMSS;
		}

		if( ( ulMSS < ( unsigned int ) pxWindow->usMSS ) || ( pxWindow->usMSS == 0 ) )
		{
			pxWindow->xSize.ulRxWindowLength = ( pxWindow->xSize.ulRxWindowLength / ulMSS ) * ulMSS;
			pxWindow->usMSS = ( unsigned short ) ulMSS;
		}
	}

	#if( ipconfigUSE_TCP_WIN == 0 )
	{
		pxWindow->xTxSegment.lMaxLength = pxWindow->usMSS;
	}
	#endif /* ipconfigUSE_TCP_WIN == 1 */

	/*Start with a timeout of 2 * 500 ms (1 sec). */
	pxWindow->lSRTT = l500ms;

	/* Just for logging, to print relative sequence numbers. */
	pxWindow->rx.ulFirstSequenceNumber = ulAckNumber;

	/* The segment asked for in the next transmission. */
	pxWindow->rx.ulCurrentSequenceNumber = ulAckNumber;

	/* The right-hand side of the receive window. */
	pxWindow->rx.ulHighestSequenceNumber = ulAckNumber;

	pxWindow->tx.ulFirstSequenceNumber = ulSequenceNumber;

	/* The segment asked for in next transmission. */
	pxWindow->tx.ulCurrentSequenceNumber = ulSequenceNumber;

	/* The sequence number given to the next outgoing byte to be added is
	maintained by lTCPWindowTxAdd(). */
	pxWindow->ulNextTxSequenceNumber = ulSequenceNumber;

	/* The right-hand side of the transmit window. */
	pxWindow->tx.ulHighestSequenceNumber = ulSequenceNumber;
	pxWindow->ulOurSequenceNumber = ulSequenceNumber;
}
/*-----------------------------------------------------------*/

/*=============================================================================
 *
 *                ######        #    #
 *                 #    #       #    #
 *                 #    #       #    #
 *                 #    #        ####
 *                 ######         ##
 *                 #  ##         ####
 *                 #   #        #    #
 *                 #    #       #    #
 *                ###  ##       #    #
 * Rx functions
 *
 *=============================================================================*/

#if( ipconfigUSE_TCP_WIN == 1 )

	static TCPSegment_t *xTCPWindowRxConfirm( TCPWindow_t *pxWindow, unsigned int ulSequenceNumber, unsigned int ulLength )
	{
	TCPSegment_t *pxBest = NULL;
	const xListItem *pxIterator;
	unsigned int ulNextSequenceNumber = ulSequenceNumber + ulLength;
	const xMiniListItem* pxEnd = ( const xMiniListItem* ) listGET_END_MARKER( &pxWindow->xRxSegments );
	TCPSegment_t *pxSegment;

		/* A segment has been received with sequence number 'ulSequenceNumber',
		where 'ulCurrentSequenceNumber == ulSequenceNumber', which means that
		exactly this segment was expected.  xTCPWindowRxConfirm() will check if
		there is already another segment with a sequence number between (ulSequenceNumber)
		and (ulSequenceNumber+ulLength).  Normally none will be found, because
		the next RX segment should have a sequence number equal to
		'(ulSequenceNumber+ulLength)'. */

		/* Iterate through all RX segments that are stored: */
		for( pxIterator  = ( const xListItem * ) listGET_NEXT( pxEnd );
			 pxIterator != ( const xListItem * ) pxEnd;
			 pxIterator  = ( const xListItem * ) listGET_NEXT( pxIterator ) )
		{
			pxSegment = ( TCPSegment_t * ) listGET_LIST_ITEM_OWNER( pxIterator );
			/* And see if there is a segment for which:
			'ulSequenceNumber' <= 'pxSegment->ulSequenceNumber' < 'ulNextSequenceNumber'
			If there are more matching segments, the one with the lowest sequence number
			shall be taken */
			if( ( xSequenceGreaterThanOrEqual( pxSegment->ulSequenceNumber, ulSequenceNumber ) != 0 ) &&
				( xSequenceLessThan( pxSegment->ulSequenceNumber, ulNextSequenceNumber ) != 0 ) )
			{
				if( ( pxBest == NULL ) || ( xSequenceLessThan( pxSegment->ulSequenceNumber, pxBest->ulSequenceNumber ) != 0 ) )
				{
					pxBest = pxSegment;
				}
			}
		}

		if( ( pxBest != NULL ) &&
			( ( pxBest->ulSequenceNumber != ulSequenceNumber ) || ( pxBest->lDataLength != ( int ) ulLength ) ) )
		{
			FreeRTOS_flush_logging();
			FreeRTOS_debug_printf( ( "xTCPWindowRxConfirm[%u]: search %lu (+%ld=%lu) found %lu (+%ld=%lu)\n",
				pxWindow->usPeerPortNumber,
				ulSequenceNumber - pxWindow->rx.ulFirstSequenceNumber,
				ulLength,
				ulSequenceNumber + ulLength - pxWindow->rx.ulFirstSequenceNumber,
				pxBest->ulSequenceNumber - pxWindow->rx.ulFirstSequenceNumber,
				pxBest->lDataLength,
				pxBest->ulSequenceNumber + ( ( unsigned int ) pxBest->lDataLength ) - pxWindow->rx.ulFirstSequenceNumber ) );
		}

		return pxBest;
	}

#endif /* ipconfgiUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	int lTCPWindowRxCheck( TCPWindow_t *pxWindow, unsigned int ulSequenceNumber, unsigned int ulLength, unsigned int ulSpace )
	{
	unsigned int ulCurrentSequenceNumber, ulLast, ulSavedSequenceNumber;
	int lReturn, lDistance;
	TCPSegment_t *pxFound;

		/* If lTCPWindowRxCheck( ) returns == 0, the packet will be passed
		directly to user (segment is expected).  If it returns a positive
		number, an earlier packet is missing, but this packet may be stored.
		If negative, the packet has already been stored, or it is out-of-order,
		or there is not enough space.

		As a side-effect, pxWindow->ulUserDataLength will get set to non-zero,
		if more Rx data may be passed to the user after this packet. */

		ulCurrentSequenceNumber = pxWindow->rx.ulCurrentSequenceNumber;

		/* For Selective Ack (SACK), used when out-of-sequence data come in. */
		pxWindow->ucOptionLength = 0;

		/* Non-zero if TCP-windows contains data which must be popped. */
		pxWindow->ulUserDataLength = 0;

		if( ulCurrentSequenceNumber == ulSequenceNumber )
		{
			/* This is the packet with the lowest sequence number we're waiting
			for.  It can be passed directly to the rx stream. */
			if( ulLength > ulSpace )
			{
				FreeRTOS_debug_printf( ( "lTCPWindowRxCheck: Refuse %lu bytes, due to lack of space (%lu)\n", ulLength, ulSpace ) );
				lReturn = -1;
			}
			else
			{
				ulCurrentSequenceNumber += ulLength;

				if( listCURRENT_LIST_LENGTH( &( pxWindow->xRxSegments ) ) != 0 )
				{
					ulSavedSequenceNumber = ulCurrentSequenceNumber;

					/* See if (part of) this segment has been stored already,
					but this rarely happens. */
					pxFound = xTCPWindowRxConfirm( pxWindow, ulSequenceNumber, ulLength );
					if( pxFound != NULL )
					{
						ulCurrentSequenceNumber = pxFound->ulSequenceNumber + ( ( unsigned int ) pxFound->lDataLength );

						/* Remove it because it will be passed to user directly. */
						vTCPWindowFree( pxFound );
					}

					/*  Check for following segments that are already in the
					queue and increment ulCurrentSequenceNumber. */
					while( ( pxFound = xTCPWindowRxFind( pxWindow, ulCurrentSequenceNumber ) ) != NULL )
					{
						ulCurrentSequenceNumber += ( unsigned int ) pxFound->lDataLength;

						/* As all packet below this one have been passed to the
						user it can be discarded. */
						vTCPWindowFree( pxFound );
					}

					if( ulSavedSequenceNumber != ulCurrentSequenceNumber )
					{
						/*  After the current data-package, there is more data
						to be popped. */
						pxWindow->ulUserDataLength = ulCurrentSequenceNumber - ulSavedSequenceNumber;

						if( xTCPWindowLoggingLevel >= 1 )
						{
							FreeRTOS_debug_printf( ( "lTCPWindowRxCheck[%d,%d]: retran %lu (Found %lu bytes at %lu cnt %ld)\n",
								pxWindow->usPeerPortNumber, pxWindow->usOurPortNumber,
								ulSequenceNumber - pxWindow->rx.ulFirstSequenceNumber,
								pxWindow->ulUserDataLength,
								ulSavedSequenceNumber - pxWindow->rx.ulFirstSequenceNumber,
								listCURRENT_LIST_LENGTH( &pxWindow->xRxSegments ) ) );
						}
					}
				}

				pxWindow->rx.ulCurrentSequenceNumber = ulCurrentSequenceNumber;

				/* Packet was expected, may be passed directly to the socket
				buffer or application.  Store the packet at offset 0. */
				lReturn = 0;
			}
		}
		else if( ulCurrentSequenceNumber == ( ulSequenceNumber + 1 ) )
		{
			/* Looks like a TCP keep-alive message.  Do not accept/store Rx data
			ulUserDataLength = 0. Not packet out-of-sync.  Just reply to it. */
			lReturn = -1;
		}
		else
		{
			/* The packet is not the one expected.  See if it falls within the Rx
			window so it can be stored. */

			/*  An "out-of-sequence" segment was received, must have missed one.
			Prepare a SACK (Selective ACK). */
			ulLast = ulSequenceNumber + ulLength;
			lDistance = ( int ) ( ulLast - ulCurrentSequenceNumber );

			if( lDistance <= 0 )
			{
				/* An earlier has been received, must be a retransmission of a
				packet that has been accepted already.  No need to send out a
				Selective ACK (SACK). */
				lReturn = -1;
			}
			else if( lDistance > ( int ) ulSpace )
			{
				/* The new segment is ahead of rx.ulCurrentSequenceNumber.  The
				sequence number of this packet is too far ahead, ignore it. */
				FreeRTOS_debug_printf( ( "lTCPWindowRxCheck: Refuse %lu+%lu bytes, due to lack of space (%lu)\n", lDistance, ulLength, ulSpace ) );
				lReturn = -1;
			}
			else
			{
				/* See if there is more data in a contiguous block to make the
				SACK describe a longer range of data. */

				/* TODO: SACK's may also be delayed for a short period
				 * This is useful because subsequent packets will be SACK'd with
				 * single one message
				 */
				while( ( pxFound = xTCPWindowRxFind( pxWindow, ulLast ) ) != NULL )
				{
					ulLast += ( unsigned int ) pxFound->lDataLength;
				}

				if( xTCPWindowLoggingLevel >= 1 )
				{
					FreeRTOS_debug_printf( ( "lTCPWindowRxCheck[%d,%d]: seqnr %lu exp %lu (dist %ld) SACK to %lu\n",
						pxWindow->usPeerPortNumber, pxWindow->usOurPortNumber,
						ulSequenceNumber - pxWindow->rx.ulFirstSequenceNumber,
						ulCurrentSequenceNumber - pxWindow->rx.ulFirstSequenceNumber,
						( portBASE_TYPE ) ( ulSequenceNumber - ulCurrentSequenceNumber ),	/* want this signed */
						ulLast - pxWindow->rx.ulFirstSequenceNumber ) );
				}

				/* Now prepare the SACK message.
				Code OPTION_CODE_SINGLE_SACK already in network byte order. */
				pxWindow->ulOptionsData[0] = OPTION_CODE_SINGLE_SACK;

				/* First sequence number that we received. */
				pxWindow->ulOptionsData[1] = FreeRTOS_htonl( ulSequenceNumber );

				/* Last + 1 */
				pxWindow->ulOptionsData[2] = FreeRTOS_htonl( ulLast );

				/* Which make 12 (3*4) option bytes. */
				pxWindow->ucOptionLength = 3 * sizeof( pxWindow->ulOptionsData[ 0 ] );

				pxFound = xTCPWindowRxFind( pxWindow, ulSequenceNumber );

				if( pxFound != NULL )
				{
					/* This out-of-sequence packet has been received for a
					second time.  It is already stored but do send a SACK
					again. */
					lReturn = -1;
				}
				else
				{
					pxFound = xTCPWindowRxNew( pxWindow, ulSequenceNumber, ( int ) ulLength );

					if( pxFound == NULL )
					{
						/* Can not send a SACK, because the segment cannot be
						stored. */
						pxWindow->ucOptionLength = 0;

						/* Needs to be stored but there is no segment
						available. */
						lReturn = -1;
					}
					else
					{
						if( xTCPWindowLoggingLevel != 0 )
						{
							FreeRTOS_debug_printf( ( "lTCPWindowRxCheck[%u,%u]: seqnr %lu (cnt %lu)\n",
								pxWindow->usPeerPortNumber, pxWindow->usOurPortNumber, ulSequenceNumber - pxWindow->rx.ulFirstSequenceNumber,
								listCURRENT_LIST_LENGTH( &pxWindow->xRxSegments ) ) );
							FreeRTOS_flush_logging( );
						}

						/* Return a positive value.  The packet may be accepted
						and stored but an earlier packet is still missing. */
						lReturn = ( int ) ( ulSequenceNumber - ulCurrentSequenceNumber );
					}
				}
			}
		}

		return lReturn;
	}

#endif /* ipconfgiUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

/*=============================================================================
 *
 *                    #########   #    #
 *                    #   #   #   #    #
 *                        #       #    #
 *                        #        ####
 *                        #         ##
 *                        #        ####
 *                        #       #    #
 *                        #       #    #
 *                      #####     #    #
 *
 * Tx functions
 *
 *=============================================================================*/

#if( ipconfigUSE_TCP_WIN == 1 )

	static int lTCPIncrementTxPosition( int lPosition, int lMax, int lCount )
	{
		/* +TCP stores data in circular buffers.  Calculate the next position to
		store. */
		lPosition += lCount;
		if( lPosition >= lMax )
		{
			lPosition -= lMax;
		}

		return lPosition;
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	int lTCPWindowTxAdd( TCPWindow_t *pxWindow, unsigned int ulLength, int lPosition, int lMax )
	{
	int lBytesLeft = ( int ) ulLength, lToWrite;
	int lDone = 0;
	TCPSegment_t *pxSegment = pxWindow->pxHeadSegment;

		/* Puts a message in the Tx-window (after buffer size has been
		verified). */
		if( pxSegment != NULL )
		{
			if( pxSegment->lDataLength < pxSegment->lMaxLength )
			{
				if( ( pxSegment->u.bits.bOutstanding == 0 ) && ( pxSegment->lDataLength != 0 ) )
				{
					/* Adding data to a segment that was already in the TX queue.  It
					will be filled-up to a maximum of MSS (maximum segment size). */
					lToWrite = FreeRTOS_min_int32( lBytesLeft, pxSegment->lMaxLength - pxSegment->lDataLength );

					pxSegment->lDataLength += lToWrite;

					if( pxSegment->lDataLength >= pxSegment->lMaxLength )
					{
						/* This segment is full, don't add more bytes. */
						pxWindow->pxHeadSegment = NULL;
					}

					lBytesLeft -= lToWrite;

					/* ulNextTxSequenceNumber is the sequence number of the next byte to
					be stored for transmission. */
					pxWindow->ulNextTxSequenceNumber += ( unsigned int ) lToWrite;

					/* Increased the return value. */
					lDone += lToWrite;

					/* Some detailed logging, for those who're interested. */
					if( ( xTCPWindowLoggingLevel >= 2 ) && ( ipconfigTCP_MAY_LOG_PORT( pxWindow->usOurPortNumber ) != 0 ) )
					{
						FreeRTOS_debug_printf( ( "lTCPWindowTxAdd: Add %4lu bytes for seqNr %lu len %4lu (nxt %lu) pos %lu\n",
							ulLength,
							pxSegment->ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber,
							pxSegment->lDataLength,
							pxWindow->ulNextTxSequenceNumber - pxWindow->tx.ulFirstSequenceNumber,
							pxSegment->lStreamPos ) );
						FreeRTOS_flush_logging( );
					}

					/* Calculate the next position in the circular data buffer, knowing
					its maximum length 'lMax'. */
					lPosition = lTCPIncrementTxPosition( lPosition, lMax, lToWrite );
				}
			}
		}

		while( lBytesLeft > 0 )
		{
			/* The current transmission segment is full, create new segments as
			needed. */
			pxSegment = xTCPWindowTxNew( pxWindow, pxWindow->ulNextTxSequenceNumber, pxWindow->usMSS );

			if( pxSegment != NULL )
			{
				/* Store as many as needed, but no more than the maximum
				(MSS). */
				lToWrite = FreeRTOS_min_int32( lBytesLeft, pxSegment->lMaxLength );

				pxSegment->lDataLength = lToWrite;
				pxSegment->lStreamPos = lPosition;
				lBytesLeft -= lToWrite;
				lPosition = lTCPIncrementTxPosition( lPosition, lMax, lToWrite );
				pxWindow->ulNextTxSequenceNumber += ( unsigned int ) lToWrite;
				lDone += lToWrite;

				/* Link this segment in the Tx-Queue. */
				vListInsertFifo( &( pxWindow->xTxQueue ), &( pxSegment->xQueueItem ) );

				/* Let 'pxHeadSegment' point to this segment if there is still
				space. */
				if( pxSegment->lDataLength < pxSegment->lMaxLength )
				{
					pxWindow->pxHeadSegment = pxSegment;
				}
				else
				{
					pxWindow->pxHeadSegment = NULL;
				}

				if( ipconfigTCP_MAY_LOG_PORT( pxWindow->usOurPortNumber ) != 0 )
				{
					if( ( xTCPWindowLoggingLevel >= 3 ) ||
						( ( xTCPWindowLoggingLevel >= 2 ) && ( pxWindow->pxHeadSegment != NULL ) ) )
					{
						FreeRTOS_debug_printf( ( "lTCPWindowTxAdd: New %4ld bytes for seqNr %lu len %4lu (nxt %lu) pos %lu\n",
							ulLength,
							pxSegment->ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber,
							pxSegment->lDataLength,
							pxWindow->ulNextTxSequenceNumber - pxWindow->tx.ulFirstSequenceNumber,
							pxSegment->lStreamPos ) );
						FreeRTOS_flush_logging( );
					}
				}
			}
			else
			{
				/* A sever situation: running out of segments for transmission.
				No more data can be sent at the moment. */
				if( lDone != 0 )
				{
					FreeRTOS_debug_printf( ( "lTCPWindowTxAdd: Sorry all buffers full (cancel %ld bytes)\n", lBytesLeft ) );
				}
				break;
			}
		}

		return lDone;
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	portBASE_TYPE xTCPWindowTxDone( TCPWindow_t *pxWindow )
	{
		return listLIST_IS_EMPTY( ( &pxWindow->xTxSegments) );
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	static portBASE_TYPE prvTCPWindowTxHasSpace( TCPWindow_t *pxWindow, unsigned int ulWindowSize )
	{
	unsigned int ulTxOutstanding;
	portBASE_TYPE xHasSpace;
	TCPSegment_t *pxSegment;

		/* This function will look if there is new transmission data.  It will
		return true if there is data to be sent. */

		pxSegment = xTCPWindowPeekHead( &( pxWindow->xTxQueue ) );

		if( pxSegment == NULL )
		{
			xHasSpace = pdFALSE;
		}
		else
		{
			/* How much data is outstanding, i.e. how much data has been sent
			but not yet acknowledged ? */
			if( pxWindow->tx.ulHighestSequenceNumber >= pxWindow->tx.ulCurrentSequenceNumber )
			{
				ulTxOutstanding = pxWindow->tx.ulHighestSequenceNumber - pxWindow->tx.ulCurrentSequenceNumber;
			}
			else
			{
				ulTxOutstanding = 0;
			}

			/* Subtract this from the peer's space. */
			ulWindowSize -= FreeRTOS_min_uint32( ulWindowSize, ulTxOutstanding );

			/* See if the next segment may be sent. */
			if( ulWindowSize >= ( unsigned int ) pxSegment->lDataLength )
			{
				xHasSpace = pdTRUE;
			}
			else
			{
				xHasSpace = pdFALSE;
			}

			/* If 'xHasSpace', it looks like the peer has at least space for 1
			more new segment of size MSS.  xSize.ulTxWindowLength is the self-imposed
			limitation of the transmission window (in case of many resends it
			may be decreased). */
			if( ( ulTxOutstanding != 0UL ) && ( pxWindow->xSize.ulTxWindowLength < ulTxOutstanding + ( ( unsigned int ) pxSegment->lDataLength ) ) )
			{
				xHasSpace = pdFALSE;
			}
		}

		return xHasSpace;
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	portBASE_TYPE xTCPWindowTxHasData( TCPWindow_t *pxWindow, unsigned int ulWindowSize, portTickType *pulDelay )
	{
	TCPSegment_t *pxSegment;
	portBASE_TYPE xReturn;
	portTickType ulAge, ulMaxAge;

		*pulDelay = 0;

		if( listLIST_IS_EMPTY( &pxWindow->xPriorityQueue ) == pdFALSE )
		{
			/* No need to look at retransmissions or new transmission as long as
			there are priority segments.  *pulDelay equals zero, meaning it must
			be sent out immediately. */
			xReturn = pdTRUE;
		}
		else
		{
			pxSegment = xTCPWindowPeekHead( &( pxWindow->xWaitQueue ) );

			if( pxSegment != NULL )
			{
				/* There is an outstanding segment, see if it is time to resend
				it. */
				ulAge = ulTimerGetAge( &pxSegment->xTransmitTimer );

				/* After a packet has been sent for the first time, it will wait
				'1 * lSRTT' ms for an ACK. A second time it will wait '2 * lSRTT' ms,
				each time doubling the time-out */
				ulMaxAge = ( 1u << pxSegment->u.bits.ucTransmitCount ) * ( ( unsigned int ) pxWindow->lSRTT );

				if( ulMaxAge > ulAge )
				{
					/* A segment must be sent after this amount of msecs */
					*pulDelay = ulMaxAge - ulAge;
				}

				xReturn = pdTRUE;
			}
			else
			{
				/* No priority segment, no outstanding data, see if there is new
				transmission data. */
				pxSegment = xTCPWindowPeekHead( &pxWindow->xTxQueue );

				/* See if it fits in the peer's reception window. */
				if( pxSegment == NULL )
				{
					xReturn = pdFALSE;
				}
				else if( prvTCPWindowTxHasSpace( pxWindow, ulWindowSize ) == pdFALSE )
				{
					/* Too many outstanding messages. */
					xReturn = pdFALSE;
				}
				else if( ( pxWindow->u.bits.bSendFullSize != 0 ) && ( pxSegment->lDataLength < pxSegment->lMaxLength ) )
				{
					/* 'bSendFullSize' is a special optimisation.  If true, the
					driver will only sent completely filled packets (of MSS
					bytes). */
					xReturn = pdFALSE;
				}
				else
				{
					xReturn = pdTRUE;
				}
			}
		}

		return xReturn;
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	unsigned int ulTCPWindowTxGet( TCPWindow_t *pxWindow, unsigned int ulWindowSize, int *plPosition )
	{
	TCPSegment_t *pxSegment;
	unsigned int ulMaxTime;
	unsigned int ulReturn  = ~0UL;


		/* Fetches data to be sent-out now.

		Priority messages: segments with a resend need no check current sliding
		window size. */
		pxSegment = xTCPWindowGetHead( &( pxWindow->xPriorityQueue ) );
		pxWindow->ulOurSequenceNumber = pxWindow->tx.ulHighestSequenceNumber;

		if( pxSegment == NULL )
		{
			/* Waiting messages: outstanding messages with a running timer
			neither check peer's reception window size because these packets
			have been sent earlier. */
			pxSegment = xTCPWindowPeekHead( &( pxWindow->xWaitQueue ) );

			if( pxSegment != NULL )
			{
				/* Do check the timing. */
				ulMaxTime = ( 1u << pxSegment->u.bits.ucTransmitCount ) * ( ( unsigned int ) pxWindow->lSRTT );

				if( ulTimerGetAge( &pxSegment->xTransmitTimer ) > ulMaxTime )
				{
					/* A normal (non-fast) retransmission.  Move it from the
					head of the waiting queue. */
					pxSegment = xTCPWindowGetHead( &( pxWindow->xWaitQueue ) );
					pxSegment->u.bits.ucDupAckCount = 0;

					/* Some detailed logging. */
					if( ( xTCPWindowLoggingLevel != 0 ) && ( ipconfigTCP_MAY_LOG_PORT( pxWindow->usOurPortNumber ) != 0 ) )
					{
						FreeRTOS_debug_printf( ( "ulTCPWindowTxGet[%u,%u]: WaitQueue %ld bytes for sequence number %lu (%lX)\n",
							pxWindow->usPeerPortNumber,
							pxWindow->usOurPortNumber,
							pxSegment->lDataLength,
							pxSegment->ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber,
							pxSegment->ulSequenceNumber ) );
						FreeRTOS_flush_logging( );
					}
				}
				else
				{
					pxSegment = NULL;
				}
			}

			if( pxSegment == NULL )
			{
				/* New messages: sent-out for the first time.  Check current
				sliding window size of peer. */
				pxSegment = xTCPWindowPeekHead( &( pxWindow->xTxQueue ) );

				if( pxSegment == NULL )
				{
					/* No segments queued. */
					ulReturn = 0;
				}
				else if( ( pxWindow->u.bits.bSendFullSize != 0 ) && ( pxSegment->lDataLength < pxSegment->lMaxLength ) )
				{
					/* A segment has been queued but the driver waits until it
					has a full size of MSS. */
					ulReturn = 0;
				}
				else if( prvTCPWindowTxHasSpace( pxWindow, ulWindowSize ) == pdFALSE )
				{
					/* Peer has no more space at this moment. */
					ulReturn = 0;
				}
				else
				{
					/* Move it out of the Tx queue. */
					pxSegment = xTCPWindowGetHead( &( pxWindow->xTxQueue ) );

					/* Don't let pxHeadSegment point to this segment any more,
					so no more data will be added. */
					if( pxWindow->pxHeadSegment == pxSegment )
					{
						pxWindow->pxHeadSegment = NULL;
					}

					/* pxWindow->tx.highest registers the highest sequence
					number in our transmission window. */
					pxWindow->tx.ulHighestSequenceNumber = pxSegment->ulSequenceNumber + ( ( unsigned int ) pxSegment->lDataLength );

					/* ...and more detailed logging */
					if( ( xTCPWindowLoggingLevel >= 2 ) && ( ipconfigTCP_MAY_LOG_PORT( pxWindow->usOurPortNumber ) != pdFALSE ) )
					{
						FreeRTOS_debug_printf( ( "ulTCPWindowTxGet[%u,%u]: XmitQueue %ld bytes for sequence number %lu (ws %lu)\n",
							pxWindow->usPeerPortNumber,
							pxWindow->usOurPortNumber,
							pxSegment->lDataLength,
							pxSegment->ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber,
							ulWindowSize ) );
						FreeRTOS_flush_logging( );
					}
				}
			}
		}
		else
		{
			/* There is a priority segment. It doesn't need any checking for
			space or timeouts. */
			if( xTCPWindowLoggingLevel != 0 )
			{
				FreeRTOS_debug_printf( ( "ulTCPWindowTxGet[%u,%u]: PrioQueue %ld bytes for sequence number %lu (ws %lu)\n",
					pxWindow->usPeerPortNumber,
					pxWindow->usOurPortNumber,
					pxSegment->lDataLength,
					pxSegment->ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber,
					ulWindowSize ) );
				FreeRTOS_flush_logging( );
			}
		}

		/* See if it has already been determined to return 0. */
		if( ulReturn != 0UL )
		{
			configASSERT( listLIST_ITEM_CONTAINER( &(pxSegment->xQueueItem ) ) == NULL );

			/* Now that the segment will be transmitted, add it to the tail of
			the waiting queue. */
			vListInsertFifo( &pxWindow->xWaitQueue, &pxSegment->xQueueItem );

			/* And mark it as outstanding. */
			pxSegment->u.bits.bOutstanding = pdTRUE;

			/* Administer the transmit count, needed for fast
			retransmissions. */
			( pxSegment->u.bits.ucTransmitCount )++;

			/* If there have been several retransmissions (4), decrease the
			size of the transmission window to at most 2 times MSS. */
			if( pxSegment->u.bits.ucTransmitCount == MAX_TRANSMIT_COUNT_USING_LARGE_WINDOW )
			{
				if( pxWindow->xSize.ulTxWindowLength > ( 2U * pxWindow->usMSS ) )
				{
					FreeRTOS_debug_printf( ( "ulTCPWindowTxGet[%u - %d]: Change Tx window: %lu -> %u\n",
						pxWindow->usPeerPortNumber, pxWindow->usOurPortNumber,
						pxWindow->xSize.ulTxWindowLength, 2 * pxWindow->usMSS ) );
					pxWindow->xSize.ulTxWindowLength = ( 2UL * pxWindow->usMSS );
				}
			}

			/* Clear the transmit timer. */
			vTCPTimerSet( &( pxSegment->xTransmitTimer ) );

			pxWindow->ulOurSequenceNumber = pxSegment->ulSequenceNumber;

			/* Inform the caller where to find the data within the queue. */
			*plPosition = pxSegment->lStreamPos;

			/* And return the length of the data segment */
			ulReturn = ( unsigned int ) pxSegment->lDataLength;
		}

		return ulReturn;
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	static unsigned int prvTCPWindowTxCheckAck( TCPWindow_t *pxWindow, unsigned int ulFirst, unsigned int ulLast )
	{
	unsigned int ulBytesConfirmed = 0;
	unsigned int ulSequenceNumber = ulFirst, ulDataLength;
	const xListItem *pxIterator;
	const xMiniListItem *pxEnd = ( const xMiniListItem* )listGET_END_MARKER( &pxWindow->xTxSegments );
	portBASE_TYPE xDoUnlink;
	TCPSegment_t *pxSegment;
		/* An acknowledgement or a selective ACK (SACK) was received.  See if some outstanding data
		may be removed from the transmission queue(s).
		All TX segments for which
		( ( ulSequenceNumber >= ulFirst ) && ( ulSequenceNumber < ulLast ) in a
		contiguous block.  Note that the segments are stored in xTxSegments in a
		strict sequential order. */

		/* SRTT[i] = (1-a) * SRTT[i-1] + a * RTT

		0 < a < 1; usually a = 1/8

		RTO = 2 * SRTT

		where:
		  RTT is Round Trip Time
		  SRTT is Smoothed RTT
		  RTO is Retransmit timeout

		 A Smoothed RTT will increase quickly, but it is conservative when
		 becoming smaller. */

		for(
				pxIterator  = ( const xListItem * ) listGET_NEXT( pxEnd );
				( pxIterator != ( const xListItem * ) pxEnd ) && ( xSequenceLessThan( ulSequenceNumber, ulLast ) != 0 );
			)
		{
			xDoUnlink = pdFALSE;
			pxSegment = ( TCPSegment_t * ) listGET_LIST_ITEM_OWNER( pxIterator );

			/* Move to the next item because the current item might get
			removed. */
			pxIterator = ( const xListItem * ) listGET_NEXT( pxIterator );

			/* Continue if this segment does not fall within the ACK'd range. */
			if( xSequenceGreaterThan( ulSequenceNumber, pxSegment->ulSequenceNumber ) != pdFALSE )
			{
				continue;
			}

			/* Is it ready? */
			if( ulSequenceNumber != pxSegment->ulSequenceNumber )
			{
				break;
			}

			ulDataLength = ( unsigned int ) pxSegment->lDataLength;

			if( pxSegment->u.bits.bAcked == pdFALSE )
			{
				if( xSequenceGreaterThan( pxSegment->ulSequenceNumber + ( unsigned int )ulDataLength, ulLast ) != pdFALSE )
				{
					/* What happens?  Only part of this segment was accepted,
					probably due to WND limits

					  AAAAAAA BBBBBBB << acked
					  aaaaaaa aaaa    << sent */
					#if( ipconfigHAS_DEBUG_PRINTF != 0 )
					{
						unsigned int ulFirstSeq = pxSegment->ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber;
						FreeRTOS_debug_printf( ( "prvTCPWindowTxCheckAck[%u.%u]: %lu - %lu Partial sequence number %lu - %lu\n",
							pxWindow->usPeerPortNumber,
							pxWindow->usOurPortNumber,
							ulFirstSeq - pxWindow->tx.ulFirstSequenceNumber,
							ulLast - pxWindow->tx.ulFirstSequenceNumber,
							ulFirstSeq, ulFirstSeq + ulDataLength ) );
					}
					#endif /* ipconfigHAS_DEBUG_PRINTF */
					break;
				}

				/* This segment is fully ACK'd, set the flag. */
				pxSegment->u.bits.bAcked = pdTRUE;

				/* Calculate the RTT only if the segment was sent-out for the
				first time and if this is the last ACK'd segment in a range. */
				if( ( pxSegment->u.bits.ucTransmitCount == 1 ) && ( ( pxSegment->ulSequenceNumber + ulDataLength ) == ulLast ) )
				{
					int mS = ( int ) ulTimerGetAge( &( pxSegment->xTransmitTimer ) );

					if( pxWindow->lSRTT >= mS )
					{
						/* RTT becomes smaller: adapt slowly. */
						pxWindow->lSRTT = ( ( winSRTT_DECREMENT_NEW * mS ) + ( winSRTT_DECREMENT_CURRENT * pxWindow->lSRTT ) ) / ( winSRTT_DECREMENT_NEW + winSRTT_DECREMENT_CURRENT );
					}
					else
					{
						/* RTT becomes larger: adapt quicker */
						pxWindow->lSRTT = ( ( winSRTT_INCREMENT_NEW * mS ) + ( winSRTT_INCREMENT_CURRENT * pxWindow->lSRTT ) ) / ( winSRTT_INCREMENT_NEW + winSRTT_INCREMENT_CURRENT );
					}

					/* Cap to the minimum of 50ms. */
					if( pxWindow->lSRTT < winSRTT_CAP_mS )
					{
						pxWindow->lSRTT = winSRTT_CAP_mS;
					}
				}

				/* Unlink it from the 3 queues, but do not destroy it (yet). */
				xDoUnlink = pdTRUE;
			}

			/* pxSegment->u.bits.bAcked is now true.  Is it located at the left
			side of the transmission queue?  If so, it may be freed. */
			if( ulSequenceNumber == pxWindow->tx.ulCurrentSequenceNumber )
			{
				if( ( xTCPWindowLoggingLevel >= 2 ) && ( ipconfigTCP_MAY_LOG_PORT( pxWindow->usOurPortNumber ) != pdFALSE ) )
				{
					FreeRTOS_debug_printf( ( "prvTCPWindowTxCheckAck: %lu - %lu Ready sequence number %lu\n",
						ulFirst - pxWindow->tx.ulFirstSequenceNumber,
						ulLast - pxWindow->tx.ulFirstSequenceNumber,
						pxSegment->ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber ) );
				}

				/* Increase the left-hand value of the transmission window. */
				pxWindow->tx.ulCurrentSequenceNumber += ulDataLength;

				/* This function will return the number of bytes that the tail
				of txStream may be advanced. */
				ulBytesConfirmed += ulDataLength;

				/* All segments below tx.ulCurrentSequenceNumber may be freed. */
				vTCPWindowFree( pxSegment );

				/* No need to unlink it any more. */
				xDoUnlink = pdFALSE;
			}

			if( ( xDoUnlink != pdFALSE ) && ( listLIST_ITEM_CONTAINER( &( pxSegment->xQueueItem ) ) != NULL ) )
			{
				/* Remove item from its queues. */
				vListRemove( &pxSegment->xQueueItem );
			}

			ulSequenceNumber += ulDataLength;
		}

		return ulBytesConfirmed;
	}
#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	static unsigned int prvTCPWindowFastRetransmit( TCPWindow_t *pxWindow, unsigned int ulFirst )
	{
	const xListItem *pxIterator;
	const xMiniListItem* pxEnd;
	TCPSegment_t *pxSegment;
	unsigned int ulCount = 0;

		/* A higher Tx block has been acknowledged.  Now iterate through the
		 xWaitQueue to find a possible condition for a FAST retransmission. */

		pxEnd = ( const xMiniListItem* ) listGET_END_MARKER( &( pxWindow->xWaitQueue ) );

		for( pxIterator  = ( const xListItem * ) listGET_NEXT( pxEnd );
			 pxIterator != ( const xListItem * ) pxEnd; )
		{
			/* Get the owner, which is a TCP segment. */
			pxSegment = ( TCPSegment_t * ) listGET_LIST_ITEM_OWNER( pxIterator );

			/* Hop to the next item before the current gets unlinked. */
			pxIterator  = ( const xListItem * ) listGET_NEXT( pxIterator );

			/* Fast retransmission:
			When 3 packets with a higher sequence number have been acknowledged
			by the peer, it is very unlikely a current packet will ever arrive.
			It will be retransmitted far before the RTO. */
			if( ( pxSegment->u.bits.bAcked == pdFALSE ) &&
				( xSequenceLessThan( pxSegment->ulSequenceNumber, ulFirst ) != pdFALSE ) &&
				( ++( pxSegment->u.bits.ucDupAckCount ) == DUPLICATE_ACKS_BEFORE_FAST_RETRANSMIT ) )
			{
				pxSegment->u.bits.ucTransmitCount = 0;

				/* Not clearing 'ucDupAckCount' yet as more SACK's might come in
				which might lead to a second fast rexmit. */
				if( ( xTCPWindowLoggingLevel >= 0 ) && ( ipconfigTCP_MAY_LOG_PORT( pxWindow->usOurPortNumber ) != pdFALSE ) )
				{
					FreeRTOS_debug_printf( ( "prvTCPWindowFastRetransmit: Requeue sequence number %lu < %lu\n",
						pxSegment->ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber,
						ulFirst - pxWindow->tx.ulFirstSequenceNumber ) );
					FreeRTOS_flush_logging( );
				}

				/* Remove it from xWaitQueue. */
				vListRemove( &pxSegment->xQueueItem );

				/* Add this segment to the priority queue so it gets
				retransmitted immediately. */
				vListInsertFifo( &( pxWindow->xPriorityQueue ), &( pxSegment->xQueueItem ) );
				ulCount++;
			}
		}

		return ulCount;
	}
#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	unsigned int ulTCPWindowTxAck( TCPWindow_t *pxWindow, unsigned int ulSequenceNumber )
	{
	unsigned int ulFirstSequence, ulReturn;

		/* Receive a normal ACK. */

		ulFirstSequence = pxWindow->tx.ulCurrentSequenceNumber;

		if( xSequenceLessThanOrEqual( ulSequenceNumber, ulFirstSequence ) != pdFALSE )
		{
			ulReturn = 0;
		}
		else
		{
			ulReturn = prvTCPWindowTxCheckAck( pxWindow, ulFirstSequence, ulSequenceNumber );
		}

		return ulReturn;
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 1 )

	unsigned int ulTCPWindowTxSack( TCPWindow_t *pxWindow, unsigned int ulFirst, unsigned int ulLast )
	{
	unsigned int ulAckCount = 0;
	unsigned int ulCurrentSequenceNumber = pxWindow->tx.ulCurrentSequenceNumber;

		/* Receive a SACK option. */
		ulAckCount = prvTCPWindowTxCheckAck( pxWindow, ulFirst, ulLast );
		prvTCPWindowFastRetransmit( pxWindow, ulFirst );

		if( ( xTCPWindowLoggingLevel >= 1 ) && ( xSequenceGreaterThan( ulFirst, ulCurrentSequenceNumber ) != pdFALSE ) )
		{
			FreeRTOS_debug_printf( ( "ulTCPWindowTxSack[%u,%u]: from %lu to %lu (ack = %lu)\n",
				pxWindow->usPeerPortNumber,
				pxWindow->usOurPortNumber,
				ulFirst - pxWindow->tx.ulFirstSequenceNumber,
				ulLast - pxWindow->tx.ulFirstSequenceNumber,
				pxWindow->tx.ulCurrentSequenceNumber - pxWindow->tx.ulFirstSequenceNumber ) );
			FreeRTOS_flush_logging( );
		}

		return ulAckCount;
	}

#endif /* ipconfigUSE_TCP_WIN == 1 */
/*-----------------------------------------------------------*/

/*
#####   #                      #####   ####  ######
# # #   #                      # # #  #    #  #    #
  #                              #   #     #  #    #
  #   ###   #####  #    #        #   #        #    #
  #     #   #    # #    #        #   #        #####
  #     #   #    # #    # ####   #   #        #
  #     #   #    # #    #        #   #     #  #
  #     #   #    #  ####         #    #    #  #
 #### ##### #    #     #        ####   ####  ####
                      #
                   ###
*/
#if( ipconfigUSE_TCP_WIN == 0 )

	int lTCPWindowRxCheck( TCPWindow_t *pxWindow, unsigned int ulSequenceNumber, unsigned int ulLength, unsigned int ulSpace )
	{
	int iReturn;

		/* Data was received at 'ulSequenceNumber'.  See if it was expected
		and if there is enough space to store the new data. */
		if( ( pxWindow->rx.ulCurrentSequenceNumber != ulSequenceNumber ) || ( ulSpace < ulLength ) )
		{
			iReturn = -1;
		}
		else
		{
			pxWindow->rx.ulCurrentSequenceNumber += ( unsigned int ) ulLength;
			iReturn = 0;
		}

		return iReturn;
	}

#endif /* ipconfigUSE_TCP_WIN == 0 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 0 )

	int lTCPWindowTxAdd( TCPWindow_t *pxWindow, unsigned int ulLength, int lPosition, int lMax )
	{
	TCPSegment_t *pxSegment = &( pxWindow->xTxSegment );
	int lResult;

		/* Data is being scheduled for transmission. */

		/* lMax would indicate the size of the txStream. */
		( void ) lMax;
		/* This is tiny TCP: there is only 1 segment for outgoing data.
		As long as 'lDataLength' is unequal to zero, the segment is still occupied. */
		if( pxSegment->lDataLength > 0 )
		{
			lResult = 0L;
		}
		else
		{
			if( ulLength > ( unsigned int ) pxSegment->lMaxLength )
			{
				if( ( xTCPWindowLoggingLevel != 0 ) && ( ipconfigTCP_MAY_LOG_PORT( pxWindow->usOurPortNumber ) != pdFALSE ) )
				{
					FreeRTOS_debug_printf( ( "lTCPWindowTxAdd: can only store %ld / %ld bytes\n", ulLength, pxSegment->lMaxLength ) );
				}

				ulLength = ( unsigned int ) pxSegment->lMaxLength;
			}

			if( ( xTCPWindowLoggingLevel != 0 ) && ( ipconfigTCP_MAY_LOG_PORT( pxWindow->usOurPortNumber ) != pdFALSE ) )
			{
				FreeRTOS_debug_printf( ( "lTCPWindowTxAdd: SeqNr %ld (%ld) Len %ld\n",
					pxWindow->ulNextTxSequenceNumber - pxWindow->tx.ulFirstSequenceNumber,
					pxWindow->tx.ulCurrentSequenceNumber - pxWindow->tx.ulFirstSequenceNumber,
					ulLength ) );
			}

			/* The sequence number of the first byte in this packet. */
			pxSegment->ulSequenceNumber = pxWindow->ulNextTxSequenceNumber;
			pxSegment->lDataLength = ( int ) ulLength;
			pxSegment->lStreamPos = lPosition;
			pxSegment->u.ulFlags = 0;
			vTCPTimerSet( &( pxSegment->xTransmitTimer ) );

			/* Increase the sequence number of the next data to be stored for
			transmission. */
			pxWindow->ulNextTxSequenceNumber += ulLength;
			lResult = ( int )ulLength;
		}

		return lResult;
	}

#endif /* ipconfigUSE_TCP_WIN == 0 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 0 )

	unsigned int ulTCPWindowTxGet( TCPWindow_t *pxWindow, unsigned int ulWindowSize, int *plPosition )
	{
	TCPSegment_t *pxSegment = &( pxWindow->xTxSegment );
	unsigned int ulLength = ( unsigned int ) pxSegment->lDataLength;
	unsigned int ulMaxTime;

		if( ulLength != 0 )
		{
			/* _HT_ Still under investigation */
			( void ) ulWindowSize;

			if( pxSegment->u.bits.bOutstanding != pdFALSE )
			{
				/* As 'ucTransmitCount' has a minimum of 1, take 2 * RTT */
				ulMaxTime = ( 1u << pxSegment->u.bits.ucTransmitCount ) * ( ( unsigned int ) pxWindow->lSRTT );

				if( ulTimerGetAge( &( pxSegment->xTransmitTimer ) ) < ulMaxTime )
				{
					ulLength = 0;
				}
			}

			if( ulLength != 0 )
			{
				pxSegment->u.bits.bOutstanding = pdTRUE;
				pxSegment->u.bits.ucTransmitCount++;
				vTCPTimerSet (&pxSegment->xTransmitTimer);
				pxWindow->ulOurSequenceNumber = pxSegment->ulSequenceNumber;
				*plPosition = pxSegment->lStreamPos;
			}
		}

		return ulLength;
	}

#endif /* ipconfigUSE_TCP_WIN == 0 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 0 )

	portBASE_TYPE xTCPWindowTxDone( TCPWindow_t *pxWindow )
	{
	portBASE_TYPE xReturn;

		/* Has the outstanding data been sent because user wants to shutdown? */
		if( pxWindow->xTxSegment.lDataLength == 0 )
		{
			xReturn = pdTRUE;
		}
		else
		{
			xReturn = pdFALSE;
		}

		return xReturn;
	}

#endif /* ipconfigUSE_TCP_WIN == 0 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 0 )

	static portBASE_TYPE prvTCPWindowTxHasSpace( TCPWindow_t *pxWindow, unsigned int ulWindowSize )
	{
	portBASE_TYPE xReturn;

		if( ulWindowSize >= pxWindow->usMSSInit )
		{
			xReturn = pdTRUE;
		}
		else
		{
			xReturn = pdFALSE;
		}

		return xReturn;
	}

#endif /* ipconfigUSE_TCP_WIN == 0 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 0 )

	portBASE_TYPE xTCPWindowTxHasData( TCPWindow_t *pxWindow, unsigned int ulWindowSize, portTickType *pulDelay )
	{
	TCPSegment_t *pxSegment = &( pxWindow->xTxSegment );
	portBASE_TYPE xReturn;
	portTickType ulAge, ulMaxAge;

		/* Check data to be sent. */
		*pulDelay = 0;
		if( pxSegment->lDataLength == 0 )
		{
			/* Got nothing to send right now. */
			xReturn = pdFALSE;
		}
		else
		{
			if( pxSegment->u.bits.bOutstanding != pdFALSE )
			{
				ulAge = ulTimerGetAge ( &pxSegment->xTransmitTimer );
				ulMaxAge = (1u << pxSegment->u.bits.ucTransmitCount ) * ( ( unsigned int ) pxWindow->lSRTT );

				if( ulMaxAge > ulAge )
				{
					*pulDelay = ulMaxAge - ulAge;
				}

				xReturn = pdTRUE;
			}
			else if( prvTCPWindowTxHasSpace( pxWindow, ulWindowSize ) == pdFALSE )
			{
				/* Too many outstanding messages. */
				xReturn = pdFALSE;
			}
			else
			{
				xReturn = pdTRUE;
			}
		}

		return xReturn;
	}

#endif /* ipconfigUSE_TCP_WIN == 0 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 0 )

	unsigned int ulTCPWindowTxAck( TCPWindow_t *pxWindow, unsigned int ulSequenceNumber )
	{
	TCPSegment_t *pxSegment = &( pxWindow->xTxSegment );
	unsigned int ulDataLength = ( unsigned int ) pxSegment->lDataLength;

		/* Receive a normal ACK */

		if( ulDataLength != 0 )
		{
			if( ulSequenceNumber < ( pxWindow->tx.ulCurrentSequenceNumber + ulDataLength ) )
			{
				if( ipconfigTCP_MAY_LOG_PORT( pxWindow->usOurPortNumber ) != pdFALSE )
				{
					FreeRTOS_debug_printf( ( "win_tx_ack: acked %ld expc %ld len %ld\n",
						ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber,
						pxWindow->tx.ulCurrentSequenceNumber - pxWindow->tx.ulFirstSequenceNumber,
						ulDataLength ) );
				}

				/* Nothing to send right now. */
				ulDataLength = 0;
			}
			else
			{
				pxWindow->tx.ulCurrentSequenceNumber += ulDataLength;

				if( ( xTCPWindowLoggingLevel != 0 ) && ( ipconfigTCP_MAY_LOG_PORT( pxWindow->usOurPortNumber ) != pdFALSE ) )
				{
					FreeRTOS_debug_printf( ( "win_tx_ack: acked seqnr %ld len %ld\n",
						ulSequenceNumber - pxWindow->tx.ulFirstSequenceNumber,
						ulDataLength ) );
				}

				pxSegment->lDataLength = 0;
			}
		}

		return ulDataLength;
	}

#endif /* ipconfigUSE_TCP_WIN == 0 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 0 )

	portBASE_TYPE xTCPWindowRxEmpty( TCPWindow_t *pxWindow )
	{
		/* Return true if 'ulCurrentSequenceNumber >= ulHighestSequenceNumber'
		'ulCurrentSequenceNumber' is the highest sequence number stored,
		'ulHighestSequenceNumber' is the highest sequence number seen. */
		return xSequenceGreaterThanOrEqual( pxWindow->rx.ulCurrentSequenceNumber, pxWindow->rx.ulHighestSequenceNumber );
	}

#endif /* ipconfigUSE_TCP_WIN == 0 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_TCP_WIN == 0 )

	/* Destroy a window (always returns NULL) */
	void vTCPWindowDestroy( TCPWindow_t *pxWindow )
	{
		/* As in tiny TCP there are no shared segments descriptors, there is
		nothing to release. */
		( void ) pxWindow;
	}

#endif /* ipconfigUSE_TCP_WIN == 0 */
/*-----------------------------------------------------------*/


