/* INTEL CONFIDENTIAL
 * Copyright 2011 - 2019 Intel Corporation.
 * This software and the related documents are Intel copyrighted materials, and your use of them is governed by the express license under which they were provided to you ("License"). Unless the License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or transmit this software or the related documents without Intel's prior written permission.
 * This software and the related documents are provided as is, with no express or implied warranties, other than those that are expressly stated in the License.
 */

#ifdef MEMORY_CHECK
#include <assert.h>
#define MEMCHECK(x) x
#else
#define MEMCHECK(x)
#endif

#if defined(WIN32)  && !defined(_MINCORE)
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#else
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#define SOCKET_ERROR            (-1)
#define UNREFERENCED_PARAMETER(P) (P)
#endif

#if defined(WINSOCK2)
#include <winsock2.h>
#include <ws2tcpip.h>
#define MSG_NOSIGNAL 0
#elif defined(WINSOCK1)
#include <winsock.h>
#include <wininet.h>
#endif

#include "ILibParsers.h"
#include "ILibAsyncSocket.h"
#include "ILibRemoteLogging.h"

#include <assert.h>

//#ifndef WINSOCK2
//#define SOCKET unsigned int
//#endif

#define INET_SOCKADDR_LENGTH(x) ((x==AF_INET6?sizeof(struct sockaddr_in6):sizeof(struct sockaddr_in)))

#ifdef SEMAPHORE_TRACKING
#define SEM_TRACK(x) x
void AsyncSocket_TrackLock(const char* MethodName, int Occurance, void *data)
{
	char v[100];
	wchar_t wv[100];
	size_t l;

	sprintf_s(v, 100, "  LOCK[%s, %d] (%x)\r\n",MethodName,Occurance,data);
#ifdef WIN32
	mbstowcs_s(&l, wv, 100, v, 100);
	OutputDebugString(wv);
#else
	printf(v);
#endif
}
void AsyncSocket_TrackUnLock(const char* MethodName, int Occurance, void *data)
{
	char v[100];
	wchar_t wv[100];
	size_t l;

	sprintf_s(v, 100, "UNLOCK[%s, %d] (%x)\r\n",MethodName,Occurance,data);
#ifdef WIN32
	mbstowcs_s(&l, wv, 100, v, 100);
	OutputDebugString(wv);
#else
	printf(v);
#endif
}
#else
#define SEM_TRACK(x)
#endif

struct ILibAsyncSocket_SendData
{
	char* buffer;
	int bufferSize;
	int bytesSent;

	struct sockaddr_in6 remoteAddress;

	int UserFree;
	struct ILibAsyncSocket_SendData *Next;
};

typedef struct ILibAsyncSocketModule
{
	ILibTransport Transport;
	// DO NOT MODIFY THE ABOVE FIELDS (ILibTransport)

	unsigned int PendingBytesToSend;
	unsigned int TotalBytesSent;

#if defined(WIN32)
	SOCKET internalSocket;
#elif defined(_POSIX)
	int internalSocket;
#endif

	// The IPv4/IPv6 compliant address of the remote endpoint. We are not going to be using IPv6 all the time,
	// but we use the IPv6 structure to allocate the meximum space we need.
	struct sockaddr_in6 RemoteAddress;

	// Local interface of a given socket. This module will bind to any interface, but the actual interface used
	// is stored here.
	struct sockaddr_in6 LocalAddress;

	// Source address. Here is stored the actual source of a packet, usualy used with UDP where the source
	// of the traffic changes.
	struct sockaddr_in6 SourceAddress;

#ifdef MICROSTACK_PROXY
	// The address and port of a HTTPS proxy
	struct sockaddr_in6 ProxyAddress;
	char ProxiedHost[255];
	int ProxyState;
	char* ProxyUser;
	char* ProxyPass;
#endif

	ILibAsyncSocket_OnData OnData;
	ILibAsyncSocket_OnConnect OnConnect;
	ILibAsyncSocket_OnDisconnect OnDisconnect;
	ILibAsyncSocket_OnSendOK OnSendOK;
	ILibAsyncSocket_OnInterrupt OnInterrupt;

	ILibAsyncSocket_OnBufferSizeExceeded OnBufferSizeExceeded;
	ILibAsyncSocket_OnBufferReAllocated OnBufferReAllocated;

	void *LifeTime;
	void *user;
	void *user2;
	int user3;
	int PAUSE;
	int FinConnect;
	int BeginPointer;
	int EndPointer;
	char* buffer;
	int MallocSize;
	int InitialSize;

	struct ILibAsyncSocket_SendData *PendingSend_Head;
	struct ILibAsyncSocket_SendData *PendingSend_Tail;
	sem_t SendLock;

	int MaxBufferSize;
	int MaxBufferSizeExceeded;
	void *MaxBufferSizeUserObject;

	// Added for TLS support
	long long timeout_lastActivity;
	int timeout_milliSeconds;
	ILibAsyncSocket_TimeoutHandler timeout_handler;
}ILibAsyncSocketModule;

void ILibAsyncSocket_PostSelect(void* object,int slct, fd_set *readset, fd_set *writeset, fd_set *errorset);
void ILibAsyncSocket_PreSelect(void* object,fd_set *readset, fd_set *writeset, fd_set *errorset, int* blocktime);
const int ILibMemory_ASYNCSOCKET_CONTAINERSIZE = (const int)sizeof(ILibAsyncSocketModule);

typedef enum ILibAsyncSocket_TLSPlainText_ContentType
{
	ILibAsyncSocket_TLSPlainText_ContentType_ChangeCipherSpec = 20,
	ILibAsyncSocket_TLSPlainText_ContentType_Alert = 21,
	ILibAsyncSocket_TLSPlainText_ContentType_Handshake = 22,
	ILibAsyncSocket_TLSPlainText_ContentType_ApplicationData = 23
}ILibAsyncSocket_TLSPlainText_ContentType;
typedef enum ILibAsyncSocket_TLSHandshakeType
{
	ILibAsyncSocket_TLSHandshakeType_hello = 0,
	ILibAsyncSocket_TLSHandshakeType_clienthello = 1,
	ILibAsyncSocket_TLSHandshakeType_serverhello = 2,
	ILibAsyncSocket_TLSHandshakeType_certificate = 11,
	ILibAsyncSocket_TLSHandshakeType_serverkeyexchange = 12,
	ILibAsyncSocket_TLSHandshakeType_certificaterequest = 13,
	ILibAsyncSocket_TLSHandshakeType_serverhellodone = 14,
	ILibAsyncSocket_TLSHandshakeType_certificateverify = 15,
	ILibAsyncSocket_TLSHandshakeType_clientkeyexchange = 16,
	ILibAsyncSocket_TLSHandshakeType_finished = 20
}ILibAsyncSocket_TLSHandshakeType;

//
// An internal method called by Chain as Destroy, to cleanup AsyncSocket
//
// <param name="socketModule">The AsyncSocketModule</param>
void ILibAsyncSocket_Destroy(void *socketModule)
{
	struct ILibAsyncSocketModule* module = (struct ILibAsyncSocketModule*)socketModule;
	struct ILibAsyncSocket_SendData *temp, *current;

	// Call the interrupt event if necessary
	if (!ILibAsyncSocket_IsFree(module))
	{
		if (module->OnInterrupt != NULL) module->OnInterrupt(module, module->user);
	}

	// Close socket if necessary
	if (module->internalSocket != ~0)
	{
#if defined(WIN32)
#if defined(WINSOCK2)
		shutdown(module->internalSocket, SD_BOTH);
#endif
		closesocket(module->internalSocket);
#elif defined(_POSIX)
		shutdown(module->internalSocket, SHUT_RDWR);
		close(module->internalSocket);
#endif
		module->internalSocket = (SOCKET)~0;
	}

	// Free the buffer if necessary
	if (module->buffer != NULL)
	{
		if (module->buffer != ILibScratchPad2) free(module->buffer);
		module->buffer = NULL;
		module->MallocSize = 0;
	}

	// Clear all the data that is pending to be sent
	temp = current = module->PendingSend_Head;
	while (current != NULL)
	{
		temp = current->Next;
		if (current->UserFree == 0) free(current->buffer);
		free(current);
		current = temp;
	}

	module->FinConnect = 0;
	module->user = NULL;
	sem_destroy(&(module->SendLock));
}
/*! \fn ILibAsyncSocket_SetReAllocateNotificationCallback(ILibAsyncSocket_SocketModule AsyncSocketToken, ILibAsyncSocket_OnBufferReAllocated Callback)
\brief Set the callback handler for when the internal data buffer has been resized
\param AsyncSocketToken The specific connection to set the callback with
\param Callback The callback handler to set
*/
void ILibAsyncSocket_SetReAllocateNotificationCallback(ILibAsyncSocket_SocketModule AsyncSocketToken, ILibAsyncSocket_OnBufferReAllocated Callback)
{
	if (AsyncSocketToken != NULL) { ((struct ILibAsyncSocketModule*)AsyncSocketToken)->OnBufferReAllocated = Callback; }
}

ILibTransport_DoneState ILibAsyncSocket_TransportSend(void *transport, char* buffer, int bufferLength, ILibTransport_MemoryOwnership ownership, ILibTransport_DoneState done)
{
	UNREFERENCED_PARAMETER(done);
	return((ILibTransport_DoneState)ILibAsyncSocket_Send(transport, buffer, bufferLength, (enum ILibAsyncSocket_MemoryOwnership)ownership));
}


