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
#include "queue.h"
#include "list.h"
#include "semphr.h"

/* FreeRTOS+TCP includes. */
#include "FreeRTOS_IP.h"
#include "FreeRTOS_Sockets.h"
#include "FreeRTOS_IP_Private.h"
#include "FreeRTOS_UDP_IP.h"
#include "FreeRTOS_DNS.h"
#include "NetworkBufferManagement.h"
#include "NetworkInterface.h"
#include "IPTraceMacroDefaults.h"

/* Exclude the entire file if DNS is not enabled. */
#if( ipconfigUSE_DNS != 0 )

#if( ipconfigBYTE_ORDER == pdFREERTOS_LITTLE_ENDIAN )
	#define dnsDNS_PORT						0x3500
	#define dnsONE_QUESTION					0x0100
	#define dnsOUTGOING_FLAGS				0x0001 /* Standard query. */
	#define dnsRX_FLAGS_MASK				0x0f80 /* The bits of interest in the flags field of incoming DNS messages. */
	#define dnsEXPECTED_RX_FLAGS			0x0080 /* Should be a response, without any errors. */
#else
	#define dnsDNS_PORT						0x0035
	#define dnsONE_QUESTION					0x0001
	#define dnsOUTGOING_FLAGS				0x0100 /* Standard query. */
	#define dnsRX_FLAGS_MASK				0x800f /* The bits of interest in the flags field of incoming DNS messages. */
	#define dnsEXPECTED_RX_FLAGS			0x8000 /* Should be a response, without any errors. */

#endif /* ipconfigBYTE_ORDER */

/* The maximum number of times a DNS request should be sent out if a response
is not received, before giving up. */
#ifndef ipconfigDNS_REQUEST_ATTEMPTS
	#define ipconfigDNS_REQUEST_ATTEMPTS		5
#endif

/* If the top two bits in the first character of a name field are set then the
name field is an offset to the string, rather than the string itself. */
#define dnsNAME_IS_OFFSET					( ( unsigned char ) 0xc0 )

/* NBNS flags. */
#define ipNBNS_FLAGS_RESPONSE				0x8000
#define ipNBNS_FLAGS_OPCODE_MASK			0x7800
#define ipNBNS_FLAGS_OPCODE_QUERY			0x0000
#define ipNBNS_FLAGS_OPCODE_REGISTRATION	0x2800

/*
 * Create a socket and bind it to the standard DNS port number.  Return the
 * the created socket - or NULL if the socket could not be created or bound.
 */
static Socket_t prvCreateDNSSocket( void );

/*
 * Create the DNS message in the zero copy buffer passed in the first parameter.
 */
static size_t prvCreateDNSMessage( unsigned char *pucUDPPayloadBuffer, const char *pcHostName, portTickType xIdentifier );

/*
 * Simple routine that jumps over the NAME field of a resource record.
 */
static unsigned char *prvSkipNameField( unsigned char *pucByte );

/*
 * Process a response packet from a DNS server.
 */
static unsigned int prvParseDNSReply( unsigned char *pucUDPPayloadBuffer, portTickType xIdentifier );

/*
 * Prepare and send a message to a DNS server.  'xReadTimeOut_ms' will be passed as
 * zero, in case the user has supplied a call-back function.
 */
static unsigned int prvGetHostByName( const char *pcHostName, portTickType xIdentifier, portTickType xReadTimeOut_ms );

#if( ipconfigUSE_DNS_CACHE == 1 )
	static unsigned char *prvReadNameField( unsigned char *pucByte, char *pcName, portBASE_TYPE xLen );
	static void prvProcessDNSCache( const char *pcName, unsigned int *pulIP, portBASE_TYPE xLookUp );

	typedef struct xDNS_CACHE_TABLE_ROW
	{
		unsigned int ulIPAddress;		/* The IP address of an ARP cache entry. */
		char pcName[ipconfigDNS_CACHE_NAME_LENGTH];  /* The name of the host */
		unsigned char ucAge;				/* A value that is periodically decremented but can also be refreshed by active communication.  The ARP cache entry is removed if the value reaches zero. */
	} DNSCacheRow_t;

	static DNSCacheRow_t xDNSCache[ ipconfigDNS_CACHE_ENTRIES ];
#endif /* ipconfigUSE_DNS_CACHE == 1 */

/*
 * The NBNS and the LLMNR protocol share this reply function.
 */
#if( ( ipconfigUSE_NBNS == 1 ) || ( ipconfigUSE_LLMNR == 1 ) )
	static void prvReplyDNSMessage( NetworkBufferDescriptor_t *pxNetworkBuffer, portBASE_TYPE lNetLength );
#endif

#if( ipconfigUSE_LLMNR == 1 )
	const MACAddress_t xLLMNR_MacAdress = { { 0x01, 0x00, 0x5e, 0x00, 0x00, 0xfc } };
#endif	/* ipconfigUSE_LLMNR == 1 */


#if( ipconfigUSE_NBNS == 1 )
	static portINLINE void prvTreatNBNS( unsigned char *pucUDPPayloadBuffer, unsigned int ulIPAddress );
#endif /* ipconfigUSE_NBNS */

/*-----------------------------------------------------------*/

#define DNS_TYPE_A_HOST			0x01
#define DNS_CLASS_IN			0x01

#include "pack_struct_start.h"
struct xDNSMessage
{
	unsigned short usIdentifier;
	unsigned short usFlags;
	unsigned short usQuestions;
	unsigned short usAnswers;
	unsigned short usAuthorityRRs;
	unsigned short usAdditionalRRs;
}
#include "pack_struct_end.h"
typedef struct xDNSMessage DNSMessage_t;

/* A DNS query consists of a header, as described in 'struct xDNSMessage'
It is followed by 1 or more queries, each one consisting of a name and a tail,
with two fields: type and class
*/
#include "pack_struct_start.h"
struct xDNSTail
{
	unsigned short usType;
	unsigned short usClass;
}
#include "pack_struct_end.h"
typedef struct xDNSTail DNSTail_t;

