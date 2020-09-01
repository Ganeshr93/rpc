/* INTEL CONFIDENTIAL
 * Copyright 2011 - 2019 Intel Corporation.
 * This software and the related documents are Intel copyrighted materials, and your use of them is governed by the express license under which they were provided to you ("License"). Unless the License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or transmit this software or the related documents without Intel's prior written permission.
 * This software and the related documents are provided as is, with no express or implied warranties, other than those that are expressly stated in the License.
 */

#ifndef ___ILibAsyncSocket___
#define ___ILibAsyncSocket___

/*! \file ILibAsyncSocket.h 
\brief MicroStack APIs for TCP Client Functionality
*/

/*! \defgroup ILibAsyncSocket ILibAsyncSocket Module
\{
*/

#if defined(WIN32) 
#include <STDDEF.H>
#elif defined(_POSIX)
#include <malloc.h>
#endif

//#include "ssl.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! \def MEMORYCHUNKSIZE
\brief Incrementally grow the buffer by this amount of bytes
*/
#define MEMORYCHUNKSIZE 4096

#define ILibTransports_AsyncSocket 0x40

typedef enum ILibAsyncSocket_SendStatus
{
	ILibAsyncSocket_ALL_DATA_SENT = 1, /*!< All of the data has already been sent */
	ILibAsyncSocket_NOT_ALL_DATA_SENT_YET = 0, /*!< Not all of the data could be sent, but is queued to be sent as soon as possible */
	ILibAsyncSocket_SEND_ON_CLOSED_SOCKET_ERROR	= -4 /*!< A send operation was attmepted on a closed socket */
}ILibAsyncSocket_SendStatus;

/*! \enum ILibAsyncSocket_MemoryOwnership
\brief Enumeration values for Memory Ownership of variables
*/
typedef enum ILibAsyncSocket_MemoryOwnership
{
	ILibAsyncSocket_MemoryOwnership_CHAIN = 0, /*!< The Microstack will own this memory, and free it when it is done with it */
	ILibAsyncSocket_MemoryOwnership_STATIC = 1, /*!< This memory is static, so the Microstack will not free it, and assume it will not go away, so it won't copy it either */
	ILibAsyncSocket_MemoryOwnership_USER = 2 /*!< The Microstack doesn't own this memory, so if necessary the memory will be copied */
}ILibAsyncSocket_MemoryOwnership;

/*! \typedef ILibAsyncSocket_SocketModule
\brief The handle for an ILibAsyncSocket module
*/
typedef void* ILibAsyncSocket_SocketModule;
/*! \typedef ILibAsyncSocket_OnInterrupt
\brief Handler for when a session was interrupted by a call to ILibStopChain
\param socketModule The \a ILibAsyncSocket_SocketModule that was interrupted
\param user The user object that was associated with this connection
*/

//typedef void(*ILibAsyncSocket_OnReplaceSocket)(ILibAsyncSocket_SocketModule socketModule, void *user);
typedef void(*ILibAsyncSocket_OnBufferSizeExceeded)(ILibAsyncSocket_SocketModule socketModule, void *user);