/*! \fn ILibCreateAsyncSocketModule(void *Chain, int initialBufferSize, ILibAsyncSocket_OnData OnData, ILibAsyncSocket_OnConnect OnConnect, ILibAsyncSocket_OnDisconnect OnDisconnect,ILibAsyncSocket_OnSendOK OnSendOK)
\brief Creates a new AsyncSocketModule
\param Chain The chain to add this module to. (Chain must <B>not</B> be running)
\param initialBufferSize The initial size of the receive buffer
\param OnData Function Pointer that triggers when Data is received
\param OnConnect Function Pointer that triggers upon successfull connection establishment
\param OnDisconnect Function Pointer that triggers upon disconnect
\param OnSendOK Function Pointer that triggers when pending sends are complete
\param AutoFreeMemorySize Amount of memory to create along side the object, that will be freed when the parent object is freed
\returns An ILibAsyncSocket token
*/
ILibAsyncSocket_SocketModule ILibCreateAsyncSocketModuleWithMemory(void *Chain, int initialBufferSize, ILibAsyncSocket_OnData OnData, ILibAsyncSocket_OnConnect OnConnect, ILibAsyncSocket_OnDisconnect OnDisconnect, ILibAsyncSocket_OnSendOK OnSendOK, int UserMappedMemorySize)
{
	struct ILibAsyncSocketModule *RetVal = (struct ILibAsyncSocketModule*)ILibChain_Link_Allocate(sizeof(struct ILibAsyncSocketModule), UserMappedMemorySize);

	RetVal->Transport.IdentifierFlags = ILibTransports_AsyncSocket;
	RetVal->Transport.SendPtr = &ILibAsyncSocket_TransportSend;
	RetVal->Transport.ClosePtr = &ILibAsyncSocket_Disconnect;
	RetVal->Transport.PendingBytesPtr = &ILibAsyncSocket_GetPendingBytesToSend;
	RetVal->Transport.ChainLink.ParentChain = Chain;

	if (initialBufferSize != 0)
	{
		// Use a new buffer
		if ((RetVal->buffer = (char*)malloc(initialBufferSize)) == NULL) ILIBCRITICALEXIT(254);
	}
	else
	{
		// Use a static buffer, often used for UDP.
		initialBufferSize = sizeof(ILibScratchPad2);
		RetVal->buffer = ILibScratchPad2;
	}
	RetVal->Transport.ChainLink.PreSelectHandler = &ILibAsyncSocket_PreSelect;
	RetVal->Transport.ChainLink.PostSelectHandler = &ILibAsyncSocket_PostSelect;
	RetVal->Transport.ChainLink.DestroyHandler = &ILibAsyncSocket_Destroy;
	RetVal->internalSocket = (SOCKET)~0;
	RetVal->OnData = OnData;
	RetVal->OnConnect = OnConnect;
	RetVal->OnDisconnect = OnDisconnect;
	RetVal->OnSendOK = OnSendOK;
	RetVal->InitialSize = initialBufferSize;
	RetVal->MallocSize = initialBufferSize;
	RetVal->LifeTime = ILibGetBaseTimer(Chain); //ILibCreateLifeTime(Chain);

	sem_init(&(RetVal->SendLock), 0, 1);

	ILibAddToChain(Chain, RetVal);

	return((void*)RetVal);
}

/*! \fn ILibAsyncSocket_ClearPendingSend(ILibAsyncSocket_SocketModule socketModule)
\brief Clears all the pending data to be sent for an AsyncSocket
\param socketModule The ILibAsyncSocket to clear
*/
void ILibAsyncSocket_ClearPendingSend(ILibAsyncSocket_SocketModule socketModule)
{
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
	struct ILibAsyncSocket_SendData *data, *temp;

	data = module->PendingSend_Head;
	module->PendingSend_Tail = NULL;
	module->PendingSend_Head = NULL;
	module->PendingBytesToSend = 0;
	while (data != NULL)
	{
		temp = data->Next;
		// We only need to free this if we have ownership of this memory
		if (data->UserFree == 0) free(data->buffer);
		free(data);
		data = temp;
	}
}

/*! \fn ILibAsyncSocket_SendTo(ILibAsyncSocket_SocketModule socketModule, char* buffer, int length, int remoteAddress, unsigned short remotePort, enum ILibAsyncSocket_MemoryOwnership UserFree)
\brief Sends data on an AsyncSocket module to a specific destination. (Valid only for <B>UDP</B>)
\param socketModule The ILibAsyncSocket module to send data on
\param buffer The buffer to send
\param length The length of the buffer to send
\param remoteAddress The IPAddress of the destination 
\param remotePort The Port number of the destination
\param UserFree Flag indicating memory ownership. 
\returns \a ILibAsyncSocket_SendStatus indicating the send status
*/
enum ILibAsyncSocket_SendStatus ILibAsyncSocket_SendTo(ILibAsyncSocket_SocketModule socketModule, char* buffer, int length, struct sockaddr *remoteAddress, enum ILibAsyncSocket_MemoryOwnership UserFree)
{
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
	struct ILibAsyncSocket_SendData *data;
	int bytesSent = 0;
	enum ILibAsyncSocket_SendStatus retVal = ILibAsyncSocket_ALL_DATA_SENT;
	
	// If the socket is empty, return now.
	if (socketModule == NULL) return ILibAsyncSocket_SEND_ON_CLOSED_SOCKET_ERROR;

	// Setup a new send data structure
	if ((data = (struct ILibAsyncSocket_SendData*)malloc(sizeof(struct ILibAsyncSocket_SendData))) == NULL) ILIBCRITICALEXIT(254);
	memset(data, 0, sizeof(struct ILibAsyncSocket_SendData));
	data->buffer = buffer;
	data->bufferSize = length;
	data->bytesSent = 0;
	data->UserFree = UserFree;
	data->Next = NULL;

	// Copy the address to the send data structure
	memset(&(data->remoteAddress), 0, sizeof(struct sockaddr_in6));
	if (remoteAddress != NULL) memcpy_s(&(data->remoteAddress), sizeof(struct sockaddr_in6), remoteAddress, INET_SOCKADDR_LENGTH(remoteAddress->sa_family));

	SEM_TRACK(AsyncSocket_TrackLock("ILibAsyncSocket_Send", 1, module);)
	sem_wait(&(module->SendLock));

	if (module->internalSocket == ~0)
	{
#ifdef WIN32
		ILIBMESSAGE2("internalSocket ERROR", WSAGetLastError());			
#endif
		// Too Bad, the socket closed
		if (UserFree == 0) free(buffer);
		free(data);
		SEM_TRACK(AsyncSocket_TrackUnLock("ILibAsyncSocket_Send", 2, module);)
		sem_post(&(module->SendLock));
		return ILibAsyncSocket_SEND_ON_CLOSED_SOCKET_ERROR;
	}

	module->PendingBytesToSend += length;
	if (module->PendingSend_Tail != NULL || module->FinConnect == 0)
	{
		// There are still bytes that are pending to be sent, or pending connection, so we need to queue this up
		if (module->PendingSend_Tail == NULL)
		{
			module->PendingSend_Tail = data;
			module->PendingSend_Head = data;
		}
		else
		{
			module->PendingSend_Tail->Next = data;
			module->PendingSend_Tail = data;
		}
		
		retVal = ILibAsyncSocket_NOT_ALL_DATA_SENT_YET;

		if (UserFree == ILibAsyncSocket_MemoryOwnership_USER)
		{
			// If we don't own this memory, we need to copy the buffer,
			// because the user may free this memory before we have a chance to send it
			if ((data->buffer = (char*)malloc(data->bufferSize)) == NULL) ILIBCRITICALEXIT(254);
			memcpy_s(data->buffer, data->bufferSize, buffer, length);
			MEMCHECK(assert(length <= data->bufferSize);)
			data->UserFree = ILibAsyncSocket_MemoryOwnership_CHAIN;
		}
	}
	else
	{
		// There is no data pending to be sent, so lets go ahead and try to send this data
		module->PendingSend_Tail = data;
		module->PendingSend_Head = data;

		if (remoteAddress == NULL)
		{
			// Set MSG_NOSIGNAL since we don't want to get Broken Pipe signals in Linux, ignored if Windows.
			bytesSent = send(module->internalSocket, module->PendingSend_Head->buffer + module->PendingSend_Head->bytesSent, module->PendingSend_Head->bufferSize-module->PendingSend_Head->bytesSent, MSG_NOSIGNAL);
		}
		else
		{
			bytesSent = sendto(module->internalSocket, module->PendingSend_Head->buffer+module->PendingSend_Head->bytesSent, module->PendingSend_Head->bufferSize-module->PendingSend_Head->bytesSent, MSG_NOSIGNAL, (struct sockaddr*)remoteAddress, INET_SOCKADDR_LENGTH(remoteAddress->sa_family));
		}

		module->timeout_lastActivity = ILibGetUptime();

		if (bytesSent > 0)
		{
			// We were able to send something, so lets increment the counters
			module->PendingSend_Head->bytesSent += bytesSent;
			module->PendingBytesToSend -= bytesSent;
			module->TotalBytesSent += bytesSent;
		}

		if (bytesSent == -1)
		{
			// Send returned an error, so lets figure out what it was, as it could be normal
#if  defined(WIN32)
			bytesSent = WSAGetLastError();
			if (bytesSent != WSAEWOULDBLOCK)
#elif defined(_POSIX)
			if (errno != EWOULDBLOCK)
#endif
			{
				// printf("ILibAsyncSocket_Send ERROR %d\r\n", errno);

#ifdef WIN32
				ILIBMESSAGE2("ILibAsyncSocket_Send ERROR", WSAGetLastError());
#endif // WIN32

				// Most likely the socket closed while we tried to send
				module->PAUSE = 1;
				ILibAsyncSocket_ClearPendingSend(module); // This causes a segfault
				SEM_TRACK(AsyncSocket_TrackUnLock("ILibAsyncSocket_Send", 3, module);)
				sem_post(&(module->SendLock));

				// Ensure Calling On_Disconnect with MicroStackThread
				ILibLifeTime_Add(module->LifeTime, socketModule, 0, &ILibAsyncSocket_Disconnect, NULL);

				return ILibAsyncSocket_SEND_ON_CLOSED_SOCKET_ERROR;
			}
		}
		if (module->PendingSend_Head->bytesSent == module->PendingSend_Head->bufferSize)
		{
			// All of the data has been sent
			if (UserFree == 0) free(module->PendingSend_Head->buffer);
			module->PendingSend_Tail = NULL;
			free(module->PendingSend_Head);
			module->PendingSend_Head = NULL;
		}
		else
		{
			// All of the data wasn't sent, so we need to copy the buffer
			// if we don't own the memory, because the user may free the
			// memory, before we have a chance to complete sending it.
			if (UserFree == ILibAsyncSocket_MemoryOwnership_USER)
			{
				if ((data->buffer = (char*)malloc(data->bufferSize)) == NULL) ILIBCRITICALEXIT(254);
				memcpy_s(data->buffer, data->bufferSize, buffer, length);
				MEMCHECK(assert(length <= data->bufferSize);)
				data->UserFree = ILibAsyncSocket_MemoryOwnership_CHAIN;
			}
			retVal = ILibAsyncSocket_NOT_ALL_DATA_SENT_YET;
			ILibRemoteLogging_printf(ILibChainGetLogger(module->Transport.ChainLink.ParentChain), ILibRemoteLogging_Modules_Microstack_AsyncSocket, ILibRemoteLogging_Flags_VerbosityLevel_3, "AsyncSocket[%p] NOT_ALL_DATA_SENT", (void*)module);
		}

	}
	SEM_TRACK(AsyncSocket_TrackUnLock("ILibAsyncSocket_Send", 4, module);)
	sem_post(&(module->SendLock));
	if (retVal != ILibAsyncSocket_ALL_DATA_SENT) ILibForceUnBlockChain(module->Transport.ChainLink.ParentChain);
	return (retVal);
}