#if( ipconfigUSE_LLMNR == 1 )

	#define LLMNR_TTL_VALUE			300000 /* recommended value */
	#define LLMNR_FLAGS_IS_REPONSE  0x8000

	#include "pack_struct_start.h"
	struct xLLMNRAnswer
	{
		unsigned char ucNameCode;
		unsigned char ucNameOffset;	/* The name is not repeated in the answer, only the offset is given with "0xc0 <offs>" */
		unsigned short usType;
		unsigned short usClass;
		unsigned int ulTTL;
		unsigned short usDataLength;
		unsigned int ulIPAddress;
	}
	#include "pack_struct_end.h"
	typedef struct xLLMNRAnswer LLMNRAnswer_t;

#endif /* ipconfigUSE_LLMNR == 1 */

#if( ipconfigUSE_NBNS == 1 )

	#define NBNS_TTL_VALUE			3600 /* 1 hour valid */
	#define NBNS_TYPE_NET_BIOS		0x0020
	#define NBNS_CLASS_IN			0x01
	#define NBNS_NAME_FLAGS			0x6000
	#define NBNS_ENCODED_NAME_LENGTH	32

	/* If the queried NBNS name matches with the device's name,
	the query will be responded to with these flags: */
	#define NBNS_QUERY_RESPONSE_FLAGS	( 0x8500 )

	#include "pack_struct_start.h"
	struct xNBNSRequest
	{
		unsigned short usRequestId;
		unsigned short usFlags;
		unsigned short ulRequestCount;
		unsigned short usAnswerRSS;
		unsigned short usAuthRSS;
		unsigned short usAdditionalRSS;
		unsigned char ucNameSpace;
		unsigned char ucName[ NBNS_ENCODED_NAME_LENGTH ];
		unsigned char ucNameZero;
		unsigned short usType;
		unsigned short usClass;
	}
	#include "pack_struct_end.h"
	typedef struct xNBNSRequest NBNSRequest_t;

	#include "pack_struct_start.h"
	struct xNBNSAnswer
	{
		unsigned short usType;
		unsigned short usClass;
		unsigned int ulTTL;
		unsigned short usDataLength;
		unsigned short usNbFlags;		/* NetBIOS flags 0x6000 : IP-address, big-endian */
		unsigned int ulIPAddress;
	}
	#include "pack_struct_end.h"
	typedef struct xNBNSAnswer NBNSAnswer_t;

#endif /* ipconfigUSE_NBNS == 1 */

/*-----------------------------------------------------------*/

#if( ipconfigUSE_DNS_CACHE == 1 )
	unsigned int FreeRTOS_dnslookup( const char *pcHostName )
	{
	unsigned int ulIPAddress = 0UL;
		prvProcessDNSCache( pcHostName, &ulIPAddress, pdTRUE );
		return ulIPAddress;
	}
#endif /* ipconfigUSE_DNS_CACHE == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigDNS_USE_CALLBACKS != 0 )
	typedef struct xDNS_Callback {
		portTickType xRemaningTime;		/* Timeout in ms */
		FOnDNSEvent pCallbackFunction;	/* Function to be called when the address has been found or when a timeout has beeen reached */
		xTimeOutType xTimeoutState;
		void *pvSearchID;
		struct xLIST_ITEM xListItem;
		char pcName[ 1 ];
	} DNSCallback_t;

	static xList xCallbackList;

	/* Define FreeRTOS_gethostbyname() as a normal blocking call. */
	unsigned int FreeRTOS_gethostbyname( const char *pcHostName )
	{
		return FreeRTOS_gethostbyname_a( pcHostName, ( FOnDNSEvent ) NULL, ( void* )NULL, 0 );
	}
	/*-----------------------------------------------------------*/

	/* Initialise the list of call-back structures. */
	void vDNSInitialise( void );
	void vDNSInitialise( void )
	{
		vListInitialise( &xCallbackList );
	}
	/*-----------------------------------------------------------*/

	/* Iterate through the list of call-back structures and remove
	old entries which have reached a timeout.
	As soon as the list hase become empty, the DNS timer will be stopped
	In case pvSearchID is supplied, the user wants to cancel a DNS request
	*/
	void vDNSCheckCallBack( void *pvSearchID );
	void vDNSCheckCallBack( void *pvSearchID )
	{
	const xListItem *pxIterator;
	const xMiniListItem* xEnd = ( const xMiniListItem* )listGET_END_MARKER( &xCallbackList );

		vTaskSuspendAll();
		{
			for( pxIterator  = ( const xListItem * ) listGET_NEXT( xEnd );
				 pxIterator != ( const xListItem * ) xEnd;
				  )
			{
				DNSCallback_t *pxCallback = ( DNSCallback_t * ) listGET_LIST_ITEM_OWNER( pxIterator );
				/* Move to the next item because we might remove this item */
				pxIterator  = ( const xListItem * ) listGET_NEXT( pxIterator );
				if( ( pvSearchID != NULL ) && ( pvSearchID == pxCallback->pvSearchID ) )
				{
					vListRemove( &pxCallback->xListItem );
					vPortFree( pxCallback );
				}
				else if( xTaskCheckForTimeOut( &pxCallback->xTimeoutState, &pxCallback->xRemaningTime ) != pdFALSE )
				{
					pxCallback->pCallbackFunction( pxCallback->pcName, pxCallback->pvSearchID, 0 );
					vListRemove( &pxCallback->xListItem );
					vPortFree( ( void * ) pxCallback );
				}
			}
		}
		xTaskResumeAll();

		if( listLIST_IS_EMPTY( &xCallbackList ) )
		{
			vIPSetDnsTimerEnableState( pdFALSE );
		}
	}
	/*-----------------------------------------------------------*/
	void FreeRTOS_gethostbyname_cancel( void *pvSearchID )
	{
		/* _HT_ Should better become a new API call to have the IP-task remove the callback */
		vDNSCheckCallBack( pvSearchID );
	}
	/*-----------------------------------------------------------*/

	/* FreeRTOS_gethostbyname_a() was called along with callback parameters.
	Store them in a list for later reference. */
	static void vDNSSetCallBack( const char *pcHostName, void *pvSearchID, FOnDNSEvent pCallbackFunction, portTickType xTimeout, portTickType xIdentifier );
	static void vDNSSetCallBack( const char *pcHostName, void *pvSearchID, FOnDNSEvent pCallbackFunction, portTickType xTimeout, portTickType xIdentifier )
	{
		size_t lLength = strlen( pcHostName );
		DNSCallback_t *pxCallback = ( DNSCallback_t * )pvPortMalloc( sizeof( *pxCallback ) + lLength );

		/* Translate from ms to number of clock ticks. */
		xTimeout /= portTICK_PERIOD_MS;
		if( pxCallback != NULL )
		{
			if( listLIST_IS_EMPTY( &xCallbackList ) )
			{
				/* This is the first one, start the DNS timer to check for timeouts */
				vIPReloadDNSTimer( FreeRTOS_min_uint32( 1000U, xTimeout ) );
			}
			strcpy( pxCallback->pcName, pcHostName );
			pxCallback->pCallbackFunction = pCallbackFunction;
			pxCallback->pvSearchID = pvSearchID;
			pxCallback->xRemaningTime = xTimeout;
			vTaskSetTimeOutState( &pxCallback->xTimeoutState );
			listSET_LIST_ITEM_OWNER( &( pxCallback->xListItem ), ( void* ) pxCallback );
			listSET_LIST_ITEM_VALUE( &( pxCallback->xListItem ), xIdentifier );
			vTaskSuspendAll();
			{
				vListInsertEnd( &xCallbackList, &pxCallback->xListItem );
			}
			xTaskResumeAll();
		}
	}
	/*-----------------------------------------------------------*/

	/* A DNS reply was received, see if there is any matching entry and
	call the handler. */
	static void vDNSDoCallback( portTickType xIdentifier, const char *pcName, unsigned int ulIPAddress );
	static void vDNSDoCallback( portTickType xIdentifier, const char *pcName, unsigned int ulIPAddress )
	{
		const xListItem *pxIterator;
		const xMiniListItem* xEnd = ( const xMiniListItem* )listGET_END_MARKER( &xCallbackList );

		vTaskSuspendAll();
		{
			for( pxIterator  = ( const xListItem * ) listGET_NEXT( xEnd );
				 pxIterator != ( const xListItem * ) xEnd;
				 pxIterator  = ( const xListItem * ) listGET_NEXT( pxIterator ) )
			{
				if( listGET_LIST_ITEM_VALUE( pxIterator ) == xIdentifier )
				{
					DNSCallback_t *pxCallback = ( DNSCallback_t * ) listGET_LIST_ITEM_OWNER( pxIterator );
					pxCallback->pCallbackFunction( pcName, pxCallback->pvSearchID, ulIPAddress );
					vListRemove( &pxCallback->xListItem );
					vPortFree( pxCallback );
					if( listLIST_IS_EMPTY( &xCallbackList ) )
					{
						vIPSetDnsTimerEnableState( pdFALSE );
					}
					break;
				}
			}
		}
		xTaskResumeAll();
	}