typedef void(*ILibAsyncSocket_OnInterrupt)(ILibAsyncSocket_SocketModule socketModule, void *user);
/*! \typedef ILibAsyncSocket_OnData
\brief Handler for when data is received
\par
<B>Note on memory handling:</B>
When you process the received buffer, you must advance \a p_beginPointer the number of bytes that you 
have processed. If \a p_beginPointer does not equal \a endPointer when this method completes,
the system will continue to reclaim any memory that has already been processed, and call this method again
until no more memory has been processed. If no memory has been processed, and more data has been received
on the network, the buffer will be automatically grown (according to a specific alogrythm), to accomodate any new data.
\param socketModule The \a ILibAsyncSocket_SocketModule that received data
\param buffer The data that was received
\param[in,out] p_beginPointer The start index of the data that was received
\param endPointer The end index of the data that was received
\param[in,out] OnInterrupt Set this pointer to receive notification if this session is interrupted
\param[in,out] user Set a custom user object
\param[out] PAUSE Flag to indicate if the system should continue reading data off the network
*/
typedef void(*ILibAsyncSocket_OnData)(ILibAsyncSocket_SocketModule socketModule, char* buffer, int *p_beginPointer, int endPointer,ILibAsyncSocket_OnInterrupt* OnInterrupt, void **user, int *PAUSE);
/*! \typedef ILibAsyncSocket_OnConnect
\brief Handler for when a connection is made
\param socketModule The \a ILibAsyncSocket_SocketModule that was connected
\param user The user object that was associated with this object
*/
typedef void(*ILibAsyncSocket_OnConnect)(ILibAsyncSocket_SocketModule socketModule, int Connected, void *user);
/*! \typedef ILibAsyncSocket_OnDisconnect
\brief Handler for when a connection is terminated normally
\param socketModule The \a ILibAsyncSocket_SocketModule that was disconnected
\param user User object that was associated with this connection
*/
typedef void(*ILibAsyncSocket_OnDisconnect)(ILibAsyncSocket_SocketModule socketModule, void *user);
/*! \typedef ILibAsyncSocket_OnSendOK
\brief Handler for when pending send operations have completed
\par
This handler will only be called if a call to \a ILibAsyncSocket_Send returned a value greater
than 0, which indicates that not all of the data could be sent.
\param socketModule The \a ILibAsyncSocket_SocketModule whos pending sends have completed
\param user User object that was associated with this connection
*/
typedef void(*ILibAsyncSocket_OnSendOK)(ILibAsyncSocket_SocketModule socketModule, void *user);
/*! \typedef ILibAsyncSocket_OnBufferReAllocated
\brief Handler for when the internal data buffer has been resized.
\par
<B>Note:</B> This is only useful if you are storing pointer values into the buffer supplied in \a ILibAsyncSocket_OnData. 
\param AsyncSocketToken The \a ILibAsyncSocket_SocketModule whos buffer was resized
\param user The user object that was associated with this connection
\param newOffset The new offset differential. Simply add this value to your existing pointers, to obtain the correct pointer into the resized buffer.
*/
typedef void(*ILibAsyncSocket_OnBufferReAllocated)(ILibAsyncSocket_SocketModule AsyncSocketToken, void *user, ptrdiff_t newOffset);

/*! \defgroup TLSGroup TLS Related Methods
* @{
*/
/*! @} */

extern const int ILibMemory_ASYNCSOCKET_CONTAINERSIZE;

void ILibAsyncSocket_SetReAllocateNotificationCallback(ILibAsyncSocket_SocketModule AsyncSocketToken, ILibAsyncSocket_OnBufferReAllocated Callback);
void *ILibAsyncSocket_GetUser(ILibAsyncSocket_SocketModule socketModule);
void ILibAsyncSocket_SetUser(ILibAsyncSocket_SocketModule socketModule, void* user);
void *ILibAsyncSocket_GetUser2(ILibAsyncSocket_SocketModule socketModule);
void ILibAsyncSocket_SetUser2(ILibAsyncSocket_SocketModule socketModule, void* user);
int ILibAsyncSocket_GetUser3(ILibAsyncSocket_SocketModule socketModule);
void ILibAsyncSocket_SetUser3(ILibAsyncSocket_SocketModule socketModule, int user);
void ILibAsyncSocket_UpdateOnData(ILibAsyncSocket_SocketModule module, ILibAsyncSocket_OnData OnData);

#define ILibCreateAsyncSocketModule(Chain, initialBufferSize, OnData, OnConnect, OnDisconnect, OnSendOK) ILibCreateAsyncSocketModuleWithMemory(Chain, initialBufferSize, OnData, OnConnect, OnDisconnect, OnSendOK, 0)
ILibAsyncSocket_SocketModule ILibCreateAsyncSocketModuleWithMemory(void *Chain, int initialBufferSize, ILibAsyncSocket_OnData OnData, ILibAsyncSocket_OnConnect OnConnect, ILibAsyncSocket_OnDisconnect OnDisconnect, ILibAsyncSocket_OnSendOK OnSendOK, int UserMappedMemorySize);

void *ILibAsyncSocket_GetSocket(ILibAsyncSocket_SocketModule module);
void ILibAsyncSocket_Shutdown(ILibAsyncSocket_SocketModule module);

unsigned int ILibAsyncSocket_GetPendingBytesToSend(ILibAsyncSocket_SocketModule socketModule);
unsigned int ILibAsyncSocket_GetTotalBytesSent(ILibAsyncSocket_SocketModule socketModule);
void ILibAsyncSocket_ResetTotalBytesSent(ILibAsyncSocket_SocketModule socketModule);