/*! \fn ILibAsyncSocket_Disconnect(ILibAsyncSocket_SocketModule socketModule)
\brief Disconnects an ILibAsyncSocket
\param socketModule The ILibAsyncSocket to disconnect
*/
void ILibAsyncSocket_Disconnect(ILibAsyncSocket_SocketModule socketModule)
{
#if defined(WIN32)
	SOCKET s;
#else
	int s;
#endif

	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;

	ILibRemoteLogging_printf(ILibChainGetLogger(module->Transport.ChainLink.ParentChain), ILibRemoteLogging_Modules_Microstack_AsyncSocket, ILibRemoteLogging_Flags_VerbosityLevel_1, "AsyncSocket[%p] << DISCONNECT", (void*)module);

	SEM_TRACK(AsyncSocket_TrackLock("ILibAsyncSocket_Disconnect", 1, module);)
	sem_wait(&(module->SendLock));
	module->timeout_handler = NULL;
	module->timeout_milliSeconds = 0;


	if (module->internalSocket != ~0)
	{
		// There is an associated socket that is still valid, so we need to close it
		module->PAUSE = 1;
		s = module->internalSocket;
		module->internalSocket = (SOCKET)~0;
		if (s != -1)
		{
#if  defined(WIN32)
#if defined(WINSOCK2)
			shutdown(s, SD_BOTH);
#endif
			closesocket(s);
#elif defined(_POSIX)
			shutdown(s, SHUT_RDWR);
			close(s);
#endif
		}

		// Since the socket is closing, we need to clear the data that is pending to be sent
		ILibAsyncSocket_ClearPendingSend(socketModule);
		SEM_TRACK(AsyncSocket_TrackUnLock("ILibAsyncSocket_Disconnect", 2, module);)
		sem_post(&(module->SendLock));

			// This was a normal socket, fire the event notifying the user. Depending on connection state, we event differently
			if (module->FinConnect <= 0 && module->OnConnect != NULL) { module->OnConnect(module, 0, module->user); } // Connection Failed
			if (module->FinConnect > 0 && module->OnDisconnect != NULL) { module->OnDisconnect(module, module->user); } // Socket Disconnected

		module->FinConnect = 0;
		module->user = NULL;
	}
	else
	{
		SEM_TRACK(AsyncSocket_TrackUnLock("ILibAsyncSocket_Disconnect", 3, module);)
		sem_post(&(module->SendLock));
	}
}

void ILibProcessAsyncSocket(struct ILibAsyncSocketModule *Reader, int pendingRead);
void ILibAsyncSocket_Callback(ILibAsyncSocket_SocketModule socketModule, int connectDisconnectReadWrite)
{
	if (socketModule != NULL)
	{
		struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
		if (connectDisconnectReadWrite == 0) // Connected
		{
			memset(&(module->LocalAddress), 0, sizeof(struct sockaddr_in6));

			module->FinConnect = 1;
			module->PAUSE = 0;

				// No SSL, tell application we are connected.
				module->OnConnect(module, -1, module->user);			
		}
		else if (connectDisconnectReadWrite == 1) // Disconnected
			ILibAsyncSocket_Disconnect(module);
		else if (connectDisconnectReadWrite == 2) // Data read
			ILibProcessAsyncSocket(module, 1);
	}
}


/*! \fn ILibAsyncSocket_ConnectTo(ILibAsyncSocket_SocketModule socketModule, int localInterface, int remoteInterface, int remotePortNumber, ILibAsyncSocket_OnInterrupt InterruptPtr,void *user)
\brief Attempts to establish a TCP connection
\param socketModule The ILibAsyncSocket to initiate the connection
\param localInterface The interface to use to establish the connection
\param remoteInterface The remote interface to connect to
\param InterruptPtr Function Pointer that triggers if connection attempt is interrupted
\param user User object that will be passed to the \a OnConnect method
*/
void ILibAsyncSocket_ConnectTo(void* socketModule, struct sockaddr *localInterface, struct sockaddr *remoteInterface, ILibAsyncSocket_OnInterrupt InterruptPtr, void *user)
{
	int flags = 1;
	char *tmp;
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
	struct sockaddr_in6 any;

	// If there is something going on and we try to connect using this socket, fail! This is not supposed to happen.
	if (module->internalSocket != -1)
	{
		ILIBCRITICALEXIT2(253, (int)(module->internalSocket));
	}

	// Clean up
	memset(&(module->RemoteAddress), 0, sizeof(struct sockaddr_in6));
	memset(&(module->LocalAddress) , 0, sizeof(struct sockaddr_in6));
	memset(&(module->SourceAddress), 0, sizeof(struct sockaddr_in6));

	// Setup
	memcpy_s(&(module->RemoteAddress), sizeof(struct sockaddr_in6), remoteInterface, INET_SOCKADDR_LENGTH(remoteInterface->sa_family));
	module->PendingBytesToSend = 0;
	module->TotalBytesSent = 0;
	module->PAUSE = 0;
	module->user = user;
	module->OnInterrupt = InterruptPtr;
	if ((tmp = (char*)realloc(module->buffer, module->InitialSize)) == NULL) ILIBCRITICALEXIT(254);
	module->buffer = tmp;
	module->MallocSize = module->InitialSize;

	// If localInterface is NULL, we will assume INADDRANY - IPv4/IPv6 based on remote address
	if (localInterface == NULL)
	{
		memset(&any, 0, sizeof(struct sockaddr_in6));
		#ifdef MICROSTACK_PROXY
		if (module->ProxyAddress.sin6_family == 0) any.sin6_family = remoteInterface->sa_family; else any.sin6_family = module->ProxyAddress.sin6_family;
		#else
		any.sin6_family = remoteInterface->sa_family;
		#endif
		localInterface = (struct sockaddr*)&any;
	}

	// The local port should always be zero
#ifdef _DEBUG
	if (localInterface->sa_family == AF_INET && ((struct sockaddr_in*)localInterface)->sin_port != 0) { ILIBCRITICALEXIT(253); }
	if (localInterface->sa_family == AF_INET6 && ((struct sockaddr_in6*)localInterface)->sin6_port != 0) { ILIBCRITICALEXIT(253); }
#endif

	// Allocate a new socket
	if ((module->internalSocket = ILibGetSocket(localInterface, SOCK_STREAM, IPPROTO_TCP)) == 0) { ILIBCRITICALEXIT(253); return; }

	// Initialise the buffer pointers, since no data is in them yet.
	module->FinConnect = 0;
	module->BeginPointer = 0;
	module->EndPointer = 0;

	// Turn on keep-alives for the socket
	if (setsockopt(module->internalSocket, SOL_SOCKET, SO_KEEPALIVE, (char*)&flags, sizeof(flags)) != 0) ILIBCRITICALERREXIT(253);

	// Set the socket to non-blocking mode, because we need to play nice and share the MicroStack thread
#if defined(WIN32)
	ioctlsocket(module->internalSocket, FIONBIO, (u_long *)(&flags));
#elif defined(_POSIX)
	flags = fcntl(module->internalSocket, F_GETFL,0);
	fcntl(module->internalSocket, F_SETFL, O_NONBLOCK | flags);
#endif

	// Connect the socket, and force the chain to unblock, since the select statement doesn't have us in the fdset yet.
#ifdef MICROSTACK_PROXY
	if (module->ProxyAddress.sin6_family != 0)
	{
		if (connect(module->internalSocket, (struct sockaddr*)&(module->ProxyAddress), INET_SOCKADDR_LENGTH(module->ProxyAddress.sin6_family)) != -1)
		{
			// Connect failed. Set a short time and call disconnect.
			module->FinConnect = -1;
			ILibLifeTime_Add(module->LifeTime, socketModule, 0, &ILibAsyncSocket_Disconnect, NULL);
			return;
		}
	}
	else
#endif
		if (connect(module->internalSocket, (struct sockaddr*)remoteInterface, INET_SOCKADDR_LENGTH(remoteInterface->sa_family)) != -1)
	{
		// Connect failed. Set a short time and call disconnect.
		module->FinConnect = -1;
		ILibLifeTime_Add(module->LifeTime, socketModule, 0, &ILibAsyncSocket_Disconnect, NULL);
		return;
	}

#ifdef _DEBUG
	#ifdef _POSIX
		if (errno != EINPROGRESS) // The result of the connect should always be "WOULD BLOCK" on Linux. But sometimes this fails.
		{
			// This happens when the interface is no longer available. Disconnect socket.
			module->FinConnect = -1;
			ILibLifeTime_Add(module->LifeTime, socketModule, 0, &ILibAsyncSocket_Disconnect, NULL);
			return;
		}
	#endif
	#ifdef WIN32
		{
			if (GetLastError() != WSAEWOULDBLOCK) // The result of the connect should always be "WOULD BLOCK" on Windows.
			{
				// This happens when the interface is no longer available. Disconnect socket.
				module->FinConnect = -1;
				ILibLifeTime_Add(module->LifeTime, socketModule, 0, &ILibAsyncSocket_Disconnect, NULL);
				return;
			}
		}
	#endif
#endif

	ILibForceUnBlockChain(module->Transport.ChainLink.ParentChain);
}

