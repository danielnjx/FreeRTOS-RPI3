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
 *	FreeRTOS_TCP_WIN.c
 *  Module which handles the TCP windowing schemes for FreeRTOS-PLUS-TCP
 */

#ifndef	FREERTOS_TCP_WIN_H
#define	FREERTOS_TCP_WIN_H

#ifdef	__cplusplus
extern "C" {
#endif

extern portBASE_TYPE xTCPWindowLoggingLevel;

typedef struct _STcpTimer
{
	unsigned int ulBorn;
} TcpTimer_t;

typedef struct xTCP_SEGMENT
{
	unsigned int ulSequenceNumber;		/* The sequence number of the first byte in this packet */
	int lMaxLength;			/* Maximum space, number of bytes which can be stored in this segment */
	int lDataLength;			/* Actual number of bytes */
	int lStreamPos;				/* reference to the [t|r]xStream of the socket */
	TcpTimer_t xTransmitTimer;		/* saves a timestamp at the moment this segment gets transmitted (TX only) */
	union
	{
		struct
		{
			unsigned int
				ucTransmitCount : 8,/* Number of times the segment has been transmitted, used to calculate the RTT */
				ucDupAckCount : 8,	/* Counts the number of times that a higher segment was ACK'd. After 3 times a Fast Retransmission takes place */
				bOutstanding : 1,	/* It the peer's turn, we're just waiting for an ACK */
				bAcked : 1,			/* This segment has been acknowledged */
				bIsForRx : 1;		/* pdTRUE if segment is used for reception */
		} bits;
		unsigned int ulFlags;
	} u;
#if( ipconfigUSE_TCP_WIN != 0 )
	struct xLIST_ITEM xQueueItem;	/* TX only: segments can be linked in one of three queues: xPriorityQueue, xTxQueue, and xWaitQueue */
	struct xLIST_ITEM xListItem;	/* With this item the segment can be connected to a list, depending on who is owning it */
#endif
} TCPSegment_t;

typedef struct xTCP_WINSIZE
{
	unsigned int ulRxWindowLength;
	unsigned int ulTxWindowLength;
} TCPWinSize_t;

/*
 * If TCP time-stamps are being used, they will occupy 12 bytes in
 * each packet, and thus the message space will become smaller
 */
/* Keep this as a multiple of 4 */
#if	ipconfigUSE_TCP_TIMESTAMPS == 1
#	define ipSIZE_TCP_OPTIONS   (12+12)
#else
#	define ipSIZE_TCP_OPTIONS   12
#endif

/*
 *	Every TCP connection owns a TCP window for the administration of all packets
 *	It owns two sets of segment descriptors, incoming and outgoing
 */
typedef struct xTCP_WINDOW
{
	union
	{
		struct
		{
			unsigned int
				bHasInit : 1,		/* The window structure has been initialised */
				bSendFullSize : 1,	/* May only send packets with a size equal to MSS (for optimisation) */
				bTimeStamps : 1;	/* Socket is supposed to use TCP time-stamps. This depends on the */
		} bits;						/* party which opens the connection */
		unsigned int ulFlags;
	} u;
	TCPWinSize_t xSize;
	struct
	{
		unsigned int ulFirstSequenceNumber;	 /* Logging & debug: the first segment received/sent in this connection
										  * for Tx: initial send sequence number (ISS)
										  * for Rx: initial receive sequence number (IRS) */
		unsigned int ulCurrentSequenceNumber;/* Tx/Rx: the oldest sequence number not yet confirmed, also SND.UNA / RCV.NXT
										  * In other words: the sequence number of the left side of the sliding window */
		unsigned int ulFINSequenceNumber;	 /* The sequence number which carried the FIN flag */
		unsigned int ulHighestSequenceNumber;/* Sequence number of the right-most byte + 1 */
#if( ipconfigUSE_TCP_TIMESTAMPS == 1 )
		unsigned int ulTimeStamp;			 /* The value of the TCP timestamp, transmitted or received */
#endif
	} rx, tx;
	unsigned int ulOurSequenceNumber;		/* The SEQ number we're sending out */
	unsigned int ulUserDataLength;			/* Number of bytes in Rx buffer which may be passed to the user, after having received a 'missing packet' */
	unsigned int ulNextTxSequenceNumber;	/* The sequence number given to the next byte to be added for transmission */
	int lSRTT;						/* Smoothed Round Trip Time, it may increment quickly and it decrements slower */
	unsigned char ucOptionLength;				/* Number of valid bytes in ulOptionsData[] */
#if( ipconfigUSE_TCP_WIN == 1 )
	xList xPriorityQueue;				/* Priority queue: segments which must be sent immediately */
	xList xTxQueue;					/* Transmit queue: segments queued for transmission */
	xList xWaitQueue;					/* Waiting queue:  outstanding segments */
	TCPSegment_t *pxHeadSegment;		/* points to a segment which has not been transmitted and it's size is still growing (user data being added) */
	unsigned int ulOptionsData[ipSIZE_TCP_OPTIONS/sizeof(unsigned int)];	/* Contains the options we send out */
	xList xTxSegments;					/* A linked list of all transmission segments, sorted on sequence number */
	xList xRxSegments;					/* A linked list of reception segments, order depends on sequence of arrival */
#else
	/* For tiny TCP, there is only 1 outstanding TX segment */
	TCPSegment_t xTxSegment;			/* Priority queue */
#endif
	unsigned short usOurPortNumber;			/* Mostly for debugging/logging: our TCP port number */
	unsigned short usPeerPortNumber;			/* debugging/logging: the peer's TCP port number */
	unsigned short usMSS;						/* Current accepted MSS */
	unsigned short usMSSInit;					/* MSS as configured by the socket owner */
} TCPWindow_t;


/*=============================================================================
 *
 * Creation and destruction
 *
 *=============================================================================*/

/* Create and initialize a window */
void vTCPWindowCreate( TCPWindow_t *pxWindow, unsigned int xRxWindowLength,
	unsigned int ulTxWindowLength, unsigned int ulAckNumber, unsigned int ulSequenceNumber, unsigned int ulMSS );

/* Destroy a window (always returns NULL)
 * It will free some resources: a collection of segments */
void vTCPWindowDestroy( TCPWindow_t *pxWindow );

/* Initialize a window */
void vTCPWindowInit( TCPWindow_t *pxWindow, unsigned int ulAckNumber, unsigned int ulSequenceNumber, unsigned int ulMSS );

/*=============================================================================
 *
 * Rx functions
 *
 *=============================================================================*/

/* if true may be passed directly to user (segment expected and window is empty)
 * But pxWindow->ackno should always be used to set "BUF->ackno" */
int lTCPWindowRxCheck( TCPWindow_t *pxWindow, unsigned int ulSequenceNumber, unsigned int ulLength, unsigned int ulSpace );

/* When lTCPWindowRxCheck returned false, please call store for this unexpected data */
portBASE_TYPE xTCPWindowRxStore( TCPWindow_t *pxWindow, unsigned int ulSequenceNumber, unsigned int ulLength );

/* This function will be called as soon as a FIN is received. It will return true
 * if there are no 'open' reception segments */
portBASE_TYPE xTCPWindowRxEmpty( TCPWindow_t *pxWindow );

/* _HT_ Temporary function for testing/debugging
 * Not used at this moment */
void vTCPWinShowSegments( TCPWindow_t *pxWindow, portBASE_TYPE bForRx );

/*=============================================================================
 *
 * Tx functions
 *
 *=============================================================================*/

/* Adds data to the Tx-window */
int lTCPWindowTxAdd( TCPWindow_t *pxWindow, unsigned int ulLength, int lPosition, int lMax );

/* Check data to be sent and calculate the time period we may sleep */
portBASE_TYPE xTCPWindowTxHasData( TCPWindow_t *pxWindow, unsigned int ulWindowSize, portTickType *pulDelay );

/* See if anything is left to be sent
 * Function will be called when a FIN has been received. Only when the TX window is clean,
 * it will return pdTRUE */
portBASE_TYPE xTCPWindowTxDone( TCPWindow_t *pxWindow );

/* Fetches data to be sent.
 * apPos will point to a location with the circular data buffer: txStream */
unsigned int ulTCPWindowTxGet( TCPWindow_t *pxWindow, unsigned int ulWindowSize, int *plPosition );

/* Receive a normal ACK */
unsigned int ulTCPWindowTxAck( TCPWindow_t *pxWindow, unsigned int ulSequenceNumber );

/* Receive a SACK option */
unsigned int ulTCPWindowTxSack( TCPWindow_t *pxWindow, unsigned int ulFirst, unsigned int ulLast );


#ifdef	__cplusplus
}	/* extern "C" */
#endif

#endif /* FREERTOS_TCP_WIN_H */