void ILibAsyncSocket_ConnectTo(void* socketModule, struct sockaddr *localInterface, struct sockaddr *remoteAddress, ILibAsyncSocket_OnInterrupt InterruptPtr, void *user);

#ifdef MICROSTACK_PROXY
void ILibAsyncSocket_ConnectToProxy(void* socketModule, struct sockaddr *localInterface, struct sockaddr *remoteAddress, struct sockaddr *proxyAddress, char* proxyUser, char* proxyPass, ILibAsyncSocket_OnInterrupt InterruptPtr, void *user);
#endif

enum ILibAsyncSocket_SendStatus ILibAsyncSocket_SendTo(ILibAsyncSocket_SocketModule socketModule, char* buffer, int length, struct sockaddr *remoteAddress, enum ILibAsyncSocket_MemoryOwnership UserFree);

/*! \def ILibAsyncSocket_Send
\brief Sends data onto the TCP stream
\param socketModule The \a ILibAsyncSocket_SocketModule to send data on
\param buffer The data to be sent
\param length The length of \a buffer
\param UserFree The \a ILibAsyncSocket_MemoryOwnership enumeration, that identifies how the memory pointer to by \a buffer is to be handled
\returns \a ILibAsyncSocket_SendStatus indicating the send status
*/
#define ILibAsyncSocket_Send(socketModule, buffer, length, UserFree) ILibAsyncSocket_SendTo(socketModule, buffer, length, NULL, UserFree)
void ILibAsyncSocket_Disconnect(ILibAsyncSocket_SocketModule socketModule);
void ILibAsyncSocket_GetBuffer(ILibAsyncSocket_SocketModule socketModule, char **buffer, int *BeginPointer, int *EndPointer);

#if  defined(WIN32)
void ILibAsyncSocket_UseThisSocket(ILibAsyncSocket_SocketModule socketModule, SOCKET UseThisSocket, ILibAsyncSocket_OnInterrupt InterruptPtr, void *user);
#elif defined(_POSIX)
void ILibAsyncSocket_UseThisSocket(ILibAsyncSocket_SocketModule socketModule, int UseThisSocket, ILibAsyncSocket_OnInterrupt InterruptPtr, void *user);
#endif


void ILibAsyncSocket_SetRemoteAddress(ILibAsyncSocket_SocketModule socketModule, struct sockaddr *remoteAddress);
void ILibAsyncSocket_SetLocalInterface(ILibAsyncSocket_SocketModule module, struct sockaddr *localAddress);

int ILibAsyncSocket_IsFree(ILibAsyncSocket_SocketModule socketModule);
int ILibAsyncSocket_IsConnected(ILibAsyncSocket_SocketModule socketModule);
int ILibAsyncSocket_GetLocalInterface(ILibAsyncSocket_SocketModule socketModule, struct sockaddr *localAddress);
int ILibAsyncSocket_GetRemoteInterface(ILibAsyncSocket_SocketModule socketModule, struct sockaddr *remoteAddress);
unsigned short ILibAsyncSocket_GetLocalPort(ILibAsyncSocket_SocketModule socketModule);

void ILibAsyncSocket_Resume(ILibAsyncSocket_SocketModule socketModule);
void ILibAsyncSocket_Pause(ILibAsyncSocket_SocketModule socketModule);
int ILibAsyncSocket_WasClosedBecauseBufferSizeExceeded(ILibAsyncSocket_SocketModule socketModule);
void ILibAsyncSocket_SetMaximumBufferSize(ILibAsyncSocket_SocketModule module, int maxSize, ILibAsyncSocket_OnBufferSizeExceeded OnBufferSizeExceededCallback, void *user);
void ILibAsyncSocket_SetSendOK(ILibAsyncSocket_SocketModule module, ILibAsyncSocket_OnSendOK OnSendOK);
int ILibAsyncSocket_IsIPv6LinkLocal(struct sockaddr *LocalAddress);
int ILibAsyncSocket_IsModuleIPv6LinkLocal(ILibAsyncSocket_SocketModule module);

typedef void(*ILibAsyncSocket_TimeoutHandler)(ILibAsyncSocket_SocketModule module, void *user);
void ILibAsyncSocket_SetTimeout(ILibAsyncSocket_SocketModule module, int timeoutSeconds, ILibAsyncSocket_TimeoutHandler timeoutHandler);

#ifdef __cplusplus
}
#endif

#endif