#ifdef MICROSTACK_PROXY
//! Connect using an HTTPS proxy. If "proxyAddress" is set to NULL, this call acts just to a normal connect call without a proxy.
/*!
	\param socketModule ILibAsyncSocket Client to initiate the connection
	\param localInterface Local endpoint to originate the connection request from
	\param remoteAddress Destination endpoint to connect to
	\param proxyAddress Proxy Server to relay the connection thru.
	\param proxyUser Proxy Server username (Username is stored by reference, so this memory must remain valid for duration of connection)
	\param proxyPass Proxy Server password (Password is stored by reference, so this memory must remain valid for duration of connection)
	\param InterruptPtr Event handler triggered if connection request is interrupted
	\param user Custom user state data
*/
void ILibAsyncSocket_ConnectToProxy(void* socketModule, struct sockaddr *localInterface, struct sockaddr *remoteAddress, struct sockaddr *proxyAddress, char* proxyUser, char* proxyPass, ILibAsyncSocket_OnInterrupt InterruptPtr, void *user)
{
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
	memset(&(module->ProxyAddress), 0, sizeof(struct sockaddr_in6));
	module->ProxyState = 0;
	module->ProxyUser = proxyUser; // Proxy user & password are kept by reference!!!
	module->ProxyPass = proxyPass;

	if (proxyAddress != NULL) memcpy_s(&(module->ProxyAddress), sizeof(struct sockaddr_in6), proxyAddress, INET_SOCKADDR_LENGTH(proxyAddress->sa_family));
	ILibAsyncSocket_ConnectTo(socketModule, localInterface, remoteAddress, InterruptPtr, user);
}
#endif

//
// Internal method called when data is ready to be processed on an ILibAsyncSocket
//
// <param name="Reader">The ILibAsyncSocket with pending data</param>
void ILibProcessAsyncSocket(struct ILibAsyncSocketModule *Reader, int pendingRead)
{
	int bytesReceived = 0;
	int len;
	char *temp;

	if (Reader->PAUSE > 0)
	{
		ILibRemoteLogging_printf(ILibChainGetLogger(Reader->Transport.ChainLink.ParentChain), ILibRemoteLogging_Modules_Microstack_AsyncSocket, ILibRemoteLogging_Flags_VerbosityLevel_2, "AsyncSocket[%p] is PAUSED", (void*)Reader);
		return;
	}


	//
	// Try to read from the socket
	//
	if (pendingRead != 0)
	{
		{
			// Read data off the non-SSL, generic socket.
			// Set the receive address buffer size and read from the socket.
			len = sizeof(struct sockaddr_in6);
#if defined(WINSOCK2)
			bytesReceived = recvfrom(Reader->internalSocket, Reader->buffer + Reader->EndPointer, Reader->MallocSize - Reader->EndPointer, 0, (struct sockaddr*)&(Reader->SourceAddress), (int*)&len);
#else
			bytesReceived = (int)recvfrom(Reader->internalSocket, Reader->buffer + Reader->EndPointer, Reader->MallocSize - Reader->EndPointer, 0, (struct sockaddr*)&(Reader->SourceAddress), (socklen_t*)&len);
#endif
#ifdef WIN32
#ifdef _DEBUG
			if (bytesReceived == SOCKET_ERROR) ILIBMESSAGE2("Failed socket recvfrom function", WSAGetLastError());
#endif
#endif
			ILib6to4((struct sockaddr*)&(Reader->SourceAddress));
			ILibRemoteLogging_printf(ILibChainGetLogger(Reader->Transport.ChainLink.ParentChain), ILibRemoteLogging_Modules_Microstack_AsyncSocket, ILibRemoteLogging_Flags_VerbosityLevel_2, "AsyncSocket[%p] recv (NON-TLS) returned %d", (void*)Reader, bytesReceived);
		}
		if (bytesReceived > 0)
		{
			//
			// Data was read, so increment our counters
			//
			Reader->EndPointer += bytesReceived;
		}
	}

	//
	// Event OnData up the stack, to process any data that is available
	//
	while (Reader->internalSocket != ~0 && Reader->PAUSE <= 0 && Reader->BeginPointer != Reader->EndPointer && Reader->EndPointer != 0)
	{
		int iPointer = 0;

		if (Reader->OnData != NULL)
		{
			Reader->OnData(Reader, Reader->buffer + Reader->BeginPointer, &(iPointer), Reader->EndPointer - Reader->BeginPointer, &(Reader->OnInterrupt), &(Reader->user), &(Reader->PAUSE));
			if (iPointer == 0) { break; }
			Reader->BeginPointer += iPointer;
		}
	}
	if (Reader->BeginPointer == Reader->EndPointer) { Reader->BeginPointer = Reader->EndPointer = 0; }


	if (bytesReceived <= 0 && pendingRead != 0)
	{
		// If a UDP packet is larger than the buffer, drop it.
#if defined(WINSOCK2)
		if (bytesReceived == SOCKET_ERROR && WSAGetLastError() == 10040) { ILIBMESSAGE2("Failed bytesReceived", WSAGetLastError()); return; }
#else
		// TODO: Linux errno
		//if (bytesReceived == -1 && errno != 0) printf("ERROR: errno = %d, %s\r\n", errno, strerror(errno));
#endif

		//
		// This means the socket was gracefully closed by the remote endpoint
		//
		SEM_TRACK(AsyncSocket_TrackLock("ILibProcessAsyncSocket", 1, Reader);)
			ILibAsyncSocket_ClearPendingSend(Reader);
		SEM_TRACK(AsyncSocket_TrackUnLock("ILibProcessAsyncSocket", 2, Reader);)

#if  defined(WIN32)
#if defined(WINSOCK2)
			shutdown(Reader->internalSocket, SD_BOTH);
#endif
		closesocket(Reader->internalSocket);
#elif defined(_POSIX)
			shutdown(Reader->internalSocket, SHUT_RDWR);
		close(Reader->internalSocket);
#endif
		Reader->internalSocket = (SOCKET)~0;

		ILibAsyncSocket_ClearPendingSend(Reader);
	
		//
		// Inform the user the socket has closed
		//
		Reader->timeout_handler = NULL;
		Reader->timeout_milliSeconds = 0;
			// This was a normal socket, fire the event notifying the user. Depending on connection state, we event differently
			if (Reader->FinConnect <= 0 && Reader->OnConnect != NULL) { Reader->OnConnect(Reader, 0, Reader->user); } // Connection Failed
			if (Reader->FinConnect > 0 && Reader->OnDisconnect != NULL) { Reader->OnDisconnect(Reader, Reader->user); } // Socket Disconnected
		Reader->FinConnect = 0;

		//
		// If we need to free the buffer, do so
		//
		if (Reader->buffer != NULL)
		{
			if (Reader->buffer != ILibScratchPad2) free(Reader->buffer);
			Reader->buffer = NULL;
			Reader->MallocSize = 0;
		}
	}
	else
	{
		// 
		// Only do these checks if the socket was not closed, otherwise we're wasting time
		//

		//
		// Check to see if we need to move any data, to maximize buffer space
		//
		if (Reader->BeginPointer > 0)
		{
			//
			// We can save some cycles by moving the data back to the top
			// of the buffer, instead of just allocating more memory.
			//
			temp = Reader->buffer + Reader->BeginPointer;
			
			memmove_s(Reader->buffer, Reader->MallocSize, temp, Reader->EndPointer - Reader->BeginPointer);
			Reader->EndPointer -= Reader->BeginPointer;
			Reader->BeginPointer = 0;

			//
			// Even though we didn't allocate new memory, we still moved data in the buffer, 
			// so we need to inform people of that, because it might be important
			//
			if (Reader->OnBufferReAllocated != NULL) Reader->OnBufferReAllocated(Reader, Reader->user, temp - Reader->buffer);
		}

		//
		// Check to see if we should grow the buffer
		//
		if (Reader->MallocSize - Reader->EndPointer < 1024 && (Reader->MaxBufferSize == 0 || Reader->MallocSize < Reader->MaxBufferSize))
		{
			Reader->MallocSize = (Reader->MallocSize + MEMORYCHUNKSIZE < Reader->MaxBufferSize) ? (Reader->MallocSize + MEMORYCHUNKSIZE) : (Reader->MaxBufferSize == 0 ? (Reader->MallocSize + MEMORYCHUNKSIZE) : Reader->MaxBufferSize);

			temp = Reader->buffer;
			if ((Reader->buffer = (char*)realloc(Reader->buffer, Reader->MallocSize)) == NULL) ILIBCRITICALEXIT(254);
			//
			// If this realloc moved the buffer somewhere, we need to inform people of it
			//
			if (Reader->buffer != temp && Reader->OnBufferReAllocated != NULL) Reader->OnBufferReAllocated(Reader, Reader->user, Reader->buffer - temp);
		}
	}
}