#endif	/* ipconfigDNS_USE_CALLBACKS != 0 */

#if( ipconfigDNS_USE_CALLBACKS == 0 )
unsigned int FreeRTOS_gethostbyname( const char *pcHostName )
#else
unsigned int FreeRTOS_gethostbyname_a( const char *pcHostName, FOnDNSEvent pCallback, void *pvSearchID, portTickType xTimeout )
#endif
{
unsigned int ulIPAddress = 0UL;
static unsigned short usIdentifier = 0;
portTickType xReadTimeOut_ms = 1200U;
/* Generate a unique identifier for this query. Keep it in a local variable
 as gethostbyname() may be called from different threads */
portTickType xIdentifier = ( portTickType )usIdentifier++;

	/* If a DNS cache is used then check the cache before issuing another DNS
	request. */
	#if( ipconfigUSE_DNS_CACHE == 1 )
	{
		ulIPAddress = FreeRTOS_dnslookup( pcHostName );
		if( ulIPAddress != 0 )
		{
			FreeRTOS_debug_printf( ( "FreeRTOS_gethostbyname: found '%s' in cache: %lxip\n", pcHostName, ulIPAddress ) );
		}
		else
		{
			/* prvGetHostByName will be called to start a DNS lookup */
		}
	}
	#endif /* ipconfigUSE_DNS_CACHE == 1 */

	#if( ipconfigDNS_USE_CALLBACKS != 0 )
	{
		if( pCallback != NULL )
		{
			if( ulIPAddress == 0UL )
			{
				/* The user has provided a callback function, so do not block on recvfrom() */
				xReadTimeOut_ms  = 0;
				vDNSSetCallBack( pcHostName, pvSearchID, pCallback, xTimeout, ( portTickType ) xIdentifier );
			}
			else
			{
				/* The IP address is known, do the call-back now. */
				pCallback( pcHostName, pvSearchID, ulIPAddress );
			}
		}
	}
	#endif

	if( ulIPAddress == 0UL)
	{
		ulIPAddress = prvGetHostByName( pcHostName, xIdentifier, xReadTimeOut_ms );
	}

	return ulIPAddress;
}

