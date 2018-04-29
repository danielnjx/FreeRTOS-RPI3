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

#ifndef FREERTOS_SOCKETS_H
#define FREERTOS_SOCKETS_H

#if __cplusplus
extern "C" {
#endif

/* Standard includes. */
#include <string.h>

/* Application level configuration options. */
#include "FreeRTOSIPConfig.h"

#ifndef FREERTOS_IP_CONFIG_H
	#error FreeRTOSIPConfig.h has not been included yet
#endif

/* Event bit definitions are required by the select functions. */
#include "event_groups.h"

#ifndef INC_FREERTOS_H
	#error FreeRTOS.h must be included before FreeRTOS_Sockets.h.
#endif

#ifndef INC_TASK_H
	#ifndef TASK_H /* For compatibility with older FreeRTOS versions. */
		#error The FreeRTOS header file task.h must be included before FreeRTOS_Sockets.h.
	#endif
#endif

/* Assigned to an Socket_t variable when the socket is not valid, probably
because it could not be created. */
#define FREERTOS_INVALID_SOCKET	( ( void * ) ~0U )

/* API function error values.  As errno is supported, the FreeRTOS sockets
functions return error codes rather than just a pass or fail indication. */
/* HT: Extended the number of error codes, gave them positive values and if possible
the corresponding found in errno.h
In case of an error, API's will still return negative numbers, e.g.
  return -pdFREERTOS_ERRNO_EWOULDBLOCK;
in case an operation would block */

/* The following defines are obsolete, please use -pdFREERTOS_ERRNO_Exxx */

#define FREERTOS_SOCKET_ERROR	( -1 )
#define FREERTOS_EWOULDBLOCK	( - pdFREERTOS_ERRNO_EWOULDBLOCK )
#define FREERTOS_EINVAL			( - pdFREERTOS_ERRNO_EINVAL )
#define FREERTOS_EADDRNOTAVAIL	( - pdFREERTOS_ERRNO_EADDRNOTAVAIL )
#define FREERTOS_EADDRINUSE		( - pdFREERTOS_ERRNO_EADDRINUSE )
#define FREERTOS_ENOBUFS		( - pdFREERTOS_ERRNO_ENOBUFS )
#define FREERTOS_ENOPROTOOPT	( - pdFREERTOS_ERRNO_ENOPROTOOPT )
#define FREERTOS_ECLOSED		( - pdFREERTOS_ERRNO_ENOTCONN )

/* Values for the parameters to FreeRTOS_socket(), inline with the Berkeley
standard.  See the documentation of FreeRTOS_socket() for more information. */
#define FREERTOS_AF_INET		( 2 )
#define FREERTOS_SOCK_DGRAM		( 2 )
#define FREERTOS_IPPROTO_UDP	( 17 )

#define FREERTOS_SOCK_STREAM	( 1 )
#define FREERTOS_IPPROTO_TCP	( 6 )
/* IP packet of type "Any local network"
 * can be used in stead of TCP for testing with sockets in raw mode
 */
#define FREERTOS_IPPROTO_USR_LAN  ( 63 )

/* A bit value that can be passed into the FreeRTOS_sendto() function as part of
the flags parameter.  Setting the FREERTOS_ZERO_COPY in the flags parameter
indicates that the zero copy interface is being used.  See the documentation for
FreeRTOS_sockets() for more information. */
#define FREERTOS_ZERO_COPY		( 0x01UL )

/* Values that can be passed in the option name parameter of calls to
FreeRTOS_setsockopt(). */
#define FREERTOS_SO_RCVTIMEO			( 0 )		/* Used to set the receive time out. */
#define FREERTOS_SO_SNDTIMEO			( 1 )		/* Used to set the send time out. */
#define FREERTOS_SO_UDPCKSUM_OUT		( 2 )	 	/* Used to turn the use of the UDP checksum by a socket on or off.  This also doubles as part of an 8-bit bitwise socket option. */
#if( ipconfigSOCKET_HAS_USER_SEMAPHORE == 1 )
	#define FREERTOS_SO_SET_SEMAPHORE	( 3 )		/* Used to set a user's semaphore */
#endif
#define FREERTOS_SO_SNDBUF				( 4 )		/* Set the size of the send buffer (TCP only) */
#define FREERTOS_SO_RCVBUF				( 5 )		/* Set the size of the receive buffer (TCP only) */

#if ipconfigUSE_CALLBACKS == 1
#define FREERTOS_SO_TCP_CONN_HANDLER	( 6 )		/* Install a callback for (dis) connection events. Supply pointer to 'F_TCP_UDP_Handler_t' (see below) */
#define FREERTOS_SO_TCP_RECV_HANDLER	( 7 )		/* Install a callback for receiving TCP data. Supply pointer to 'F_TCP_UDP_Handler_t' (see below) */
#define FREERTOS_SO_TCP_SENT_HANDLER	( 8 )		/* Install a callback for sending TCP data. Supply pointer to 'F_TCP_UDP_Handler_t' (see below) */
#define FREERTOS_SO_UDP_RECV_HANDLER	( 9 )		/* Install a callback for receiving UDP data. Supply pointer to 'F_TCP_UDP_Handler_t' (see below) */
#define FREERTOS_SO_UDP_SENT_HANDLER	( 10 )		/* Install a callback for sending UDP data. Supply pointer to 'F_TCP_UDP_Handler_t' (see below) */
#endif /* ipconfigUSE_CALLBACKS */

#define FREERTOS_SO_REUSE_LISTEN_SOCKET	( 11 )		/* When a listening socket gets connected, do not create a new one but re-use it */
#define FREERTOS_SO_CLOSE_AFTER_SEND	( 12 )		/* As soon as the last byte has been transmitted, finalise the connection */
#define FREERTOS_SO_WIN_PROPERTIES		( 13 )		/* Set all buffer and window properties in one call, parameter is pointer to WinProperties_t */
#define FREERTOS_SO_SET_FULL_SIZE		( 14 )		/* Refuse to send packets smaller than MSS  */

#define FREERTOS_SO_STOP_RX				( 15 )		/* Tempoarily hold up reception, used by streaming client */

#if( ipconfigUDP_MAX_RX_PACKETS > 0 )
	#define FREERTOS_SO_UDP_MAX_RX_PACKETS	( 16 )		/* This option helps to limit the maximum number of packets a UDP socket will buffer */
#endif

#define FREERTOS_NOT_LAST_IN_FRAGMENTED_PACKET 	( 0x80 )  /* For internal use only, but also part of an 8-bit bitwise value. */
#define FREERTOS_FRAGMENTED_PACKET				( 0x40 )  /* For internal use only, but also part of an 8-bit bitwise value. */

/* values for flag for FreeRTOS_shutdown() */
#define FREERTOS_SHUT_RD				( 0 )		/* Not really at this moment, just for compatibility of the interface */
#define FREERTOS_SHUT_WR				( 1 )
#define FREERTOS_SHUT_RDWR				( 2 )

/* values for flag for FreeRTOS_recv() */

#define FREERTOS_MSG_OOB				( 2 )		/* process out-of-band data */
#define FREERTOS_MSG_PEEK				( 4 )		/* peek at incoming message */
#define FREERTOS_MSG_DONTROUTE			( 8 )		/* send without using routing tables */
#define FREERTOS_MSG_DONTWAIT			( 16 )		/* Can be used with recvfrom(), sendto(), recv(), and send(). */

typedef struct xWIN_PROPS {
	/* Properties of the Tx buffer and Tx window */
	int lTxBufSize;	/* Unit: bytes */
	int lTxWinSize;	/* Unit: MSS */

	/* Properties of the Rx buffer and Rx window */
	int lRxBufSize;	/* Unit: bytes */
	int lRxWinSize;	/* Unit: MSS */
} WinProperties_t;

/* For compatibility with the expected Berkeley sockets naming. */
#define socklen_t unsigned int

/* For this limited implementation, only two members are required in the
Berkeley style sockaddr structure. */
struct freertos_sockaddr
{
	unsigned short sin_port;
	unsigned int sin_addr;
};

#if ipconfigBYTE_ORDER == pdFREERTOS_LITTLE_ENDIAN

	#define FreeRTOS_inet_addr_quick( ucOctet0, ucOctet1, ucOctet2, ucOctet3 )				\
										( ( ( ( unsigned int ) ( ucOctet3 ) ) << 24UL ) |		\
										( ( ( unsigned int ) ( ucOctet2 ) ) << 16UL ) |			\
										( ( ( unsigned int ) ( ucOctet1 ) ) <<  8UL ) |			\
										( ( unsigned int ) ( ucOctet0 ) ) )

	#define FreeRTOS_inet_ntoa( ulIPAddress, pucBuffer )									\
								sprintf( ( char * ) ( pucBuffer ), "%u.%u.%u.%u",			\
									( ( unsigned ) ( ( ulIPAddress ) & 0xffUL ) ),			\
									( ( unsigned ) ( ( ( ulIPAddress ) >> 8 ) & 0xffUL ) ),	\
									( ( unsigned ) ( ( ( ulIPAddress ) >> 16 ) & 0xffUL ) ),\
									( ( unsigned ) ( ( ulIPAddress ) >> 24 ) ) )

#else /* ipconfigBYTE_ORDER */

	#define FreeRTOS_inet_addr_quick( ucOctet0, ucOctet1, ucOctet2, ucOctet3 )				\
										( ( ( ( unsigned int ) ( ucOctet0 ) ) << 24UL ) |		\
										( ( ( unsigned int ) ( ucOctet1 ) ) << 16UL ) |			\
										( ( ( unsigned int ) ( ucOctet2 ) ) <<  8UL ) |			\
										( ( unsigned int ) ( ucOctet3 ) ) )

	#define FreeRTOS_inet_ntoa( ulIPAddress, pucBuffer )									\
								sprintf( ( char * ) ( pucBuffer ), "%u.%u.%u.%u",			\
									( ( unsigned ) ( ( ulIPAddress ) >> 24 ) ),				\
									( ( unsigned ) ( ( ( ulIPAddress ) >> 16 ) & 0xffUL ) ),\
									( ( unsigned ) ( ( ( ulIPAddress ) >> 8 ) & 0xffUL ) ),	\
									( ( unsigned ) ( ( ulIPAddress ) & 0xffUL ) ) )

#endif /* ipconfigBYTE_ORDER */

/* The socket type itself. */
typedef void *Socket_t;

/* The SocketSet_t type is the equivalent to the fd_set type used by the
Berkeley API. */
typedef void *SocketSet_t;

/**
 * FULL, UP-TO-DATE AND MAINTAINED REFERENCE DOCUMENTATION FOR ALL THESE
 * FUNCTIONS IS AVAILABLE ON THE FOLLOWING URL:
 * http://www.FreeRTOS.org/FreeRTOS-Plus/FreeRTOS_Plus_TCP/FreeRTOS_TCP_API_Functions.html
 */
Socket_t FreeRTOS_socket( portBASE_TYPE xDomain, portBASE_TYPE xType, portBASE_TYPE xProtocol );
int FreeRTOS_recvfrom( Socket_t xSocket, void *pvBuffer, size_t xBufferLength, unsigned int ulFlags, struct freertos_sockaddr *pxSourceAddress, socklen_t *pxSourceAddressLength );
int FreeRTOS_sendto( Socket_t xSocket, const void *pvBuffer, size_t xTotalDataLength, unsigned int ulFlags, const struct freertos_sockaddr *pxDestinationAddress, socklen_t xDestinationAddressLength );
portBASE_TYPE FreeRTOS_bind( Socket_t xSocket, struct freertos_sockaddr *pxAddress, socklen_t xAddressLength );

/* function to get the local address and IP port */
portBASE_TYPE FreeRTOS_GetLocalAddress( Socket_t xSocket, struct freertos_sockaddr *addr );

#if( ipconfigETHERNET_DRIVER_FILTERS_PACKETS == 1 )
portBASE_TYPE xPortHasUdpSocket( unsigned short usPortNr );
#endif

#if ipconfigUSE_TCP == 1

portBASE_TYPE FreeRTOS_connect( Socket_t xClientSocket, struct freertos_sockaddr *pxAddress, socklen_t xAddress_len);
portBASE_TYPE FreeRTOS_listen( Socket_t xSocket, portBASE_TYPE ulBacklog );
portBASE_TYPE FreeRTOS_recv( Socket_t xSocket, void *pvBuffer, size_t xBufferLength, portBASE_TYPE xFlags );
portBASE_TYPE FreeRTOS_send( Socket_t xSocket, const void *pvBuffer, size_t xDataLength, portBASE_TYPE xFlags );
Socket_t FreeRTOS_accept( Socket_t xSrvSocket, struct freertos_sockaddr *addr, socklen_t *addrlen);
portBASE_TYPE FreeRTOS_shutdown (Socket_t xSrvSocket, portBASE_TYPE xHow);

#if( ipconfigSUPPORT_SIGNALS != 0 )
	/* Send a signal to the task which is waiting for a given socket. */
	portBASE_TYPE FreeRTOS_SignalSocket( Socket_t xSocket );

	/* Send a signal to the task which reads from this socket (FromISR
	version). */
	portBASE_TYPE FreeRTOS_SignalSocketFromISR( Socket_t xSocket, portBASE_TYPE *pxHigherPriorityTaskWoken );
#endif /* ipconfigSUPPORT_SIGNALS */

/* Return the remote address and IP port. */
portBASE_TYPE FreeRTOS_GetRemoteAddress( Socket_t xSocket, struct freertos_sockaddr *addr );

/* returns pdTRUE if TCP socket is connected */
portBASE_TYPE FreeRTOS_issocketconnected( Socket_t xSocket );

/* returns the actual size of MSS being used */
portBASE_TYPE FreeRTOS_mss( Socket_t xSocket );

/* for internal use only: return the connection status */
portBASE_TYPE FreeRTOS_connstatus( Socket_t xSocket );

/* Returns the number of bytes that may be added to txStream */
portBASE_TYPE FreeRTOS_maywrite( Socket_t xSocket );

/*
 * Two helper functions, mostly for testing
 * rx_size returns the number of bytes available in the Rx buffer
 * tx_space returns the free space in the Tx buffer
 */
portBASE_TYPE FreeRTOS_rx_size( Socket_t xSocket );
portBASE_TYPE FreeRTOS_tx_space( Socket_t xSocket );
portBASE_TYPE FreeRTOS_tx_size( Socket_t xSocket );

/* Returns the number of outstanding bytes in txStream. */
portBASE_TYPE FreeRTOS_outstanding( Socket_t xSocket );

/* Returns the number of bytes in the socket's rxStream. */
portBASE_TYPE FreeRTOS_recvcount( Socket_t xSocket );

/*
 * For advanced applications only:
 * Get a direct pointer to the circular transmit buffer.
 * '*pxLength' will contain the number of bytes that may be written.
 */
unsigned char *FreeRTOS_get_tx_head( Socket_t xSocket, portBASE_TYPE *pxLength );

#endif /* ipconfigUSE_TCP */

/*
 * Connect / disconnect handler for a TCP socket
 * For example:
 *		static void vMyConnectHandler (Socket_t xSocket, portBASE_TYPE ulConnected)
 *		{
 *		}
 * 		F_TCP_UDP_Handler_t xHnd = { vMyConnectHandler };
 * 		FreeRTOS_setsockopt( sock, 0, FREERTOS_SO_TCP_CONN_HANDLER, ( void * ) &xHnd, sizeof( xHnd ) );
 */

typedef void (* FOnConnected) (Socket_t /* xSocket */, portBASE_TYPE /* ulConnected */);

/*
 * Reception handler for a TCP socket
 * A user-proved function will be called on reception of a message
 * If the handler returns a positive number, the messages will not be stored
 * For example:
 *		static portBASE_TYPE onTcpReceive (Socket_t xSocket, void * pData, size_t xLength )
 *		{
 *			// handle the message
 *			return 1;
 *		}
 *		F_TCP_UDP_Handler_t xHand = { onTcpReceive };
 *		FreeRTOS_setsockopt( sock, 0, FREERTOS_SO_TCP_RECV_HANDLER, ( void * ) &xHand, sizeof( xHand ) );
 */
typedef portBASE_TYPE (* FOnTcpReceive ) (Socket_t /* xSocket */, void * /* pData */, size_t /* xLength */);
typedef void (* FOnTcpSent ) (Socket_t /* xSocket */, size_t /* xLength */);

/*
 * Reception handler for a UDP socket
 * A user-proved function will be called on reception of a message
 * If the handler returns a positive number, the messages will not be stored
 */
typedef portBASE_TYPE (* FOnUdpReceive ) (Socket_t /* xSocket */, void * /* pData */, size_t /* xLength */,
	const struct freertos_sockaddr * /* pxFrom */, const struct freertos_sockaddr * /* pxDest */ );
typedef void (* FOnUdpSent ) (Socket_t /* xSocket */, size_t /* xLength */ );


typedef union xTCP_UDP_HANDLER
{
	FOnConnected	pOnTcpConnected;	/* FREERTOS_SO_TCP_CONN_HANDLER */
	FOnTcpReceive	pOnTcpReceive;		/* FREERTOS_SO_TCP_RECV_HANDLER */
	FOnTcpSent		pOnTcpSent;			/* FREERTOS_SO_TCP_SENT_HANDLER */
	FOnUdpReceive	pOnUdpReceive;		/* FREERTOS_SO_UDP_RECV_HANDLER */
	FOnUdpSent		pOnUdpSent;			/* FREERTOS_SO_UDP_SENT_HANDLER */
} F_TCP_UDP_Handler_t;

portBASE_TYPE FreeRTOS_setsockopt( Socket_t xSocket, int lLevel, int lOptionName, const void *pvOptionValue, size_t xOptionLength );
portBASE_TYPE FreeRTOS_closesocket( Socket_t xSocket );
unsigned int FreeRTOS_gethostbyname( const char *pcHostName );
unsigned int FreeRTOS_inet_addr( const char * pcIPAddress );

/*
 * For the web server: borrow the circular Rx buffer for inspection
 * HTML driver wants to see if a sequence of 13/10/13/10 is available
 */
const struct xSTREAM_BUFFER *FreeRTOS_get_rx_buf( Socket_t xSocket );

void FreeRTOS_netstat( void );

#if ipconfigSUPPORT_SELECT_FUNCTION == 1

	/* For FD_SET and FD_CLR, a combination of the following bits can be used: */

	typedef enum eSELECT_EVENT {
		eSELECT_READ    = 0x0001,
		eSELECT_WRITE   = 0x0002,
		eSELECT_EXCEPT  = 0x0004,
		eSELECT_INTR    = 0x0008,
		eSELECT_ALL		= 0x000F,
		/* Reserved for internal use: */
		eSELECT_CALL_IP	= 0x0010,
		/* end */
	} eSelectEvent_t;

	SocketSet_t FreeRTOS_CreateSocketSet( void );
	void FreeRTOS_DeleteSocketSet( SocketSet_t xSocketSet );
	void FreeRTOS_FD_SET( Socket_t xSocket, SocketSet_t xSocketSet, EventBits_t xBitsToSet );
	void FreeRTOS_FD_CLR( Socket_t xSocket, SocketSet_t xSocketSet, EventBits_t xBitsToClear );
	EventBits_t FreeRTOS_FD_ISSET( Socket_t xSocket, SocketSet_t xSocketSet );
	portBASE_TYPE FreeRTOS_select( SocketSet_t xSocketSet, portTickType xBlockTimeTicks );

#endif /* ipconfigSUPPORT_SELECT_FUNCTION */

#if __cplusplus
} // extern "C"
#endif

#endif /* FREERTOS_SOCKETS_H */