/*! \fn ILibAsyncSocket_GetUser(ILibAsyncSocket_SocketModule socketModule)
\brief Returns the user object
\param socketModule The ILibAsyncSocket token to fetch the user object from
\returns The user object
*/
void *ILibAsyncSocket_GetUser(ILibAsyncSocket_SocketModule socketModule)
{
	return(socketModule == NULL?NULL:((struct ILibAsyncSocketModule*)socketModule)->user);
}

void ILibAsyncSocket_SetUser(ILibAsyncSocket_SocketModule socketModule, void* user)
{
	if (socketModule == NULL) return;
	((struct ILibAsyncSocketModule*)socketModule)->user = user;
}

void *ILibAsyncSocket_GetUser2(ILibAsyncSocket_SocketModule socketModule)
{
	return(socketModule == NULL?NULL:((struct ILibAsyncSocketModule*)socketModule)->user2);
}

void ILibAsyncSocket_SetUser2(ILibAsyncSocket_SocketModule socketModule, void* user2)
{
	if (socketModule == NULL) return;
	((struct ILibAsyncSocketModule*)socketModule)->user2 = user2;
}

int ILibAsyncSocket_GetUser3(ILibAsyncSocket_SocketModule socketModule)
{
	return(socketModule == NULL?-1:((struct ILibAsyncSocketModule*)socketModule)->user3);
}

void ILibAsyncSocket_SetUser3(ILibAsyncSocket_SocketModule socketModule, int user3)
{
	if (socketModule == NULL) return;
	((struct ILibAsyncSocketModule*)socketModule)->user3 = user3;
}

void ILibAsyncSocket_SetTimeout(ILibAsyncSocket_SocketModule socketModule, int timeoutSeconds, ILibAsyncSocket_TimeoutHandler timeoutHandler)
{
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
	module->timeout_milliSeconds = timeoutSeconds * 1000;
	module->timeout_handler = timeoutHandler;
}

//
// Chained PreSelect handler for ILibAsyncSocket
//
// <param name="readset"></param>
// <param name="writeset"></param>
// <param name="errorset"></param>
// <param name="blocktime"></param>
void ILibAsyncSocket_PreSelect(void* socketModule,fd_set *readset, fd_set *writeset, fd_set *errorset, int* blocktime)
{
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
	if (module->internalSocket == -1) return; // If there is not internal socket, just return now.

	ILibRemoteLogging_printf(ILibChainGetLogger(module->Transport.ChainLink.ParentChain), ILibRemoteLogging_Modules_Microstack_AsyncSocket, ILibRemoteLogging_Flags_VerbosityLevel_5, "AsyncSocket[%p] entered PreSelect", (void*)module);

	SEM_TRACK(AsyncSocket_TrackLock("ILibAsyncSocket_PreSelect", 1, module);)
	sem_wait(&(module->SendLock));

	if (module->internalSocket != -1)
	{
		if (module->timeout_milliSeconds != 0)
		{
			// User has set idle timeout, so we need to start checking
			if (module->timeout_lastActivity == 0)
			{
				// No activity yet on socket, so set the idle timeout for the full duration
				*blocktime = module->timeout_milliSeconds;
			}
			else
			{
				long long activity = ILibGetUptime() - module->timeout_lastActivity; // number of milliseconds since last activity
				if (activity >= module->timeout_milliSeconds) 
				{
					// Idle Timeout Occured
					ILibAsyncSocket_TimeoutHandler h = module->timeout_handler;
					module->timeout_milliSeconds = 0;
					module->timeout_handler = NULL;
					if (h != NULL)
					{
						sem_post(&(module->SendLock));
						h(module, module->user);
						sem_wait(&(module->SendLock));
						if (module->timeout_milliSeconds != 0) { *blocktime = module->timeout_milliSeconds; }
					}
				}
				else
				{
					// Idle Timeout did not occur yet, so set the blocktime for the rest of the timeout
					*blocktime = (int)(module->timeout_milliSeconds - activity);
				}
			}
		}

		if (module->PAUSE < 0) *blocktime = 0;
		if (module->FinConnect == 0)
		{
			// Not Connected Yet
			#if defined(WIN32)
			#pragma warning( push, 3 ) // warning C4127: conditional expression is constant
			#endif
			FD_SET(module->internalSocket, writeset);
			FD_SET(module->internalSocket, errorset);
			#if defined(WIN32)
			#pragma warning( pop )
			#endif
		}
		else
		{
			if (module->PAUSE == 0) // Only if this is zero. <0 is resume, so we want to process first
			{
				// Already Connected, just needs reading
				#if defined(WIN32)
				#pragma warning( push, 3 ) // warning C4127: conditional expression is constant
				#endif
				FD_SET(module->internalSocket, readset);
				FD_SET(module->internalSocket, errorset);
				#if defined(WIN32)
				#pragma warning( pop )
				#endif
			}
		}

		if (module->PendingSend_Head != NULL)
		{
			// If there is pending data to be sent, then we need to check when the socket is writable
			#if defined(WIN32)
			#pragma warning( push, 3 ) // warning C4127: conditional expression is constant
			#endif
			FD_SET(module->internalSocket, writeset);
			#if defined(WIN32)
			#pragma warning( pop )
			#endif
		}
	}

	SEM_TRACK(AsyncSocket_TrackUnLock("ILibAsyncSocket_PreSelect", 2, module);)
	sem_post(&(module->SendLock));

	ILibRemoteLogging_printf(ILibChainGetLogger(module->Transport.ChainLink.ParentChain), ILibRemoteLogging_Modules_Microstack_AsyncSocket, ILibRemoteLogging_Flags_VerbosityLevel_5, "...AsyncSocket[%p] exited PreSelect", (void*)module);
}

void ILibAsyncSocket_PrivateShutdown(void* socketModule)
{
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;

	// If this is an SSL socket, close down the SSL state

	// Now shutdown the socket and set it to zero
	#if  defined(WIN32)
	#if defined(WINSOCK2)
		shutdown(module->internalSocket, SD_BOTH);
	#endif
		closesocket(module->internalSocket);
	#elif defined(_POSIX)
		shutdown(module->internalSocket, SHUT_RDWR);
		close(module->internalSocket);
	#endif
	module->internalSocket = (SOCKET)~0;
	module->timeout_handler = NULL;
	module->timeout_milliSeconds = 0;

		// This was a normal socket, fire the event notifying the user. Depending on connection state, we event differently
		if (module->FinConnect <= 0 && module->OnConnect != NULL) { module->OnConnect(module, 0, module->user); } // Connection Failed
		if (module->FinConnect > 0 && module->OnDisconnect != NULL) { module->OnDisconnect(module, module->user); } // Socket Disconnected
	module->FinConnect = 0;
}