static unsigned int prvGetHostByName( const char *pcHostName, portTickType xIdentifier, portTickType xReadTimeOut_ms )
{
struct freertos_sockaddr xAddress;
Socket_t xDNSSocket;
unsigned int ulIPAddress = 0UL;
unsigned char *pucUDPPayloadBuffer;
static unsigned int ulAddressLength;
portBASE_TYPE xAttempt;
int lBytes;
size_t xPayloadLength, xExpectedPayloadLength;
portTickType xWriteTimeOut_ms = 100U;

#if( ipconfigUSE_LLMNR == 1 )
	portBASE_TYPE bHasDot = pdFALSE;
#endif /* ipconfigUSE_LLMNR == 1 */

	/* If LLMNR is being used then determine if the host name includes a '.' -
	if not then LLMNR can be used as the lookup method. */
	#if( ipconfigUSE_LLMNR == 1 )
	{
		const char *pucPtr;
		for( pucPtr = pcHostName; *pucPtr; pucPtr++ )
		{
			if( *pucPtr == '.' )
			{
				bHasDot = pdTRUE;
				break;
			}
		}
	}
	#endif /* ipconfigUSE_LLMNR == 1 */

	/* Two is added at the end for the count of characters in the first
	subdomain part and the string end byte. */
	xExpectedPayloadLength = sizeof( DNSMessage_t ) + strlen( pcHostName ) + sizeof( unsigned short ) + sizeof( unsigned short ) + 2;

	xDNSSocket = prvCreateDNSSocket();

	if( xDNSSocket != NULL )
	{
		FreeRTOS_setsockopt( xDNSSocket, 0, FREERTOS_SO_SNDTIMEO, ( void * ) &xWriteTimeOut_ms, sizeof( portTickType ) );
		FreeRTOS_setsockopt( xDNSSocket, 0, FREERTOS_SO_RCVTIMEO, ( void * ) &xReadTimeOut_ms,  sizeof( portTickType ) );

		for( xAttempt = 0; xAttempt < ipconfigDNS_REQUEST_ATTEMPTS; xAttempt++ )
		{
			/* Get a buffer.  This uses a maximum delay, but the delay will be
			capped to ipconfigUDP_MAX_SEND_BLOCK_TIME_TICKS so the return value
			still needs to be tested. */
			pucUDPPayloadBuffer = ( unsigned char * ) FreeRTOS_GetUDPPayloadBuffer( xExpectedPayloadLength, portMAX_DELAY );

			if( pucUDPPayloadBuffer != NULL )
			{
				/* Create the message in the obtained buffer. */
				xPayloadLength = prvCreateDNSMessage( pucUDPPayloadBuffer, pcHostName, xIdentifier );

				iptraceSENDING_DNS_REQUEST();

				/* Obtain the DNS server address. */
				FreeRTOS_GetAddressConfiguration( NULL, NULL, NULL, &ulIPAddress );

				/* Send the DNS message. */
#if( ipconfigUSE_LLMNR == 1 )
				if( bHasDot == pdFALSE )
				{
					/* Use LLMNR addressing. */
					( ( DNSMessage_t * ) pucUDPPayloadBuffer) -> usFlags = 0;
					xAddress.sin_addr = ipLLMNR_IP_ADDR;	/* Is in network byte order. */
					xAddress.sin_port = FreeRTOS_ntohs( ipLLMNR_PORT );
				}
				else
#endif
				{
					/* Use DNS server. */
					xAddress.sin_addr = ulIPAddress;
					xAddress.sin_port = dnsDNS_PORT;
				}

				ulIPAddress = 0;

				if( FreeRTOS_sendto( xDNSSocket, pucUDPPayloadBuffer, xPayloadLength, FREERTOS_ZERO_COPY, &xAddress, sizeof( xAddress ) ) != 0 )
				{
					/* Wait for the reply. */
					lBytes = FreeRTOS_recvfrom( xDNSSocket, &pucUDPPayloadBuffer, 0, FREERTOS_ZERO_COPY, &xAddress, &ulAddressLength );

					if( lBytes > 0 )
					{
						/* The reply was received.  Process it. */
						ulIPAddress = prvParseDNSReply( pucUDPPayloadBuffer, xIdentifier );

						/* Finished with the buffer.  The zero copy interface
						is being used, so the buffer must be freed by the
						task. */
						FreeRTOS_ReleaseUDPPayloadBuffer( ( void * ) pucUDPPayloadBuffer );

						if( ulIPAddress != 0 )
						{
							/* All done. */
							break;
						}
					}
				}
				else
				{
					/* The message was not sent so the stack will not be
					releasing the zero copy - it must be released here. */
					FreeRTOS_ReleaseUDPPayloadBuffer( ( void * ) pucUDPPayloadBuffer );
				}
			}
		}

		/* Finished with the socket. */
		FreeRTOS_closesocket( xDNSSocket );
	}

	return ulIPAddress;
}
/*-----------------------------------------------------------*/

static size_t prvCreateDNSMessage( unsigned char *pucUDPPayloadBuffer, const char *pcHostName, portTickType xIdentifier )
{
DNSMessage_t *pxDNSMessageHeader;
unsigned char *pucStart, *pucByte;
DNSTail_t *pxTail;
static const DNSMessage_t xDefaultPartDNSHeader =
{
	0,					/* The identifier will be overwritten. */
	dnsOUTGOING_FLAGS,	/* Flags set for standard query. */
	dnsONE_QUESTION,	/* One question is being asked. */
	0,					/* No replies are included. */
	0,					/* No authorities. */
	0					/* No additional authorities. */
};

	/* Copy in the const part of the header. */
	memcpy2( ( void * ) pucUDPPayloadBuffer, ( void * ) &xDefaultPartDNSHeader, sizeof( xDefaultPartDNSHeader ) );

	/* Write in a unique identifier. */
	pxDNSMessageHeader = ( DNSMessage_t * ) pucUDPPayloadBuffer;
	pxDNSMessageHeader->usIdentifier = ( unsigned short ) xIdentifier;

	/* Create the resource record at the end of the header.  First
	find the end of the header. */
	pucStart = pucUDPPayloadBuffer + sizeof( xDefaultPartDNSHeader );

	/* Leave a gap for the first length bytes. */
	pucByte = pucStart + 1;

	/* Copy in the host name. */
	strcpy( ( char * ) pucByte, pcHostName );

	/* Mark the end of the string. */
	pucByte += strlen( pcHostName );
	*pucByte = 0x00;

	/* Walk the string to replace the '.' characters with byte counts.
	pucStart holds the address of the byte count.  Walking the string
	starts after the byte count position. */
	pucByte = pucStart;

	do
	{
		pucByte++;

		while( ( *pucByte != 0x00 ) && ( *pucByte != '.' ) )
		{
			pucByte++;
		}

		/* Fill in the byte count, then move the pucStart pointer up to
		the found byte position. */
		*pucStart = ( unsigned char ) ( ( unsigned int ) pucByte - ( unsigned int ) pucStart );
		( *pucStart )--;

		pucStart = pucByte;

	} while( *pucByte != 0x00 );

	/* Finish off the record. */

	pxTail = (DNSTail_t *)( pucByte + 1 );

	vSetField16( pxTail, DNSTail_t, usType, DNS_TYPE_A_HOST );	/* Type A: host */
	vSetField16( pxTail, DNSTail_t, usClass, DNS_CLASS_IN );	/* 1: Class IN */

	/* Return the total size of the generated message, which is the space from
	the last written byte to the beginning of the buffer. */
	return ( ( unsigned int ) pucByte - ( unsigned int ) pucUDPPayloadBuffer + 1 ) + sizeof( *pxTail );
}
/*-----------------------------------------------------------*/

