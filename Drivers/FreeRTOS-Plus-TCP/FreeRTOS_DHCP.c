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

/* Standard includes. */
#include <stdint.h>

/* FreeRTOS includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_UDP_IP.h"
#include "FreeRTOS_TCP_IP.h"
#include "FreeRTOS_DHCP.h"
#include "FreeRTOS_ARP.h"
#include "NetworkInterface.h"
#include "NetworkBufferManagement.h"

/* Exclude the entire file if DHCP is not enabled. */
#if( ipconfigUSE_DHCP != 0 )

#if ( ipconfigUSE_DHCP != 0 ) && ( ipconfigNETWORK_MTU < 586 )
	/* DHCP must be able to receive an options field of 312 bytes, the fixed
	part of the DHCP packet is 240 bytes, and the IP/UDP headers take 28 bytes. */
	#error ipconfigNETWORK_MTU needs to be at least 586 to use DHCP
#endif

/* Parameter widths in the DHCP packet. */
#define dhcpCLIENT_HARDWARE_ADDRESS_LENGTH		16
#define dhcpSERVER_HOST_NAME_LENGTH				64
#define dhcpBOOT_FILE_NAME_LENGTH 				128

/* Timer parameters */
#ifndef dhcpINITIAL_DHCP_TX_PERIOD
	#define dhcpINITIAL_TIMER_PERIOD			( pdMS_TO_TICKS( 250 ) )
	#define dhcpINITIAL_DHCP_TX_PERIOD			( pdMS_TO_TICKS( 5000 ) )
#endif

/* Codes of interest found in the DHCP options field. */
#define dhcpZERO_PAD_OPTION_CODE				( 0 )
#define dhcpSUBNET_MASK_OPTION_CODE				( 1 )
#define dhcpGATEWAY_OPTION_CODE					( 3 )
#define dhcpDNS_SERVER_OPTIONS_CODE				( 6 )
#define dhcpDNS_HOSTNAME_OPTIONS_CODE			( 12 )
#define dhcpREQUEST_IP_ADDRESS_OPTION_CODE		( 50 )
#define dhcpLEASE_TIME_OPTION_CODE				( 51 )
#define dhcpMESSAGE_TYPE_OPTION_CODE			( 53 )
#define dhcpSERVER_IP_ADDRESS_OPTION_CODE		( 54 )
#define dhcpPARAMETER_REQUEST_OPTION_CODE		( 55 )
#define dhcpCLIENT_IDENTIFIER_OPTION_CODE		( 61 )

/* The four DHCP message types of interest. */
#define dhcpMESSAGE_TYPE_DISCOVER				( 1 )
#define dhcpMESSAGE_TYPE_OFFER					( 2 )
#define dhcpMESSAGE_TYPE_REQUEST				( 3 )
#define dhcpMESSAGE_TYPE_ACK					( 5 )
#define dhcpMESSAGE_TYPE_NACK					( 6 )

/* Offsets into the transmitted DHCP options fields at which various parameters
are located. */
#define dhcpCLIENT_IDENTIFIER_OFFSET			( 5 )
#define dhcpREQUESTED_IP_ADDRESS_OFFSET			( 13 )
#define dhcpDHCP_SERVER_IP_ADDRESS_OFFSET		( 19 )

/* Values used in the DHCP packets. */
#define dhcpREQUEST_OPCODE						( 1 )
#define dhcpREPLY_OPCODE						( 2 )
#define dhcpADDRESS_TYPE_ETHERNET				( 1 )
#define dhcpETHERNET_ADDRESS_LENGTH				( 6 )

/* If a lease time is not received, use the default of two days. */
/* 48 hours in ticks.  Can not use pdMS_TO_TICKS() as integer overflow can occur. */
#define dhcpDEFAULT_LEASE_TIME					( ( 48UL * 60UL * 60UL ) * configTICK_RATE_HZ )

/* Don't allow the lease time to be too short. */
#define dhcpMINIMUM_LEASE_TIME					( pdMS_TO_TICKS( 60000UL ) )	/* 60 seconds in ticks. */

/* Marks the end of the variable length options field in the DHCP packet. */
#define dhcpOPTION_END_BYTE 0xff

/* Offset into a DHCP message at which the first byte of the options is
located. */
#define dhcpFIRST_OPTION_BYTE_OFFSET			( 0xf0 )

/* When walking the variable length options field, the following value is used
to ensure the walk has not gone past the end of the valid options.  2 bytes is
made up of the length byte, and minimum one byte value. */
#define dhcpMAX_OPTION_LENGTH_OF_INTEREST		( 2L )

/* Standard DHCP port numbers and magic cookie value. */
#if( ipconfigBYTE_ORDER == pdFREERTOS_LITTLE_ENDIAN )
	#define dhcpCLIENT_PORT 0x4400
	#define dhcpSERVER_PORT 0x4300
	#define dhcpCOOKIE		0x63538263
	#define dhcpBROADCAST	0x0080
#else
	#define dhcpCLIENT_PORT 0x0044
	#define dhcpSERVER_PORT 0x0043
	#define dhcpCOOKIE		0x63825363
	#define dhcpBROADCAST	0x8000
#endif /* ipconfigBYTE_ORDER */