//
// Chained PostSelect handler for ILibAsyncSocket
//
// <param name="socketModule"></param>
// <param name="slct"></param>
// <param name="readset"></param>
// <param name="writeset"></param>
// <param name="errorset"></param>
void ILibAsyncSocket_PostSelect(void* socketModule, int slct, fd_set *readset, fd_set *writeset, fd_set *errorset)
{
	int TriggerSendOK = 0;
	struct ILibAsyncSocket_SendData *temp;
	int bytesSent = 0;
	int flags, len;
	int TRY_TO_SEND = 1;
	int triggerReadSet = 0;
	int triggerResume = 0;
	int triggerWriteSet = 0;
	int serr = 0, serrlen = sizeof(serr);
	int fd_error, fd_read, fd_write;
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;

	// If there is no internal socket or no events, just return now.
	if (module->internalSocket == -1 || module->FinConnect == -1) return;
	fd_error = FD_ISSET(module->internalSocket, errorset);
	fd_read = FD_ISSET(module->internalSocket, readset);
	fd_write = FD_ISSET(module->internalSocket, writeset);

	ILibRemoteLogging_printf(ILibChainGetLogger(module->Transport.ChainLink.ParentChain), ILibRemoteLogging_Modules_Microstack_AsyncSocket, ILibRemoteLogging_Flags_VerbosityLevel_5, "AsyncSocket[%p] entered PostSelect", (void*)module);



	UNREFERENCED_PARAMETER( slct );

	SEM_TRACK(AsyncSocket_TrackLock("ILibAsyncSocket_PostSelect", 1, module);)
	sem_wait(&(module->SendLock)); // Lock!
	
	//if (fd_error != 0) printf("ILibAsyncSocket_PostSelect-ERROR\r\n");
	//if (fd_read != 0) printf("ILibAsyncSocket_PostSelect-READ\r\n");
	//if (fd_write != 0) printf("ILibAsyncSocket_PostSelect-WRITE\r\n");

	//
	// Error Handling. If the ERROR flag is set we have a problem. If not, we must check the socket status for an error.
	// Yes, this is odd, but it's possible for a socket to report a read set and still have an error, in this past this
	// was not handled and caused a lot of problems.
	//
	if (fd_error != 0)
	{
		serr = 1;
	}
	else
	{
		// Fetch the socket error code
#if defined(WINSOCK2)
		getsockopt(module->internalSocket, SOL_SOCKET, SO_ERROR, (char*)&serr, (int*)&serrlen);
#else
		getsockopt(module->internalSocket, SOL_SOCKET, SO_ERROR, (char*)&serr, (socklen_t*)&serrlen);
#endif
	}

	#ifdef MICROSTACK_PROXY
	// Handle proxy, we need to read the proxy response, all of it and not a byte more.
	if (module->FinConnect == 1 && module->ProxyState == 1 && serr == 0 && fd_read != 0)
	{
		char *ptr1, *ptr2;
		int len2, len3;
		serr = 555; // Fake proxy error
		len2 = recv(module->internalSocket, ILibScratchPad2, 1024, MSG_PEEK | MSG_NOSIGNAL);
		if (len2 > 0 && len2 < 1024)
		{
			ILibScratchPad2[len2] = 0;
			ptr1 = strstr(ILibScratchPad2, "\r\n\r\n");
			ptr2 = strstr(ILibScratchPad2, " 200 ");
			if (ptr1 != NULL && ptr2 != NULL && ptr2 < ptr1)
			{
				len3 = (int)((ptr1 + 4) - ILibScratchPad2);
				recv(module->internalSocket, ILibScratchPad2, len3, MSG_NOSIGNAL);
				module->FinConnect = 0; // Let pretend we never connected, this will trigger all the connection stuff.
				module->ProxyState = 2; // Move the proxy connection state forward.
				serr = 0;				// Proxy connected collectly.
			}
		}
	}
	#endif

	// If there are any errors, shutdown this socket
	if (serr != 0)
	{
		// Unlock before fireing the event
		SEM_TRACK(AsyncSocket_TrackUnLock("ILibAsyncSocket_PostSelect", 4, module);)
		sem_post(&(module->SendLock));
		ILibAsyncSocket_PrivateShutdown(module);
	}
	else
	{
		// There are no errors, lets keep processing the socket normally
		if (module->FinConnect == 0)
		{
			// Check to see if the socket is connected
#ifdef MICROSTACK_PROXY
			if (fd_write != 0 || module->ProxyState == 2)
#else
			if (fd_write != 0)
#endif
			{
				// Connected
				len = sizeof(struct sockaddr_in6);
#if defined(WINSOCK2)
				getsockname(module->internalSocket, (struct sockaddr*)(&module->LocalAddress), (int*)&len);
#else
				getsockname(module->internalSocket, (struct sockaddr*)(&module->LocalAddress), (socklen_t*)&len);
#endif
				module->FinConnect = 1;
				module->PAUSE = 0;

				// Set the socket to non-blocking mode, so we can play nice and share the thread
				#if defined(WIN32)
					flags = 1;
					ioctlsocket(module->internalSocket, FIONBIO, (u_long *)(&flags));
				#elif defined(_POSIX)
				flags = fcntl(module->internalSocket, F_GETFL,0);
				fcntl(module->internalSocket, F_SETFL, O_NONBLOCK|flags);
				#endif

				// If this is a proxy connection, send the proxy connect header now.
#ifdef MICROSTACK_PROXY
				if (module->ProxyAddress.sin6_family != 0 && module->ProxyState == 0)
				{
					int len2;
					ILibInet_ntop((int)(module->RemoteAddress.sin6_family), (void*)&(((struct sockaddr_in*)&(module->RemoteAddress))->sin_addr), ILibScratchPad, 4096);
					if (module->ProxyUser == NULL || module->ProxyPass == NULL)
					{
						len2 = sprintf_s(ILibScratchPad2, 4096, "CONNECT %s:%u HTTP/1.1\r\nProxy-Connection: keep-alive\r\nHost: %s\r\n\r\n", ILibScratchPad, ntohs(module->RemoteAddress.sin6_port), ILibScratchPad);
					} else {
						char* ProxyAuth = NULL;
						len2 = sprintf_s(ILibScratchPad2, 4096, "%s:%s", module->ProxyUser, module->ProxyPass);
						len2 = ILibBase64Encode((unsigned char*)ILibScratchPad2, len2, (unsigned char**)&ProxyAuth);
						len2 = sprintf_s(ILibScratchPad2, 4096, "CONNECT %s:%u HTTP/1.1\r\nProxy-Connection: keep-alive\r\nHost: %s\r\nProxy-authorization: basic %s\r\n\r\n", ILibScratchPad, ntohs(module->RemoteAddress.sin6_port), ILibScratchPad, ProxyAuth);
						if (ProxyAuth != NULL) free(ProxyAuth);
					}
					module->timeout_lastActivity = ILibGetUptime();
					send(module->internalSocket, ILibScratchPad2, len2, MSG_NOSIGNAL);
					module->ProxyState = 1;
					// TODO: Set timeout. If the proxy does not respond, we need to close this connection.
					// On the other hand... This is not generally a problem, proxies will disconnect after a timeout anyway.
					
					SEM_TRACK(AsyncSocket_TrackUnLock("ILibAsyncSocket_PostSelect", 4, module);)
					sem_post(&(module->SendLock));
					return;
				}
				if (module->ProxyState == 2) module->ProxyState = 3;
#endif

				// Connection Complete
				triggerWriteSet = 1;
			}

			// Unlock before fireing the event
			SEM_TRACK(AsyncSocket_TrackUnLock("ILibAsyncSocket_PostSelect", 4, module);)
			sem_post(&(module->SendLock));

			// If we did connect, we got more things to do
			if (triggerWriteSet != 0)
			{
				module->timeout_lastActivity = ILibGetUptime();
				{
					// If this is a normal socket, event the connection now.
					if (module->OnConnect != NULL) module->OnConnect(module, -1, module->user);
				}
			}
		}
		else
		{
			// Connected socket, we need to read data
			if (fd_read != 0)
			{
				module->timeout_lastActivity = ILibGetUptime();
				triggerReadSet = 1; // Data Available
			}
			else if (module->PAUSE < 0)
			{
				// Someone resumed a paused connection, but the FD_SET was not triggered because there is no new data on the socket.
				module->timeout_lastActivity = ILibGetUptime();
				triggerResume = 1;
				++module->PAUSE;
				ILibRemoteLogging_printf(ILibChainGetLogger(module->Transport.ChainLink.ParentChain), ILibRemoteLogging_Modules_Microstack_AsyncSocket, ILibRemoteLogging_Flags_VerbosityLevel_2, "AsyncSocket[%p] was RESUMED with no new data on socket", (void*)module);
			}

			// Unlock before fireing the event
			SEM_TRACK(AsyncSocket_TrackUnLock("ILibAsyncSocket_PostSelect", 4, module);)
			sem_post(&(module->SendLock));

			if (triggerReadSet != 0 || triggerResume != 0) ILibProcessAsyncSocket(module, triggerReadSet);
		}
	}
	SEM_TRACK(AsyncSocket_TrackUnLock("ILibAsyncSocket_PostSelect", 4, module);)


	SEM_TRACK(AsyncSocket_TrackLock("ILibAsyncSocket_PostSelect", 1, module);)
	sem_wait(&(module->SendLock));
	// Write Handling
	if (module->FinConnect > 0 && module->internalSocket != ~0 && fd_write != 0 && module->PendingSend_Head != NULL)
	{
		//
		// Keep trying to send data, until we are told we can't
		//
		module->timeout_lastActivity = ILibGetUptime();
		while (TRY_TO_SEND != 0)
		{
			if (module->PendingSend_Head == NULL) break;
			if (module->PendingSend_Head->remoteAddress.sin6_family == 0)
			{
				bytesSent = (int)send(module->internalSocket, module->PendingSend_Head->buffer + module->PendingSend_Head->bytesSent, module->PendingSend_Head->bufferSize - module->PendingSend_Head->bytesSent, MSG_NOSIGNAL); // Klocwork reports that this could block while holding a lock... This socket has been set to O_NONBLOCK, so that will never happen
			}
			else
			{
				bytesSent = (int)sendto(module->internalSocket, module->PendingSend_Head->buffer + module->PendingSend_Head->bytesSent, module->PendingSend_Head->bufferSize - module->PendingSend_Head->bytesSent, MSG_NOSIGNAL, (struct sockaddr*)&module->PendingSend_Head->remoteAddress, INET_SOCKADDR_LENGTH(module->PendingSend_Head->remoteAddress.sin6_family)); // Klocwork reports that this could block while holding a lock... This socket has been set to O_NONBLOCK, so that will never happen
			}

			if (bytesSent == 0) { TRY_TO_SEND = 0; } //To avoid get stuck in an infinite loop when bytesSent == 0

			if (bytesSent > 0)
			{
				module->PendingBytesToSend -= bytesSent;
				module->TotalBytesSent += bytesSent;
				module->PendingSend_Head->bytesSent += bytesSent;
				if (module->PendingSend_Head->bytesSent == module->PendingSend_Head->bufferSize)
				{
					// Finished Sending this block
					if (module->PendingSend_Head == module->PendingSend_Tail)
					{
						module->PendingSend_Tail = NULL;
					}
					if (module->PendingSend_Head->UserFree == 0)
					{
						free(module->PendingSend_Head->buffer);
					}
					temp = module->PendingSend_Head->Next;
					free(module->PendingSend_Head);
					module->PendingSend_Head = temp;
					if (module->PendingSend_Head == NULL) { TRY_TO_SEND = 0; }
				}
				else
				{
					// We sent data, but not everything that needs to get sent was sent, try again
					TRY_TO_SEND = 1;
				}
			}
			if (bytesSent == -1)
			{
				// Error, clean up everything
				TRY_TO_SEND = 0;
#if  defined(WIN32)
				if (WSAGetLastError() != WSAEWOULDBLOCK)
#elif defined(_POSIX)
				if (errno != EWOULDBLOCK)
#endif
				{
					// There was an error sending
					ILibAsyncSocket_ClearPendingSend(socketModule);
					ILibLifeTime_Add(module->LifeTime, socketModule, 0, &ILibAsyncSocket_Disconnect, NULL);
				}
			}
		}
		// This triggers OnSendOK, if all the pending data has been sent.
		if (module->PendingSend_Head == NULL && bytesSent != -1) { TriggerSendOK = 1; }
		SEM_TRACK(AsyncSocket_TrackUnLock("ILibAsyncSocket_PostSelect", 2, module);)
		sem_post(&(module->SendLock));
		if (TriggerSendOK != 0)
		{
			module->OnSendOK(module, module->user);
			if (module->Transport.SendOkPtr != NULL) { module->Transport.SendOkPtr(module); }
		}

		if (bytesSent == 0) { ILibAsyncSocket_ClearPendingSend(socketModule); } //If bytesSent == 0 then clear pending data
	}
	else
	{
		SEM_TRACK(AsyncSocket_TrackUnLock("ILibAsyncSocket_PostSelect", 2, module);)
		sem_post(&(module->SendLock));
	}

	ILibRemoteLogging_printf(ILibChainGetLogger(module->Transport.ChainLink.ParentChain), ILibRemoteLogging_Modules_Microstack_AsyncSocket, ILibRemoteLogging_Flags_VerbosityLevel_5, "...AsyncSocket[%p] exited PostSelect", (void*)module);
}