#if( ipconfigUSE_DNS_CACHE == 1 )

	static unsigned char *prvReadNameField( unsigned char *pucByte, char *pcName, portBASE_TYPE xLen )
	{
	portBASE_TYPE xNameLen = 0;
		/* Determine if the name is the fully coded name, or an offset to the name
		elsewhere in the message. */
		if( ( *pucByte & dnsNAME_IS_OFFSET ) == dnsNAME_IS_OFFSET )
		{
			/* Jump over the two byte offset. */
			pucByte += sizeof( unsigned short );

		}
		else
		{
			/* pucByte points to the full name.  Walk over the string. */
			while( *pucByte != 0x00 )
			{
				portBASE_TYPE xCount;
				if( xNameLen && xNameLen < xLen - 1 )
					pcName[xNameLen++] = '.';
				for( xCount = *(pucByte++); xCount--; pucByte++ )
				{
					if( xNameLen < xLen - 1 )
						pcName[xNameLen++] = *( ( char * ) pucByte );
				}
			}

			pucByte++;
		}

		return pucByte;
	}
#endif	/* ipconfigUSE_DNS_CACHE == 1 */
/*-----------------------------------------------------------*/

static unsigned char *prvSkipNameField( unsigned char *pucByte )
{
	/* Determine if the name is the fully coded name, or an offset to the name
	elsewhere in the message. */
	if( ( *pucByte & dnsNAME_IS_OFFSET ) == dnsNAME_IS_OFFSET )
	{
		/* Jump over the two byte offset. */
		pucByte += sizeof( unsigned short );

	}
	else
	{
		/* pucByte points to the full name.  Walk over the string. */
		while( *pucByte != 0x00 )
		{
			/* The number of bytes to jump for each name section is stored in the byte
			before the name section. */
			pucByte += ( *pucByte + 1 );
		}

		pucByte++;
	}

	return pucByte;
}
/*-----------------------------------------------------------*/

unsigned int ulDNSHandlePacket( NetworkBufferDescriptor_t *pxNetworkBuffer )
{
unsigned char *pucUDPPayloadBuffer = pxNetworkBuffer->pucEthernetBuffer + sizeof( UDPPacket_t );
DNSMessage_t *pxDNSMessageHeader = ( DNSMessage_t * ) pucUDPPayloadBuffer;

	prvParseDNSReply( pucUDPPayloadBuffer, ( unsigned int ) pxDNSMessageHeader->usIdentifier );

	/* The packet was not consumed. */
	return pdFAIL;
}
/*-----------------------------------------------------------*/

#if( ipconfigUSE_NBNS == 1 )

	unsigned int ulNBNSHandlePacket (NetworkBufferDescriptor_t *pxNetworkBuffer )
	{
	UDPPacket_t *pxUDPPacket = ( UDPPacket_t * ) pxNetworkBuffer->pucEthernetBuffer;
	unsigned char *pucUDPPayloadBuffer = pxNetworkBuffer->pucEthernetBuffer + sizeof( *pxUDPPacket );

		prvTreatNBNS( pucUDPPayloadBuffer, pxUDPPacket->xIPHeader.ulSourceIPAddress );

		/* The packet was not consumed. */
		return pdFAIL;
	}

#endif /* ipconfigUSE_NBNS */
/*-----------------------------------------------------------*/