#include "pack_struct_start.h"
struct xDHCPMessage
{
	unsigned char ucOpcode;
	unsigned char ucAddressType;
	unsigned char ucAddressLength;
	unsigned char ucHops;
	unsigned int ulTransactionID;
	unsigned short usElapsedTime;
	unsigned short usFlags;
	unsigned int ulClientIPAddress_ciaddr;
	unsigned int ulYourIPAddress_yiaddr;
	unsigned int ulServerIPAddress_siaddr;
	unsigned int ulRelayAgentIPAddress_giaddr;
	unsigned char ucClientHardwareAddress[ dhcpCLIENT_HARDWARE_ADDRESS_LENGTH ];
	unsigned char ucServerHostName[ dhcpSERVER_HOST_NAME_LENGTH ];
	unsigned char ucBootFileName[ dhcpBOOT_FILE_NAME_LENGTH ];
	unsigned int ulDHCPCookie;
	unsigned char ucFirstOptionByte;
}
#include "pack_struct_end.h"
typedef struct xDHCPMessage DHCPMessage_t;

/* DHCP state machine states. */
typedef enum
{
	eWaitingSendFirstDiscover = 0,	/* Initial state.  Send a discover the first time it is called, and reset all timers. */
	eWaitingOffer,					/* Either resend the discover, or, if the offer is forthcoming, send a request. */
	eWaitingAcknowledge,			/* Either resend the request. */
	#if( ipconfigDHCP_FALL_BACK_AUTO_IP != 0 )
		eGetLinkLayerAddress,		/* When DHCP didn't respond, try to obtain a LinkLayer address 168.254.x.x. */
	#endif
	eLeasedAddress,					/* Resend the request at the appropriate time to renew the lease. */
	eNotUsingLeasedAddress			/* DHCP failed, and a default IP address is being used. */
} eDHCPState_t;

/* Hold information in between steps in the DHCP state machine. */
struct xDHCP_DATA
{
	unsigned int ulTransactionId;
	unsigned int ulOfferedIPAddress;
	unsigned int ulDHCPServerAddress;
	unsigned int ulLeaseTime;
	/* Hold information on the current timer state. */
	portTickType xDHCPTxTime;
	portTickType xDHCPTxPeriod;
	/* Try both without and with the broadcast flag */
	portBASE_TYPE xUseBroadcast;
	/* Maintains the DHCP state machine state. */
	eDHCPState_t eDHCPState;
	/* The UDP socket used for all incoming and outgoing DHCP traffic. */
	Socket_t xDHCPSocket;
};

typedef struct xDHCP_DATA DHCPData_t;

#if( ipconfigDHCP_FALL_BACK_AUTO_IP != 0 )
	/* Define the Link Layer IP address: 169.254.x.x */
	#define LINK_LAYER_ADDRESS_0	169
	#define LINK_LAYER_ADDRESS_1	254

	/* Define the netmask used: 255.255.0.0 */
	#define LINK_LAYER_NETMASK_0	255
	#define LINK_LAYER_NETMASK_1	255
	#define LINK_LAYER_NETMASK_2	0
	#define LINK_LAYER_NETMASK_3	0
#endif


/*
 * Generate a DHCP discover message and send it on the DHCP socket.
 */
static void prvSendDHCPDiscover( void );

/*
 * Interpret message received on the DHCP socket.
 */
static portBASE_TYPE prvProcessDHCPReplies( portBASE_TYPE xExpectedMessageType );

/*
 * Generate a DHCP request packet, and send it on the DHCP socket.
 */
static void prvSendDHCPRequest( void );

/*
 * Prepare to start a DHCP transaction.  This initialises some state variables
 * and creates the DHCP socket if necessary.
 */
static void prvInitialiseDHCP( void );

/*
 * Creates the part of outgoing DHCP messages that are common to all outgoing
 * DHCP messages.
 */
static unsigned char *prvCreatePartDHCPMessage( struct freertos_sockaddr *pxAddress, portBASE_TYPE xOpcode, const unsigned char * const pucOptionsArray, size_t *pxOptionsArraySize );

/*
 * Create the DHCP socket, if it has not been created already.
 */
static void prvCreateDHCPSocket( void );

/*
 * After DHCP has failed to answer, prepare everything to start searching
 * for (trying-out) LinkLayer IP-addresses, using the random method: Send
 * a gratuitos ARP request and wait if another device responds to it.
 */
#if( ipconfigDHCP_FALL_BACK_AUTO_IP != 0 )
	static void prvPrepareLinkLayerIPLookUp( void );
#endif

/*-----------------------------------------------------------*/

/* The next DHCP transaction Id to be used. */
static DHCPData_t xDHCPData;

/*-----------------------------------------------------------*/