/*! \fn ILibAsyncSocket_IsFree(ILibAsyncSocket_SocketModule socketModule)
\brief Determines if an ILibAsyncSocket is in use
\param socketModule The ILibAsyncSocket to query
\returns 0 if in use, nonzero otherwise
*/
int ILibAsyncSocket_IsFree(ILibAsyncSocket_SocketModule socketModule)
{
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
	return((module == NULL || module->internalSocket==~0)?1:0);
}

int ILibAsyncSocket_IsConnected(ILibAsyncSocket_SocketModule socketModule)
{
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
	return module->FinConnect;
}

/*! \fn ILibAsyncSocket_GetPendingBytesToSend(ILibAsyncSocket_SocketModule socketModule)
\brief Returns the number of bytes that are pending to be sent
\param socketModule The ILibAsyncSocket to query
\returns Number of pending bytes
*/
unsigned int ILibAsyncSocket_GetPendingBytesToSend(ILibAsyncSocket_SocketModule socketModule)
{
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
	return(module->PendingBytesToSend);
}

/*! \fn ILibAsyncSocket_GetTotalBytesSent(ILibAsyncSocket_SocketModule socketModule)
\brief Returns the total number of bytes that have been sent, since the last reset
\param socketModule The ILibAsyncSocket to query
\returns Number of bytes sent
*/
unsigned int ILibAsyncSocket_GetTotalBytesSent(ILibAsyncSocket_SocketModule socketModule)
{
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
	return(module->TotalBytesSent);
}

/*! \fn ILibAsyncSocket_ResetTotalBytesSent(ILibAsyncSocket_SocketModule socketModule)
\brief Resets the total bytes sent counter
\param socketModule The ILibAsyncSocket to reset
*/
void ILibAsyncSocket_ResetTotalBytesSent(ILibAsyncSocket_SocketModule socketModule)
{
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
	module->TotalBytesSent = 0;
}

/*! \fn ILibAsyncSocket_GetBuffer(ILibAsyncSocket_SocketModule socketModule, char **buffer, int *BeginPointer, int *EndPointer)
\brief Returns the buffer associated with an ILibAsyncSocket
\param socketModule The ILibAsyncSocket to obtain the buffer from
\param[out] buffer The buffer
\param[out] BeginPointer Stating offset of the buffer
\param[out] EndPointer Length of buffer
*/
void ILibAsyncSocket_GetBuffer(ILibAsyncSocket_SocketModule socketModule, char **buffer, int *BeginPointer, int *EndPointer)
{
	struct ILibAsyncSocketModule* module = (struct ILibAsyncSocketModule*)socketModule;

	*buffer = module->buffer;
	*BeginPointer = module->BeginPointer;
	*EndPointer = module->EndPointer;
}

void ILibAsyncSocket_ModuleOnConnect(ILibAsyncSocket_SocketModule socketModule)
{
	struct ILibAsyncSocketModule* module = (struct ILibAsyncSocketModule*)socketModule;
	if (module != NULL && module->OnConnect != NULL) module->OnConnect(module, -1, module->user);
}

//! Set the SSL client context used by all connections done by this socket module. The SSL context must
//! be set before using this module. If left to NULL, all connections are in the clear using TCP.
//!
//! This is utilized by the ILibAsyncServerSocket module
/*!
	\param socketModule The ILibAsyncSocket to modify
	\param ssl_ctx The ssl_ctx structure
	\param server ILibAsyncSocket_TLS_Mode Configuration setting to set
*/

//! Sets the remote address field.
//! This is utilized by the ILibAsyncServerSocket module
/*!
	\param socketModule ILibAsyncSocket_SocketModule to modify
	\param remoteAddress The remote endpoint to set
*/
void ILibAsyncSocket_SetRemoteAddress(ILibAsyncSocket_SocketModule socketModule, struct sockaddr *remoteAddress)
{
	if (socketModule != NULL)
	{
		struct ILibAsyncSocketModule* module = (struct ILibAsyncSocketModule*)socketModule;
		memcpy_s(&(module->RemoteAddress), sizeof(struct sockaddr_in6), remoteAddress, INET_SOCKADDR_LENGTH(remoteAddress->sa_family));
	}
}

/*! \fn ILibAsyncSocket_UseThisSocket(ILibAsyncSocket_SocketModule socketModule,void* UseThisSocket,ILibAsyncSocket_OnInterrupt InterruptPtr,void *user)
\brief Associates an actual socket with ILibAsyncSocket
\par
Instead of calling \a ConnectTo, you can call this method to associate with an already
connected socket.
\param socketModule The ILibAsyncSocket to associate
\param UseThisSocket The socket to associate
\param InterruptPtr Function Pointer that triggers when the TCP connection is interrupted
\param user User object to associate with this session
*/
#if  defined(WIN32)
void ILibAsyncSocket_UseThisSocket(ILibAsyncSocket_SocketModule socketModule, SOCKET UseThisSocket, ILibAsyncSocket_OnInterrupt InterruptPtr, void *user)
#elif defined(_POSIX)
void ILibAsyncSocket_UseThisSocket(ILibAsyncSocket_SocketModule socketModule, int UseThisSocket, ILibAsyncSocket_OnInterrupt InterruptPtr, void *user)
#endif
{
	int flags;
	char *tmp;
	struct ILibAsyncSocketModule* module = (struct ILibAsyncSocketModule*)socketModule;

	module->PendingBytesToSend = 0;
	module->TotalBytesSent = 0;
	module->internalSocket = UseThisSocket;
	module->OnInterrupt = InterruptPtr;
	module->user = user;
	module->FinConnect = 1;
	module->PAUSE = 0;

	ILibRemoteLogging_printf(ILibChainGetLogger(module->Transport.ChainLink.ParentChain), ILibRemoteLogging_Modules_Microstack_AsyncSocket, ILibRemoteLogging_Flags_VerbosityLevel_1, "AsyncSocket[%p] Initialized", (void*)module);

	//
	// If the buffer is too small/big, we need to realloc it to the minimum specified size
	//
	if (module->buffer != ILibScratchPad2)
	{
		if ((tmp = (char*)realloc(module->buffer, module->InitialSize)) == NULL) ILIBCRITICALEXIT(254);
		module->buffer = tmp;
		module->MallocSize = module->InitialSize;
	}
	module->BeginPointer = 0;
	module->EndPointer = 0;

	//
	// Make sure the socket is non-blocking, so we can play nice and share the thread
	//
#if defined(WIN32)
	flags = 1;
	ioctlsocket(module->internalSocket, FIONBIO,(u_long *)(&flags));
#elif defined(_POSIX)
	flags = fcntl(module->internalSocket,F_GETFL,0);
	fcntl(module->internalSocket,F_SETFL,O_NONBLOCK|flags);
#endif
}