static unsigned int prvParseDNSReply( unsigned char *pucUDPPayloadBuffer, portTickType xIdentifier )
{
DNSMessage_t *pxDNSMessageHeader;
unsigned int ulIPAddress = 0UL;
#if( ipconfigUSE_LLMNR == 1 )
	char *pcRequestedName = NULL;
#endif
unsigned char *pucByte;
unsigned short x, usDataLength, usQuestions;
#if( ipconfigUSE_LLMNR == 1 )
	unsigned short usType = 0, usClass = 0;
#endif
#if( ipconfigUSE_DNS_CACHE == 1 )
	char pcName[128] = ""; /*_RB_ What is the significance of 128?  Probably too big to go on the stack for a small MCU but don't know how else it could be made re-entrant.  Might be necessary. */
#endif

	pxDNSMessageHeader = ( DNSMessage_t * ) pucUDPPayloadBuffer;

	if( pxDNSMessageHeader->usIdentifier == ( unsigned short ) xIdentifier )
	{
		/* Start at the first byte after the header. */
		pucByte = pucUDPPayloadBuffer + sizeof( DNSMessage_t );

		/* Skip any question records. */
		usQuestions = FreeRTOS_ntohs( pxDNSMessageHeader->usQuestions );
		for( x = 0; x < usQuestions; x++ )
		{
			#if( ipconfigUSE_LLMNR == 1 )
			{
				if( x == 0 )
				{
					pcRequestedName = ( char * ) pucByte;
				}
			}
			#endif

#if( ipconfigUSE_DNS_CACHE == 1 )
			if( x == 0 )
			{
				pucByte = prvReadNameField( pucByte, pcName, sizeof( pcName ) );
			}
			else
#endif /* ipconfigUSE_DNS_CACHE */
			{
				/* Skip the variable length pcName field. */
				pucByte = prvSkipNameField( pucByte );
			}

			#if( ipconfigUSE_LLMNR == 1 )
			{
				/* usChar2u16 returns value in host endianness */
				usType = usChar2u16( pucByte );
				usClass = usChar2u16( pucByte + 2 );
			}
			#endif /* ipconfigUSE_LLMNR */

			/* Skip the type and class fields. */
			pucByte += sizeof( unsigned int );
		}

		/* Search through the answers records. */
		pxDNSMessageHeader->usAnswers = FreeRTOS_ntohs( pxDNSMessageHeader->usAnswers );

		if( ( pxDNSMessageHeader->usFlags & dnsRX_FLAGS_MASK ) == dnsEXPECTED_RX_FLAGS )
		{
			for( x = 0; x < pxDNSMessageHeader->usAnswers; x++ )
			{
				pucByte = prvSkipNameField( pucByte );

				/* Is the type field that of an A record? */
				if( usChar2u16( pucByte ) == DNS_TYPE_A_HOST )
				{
					/* This is the required record.  Skip the type, class, and
					time to live fields, plus the first byte of the data
					length. */
					pucByte += ( sizeof( unsigned int ) + sizeof( unsigned int ) + sizeof( unsigned char ) );

					/* Sanity check the data length. */
					if( ( size_t ) *pucByte == sizeof( unsigned int ) )
					{
						/* Skip the second byte of the length. */
						pucByte++;

						/* Copy the IP address out of the record. */
						memcpy2( ( void * ) &ulIPAddress, ( void * ) pucByte, sizeof( unsigned int ) );

						#if( ipconfigUSE_DNS_CACHE == 1 )
						{
							prvProcessDNSCache( pcName, &ulIPAddress, pdFALSE );
						}
						#endif /* ipconfigUSE_DNS_CACHE */
						#if( ipconfigDNS_USE_CALLBACKS != 0 )
						{
							/* See if any asynchronous call was made to FreeRTOS_gethostbyname_a() */
							vDNSDoCallback( ( portTickType ) pxDNSMessageHeader->usIdentifier, pcName, ulIPAddress );
						}
						#endif	/* ipconfigDNS_USE_CALLBACKS != 0 */
					}

					break;
				}
				else
				{
					/* Skip the type, class and time to live fields. */
					pucByte += ( sizeof( unsigned int ) + sizeof( unsigned int ) );

					/* Determine the length of the data in the field. */
					memcpy2( ( void * ) &usDataLength, ( void * ) pucByte, sizeof( unsigned short ) );
					usDataLength = FreeRTOS_ntohs( usDataLength );

					/* Jump over the data length bytes, and the data itself. */
					pucByte += usDataLength + sizeof( unsigned short );
				}
			}
		}
#if( ipconfigUSE_LLMNR == 1 )
		else if( usQuestions && ( usType == DNS_TYPE_A_HOST ) && ( usClass == DNS_CLASS_IN ) )
		{
			/* If this is not a reply to our DNS request, it might an LLMNR
			request. */
			if( xApplicationDNSQueryHook ( ( pcRequestedName + 1 ) ) )
			{
			short usLength;
			NetworkBufferDescriptor_t *pxNewBuffer = NULL;
			NetworkBufferDescriptor_t *pxNetworkBuffer = pxUDPPayloadBuffer_to_NetworkBuffer( pucUDPPayloadBuffer );
			LLMNRAnswer_t *pxAnswer;

				if( ( xBufferAllocFixedSize == pdFALSE ) && ( pxNetworkBuffer != NULL ) )
				{
				portBASE_TYPE xDataLength = pxNetworkBuffer->xDataLength + sizeof( UDPHeader_t ) +
					sizeof( EthernetHeader_t ) + sizeof( IPHeader_t );

					/* The field xDataLength was set to the length of the UDP payload.
					The answer (reply) will be longer than the request, so the packet
					must be duplicaed into a bigger buffer */
					pxNetworkBuffer->xDataLength = xDataLength;
					pxNewBuffer = pxDuplicateNetworkBufferWithDescriptor( pxNetworkBuffer, xDataLength + 16 );
					if( pxNewBuffer != NULL )
					{
					portBASE_TYPE xOffset1, xOffset2;

						xOffset1 = ( portBASE_TYPE ) ( pucByte - pucUDPPayloadBuffer );
						xOffset2 = ( portBASE_TYPE ) ( ( ( unsigned char * ) pcRequestedName ) - pucUDPPayloadBuffer );

						pxNetworkBuffer = pxNewBuffer;
						pucUDPPayloadBuffer = pxNetworkBuffer->pucEthernetBuffer + ipUDP_PAYLOAD_OFFSET;

						pucByte = pucUDPPayloadBuffer + xOffset1;
						pcRequestedName = ( char * ) ( pucUDPPayloadBuffer + xOffset2 );
						pxDNSMessageHeader = ( DNSMessage_t * ) pucUDPPayloadBuffer;

					}
					else
					{
						/* Just to indicate that the message may not be answered. */
						pxNetworkBuffer = NULL;
					}
				}
				if( pxNetworkBuffer != NULL )
				{
					pxAnswer = (LLMNRAnswer_t *)pucByte;

					/* We leave 'usIdentifier' and 'usQuestions' untouched */
					vSetField16( pxDNSMessageHeader, DNSMessage_t, usFlags, LLMNR_FLAGS_IS_REPONSE );	/* Set the response flag */
					vSetField16( pxDNSMessageHeader, DNSMessage_t, usAnswers, 1 );	/* Provide a single answer */
					vSetField16( pxDNSMessageHeader, DNSMessage_t, usAuthorityRRs, 0 );	/* No authority */
					vSetField16( pxDNSMessageHeader, DNSMessage_t, usAdditionalRRs, 0 );	/* No additional info */

					pxAnswer->ucNameCode = dnsNAME_IS_OFFSET;
					pxAnswer->ucNameOffset = ( unsigned char )( pcRequestedName - ( char * ) pucUDPPayloadBuffer );

					vSetField16( pxAnswer, LLMNRAnswer_t, usType, DNS_TYPE_A_HOST );	/* Type A: host */
					vSetField16( pxAnswer, LLMNRAnswer_t, usClass, DNS_CLASS_IN );	/* 1: Class IN */
					vSetField32( pxAnswer, LLMNRAnswer_t, ulTTL, LLMNR_TTL_VALUE );
					vSetField16( pxAnswer, LLMNRAnswer_t, usDataLength, 4 );
					vSetField32( pxAnswer, LLMNRAnswer_t, ulIPAddress, FreeRTOS_ntohl( *ipLOCAL_IP_ADDRESS_POINTER ) );

					usLength = ( short ) ( sizeof( *pxAnswer ) + ( size_t ) ( pucByte - pucUDPPayloadBuffer ) );

					prvReplyDNSMessage( pxNetworkBuffer, usLength );

					if( pxNewBuffer != NULL )
					{
						vReleaseNetworkBufferAndDescriptor( pxNewBuffer );
					}
				}
			}
		}
#endif /* ipconfigUSE_LLMNR == 1 */
	}

	return ulIPAddress;
}
/*-----------------------------------------------------------*/

