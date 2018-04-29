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

#ifndef FREERTOS_IP_H
#define FREERTOS_IP_H

#ifdef __cplusplus
extern "C" {
#endif

/* Application level configuration options. */
#include "FreeRTOSIPConfig.h"
#include "FreeRTOSIPConfigDefaults.h"
#include "IPTraceMacroDefaults.h"

/* Some constants defining the sizes of several parts of a packet */
#define ipSIZE_OF_ETH_HEADER			14
#define ipSIZE_OF_IP_HEADER				20
#define ipSIZE_OF_IGMP_HEADER			8
#define ipSIZE_OF_ICMP_HEADER			8
#define ipSIZE_OF_UDP_HEADER			8
#define ipSIZE_OF_TCP_HEADER			20


/* The number of octets in the MAC and IP addresses respectively. */
#define ipMAC_ADDRESS_LENGTH_BYTES ( 6 )
#define ipIP_ADDRESS_LENGTH_BYTES ( 4 )

/* IP protocol definitions. */
#define ipPROTOCOL_ICMP			( 1 )
#define ipPROTOCOL_IGMP         ( 2 )
#define ipPROTOCOL_TCP			( 6 )
#define ipPROTOCOL_UDP			( 17 )

/* Dimensions the buffers that are filled by received Ethernet frames. */
#define ipSIZE_OF_ETH_CRC_BYTES					( 4UL )
#define ipSIZE_OF_ETH_OPTIONAL_802_1Q_TAG_BYTES	( 4UL )
#define ipTOTAL_ETHERNET_FRAME_SIZE				( ( ( unsigned int ) ipconfigNETWORK_MTU ) + ( ( unsigned int ) ipSIZE_OF_ETH_HEADER ) + ipSIZE_OF_ETH_CRC_BYTES + ipSIZE_OF_ETH_OPTIONAL_802_1Q_TAG_BYTES )


/* Space left at the beginning of a network buffer storage area to store a
pointer back to the network buffer.  Should be a multiple of 8 to ensure 8 byte
alignment is maintained on architectures that require it.

In order to get a 32-bit alignment of network packets, an offset of 2 bytes
would be desirable, as defined by ipconfigPACKET_FILLER_SIZE.  So the malloc'd
buffer will have the following contents:
	unsigned int pointer;	// word-aligned
	uchar_8 filler[6];
	<< ETH-header >>	// half-word-aligned
	uchar_8 dest[6];    // start of pucEthernetBuffer
	uchar_8 dest[6];
	uchar16_t type;
	<< IP-header >>		// word-aligned
	unsigned char ucVersionHeaderLength;
	etc
 */
#define ipBUFFER_PADDING		( 8 + ipconfigPACKET_FILLER_SIZE )

/* The structure used to store buffers and pass them around the network stack.
Buffers can be in use by the stack, in use by the network interface hardware
driver, or free (not in use). */
typedef struct xNETWORK_BUFFER
{
	xListItem xBufferListItem; 	/* Used to reference the buffer form the free buffer list or a socket. */
	unsigned int ulIPAddress;			/* Source or destination IP address, depending on usage scenario. */
	unsigned char *pucEthernetBuffer; 	/* Pointer to the start of the Ethernet frame. */
	size_t xDataLength; 			/* Starts by holding the total Ethernet frame length, then the UDP/TCP payload length. */
	unsigned short usPort;				/* Source or destination port, depending on usage scenario. */
	unsigned short usBoundPort;			/* The port to which a transmitting socket is bound. */
	#if( ipconfigUSE_LINKED_RX_MESSAGES != 0 )
		struct xNETWORK_BUFFER *pxNextBuffer; /* Possible optimisation for expert users - requires network driver support. */
	#endif
} NetworkBufferDescriptor_t;

#include "pack_struct_start.h"
struct xMAC_ADDRESS
{
	unsigned char ucBytes[ ipMAC_ADDRESS_LENGTH_BYTES ];
}
#include "pack_struct_end.h"
typedef struct xMAC_ADDRESS MACAddress_t;

typedef enum eNETWORK_EVENTS
{
	eNetworkUp,		/* The network is configured. */
	eNetworkDown	/* The network connection has been lost. */
} eIPCallbackEvent_t;

typedef enum ePING_REPLY_STATUS
{
	eSuccess = 0,		/* A correct reply has been received for an outgoing ping. */
	eInvalidChecksum,	/* A reply was received for an outgoing ping but the checksum of the reply was incorrect. */
	eInvalidData		/* A reply was received to an outgoing ping but the payload of the reply was not correct. */
} ePingReplyStatus_t;

/* Endian related definitions. */
#if( ipconfigBYTE_ORDER == pdFREERTOS_LITTLE_ENDIAN )

	/* FreeRTOS_htons / FreeRTOS_htonl: some platforms might have built-in versions
	using a single instruction so allow these versions to be overridden. */
	#ifndef FreeRTOS_htons
		#define FreeRTOS_htons( usIn ) ( (unsigned short) ( ( ( usIn ) << 8U ) | ( ( usIn ) >> 8U ) ) )
	#endif

	#ifndef	FreeRTOS_htonl
		#define FreeRTOS_htonl( ulIn ) 											\
			(																	\
				( unsigned int ) 													\
				( 																\
					( ( ( ( unsigned int ) ( ulIn ) )                ) << 24  ) | 	\
					( ( ( ( unsigned int ) ( ulIn ) ) & 0x0000ff00UL ) <<  8  ) | 	\
					( ( ( ( unsigned int ) ( ulIn ) ) & 0x00ff0000UL ) >>  8  ) | 	\
					( ( ( ( unsigned int ) ( ulIn ) )                ) >> 24  )  	\
				) 																\
			)
	#endif

#else /* ipconfigBYTE_ORDER */

	#define FreeRTOS_htons( x ) ( ( unsigned short ) ( x ) )
	#define FreeRTOS_htonl( x ) ( ( unsigned int ) ( x ) )

#endif /* ipconfigBYTE_ORDER == pdFREERTOS_LITTLE_ENDIAN */

#define FreeRTOS_ntohs( x ) FreeRTOS_htons( x )
#define FreeRTOS_ntohl( x ) FreeRTOS_htonl( x )

#if( ipconfigHAS_INLINE_FUNCTIONS == 1 )

	static portINLINE int  FreeRTOS_max_int32  (int  a, int  b) { return a >= b ? a : b; }
	static portINLINE unsigned int FreeRTOS_max_uint32 (unsigned int a, unsigned int b) { return a >= b ? a : b; }
	static portINLINE int  FreeRTOS_min_int32  (int  a, int  b) { return a <= b ? a : b; }
	static portINLINE unsigned int FreeRTOS_min_uint32 (unsigned int a, unsigned int b) { return a <= b ? a : b; }
	static portINLINE unsigned int FreeRTOS_round_up   (unsigned int a, unsigned int d) { return d * ( ( a + d - 1 ) / d ); }
	static portINLINE unsigned int FreeRTOS_round_down (unsigned int a, unsigned int d) { return d * ( a / d ); }

#else

	#define FreeRTOS_max_int32(a,b)  ( ( ( int  ) ( a ) ) >= ( ( int  ) ( b ) ) ? ( ( int  ) ( a ) ) : ( ( int  ) ( b ) ) )
	#define FreeRTOS_max_uint32(a,b) ( ( ( unsigned int ) ( a ) ) >= ( ( unsigned int ) ( b ) ) ? ( ( unsigned int ) ( a ) ) : ( ( unsigned int ) ( b ) ) )

	#define FreeRTOS_min_int32(a,b)  ( ( ( int  ) a ) <= ( ( int  ) b ) ? ( ( int  ) a ) : ( ( int  ) b ) )
	#define FreeRTOS_min_uint32(a,b) ( ( ( unsigned int ) a ) <= ( ( unsigned int ) b ) ? ( ( unsigned int ) a ) : ( ( unsigned int ) b ) )

	/*  Round-up: a = d * ( ( a + d - 1 ) / d ) */
	#define FreeRTOS_round_up(a,d)   ( ( ( unsigned int ) ( d ) ) * ( ( ( ( unsigned int ) ( a ) ) + ( ( unsigned int ) ( d ) ) - 1UL ) / ( ( unsigned int ) ( d ) ) ) )
	#define FreeRTOS_round_down(a,d) ( ( ( unsigned int ) ( d ) ) * ( ( ( unsigned int ) ( a ) ) / ( ( unsigned int ) ( d ) ) ) )

	#define FreeRTOS_ms_to_tick(ms)  ( ( ms * configTICK_RATE_HZ + 500 ) / 1000 )

#endif /* ipconfigHAS_INLINE_FUNCTIONS */

/*
 * FULL, UP-TO-DATE AND MAINTAINED REFERENCE DOCUMENTATION FOR ALL THESE
 * FUNCTIONS IS AVAILABLE ON THE FOLLOWING URL:
 * http://www.FreeRTOS.org/FreeRTOS-Plus/FreeRTOS_Plus_TCP/FreeRTOS_TCP_API_Functions.html
 */
portBASE_TYPE FreeRTOS_IPInit( const unsigned char ucIPAddress[ ipIP_ADDRESS_LENGTH_BYTES ],
	const unsigned char ucNetMask[ ipIP_ADDRESS_LENGTH_BYTES ],
	const unsigned char ucGatewayAddress[ ipIP_ADDRESS_LENGTH_BYTES ],
	const unsigned char ucDNSServerAddress[ ipIP_ADDRESS_LENGTH_BYTES ],
	const unsigned char ucMACAddress[ ipMAC_ADDRESS_LENGTH_BYTES ] );

void * FreeRTOS_GetUDPPayloadBuffer( size_t xRequestedSizeBytes, portTickType xBlockTimeTicks );
void FreeRTOS_GetAddressConfiguration( unsigned int *pulIPAddress, unsigned int *pulNetMask, unsigned int *pulGatewayAddress, unsigned int *pulDNSServerAddress );
void FreeRTOS_SetAddressConfiguration( const unsigned int *pulIPAddress, const unsigned int *pulNetMask, const unsigned int *pulGatewayAddress, const unsigned int *pulDNSServerAddress );
portBASE_TYPE FreeRTOS_SendPingRequest( unsigned int ulIPAddress, size_t xNumberOfBytesToSend, portTickType xBlockTimeTicks );
void FreeRTOS_ReleaseUDPPayloadBuffer( void *pvBuffer );
const unsigned char * FreeRTOS_GetMACAddress( void );
void vApplicationIPNetworkEventHook( eIPCallbackEvent_t eNetworkEvent );
void vApplicationPingReplyHook( ePingReplyStatus_t eStatus, unsigned short usIdentifier );
const unsigned char * FreeRTOS_GetMACAddress( void );
unsigned int FreeRTOS_GetIPAddress( void );
void FreeRTOS_SetIPAddress( unsigned int ulIPAddress );
void FreeRTOS_SetNetmask( unsigned int ulNetmask );
void FreeRTOS_SetGatewayAddress( unsigned int ulGatewayAddress );
unsigned int FreeRTOS_GetGatewayAddress( void );
unsigned int FreeRTOS_GetDNSServerAddress( void );
unsigned int FreeRTOS_GetNetmask( void );
void FreeRTOS_OutputARPRequest( unsigned int ulIPAddress );
portBASE_TYPE FreeRTOS_IsNetworkUp( void );

#if( ipconfigCHECK_IP_QUEUE_SPACE != 0 )
	unsigned portBASE_TYPE uxGetMinimumIPQueueSpace( void );
#endif

/*
 * Defined in FreeRTOS_Sockets.c
 * //_RB_ Don't think this comment is correct.  If this is for internal use only it should appear after all the public API functions and not start with FreeRTOS_.
 * Socket has had activity, reset the timer so it will not be closed
 * because of inactivity
 */
const char *FreeRTOS_GetTCPStateName( unsigned portBASE_TYPE ulState);

/* _HT_ Temporary: show all valid ARP entries
 */
void FreeRTOS_PrintARPCache( void );
void FreeRTOS_ClearARP( void );

#if( ipconfigDHCP_REGISTER_HOSTNAME == 1 )

	/* DHCP has an option for clients to register their hostname.  It doesn't
	have much use, except that a device can be found in a router along with its
	name. If this option is used the callback below must be provided by the
	application	writer to return a const string, denoting the device's name. */
	const char *pcApplicationHostnameHook( void );

#endif /* ipconfigDHCP_REGISTER_HOSTNAME */


/* For backward compatibility define old structure names to the newer equivalent
structure name. */
#ifndef ipconfigENABLE_BACKWARD_COMPATIBILITY
	#define ipconfigENABLE_BACKWARD_COMPATIBILITY	1
#endif

#if( ipconfigENABLE_BACKWARD_COMPATIBILITY == 1 )
	#define xIPStackEvent_t 			IPStackEvent_t
	#define xNetworkBufferDescriptor_t 	NetworkBufferDescriptor_t
	#define xMACAddress_t 				MACAddress_t
	#define xWinProperties_t 			WinProperties_t
	#define xSocket_t 					Socket_t
	#define xSocketSet_t 				SocketSet_t
#endif /* ipconfigENABLE_BACKWARD_COMPATIBILITY */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FREERTOS_IP_H */