/*! \fn ILibAsyncSocket_GetRemoteInterface(ILibAsyncSocket_SocketModule socketModule)
\brief Returns the Remote Interface of a connected session
\param socketModule The ILibAsyncSocket to query
\param[in,out] remoteAddress The remote interface
\returns Number of bytes written into remoteAddress
*/
int ILibAsyncSocket_GetRemoteInterface(ILibAsyncSocket_SocketModule socketModule, struct sockaddr *remoteAddress)
{
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
	if (module->RemoteAddress.sin6_family != 0)
	{
		memcpy_s(remoteAddress, sizeof(struct sockaddr_in6), &(module->RemoteAddress), INET_SOCKADDR_LENGTH(module->RemoteAddress.sin6_family));
		return INET_SOCKADDR_LENGTH(module->RemoteAddress.sin6_family);
	}
	memcpy_s(remoteAddress, sizeof(struct sockaddr_in6), &(module->SourceAddress), INET_SOCKADDR_LENGTH(module->SourceAddress.sin6_family));
	return INET_SOCKADDR_LENGTH(module->SourceAddress.sin6_family);
}

/*! \fn ILibAsyncSocket_GetLocalInterface(ILibAsyncSocket_SocketModule socketModule)
\brief Returns the Local Interface of a connected session, in network order
\param socketModule The ILibAsyncSocket to query
\param[in,out] localAddress The local interface
\returns The number of bytes written to localAddress
*/
int ILibAsyncSocket_GetLocalInterface(ILibAsyncSocket_SocketModule socketModule, struct sockaddr *localAddress)
{
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
	int receivingAddressLength = sizeof(struct sockaddr_in6);

	if (module->LocalAddress.sin6_family !=0)
	{
		memcpy_s(localAddress, INET_SOCKADDR_LENGTH(module->LocalAddress.sin6_family), &(module->LocalAddress), INET_SOCKADDR_LENGTH(module->LocalAddress.sin6_family));
		return INET_SOCKADDR_LENGTH(module->LocalAddress.sin6_family);
	}
	else
	{
#if defined(WINSOCK2)
		getsockname(module->internalSocket, localAddress, (int*)&receivingAddressLength);
#else
		getsockname(module->internalSocket, localAddress, (socklen_t*)&receivingAddressLength);
#endif
		return receivingAddressLength;
	}
}
//! Get's the locally bound port
/*!
	\param socketModule ILibAsyncSocket_SocketModule object to query
	\return The locallly bound port
*/
unsigned short ILibAsyncSocket_GetLocalPort(ILibAsyncSocket_SocketModule socketModule)
{
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
	int receivingAddressLength = sizeof(struct sockaddr_in6);
	struct sockaddr_in6 localAddress;

	if (module->LocalAddress.sin6_family == AF_INET6) return ntohs(module->LocalAddress.sin6_port);
	if (module->LocalAddress.sin6_family == AF_INET) return ntohs((((struct sockaddr_in*)(&(module->LocalAddress)))->sin_port));
#if defined(WINSOCK2)
	getsockname(module->internalSocket, (struct sockaddr*)&localAddress, (int*)&receivingAddressLength);
#else
	getsockname(module->internalSocket, (struct sockaddr*)&localAddress, (socklen_t*)&receivingAddressLength);
#endif
	if (localAddress.sin6_family == AF_INET6) return ntohs(localAddress.sin6_port);
	if (localAddress.sin6_family == AF_INET) return ntohs((((struct sockaddr_in*)(&localAddress))->sin_port));
	return 0;
}
/*! \fn ILibAsyncSocket_Pause(ILibAsyncSocket_SocketModule socketModule)
\brief Pauses a session
\par
Sessions can be paused, such that further data is not read from the socket until resumed. NOTE: MUST be called from Microstack thread
\param socketModule The ILibAsyncSocket to pause.
*/
void ILibAsyncSocket_Pause(ILibAsyncSocket_SocketModule socketModule)
{
	struct ILibAsyncSocketModule *sm = (struct ILibAsyncSocketModule*)socketModule;
	if (socketModule == NULL) { return; }

	sm->PAUSE = 1;
}
/*! \fn ILibAsyncSocket_Resume(ILibAsyncSocket_SocketModule socketModule)
\brief Resumes a paused session
\par
Sessions can be paused, such that further data is not read from the socket until resumed
\param socketModule The ILibAsyncSocket to resume
*/
void ILibAsyncSocket_Resume(ILibAsyncSocket_SocketModule socketModule)
{
	struct ILibAsyncSocketModule *sm = (struct ILibAsyncSocketModule*)socketModule;
	if (sm == NULL) { return; }
	ILibRemoteLogging_printf(ILibChainGetLogger(sm->Transport.ChainLink.ParentChain), ILibRemoteLogging_Modules_Microstack_AsyncSocket, ILibRemoteLogging_Flags_VerbosityLevel_2, "AsyncSocket[%p] was RESUMED", (void*)socketModule);
	if (sm->PAUSE > 0)
	{
		ILibRemoteLogging_printf(ILibChainGetLogger(sm->Transport.ChainLink.ParentChain), ILibRemoteLogging_Modules_Microstack_AsyncSocket, ILibRemoteLogging_Flags_VerbosityLevel_2, "...Unblocking Chain");
		sm->PAUSE = -1;
		ILibForceUnBlockChain(sm->Transport.ChainLink.ParentChain);
	}
}

/*! \fn ILibAsyncSocket_GetSocket(ILibAsyncSocket_SocketModule module)
\brief Obtain the underlying raw socket
\param module The ILibAsyncSocket to query
\returns The pointer to raw socket
*/
void* ILibAsyncSocket_GetSocket(ILibAsyncSocket_SocketModule module)
{
	struct ILibAsyncSocketModule *sm = (struct ILibAsyncSocketModule*)module;
	return(&(sm->internalSocket));
}

void ILibAsyncSocket_Shutdown(ILibAsyncSocket_SocketModule module)
{
	ILibAsyncSocket_PrivateShutdown(module);
}

//! Sets the local endpoint associated with this ILibAsyncSocket
/*!
	\param module ILibAsyncSocket_SocketModule to modify
	\param LocalAddress Local endpoint to set
*/
void ILibAsyncSocket_SetLocalInterface(ILibAsyncSocket_SocketModule module, struct sockaddr *LocalAddress)
{
	struct ILibAsyncSocketModule *sm = (struct ILibAsyncSocketModule*)module;
	memcpy_s(&(sm->LocalAddress), sizeof(struct sockaddr_in6), LocalAddress, INET_SOCKADDR_LENGTH(LocalAddress->sa_family));
}
//! Sets the maximum size that the internal buffer can be grown
/*!
	\param module ILibAsyncSocket_SocketModule to configure
	\param maxSize Maximum size in bytes
	\param OnBufferSizeExceededCallback ILibAsyncSocket_OnBufferSizeExceeded handler to be dispatched if the max size is exceeded
	\param user Custom user state data
*/
void ILibAsyncSocket_SetMaximumBufferSize(ILibAsyncSocket_SocketModule module, int maxSize, ILibAsyncSocket_OnBufferSizeExceeded OnBufferSizeExceededCallback, void *user)
{
	struct ILibAsyncSocketModule *sm = (struct ILibAsyncSocketModule*)module;
	sm->MaxBufferSize = maxSize;
	sm->OnBufferSizeExceeded = OnBufferSizeExceededCallback;
	sm->MaxBufferSizeUserObject = user;
}
//! Sets the SendOK event handler
/*!
	\param module ILibAsyncSocket_SocketModule to configure
	\param OnSendOK ILibAsyncSocket_OnSendOK handler to dispatch on an OnSendOK event
*/
void ILibAsyncSocket_SetSendOK(ILibAsyncSocket_SocketModule module, ILibAsyncSocket_OnSendOK OnSendOK)
{
	struct ILibAsyncSocketModule *sm = (struct ILibAsyncSocketModule*)module;
	sm->OnSendOK = OnSendOK;
}

//! Determines if the specified IPv6 address is a link local address
/*!
	\param LocalAddress IPv6 Address
	\return 1 = Link Local, 0 = Not Link Local
*/
int ILibAsyncSocket_IsIPv6LinkLocal(struct sockaddr *LocalAddress)
{
	struct sockaddr_in6 *x = (struct sockaddr_in6*)LocalAddress;
#if  defined(WIN32)
	if (LocalAddress->sa_family == AF_INET6 && x->sin6_addr.u.Byte[0] == 0xFE && x->sin6_addr.u.Byte[1] == 0x80) return 1;
#else
	if (LocalAddress->sa_family == AF_INET6 && x->sin6_addr.s6_addr[0] == 0xFE && x->sin6_addr.s6_addr[1] == 0x80) return 1;
#endif
	return 0;
}
//! Determines if the internal socket is a link local socket
/*!
	\param socketModule ILibAsyncSocket_SocketModule to query
	\return 0 = Not Link Local, 1 = Link Local
*/
int ILibAsyncSocket_IsModuleIPv6LinkLocal(ILibAsyncSocket_SocketModule socketModule)
{
	struct ILibAsyncSocketModule *module = (struct ILibAsyncSocketModule*)socketModule;
	return ILibAsyncSocket_IsIPv6LinkLocal((struct sockaddr*)&(module->LocalAddress));
}
//! Returns 1 if the ILibAsyncSocket was disconnected because the buffer size was exceeded
/*!
	\param socketModule ILibAsyncSocket_SocketModule to query
	\return 1 = BufferSizeExceeded
*/
int ILibAsyncSocket_WasClosedBecauseBufferSizeExceeded(ILibAsyncSocket_SocketModule socketModule)
{
	struct ILibAsyncSocketModule *sm = (struct ILibAsyncSocketModule*)socketModule;
	return(sm->MaxBufferSizeExceeded);
}

void ILibAsyncSocket_UpdateOnData(ILibAsyncSocket_SocketModule module, ILibAsyncSocket_OnData OnData)
{
	((ILibAsyncSocketModule*)module)->OnData = OnData;
}