#if( ipconfigUSE_NBNS == 1 )

	static void prvTreatNBNS( unsigned char *pucUDPPayloadBuffer, unsigned int ulIPAddress )
	{
		unsigned short usFlags, usType, usClass;
		unsigned char *pucSource, *pucTarget;
		unsigned char ucByte;
		unsigned char ucNBNSName[ 17 ];

		usFlags = usChar2u16( pucUDPPayloadBuffer + offsetof( NBNSRequest_t, usFlags ) );

		if( ( usFlags & ipNBNS_FLAGS_OPCODE_MASK ) == ipNBNS_FLAGS_OPCODE_QUERY )
		{
			usType  = usChar2u16( pucUDPPayloadBuffer + offsetof( NBNSRequest_t, usType ) );
			usClass = usChar2u16( pucUDPPayloadBuffer + offsetof( NBNSRequest_t, usClass ) );

			/* Not used for now */
			( void )usClass;
			/* For NBNS a name is 16 bytes long, written with capitals only.
			Make sure that the copy is terminated with a zero. */
			pucTarget = ucNBNSName + sizeof(ucNBNSName ) - 2;
			pucTarget[ 1 ] = '\0';

			/* Start with decoding the last 2 bytes. */
			pucSource = pucUDPPayloadBuffer + ( offsetof( NBNSRequest_t, ucName ) + ( NBNS_ENCODED_NAME_LENGTH - 2 ) );

			for( ;; )
			{
				ucByte = ( unsigned char ) ( ( ( pucSource[ 0 ] - 0x41 ) << 4 ) | ( pucSource[ 1 ] - 0x41 ) );

				/* Make sure there are no trailing spaces in the name. */
				if( ( ucByte == ' ' ) && ( pucTarget[ 1 ] == '\0' ) )
				{
					ucByte = '\0';
				}

				*pucTarget = ucByte;

				if( pucTarget == ucNBNSName )
				{
					break;
				}

				pucTarget -= 1;
				pucSource -= 2;
			}

			#if( ipconfigUSE_DNS_CACHE == 1 )
			{
				if( ( usFlags & ipNBNS_FLAGS_RESPONSE ) != 0 )
				{
					/* If this is a response from another device,
					add the name to the DNS cache */
					prvProcessDNSCache( ( char * ) ucNBNSName, &ulIPAddress, pdFALSE );
				}
			}
			#else
			{
				/* Avoid compiler warnings. */
				( void ) ulIPAddress;
			}
			#endif /* ipconfigUSE_DNS_CACHE */

			if( ( ( usFlags & ipNBNS_FLAGS_RESPONSE ) == 0 ) &&
				( usType == NBNS_TYPE_NET_BIOS ) &&
				( xApplicationDNSQueryHook( ( const char * ) ucNBNSName ) != pdFALSE ) )
			{
			unsigned short usLength;
			DNSMessage_t *pxMessage;
			NBNSAnswer_t *pxAnswer;

				/* Someone is looking for a device with ucNBNSName,
				prepare a positive reply. */
				NetworkBufferDescriptor_t *pxNetworkBuffer = pxUDPPayloadBuffer_to_NetworkBuffer( pucUDPPayloadBuffer );

				if( ( xBufferAllocFixedSize == pdFALSE ) && ( pxNetworkBuffer != NULL ) )
				{
				NetworkBufferDescriptor_t *pxNewBuffer;
				portBASE_TYPE xDataLength = pxNetworkBuffer->xDataLength + sizeof( UDPHeader_t ) +

					sizeof( EthernetHeader_t ) + sizeof( IPHeader_t );

					/* The field xDataLength was set to the length of the UDP payload.
					The answer (reply) will be longer than the request, so the packet
					must be duplicated into a bigger buffer */
					pxNetworkBuffer->xDataLength = xDataLength;
					pxNewBuffer = pxDuplicateNetworkBufferWithDescriptor( pxNetworkBuffer, xDataLength + 16 );
					if( pxNewBuffer != NULL )
					{
						pucUDPPayloadBuffer = pxNewBuffer->pucEthernetBuffer + sizeof( UDPPacket_t );
						pxNetworkBuffer = pxNewBuffer;
					}
					else
					{
						/* Just prevent that a reply will be sent */
						pxNetworkBuffer = NULL;
					}
				}

				/* Should not occur: pucUDPPayloadBuffer is part of a xNetworkBufferDescriptor */
				if( pxNetworkBuffer != NULL )
				{
					pxMessage = (DNSMessage_t *)pucUDPPayloadBuffer;

					/* As the fields in the structures are not word-aligned, we have to
					copy the values byte-by-byte using macro's vSetField16() and vSetField32() */
					vSetField16( pxMessage, DNSMessage_t, usFlags, NBNS_QUERY_RESPONSE_FLAGS ); /* 0x8500 */
					vSetField16( pxMessage, DNSMessage_t, usQuestions, 0 );
					vSetField16( pxMessage, DNSMessage_t, usAnswers, 1 );
					vSetField16( pxMessage, DNSMessage_t, usAuthorityRRs, 0 );
					vSetField16( pxMessage, DNSMessage_t, usAdditionalRRs, 0 );

					pxAnswer = (NBNSAnswer_t *)( pucUDPPayloadBuffer + offsetof( NBNSRequest_t, usType ) );

					vSetField16( pxAnswer, NBNSAnswer_t, usType, usType );	/* Type */
					vSetField16( pxAnswer, NBNSAnswer_t, usClass, NBNS_CLASS_IN );	/* Class */
					vSetField32( pxAnswer, NBNSAnswer_t, ulTTL, NBNS_TTL_VALUE );
					vSetField16( pxAnswer, NBNSAnswer_t, usDataLength, 6 ); /* 6 bytes including the length field */
					vSetField16( pxAnswer, NBNSAnswer_t, usNbFlags, NBNS_NAME_FLAGS );
					vSetField32( pxAnswer, NBNSAnswer_t, ulIPAddress, FreeRTOS_ntohl( *ipLOCAL_IP_ADDRESS_POINTER ) );

					usLength = ( unsigned short ) ( offsetof( NBNSRequest_t, usType ) + sizeof( NBNSAnswer_t ) );

					prvReplyDNSMessage( pxNetworkBuffer, usLength );
				}
			}
		}
	}

#endif	/* ipconfigUSE_NBNS */
/*-----------------------------------------------------------*/