portBASE_TYPE xIsDHCPSocket( Socket_t xSocket )
{
portBASE_TYPE xReturn;

	if( xDHCPData.xDHCPSocket == xSocket )
	{
		xReturn = pdTRUE;
	}
	else
	{
		xReturn = pdFALSE;
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

void vDHCPProcess( portBASE_TYPE xReset )
{
portBASE_TYPE xGivingUp = pdFALSE;
#if( ipconfigDHCP_USES_USER_HOOK != 0 )
	eDHCPCallbackAnswer_t eAnswer;
#endif	/* ipconfigDHCP_USES_USER_HOOK */

	/* Is DHCP starting over? */
	if( xReset != pdFALSE )
	{
		xDHCPData.eDHCPState = eWaitingSendFirstDiscover;
	}

	switch( xDHCPData.eDHCPState )
	{
		case eWaitingSendFirstDiscover :
			/* Ask the user if a DHCP discovery is required. */
		#if( ipconfigDHCP_USES_USER_HOOK != 0 )
			eAnswer = xApplicationDHCPUserHook( eDHCPOffer, xNetworkAddressing.ulDefaultIPAddress, xNetworkAddressing.ulNetMask );
			if( eAnswer == eDHCPContinue )
		#endif	/* ipconfigDHCP_USES_USER_HOOK */
			{
				/* Initial state.  Create the DHCP socket, timer, etc. if they
				have not already been created. */
				prvInitialiseDHCP();
				*ipLOCAL_IP_ADDRESS_POINTER = 0UL;

				/* Send the first discover request. */
				if( xDHCPData.xDHCPSocket != NULL )
				{
					xDHCPData.xDHCPTxTime = xTaskGetTickCount();
					prvSendDHCPDiscover( );
					xDHCPData.eDHCPState = eWaitingOffer;
				}
			}
		#if( ipconfigDHCP_USES_USER_HOOK != 0 )
			else
			{
				if( eAnswer == eDHCPUseDefaults )
				{
					memcpy2( &xNetworkAddressing, &xDefaultAddressing, sizeof xNetworkAddressing );
				}
				/* The user indicates that the DHCP process does not continue. */
				xGivingUp = pdTRUE;
			}
		#endif	/* ipconfigDHCP_USES_USER_HOOK */
			break;

		case eWaitingOffer :

			xGivingUp = pdFALSE;

			/* Look for offers coming in. */
			if( prvProcessDHCPReplies( dhcpMESSAGE_TYPE_OFFER ) == pdPASS )
			{
			#if( ipconfigDHCP_USES_USER_HOOK != 0 )
				/* Ask the user if a DHCP request is required. */
				eAnswer = xApplicationDHCPUserHook( eDHCPRequest, xDHCPData.ulOfferedIPAddress, xNetworkAddressing.ulNetMask );
				if( eAnswer == eDHCPContinue )
			#endif	/* ipconfigDHCP_USES_USER_HOOK */
				{
					/* An offer has been made, the user wants to continue,
					generate the request. */
					xDHCPData.xDHCPTxTime = xTaskGetTickCount();
					xDHCPData.xDHCPTxPeriod = dhcpINITIAL_DHCP_TX_PERIOD;
					prvSendDHCPRequest( );
					xDHCPData.eDHCPState = eWaitingAcknowledge;
					break;
				}
			#if( ipconfigDHCP_USES_USER_HOOK != 0 )
				if( eAnswer == eDHCPUseDefaults )
				{
					memcpy2( &xNetworkAddressing, &xDefaultAddressing, sizeof xNetworkAddressing );
				}
				/* The user indicates that the DHCP process does not continue. */
				xGivingUp = pdTRUE;
			#endif	/* ipconfigDHCP_USES_USER_HOOK */
			}

			/* Is it time to send another Discover? */
			else if( ( xTaskGetTickCount() - xDHCPData.xDHCPTxTime ) > xDHCPData.xDHCPTxPeriod )
			{
				/* Increase the time period, and if it has not got to the
				point of giving up - send another discovery. */
				xDHCPData.xDHCPTxPeriod <<= 1;

				if( xDHCPData.xDHCPTxPeriod <= ipconfigMAXIMUM_DISCOVER_TX_PERIOD )
				{
					xDHCPData.ulTransactionId++;
					xDHCPData.xDHCPTxTime = xTaskGetTickCount();
					xDHCPData.xUseBroadcast = !xDHCPData.xUseBroadcast;
					prvSendDHCPDiscover( );
					FreeRTOS_debug_printf( ( "vDHCPProcess: timeout %lu ticks\n",
						xDHCPData.xDHCPTxPeriod ) );
				}
				else
				{
					FreeRTOS_debug_printf( ( "vDHCPProcess: giving up %lu > %lu ticks\n",
						xDHCPData.xDHCPTxPeriod, ipconfigMAXIMUM_DISCOVER_TX_PERIOD ) );

					#if( ipconfigDHCP_FALL_BACK_AUTO_IP != 0 )
					{
						/* only use fake ack if default IP address = 0x00 and link local addressing is used. */
						/* Start searching a free LinkLayer IP-address.
						Next state will be 'eGetLinkLayerAddress'. */
						prvPrepareLinkLayerIPLookUp();
						xDHCPData.eDHCPState = eGetLinkLayerAddress;		/* setting IP address manually so set to not using leased address mode. */
					}
					#else
					{
						xGivingUp = pdTRUE;
					}
					#endif /* ipconfigDHCP_FALL_BACK_AUTO_IP */
				}
			}
			break;

		case eWaitingAcknowledge :

			/* Look for acks coming in. */
			if( prvProcessDHCPReplies( dhcpMESSAGE_TYPE_ACK ) == pdPASS )
			{
				FreeRTOS_debug_printf( ( "vDHCPProcess: acked %lxip\n", FreeRTOS_ntohl( xDHCPData.ulOfferedIPAddress ) ) );

				/* DHCP completed.  The IP address can now be used, and the
				timer set to the lease timeout time. */
				*ipLOCAL_IP_ADDRESS_POINTER = xDHCPData.ulOfferedIPAddress;

				/* Setting the 'local' broadcast address, something like 192.168.1.255' */
				xNetworkAddressing.ulBroadcastAddress = ( xDHCPData.ulOfferedIPAddress & xNetworkAddressing.ulNetMask ) |  ~xNetworkAddressing.ulNetMask;
				xDHCPData.eDHCPState = eLeasedAddress;

				iptraceDHCP_SUCCEDEED( xDHCPData.ulOfferedIPAddress );

				/* DHCP failed, the default configured IP-address will be used
				Now call vIPNetworkUpCalls() to send the network-up event, start Nabto
				and start the ARP timer*/
				vIPNetworkUpCalls( );

				/* Close socket to ensure packets don't queue on it. */
				FreeRTOS_closesocket( xDHCPData.xDHCPSocket );
				xDHCPData.xDHCPSocket = NULL;

				if( xDHCPData.ulLeaseTime == 0UL )
				{
					xDHCPData.ulLeaseTime = dhcpDEFAULT_LEASE_TIME;
				}
				else if( xDHCPData.ulLeaseTime < dhcpMINIMUM_LEASE_TIME )
				{
					xDHCPData.ulLeaseTime = dhcpMINIMUM_LEASE_TIME;
				}
				else
				{
					/* The lease time is already valid. */
				}

				/* Check for clashes. */
				vARPSendGratuitous();
				vIPReloadDHCPTimer( xDHCPData.ulLeaseTime );
			}
			else
			{
				/* Is it time to send another Discover? */
				if( ( xTaskGetTickCount() - xDHCPData.xDHCPTxTime ) > xDHCPData.xDHCPTxPeriod )
				{
					/* Increase the time period, and if it has not got to the
					point of giving up - send another request. */
					xDHCPData.xDHCPTxPeriod <<= 1;

					if( xDHCPData.xDHCPTxPeriod <= ipconfigMAXIMUM_DISCOVER_TX_PERIOD )
					{
						xDHCPData.xDHCPTxTime = xTaskGetTickCount();
						prvSendDHCPRequest( );
					}
					else
					{
						/* Give up, start again. */
						xDHCPData.eDHCPState = eWaitingSendFirstDiscover;
					}
				}
			}
			break;

#if( ipconfigDHCP_FALL_BACK_AUTO_IP != 0 )
		case eGetLinkLayerAddress:
			if( ( xTaskGetTickCount() - xDHCPData.xDHCPTxTime ) > xDHCPData.xDHCPTxPeriod )
			{
				if( xARPHadIPClash == pdFALSE )
				{
					/* ARP OK. proceed. */
					iptraceDHCP_SUCCEDEED( xDHCPData.ulOfferedIPAddress );

					/* Auto-IP succeeded, the default configured IP-address will be used
					Now call vIPNetworkUpCalls() to send the network-up event, start Nabto
					and start the ARP timer*/
					vIPNetworkUpCalls( );
					xDHCPData.eDHCPState = eNotUsingLeasedAddress;
				}
				else
				{
					/* ARP clashed - try another IP address. */
					prvPrepareLinkLayerIPLookUp();
					xDHCPData.eDHCPState = eGetLinkLayerAddress;		/* setting IP address manually so set to not using leased address mode. */
				}
			}
			break;
#endif	/* ipconfigDHCP_FALL_BACK_AUTO_IP */

		case eLeasedAddress :

			/* Resend the request at the appropriate time to renew the lease. */
			prvCreateDHCPSocket();

			if( xDHCPData.xDHCPSocket != NULL )
			{
				xDHCPData.xDHCPTxTime = xTaskGetTickCount();
				xDHCPData.xDHCPTxPeriod = dhcpINITIAL_DHCP_TX_PERIOD;
				prvSendDHCPRequest( );
				xDHCPData.eDHCPState = eWaitingAcknowledge;
				/* From now on, we should be called more often */
				vIPReloadDHCPTimer( dhcpINITIAL_TIMER_PERIOD );
			}
			break;

		case eNotUsingLeasedAddress:

			vIPSetDHCPTimerEnableState( pdFALSE );
			break;
	}

	if( xGivingUp != pdFALSE )
	{
		/* xGivingUp became true either because of a time-out, or because
		xApplicationDHCPUserHook() returned another value than 'eDHCPContinue',
		meaning that the conversion is cancelled from here. */

		/* Revert to static IP address. */
		taskENTER_CRITICAL();
		{
			*ipLOCAL_IP_ADDRESS_POINTER = xNetworkAddressing.ulDefaultIPAddress;
			iptraceDHCP_REQUESTS_FAILED_USING_DEFAULT_IP_ADDRESS( xNetworkAddressing.ulDefaultIPAddress );
		}
		taskEXIT_CRITICAL();

		xDHCPData.eDHCPState = eNotUsingLeasedAddress;
		vIPSetDHCPTimerEnableState( pdFALSE );

		/* DHCP failed, the default configured IP-address will be used
		Now call vIPNetworkUpCalls() to send the network-up event, start Nabto
		and start the ARP timer*/
		vIPNetworkUpCalls( );

		/* Close socket to ensure packets don't queue on it. */
		FreeRTOS_closesocket( xDHCPData.xDHCPSocket );
		xDHCPData.xDHCPSocket = NULL;
	}
}
/*-----------------------------------------------------------*/

static void prvCreateDHCPSocket( void )
{
struct freertos_sockaddr xAddress;
portBASE_TYPE xReturn;
portTickType xTimeoutTime = 0;

	/* Create the socket, if it has not already been created. */
	if( xDHCPData.xDHCPSocket == NULL )
	{
		xDHCPData.xDHCPSocket = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP );
		configASSERT( ( xDHCPData.xDHCPSocket != FREERTOS_INVALID_SOCKET ) );

		/* Ensure the Rx and Tx timeouts are zero as the DHCP executes in the
		context of the IP task. */
		FreeRTOS_setsockopt( xDHCPData.xDHCPSocket, 0, FREERTOS_SO_RCVTIMEO, ( void * ) &xTimeoutTime, sizeof( portTickType ) );
		FreeRTOS_setsockopt( xDHCPData.xDHCPSocket, 0, FREERTOS_SO_SNDTIMEO, ( void * ) &xTimeoutTime, sizeof( portTickType ) );

		/* Bind to the standard DHCP client port. */
		xAddress.sin_port = dhcpCLIENT_PORT;
		xReturn = vSocketBind( xDHCPData.xDHCPSocket, &xAddress, sizeof( xAddress ), pdFALSE );
		configASSERT( xReturn == 0 );

		/* Remove compiler warnings if configASSERT() is not defined. */
		( void ) xReturn;
	}
}
/*-----------------------------------------------------------*/

static void prvInitialiseDHCP( void )
{
	/* Initialise the parameters that will be set by the DHCP process. */
	if( xDHCPData.ulTransactionId == 0 )
	{
		xDHCPData.ulTransactionId = ipconfigRAND32();
	}
	else
	{
		xDHCPData.ulTransactionId++;
	}

	xDHCPData.xUseBroadcast = 0;
	xDHCPData.ulOfferedIPAddress = 0UL;
	xDHCPData.ulDHCPServerAddress = 0UL;
	xDHCPData.xDHCPTxPeriod = dhcpINITIAL_DHCP_TX_PERIOD;

	/* Create the DHCP socket if it has not already been created. */
	prvCreateDHCPSocket();
	FreeRTOS_debug_printf( ( "prvInitialiseDHCP: start after %lu ticks\n", dhcpINITIAL_TIMER_PERIOD ) );
	vIPReloadDHCPTimer( dhcpINITIAL_TIMER_PERIOD );
}
/*-----------------------------------------------------------*/

static portBASE_TYPE prvProcessDHCPReplies( portBASE_TYPE xExpectedMessageType )
{
unsigned char *pucUDPPayload, *pucLastByte;
struct freertos_sockaddr xClient;
unsigned int xClientLength = sizeof( xClient );
int lBytes;
DHCPMessage_t *pxDHCPMessage;
unsigned char *pucByte, ucOptionCode, ucLength;
unsigned int ulProcessed, ulParameter;
portBASE_TYPE xReturn = pdFALSE;
const unsigned int ulMandatoryOptions = 2; /* DHCP server address, and the correct DHCP message type must be present in the options. */

	lBytes = FreeRTOS_recvfrom( xDHCPData.xDHCPSocket, ( void * ) &pucUDPPayload, 0, FREERTOS_ZERO_COPY, &xClient, &xClientLength );

	if( lBytes > 0 )
	{
		/* Map a DHCP structure onto the received data. */
		pxDHCPMessage = ( DHCPMessage_t * ) ( pucUDPPayload );

		/* Sanity check. */
		if( ( pxDHCPMessage->ulDHCPCookie == dhcpCOOKIE ) && ( pxDHCPMessage->ucOpcode == dhcpREPLY_OPCODE ) &&
			( pxDHCPMessage->ulTransactionID == FreeRTOS_htonl( xDHCPData.ulTransactionId ) ) )
		{
			if( memcmp( ( void * ) &( pxDHCPMessage->ucClientHardwareAddress ), ( void * ) ipLOCAL_MAC_ADDRESS, sizeof( MACAddress_t ) ) == 0 )
			{
				/* None of the essential options have been processed yet. */
				ulProcessed = 0;

				/* Walk through the options until the dhcpOPTION_END_BYTE byte
				is found, taking care not to walk off the end of the options. */
				pucByte = &( pxDHCPMessage->ucFirstOptionByte );
				pucLastByte = &( pucUDPPayload[ lBytes - dhcpMAX_OPTION_LENGTH_OF_INTEREST ] );

				while( pucByte < pucLastByte )
				{
					ucOptionCode = pucByte[ 0 ];
					if( ucOptionCode == dhcpOPTION_END_BYTE )
					{
						/* Ready, the last byte has been seen. */
						break;
					}
					if( ucOptionCode == dhcpZERO_PAD_OPTION_CODE )
					{
						/* The value zero is used as a pad byte,
						it is not followed by a length byte. */
						pucByte += 1;
						continue;
					}
					ucLength = pucByte[ 1 ];
					pucByte += 2;

					/* In most cases, a 4-byte network-endian parameter follows,
					just get it once here and use later */
					memcpy2( ( void * ) &( ulParameter ), ( void * ) pucByte, ( size_t ) sizeof( ulParameter ) );

					switch( ucOptionCode )
					{
						case dhcpMESSAGE_TYPE_OPTION_CODE	:

							if( *pucByte == ( unsigned char ) xExpectedMessageType )
							{
								/* The message type is the message type the
								state machine is expecting. */
								ulProcessed++;
							}
							else if( *pucByte == dhcpMESSAGE_TYPE_NACK )
							{
								if( dhcpMESSAGE_TYPE_ACK == ( unsigned char ) xExpectedMessageType)
								{
									/* Start again. */
									xDHCPData.eDHCPState = eWaitingSendFirstDiscover;
								}
							}
							else
							{
								/* Don't process other message types. */
							}
							break;

						case dhcpSUBNET_MASK_OPTION_CODE :

							if( ucLength == sizeof( unsigned int ) )
							{
								xNetworkAddressing.ulNetMask = ulParameter;
							}
							break;

						case dhcpGATEWAY_OPTION_CODE :

							if( ucLength == sizeof( unsigned int ) )
							{
								/* ulProcessed is not incremented in this case
								because the gateway is not essential. */
								xNetworkAddressing.ulGatewayAddress = ulParameter;
							}
							break;

						case dhcpDNS_SERVER_OPTIONS_CODE :

							/* ulProcessed is not incremented in this case
							because the DNS server is not essential.  Only the
							first DNS server address is taken. */
							xNetworkAddressing.ulDNSServerAddress = ulParameter;
							break;

						case dhcpSERVER_IP_ADDRESS_OPTION_CODE :

							if( ucLength == sizeof( unsigned int ) )
							{
								if( dhcpMESSAGE_TYPE_OFFER == ( unsigned char ) xExpectedMessageType )
								{
									/* Offers state the replying server. */
									ulProcessed++;
									xDHCPData.ulDHCPServerAddress = ulParameter;
								}
								else
								{
									/* The ack must come from the expected server. */
									if( xDHCPData.ulDHCPServerAddress == ulParameter )
									{
										ulProcessed++;
									}
								}
							}
							break;

						case dhcpLEASE_TIME_OPTION_CODE :

							if( ucLength == sizeof( &( xDHCPData.ulLeaseTime ) ) )
							{
								/* ulProcessed is not incremented in this case
								because the lease time is not essential. */
								/* The DHCP parameter is in seconds, convert
								to host-endian format. */
								xDHCPData.ulLeaseTime = FreeRTOS_ntohl( ulParameter );

								/* Divide the lease time by two to ensure a renew
								request is sent before the lease actually expires. */
								xDHCPData.ulLeaseTime >>= 1UL;

								/* Multiply with configTICK_RATE_HZ to get clock ticks. */
								xDHCPData.ulLeaseTime = configTICK_RATE_HZ * xDHCPData.ulLeaseTime;
							}
							break;

						default :

							/* Not interested in this field. */

							break;
					}

					/* Jump over the data to find the next option code. */
					if( ucLength == 0 )
					{
						break;
					}
					else
					{
						pucByte += ucLength;
					}
				}

				/* Were all the mandatory options received? */
				if( ulProcessed >= ulMandatoryOptions )
				{
					/* HT:endian: used to be network order */
					xDHCPData.ulOfferedIPAddress = pxDHCPMessage->ulYourIPAddress_yiaddr;
					FreeRTOS_printf( ( "vDHCPProcess: offer %lxip\n", FreeRTOS_ntohl( xDHCPData.ulOfferedIPAddress ) ) );
					xReturn = pdPASS;
				}
			}
		}

		FreeRTOS_ReleaseUDPPayloadBuffer( ( void * ) pucUDPPayload );
	}

	return xReturn;
}
/*-----------------------------------------------------------*/

static unsigned char *prvCreatePartDHCPMessage( struct freertos_sockaddr *pxAddress, portBASE_TYPE xOpcode, const unsigned char * const pucOptionsArray, size_t *pxOptionsArraySize )
{
DHCPMessage_t *pxDHCPMessage;
size_t xRequiredBufferSize = sizeof( DHCPMessage_t ) + *pxOptionsArraySize;
unsigned char *pucUDPPayloadBuffer;

#if( ipconfigDHCP_REGISTER_HOSTNAME == 1 )
	const char *pucHostName = pcApplicationHostnameHook ();
	size_t xNameLength = strlen( pucHostName );
	unsigned char *pucPtr;

	xRequiredBufferSize += ( 2 + xNameLength );
#endif

	/* Get a buffer.  This uses a maximum delay, but the delay will be capped
	to ipconfigUDP_MAX_SEND_BLOCK_TIME_TICKS so the return value still needs to
	be test. */
	do
	{
	} while( ( pucUDPPayloadBuffer = ( unsigned char * ) FreeRTOS_GetUDPPayloadBuffer( xRequiredBufferSize, portMAX_DELAY ) ) == NULL );

	pxDHCPMessage = ( DHCPMessage_t * ) pucUDPPayloadBuffer;

	/* Most fields need to be zero. */
	memset( ( void * ) pxDHCPMessage, 0x00, sizeof( DHCPMessage_t ) );

	/* Create the message. */
	pxDHCPMessage->ucOpcode = ( unsigned char ) xOpcode;
	pxDHCPMessage->ucAddressType = dhcpADDRESS_TYPE_ETHERNET;
	pxDHCPMessage->ucAddressLength = dhcpETHERNET_ADDRESS_LENGTH;

	/* ulTransactionID doesn't really need a htonl() translation, but when DHCP
	times out, it is nicer to see an increasing number in this ID field */
	pxDHCPMessage->ulTransactionID = FreeRTOS_htonl( xDHCPData.ulTransactionId );
	pxDHCPMessage->ulDHCPCookie = dhcpCOOKIE;
	if( xDHCPData.xUseBroadcast != pdFALSE )
	{
		pxDHCPMessage->usFlags = dhcpBROADCAST;
	}
	else
	{
		pxDHCPMessage->usFlags = 0;
	}

	memcpy2( ( void * ) &( pxDHCPMessage->ucClientHardwareAddress[ 0 ] ), ( void * ) ipLOCAL_MAC_ADDRESS, sizeof( MACAddress_t ) );

	/* Copy in the const part of the options options. */
	memcpy2( ( void * ) &( pucUDPPayloadBuffer[ dhcpFIRST_OPTION_BYTE_OFFSET ] ), ( void * ) pucOptionsArray, *pxOptionsArraySize );

	#if( ipconfigDHCP_REGISTER_HOSTNAME == 1 )
	{
		/* With this option, the hostname can be registered as well which makes
		it easier to lookup a device in a router's list of DHCP clients. */

		/* Point to where the OPTION_END was stored to add data. */
		pucPtr = &( pucUDPPayloadBuffer[ dhcpFIRST_OPTION_BYTE_OFFSET + ( *pxOptionsArraySize - 1 ) ] );
		pucPtr[ 0 ] = dhcpDNS_HOSTNAME_OPTIONS_CODE;
		pucPtr[ 1 ] = ( unsigned char ) xNameLength;
		memcpy2( ( void *) ( pucPtr + 2 ), pucHostName, xNameLength );
		pucPtr[ 2 + xNameLength ] = dhcpOPTION_END_BYTE;
		*pxOptionsArraySize += ( 2 + xNameLength );
	}
	#endif

	/* Map in the client identifier. */
	memcpy2( ( void * ) &( pucUDPPayloadBuffer[ dhcpFIRST_OPTION_BYTE_OFFSET + dhcpCLIENT_IDENTIFIER_OFFSET ] ),
		( void * ) ipLOCAL_MAC_ADDRESS, sizeof( MACAddress_t ) );

	/* Set the addressing. */
	pxAddress->sin_addr = ipBROADCAST_IP_ADDRESS;
	pxAddress->sin_port = ( unsigned short ) dhcpSERVER_PORT;

	return pucUDPPayloadBuffer;
}
/*-----------------------------------------------------------*/

static void prvSendDHCPRequest( void )
{
unsigned char *pucUDPPayloadBuffer;
struct freertos_sockaddr xAddress;
static const unsigned char ucDHCPRequestOptions[] =
{
	/* Do not change the ordering without also changing
	dhcpCLIENT_IDENTIFIER_OFFSET, dhcpREQUESTED_IP_ADDRESS_OFFSET and
	dhcpDHCP_SERVER_IP_ADDRESS_OFFSET. */
	dhcpMESSAGE_TYPE_OPTION_CODE, 1, dhcpMESSAGE_TYPE_REQUEST,		/* Message type option. */
	dhcpCLIENT_IDENTIFIER_OPTION_CODE, 6, 0, 0, 0, 0, 0, 0,			/* Client identifier. */
	dhcpREQUEST_IP_ADDRESS_OPTION_CODE, 4, 0, 0, 0, 0,				/* The IP address being requested. */
	dhcpSERVER_IP_ADDRESS_OPTION_CODE, 4, 0, 0, 0, 0,				/* The IP address of the DHCP server. */
	dhcpOPTION_END_BYTE
};
size_t xOptionsLength = sizeof( ucDHCPRequestOptions );

	pucUDPPayloadBuffer = prvCreatePartDHCPMessage( &xAddress, (unsigned char)dhcpREQUEST_OPCODE, ucDHCPRequestOptions, &xOptionsLength );

	/* Copy in the IP address being requested. */
	memcpy2( ( void * ) &( pucUDPPayloadBuffer[ dhcpFIRST_OPTION_BYTE_OFFSET + dhcpREQUESTED_IP_ADDRESS_OFFSET ] ),
		( void * ) &( xDHCPData.ulOfferedIPAddress ), sizeof( xDHCPData.ulOfferedIPAddress ) );

	/* Copy in the address of the DHCP server being used. */
	memcpy2( ( void * ) &( pucUDPPayloadBuffer[ dhcpFIRST_OPTION_BYTE_OFFSET + dhcpDHCP_SERVER_IP_ADDRESS_OFFSET ] ),
		( void * ) &( xDHCPData.ulDHCPServerAddress ), sizeof( xDHCPData.ulDHCPServerAddress ) );

	FreeRTOS_debug_printf( ( "vDHCPProcess: reply %lxip\n", FreeRTOS_ntohl( xDHCPData.ulOfferedIPAddress ) ) );
	iptraceSENDING_DHCP_REQUEST();

	if( FreeRTOS_sendto( xDHCPData.xDHCPSocket, pucUDPPayloadBuffer, ( sizeof( DHCPMessage_t ) + xOptionsLength ), FREERTOS_ZERO_COPY, &xAddress, sizeof( xAddress ) ) == 0 )
	{
		/* The packet was not successfully queued for sending and must be
		returned to the stack. */
		FreeRTOS_ReleaseUDPPayloadBuffer( pucUDPPayloadBuffer );
	}
}
/*-----------------------------------------------------------*/

static void prvSendDHCPDiscover( void )
{
unsigned char *pucUDPPayloadBuffer;
struct freertos_sockaddr xAddress;
static const unsigned char ucDHCPDiscoverOptions[] =
{
	/* Do not change the ordering without also changing dhcpCLIENT_IDENTIFIER_OFFSET. */
	dhcpMESSAGE_TYPE_OPTION_CODE, 1, dhcpMESSAGE_TYPE_DISCOVER,					/* Message type option. */
	dhcpCLIENT_IDENTIFIER_OPTION_CODE, 6, 0, 0, 0, 0, 0, 0,						/* Client identifier. */
	dhcpPARAMETER_REQUEST_OPTION_CODE, 3, dhcpSUBNET_MASK_OPTION_CODE, dhcpGATEWAY_OPTION_CODE, dhcpDNS_SERVER_OPTIONS_CODE,	/* Parameter request option. */
	dhcpOPTION_END_BYTE
};
size_t xOptionsLength = sizeof( ucDHCPDiscoverOptions );

	pucUDPPayloadBuffer = prvCreatePartDHCPMessage( &xAddress, (unsigned char)dhcpREQUEST_OPCODE, ucDHCPDiscoverOptions, &xOptionsLength );

	FreeRTOS_debug_printf( ( "vDHCPProcess: discover\n" ) );
	iptraceSENDING_DHCP_DISCOVER();

	if( FreeRTOS_sendto( xDHCPData.xDHCPSocket, pucUDPPayloadBuffer, ( sizeof( DHCPMessage_t ) + xOptionsLength ), FREERTOS_ZERO_COPY, &xAddress, sizeof( xAddress ) ) == 0 )
	{
		/* The packet was not successfully queued for sending and must be
		returned to the stack. */
		FreeRTOS_ReleaseUDPPayloadBuffer( pucUDPPayloadBuffer );
	}
}
/*-----------------------------------------------------------*/

#if( ipconfigDHCP_FALL_BACK_AUTO_IP != 0 )

	static void prvPrepareLinkLayerIPLookUp()
	{
	unsigned char ucLinkLayerIPAddress[ 2 ];

		/* After DHCP has failed to answer, prepare everything to start
		trying-out LinkLayer IP-addresses, using the random method. */
		xDHCPData.xDHCPTxTime = xTaskGetTickCount();

		ucLinkLayerIPAddress[ 0 ] = ( unsigned char )1 + ( ipconfigRAND32() % 0xFD );		/* get value 1..254 for IP-address 3rd byte of IP address to try. */
		ucLinkLayerIPAddress[ 1 ] = ( unsigned char )1 + ( ipconfigRAND32() % 0xFD );		/* get value 1..254 for IP-address 4th byte of IP address to try. */

		xNetworkAddressing.ulGatewayAddress = FreeRTOS_htonl( 0xA9FE0203 );

		/* prepare xDHCPData with data to test. */
		xDHCPData.ulOfferedIPAddress =
			FreeRTOS_inet_addr_quick( LINK_LAYER_ADDRESS_0, LINK_LAYER_ADDRESS_1, ucLinkLayerIPAddress[ 0 ], ucLinkLayerIPAddress[ 1 ] );

		xDHCPData.ulLeaseTime = dhcpDEFAULT_LEASE_TIME;	 /*  don't care about lease time. just put anything. */

		xNetworkAddressing.ulNetMask =
			FreeRTOS_inet_addr_quick( LINK_LAYER_NETMASK_0, LINK_LAYER_NETMASK_1, LINK_LAYER_NETMASK_2, LINK_LAYER_NETMASK_3 );

		/* DHCP completed.  The IP address can now be used, and the
		timer set to the lease timeout time. */
		*ipLOCAL_IP_ADDRESS_POINTER = xDHCPData.ulOfferedIPAddress;

		/* Setting the 'local' broadcast address, something like 192.168.1.255' */
		xNetworkAddressing.ulBroadcastAddress = ( xDHCPData.ulOfferedIPAddress & xNetworkAddressing.ulNetMask ) |  ~xNetworkAddressing.ulNetMask;

		/* Close socket to ensure packets don't queue on it. not needed anymore as DHCP failed. but still need timer for ARP testing. */
		FreeRTOS_closesocket( xDHCPData.xDHCPSocket );
		xDHCPData.xDHCPSocket = NULL;

		xDHCPData.xDHCPTxPeriod = pdMS_TO_TICKS( 3000 + ( ipconfigRAND32() & 0x3ff ) ); /*  do ARP test every (3 + 0-1024mS) seconds. */

		xARPHadIPClash = pdFALSE;	   /* reset flag that shows if have ARP clash. */
		vARPSendGratuitous();
	}

#endif /* ipconfigDHCP_FALL_BACK_AUTO_IP */
/*-----------------------------------------------------------*/

#endif /* ipconfigUSE_DHCP != 0 */