static Socket_t prvCreateDNSSocket( void )
{
static Socket_t xSocket = NULL;
struct freertos_sockaddr xAddress;
portBASE_TYPE xReturn;
portTickType xTimeoutTime = pdMS_TO_TICKS( 200 );

	/* This must be the first time this function has been called.  Create
	the socket. */
	xSocket = FreeRTOS_socket( FREERTOS_AF_INET, FREERTOS_SOCK_DGRAM, FREERTOS_IPPROTO_UDP );

	/* Auto bind the port. */
	xAddress.sin_port = 0;
	xReturn = FreeRTOS_bind( xSocket, &xAddress, sizeof( xAddress ) );

	/* Check the bind was successful, and clean up if not. */
	if( xReturn != 0 )
	{
		FreeRTOS_closesocket( xSocket );
		xSocket = NULL;
	}
	else
	{
		/* Set the send and receive timeouts. */
		FreeRTOS_setsockopt( xSocket, 0, FREERTOS_SO_RCVTIMEO, ( void * ) &xTimeoutTime, sizeof( portTickType ) );
		FreeRTOS_setsockopt( xSocket, 0, FREERTOS_SO_SNDTIMEO, ( void * ) &xTimeoutTime, sizeof( portTickType ) );
	}

	return xSocket;
}
/*-----------------------------------------------------------*/

#if( ipconfigUSE_NBNS == 1 ) || ( ipconfigUSE_LLMNR == 1 )

static void prvReplyDNSMessage( NetworkBufferDescriptor_t *pxNetworkBuffer, portBASE_TYPE lNetLength )
{
UDPPacket_t *pxUDPPacket;
IPHeader_t *pxIPHeader;
UDPHeader_t *pxUDPHeader;

	pxUDPPacket = (UDPPacket_t *) pxNetworkBuffer->pucEthernetBuffer;
	pxIPHeader = &pxUDPPacket->xIPHeader;
	pxUDPHeader = &pxUDPPacket->xUDPHeader;
	/* HT: started using defines like 'ipSIZE_OF_xxx' */
	pxIPHeader->usLength               = FreeRTOS_htons( lNetLength + ipSIZE_OF_IP_HEADER + ipSIZE_OF_UDP_HEADER ); /* for IPv6:  minus ipSIZE_OF_IP_HEADER; */
	/* HT:endian: should not be translated, copying from packet to packet */
	pxIPHeader->ulDestinationIPAddress = pxIPHeader->ulSourceIPAddress;
	pxIPHeader->ulSourceIPAddress      = *ipLOCAL_IP_ADDRESS_POINTER;
	pxIPHeader->ucTimeToLive           = ipconfigUDP_TIME_TO_LIVE;
	pxIPHeader->usIdentification       = FreeRTOS_htons( usPacketIdentifier );
	usPacketIdentifier++;
	pxUDPHeader->usLength              = FreeRTOS_htons( lNetLength + ipSIZE_OF_UDP_HEADER );
	vFlip_16( pxUDPPacket->xUDPHeader.usSourcePort, pxUDPPacket->xUDPHeader.usDestinationPort );

	#if( ipconfigDRIVER_INCLUDED_TX_IP_CHECKSUM == 0 )
	{
		/* calculate the IP header checksum */
		pxIPHeader->usHeaderChecksum       = 0x00;
		pxIPHeader->usHeaderChecksum       = usGenerateChecksum( 0UL, ( unsigned char * ) &( pxIPHeader->ucVersionHeaderLength ), ipSIZE_OF_IP_HEADER );
		pxIPHeader->usHeaderChecksum       = ~FreeRTOS_htons( pxIPHeader->usHeaderChecksum );

		/* calculate the UDP checksum for outgoing package */
		usGenerateProtocolChecksum( ( unsigned char* ) pxUDPPacket, pdTRUE );
	}
	#endif

	/* Important: tell NIC driver how many bytes must be sent */
	pxNetworkBuffer->xDataLength = ( size_t ) ( lNetLength + ipSIZE_OF_IP_HEADER + ipSIZE_OF_UDP_HEADER + ipSIZE_OF_ETH_HEADER );

	/* This function will fill in the eth addresses and send the packet */
	vReturnEthernetFrame( pxNetworkBuffer, pdFALSE );
}

#endif /* ipconfigUSE_NBNS == 1 || ipconfigUSE_LLMNR == 1 */
/*-----------------------------------------------------------*/

#if( ipconfigUSE_DNS_CACHE == 1 )

static void prvProcessDNSCache( const char *pcName, unsigned int *pulIP, portBASE_TYPE xLookUp )
{
portBASE_TYPE x;
portBASE_TYPE xFound = pdFALSE;
static portBASE_TYPE xFreeEntry = 0;

	/* For each entry in the DNS cache table. */
	for( x = 0; x < ipconfigDNS_CACHE_ENTRIES; x++ )
	{
		if( xDNSCache[ x ].pcName[ 0 ] == 0 )
		{
			break;
		}

		if( strncmp( xDNSCache[ x ].pcName, pcName, sizeof( xDNSCache[ x ].pcName ) ) == 0 )
		{
			/* Is this function called for a lookup or to add/update an IP address? */
			if( xLookUp != pdFALSE )
			{
				*pulIP = xDNSCache[ x ].ulIPAddress;
			}
			else
			{
				xDNSCache[ x ].ulIPAddress = *pulIP;
			}

			xFound = pdTRUE;
			break;
		}
	}

	if( xFound == pdFALSE )
	{
		if( xLookUp != pdFALSE )
		{
			*pulIP = 0;
		}
		else
		{
			/* Called to add or update an item */
			strncpy( xDNSCache[ xFreeEntry ].pcName, pcName, sizeof( xDNSCache[ xFreeEntry ].pcName ) );
			xDNSCache[ xFreeEntry ].ulIPAddress = *pulIP;

			xFreeEntry++;
			if( xFreeEntry == ipconfigDNS_CACHE_ENTRIES )
			{
				xFreeEntry = 0;
			}
		}
	}

	if( ( xLookUp == 0 ) || ( *pulIP != 0 ) )
	{
		FreeRTOS_debug_printf( ( "prvProcessDNSCache: %s: '%s' @ %lxip\n", xLookUp ? "look-up" : "add", pcName, FreeRTOS_ntohl( *pulIP ) ) );
	}
}
#endif /* ipconfigUSE_DNS_CACHE */

#endif /* ipconfigUSE_DNS != 0 */


