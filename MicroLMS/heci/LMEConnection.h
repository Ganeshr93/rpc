/* INTEL CONFIDENTIAL
 * Copyright 2011 - 2019 Intel Corporation.
 * This software and the related documents are Intel copyrighted materials, and your use of them is governed by the express license under which they were provided to you ("License"). Unless the License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or transmit this software or the related documents without Intel's prior written permission.
 * This software and the related documents are provided as is, with no express or implied warranties, other than those that are expressly stated in the License.
 */

#ifndef _MINCORE

#ifndef __LME_CONNECTION_H__
#define __LME_CONNECTION_H__

#ifdef WIN32
#include "HECIWin.h"
#else
#include "HECILinux.h"
#endif

#include "LMS_if.h"

enum INIT_STATES {
	INIT_STATE_DISCONNECTED = 0,
	INIT_STATE_CONNECTING,
	INIT_STATE_CONNECTED
};

struct LMEConnection;

typedef void (*MEICallback)(struct LMEConnection* module, void *param, void *buffer, unsigned int len);

struct LMEConnection
{
	unsigned char reqID;
	unsigned char *txBuffer;
	MEICallback cb;
	void* cbParam;
	enum INIT_STATES initState;
	unsigned char protocolVer;
	struct MEImodule mei;
	unsigned char exit;
};

struct LMEDisconnectMessage
{
	APF_MESSAGE_TYPE MessageType;
	APF_DISCONNECT_REASON_CODE ReasonCode;
};

struct LMEServiceRequestMessage
{
	APF_MESSAGE_TYPE MessageType;
	char* ServiceName;
};

typedef enum APF_REQUEST_ENUM {
	TCP_FORWARD_REQUEST,
	TCP_FORWARD_CANCEL_REQUEST,
	UDP_SEND_TO
} APF_REQUEST_TYPE;

struct LMEGlobalRequestMessage
{
	APF_MESSAGE_TYPE MessageType;
	APF_REQUEST_TYPE RequestType;
};

struct LMEProtocolVersionMessage
{
	APF_MESSAGE_TYPE MessageType;
	unsigned int MajorVersion;
	unsigned int MinorVersion;
	APF_TRIGGER_REASON TriggerReason;
};

struct LMEUserAuthRequestMessage
{
	APF_MESSAGE_TYPE MessageType;
	char* Username;
	char* ServiceName;
	char* MethodName;
	char* MethodData;
};

struct LMETcpForwardRequestMessage
{
	APF_MESSAGE_TYPE MessageType;
	APF_REQUEST_TYPE RequestType;
	char* Address;
	unsigned int Port;
};

struct LMETcpForwardCancelRequestMessage {

	APF_MESSAGE_TYPE MessageType;
	APF_REQUEST_TYPE RequestType;
	char* Address;
	unsigned int Port;
};

struct LMEUdpSendToMessage
{
	APF_MESSAGE_TYPE MessageType;
	APF_REQUEST_TYPE RequestType;
	char* Address;
	unsigned int Port;
	unsigned int DataLength;
	unsigned char *Data;
};

typedef enum APF_CHANNEL_ENUM {
	APF_CHANNEL_FORWARDED,
	APF_CHANNEL_DIRECT
} APF_CHANNEL_TYPE;

struct LMEChannelOpenRequestMessage
{
	APF_MESSAGE_TYPE MessageType;
	APF_CHANNEL_TYPE ChannelType;
	unsigned int SenderChannel;
	unsigned int InitialWindow;
	char* Address;
	unsigned int Port;
};

struct LMEChannelOpenReplySuccessMessage
{
	APF_MESSAGE_TYPE MessageType;
	unsigned int RecipientChannel;
	unsigned int SenderChannel;
	unsigned int InitialWindow;
};

struct LMEChannelOpenReplyFailureMessage
{
	APF_MESSAGE_TYPE MessageType;
	unsigned int RecipientChannel;
	OPEN_FAILURE_REASON ReasonCode;
};

struct LMEChannelCloseMessage
{
	APF_MESSAGE_TYPE MessageType;
	unsigned int RecipientChannel;
};

struct LMEChannelDataMessage
{
	APF_MESSAGE_TYPE MessageType;
	unsigned int RecipientChannel;
	unsigned int DataLength;
	unsigned char *Data;
};

struct LMEChannelWindowAdjustMessage
{
	APF_MESSAGE_TYPE MessageType;
	unsigned int RecipientChannel;
	unsigned int BytesToAdd;
};

bool LME_Init(struct LMEConnection* module, MEICallback cb, void *param);
void LME_Deinit(struct LMEConnection* module);
bool LME_IsInitialized(struct LMEConnection* module);
bool LME_Disconnect(struct LMEConnection* module, APF_DISCONNECT_REASON_CODE reasonCode);
bool LME_ServiceAccept(struct LMEConnection* module, char* serviceName);
bool LME_ProtocolVersion(struct LMEConnection* module, unsigned int majorversion, unsigned int minorversion, unsigned int triggerreason);
bool LME_TcpForwardReplySuccess(struct LMEConnection* module, unsigned int port);
bool LME_SendShortMessage(struct LMEConnection* module, unsigned char buf);
bool LME_ChannelOpenForwardedRequest(struct LMEConnection* module, unsigned int sender, char* connectedIP, unsigned int connectedPort, char* originatorIP, unsigned int originatorPort);
bool LME_ChannelOpenReplySuccess(struct LMEConnection* module, unsigned int recipient, unsigned int sender);
bool LME_ChannelOpenReplyFailure(struct LMEConnection* module, unsigned int recipient, unsigned int reason);
bool LME_ChannelClose(struct LMEConnection* module, unsigned int recipient, unsigned int sender);
int  LME_ChannelData(struct LMEConnection* module, unsigned int recipient, unsigned int len, unsigned char *buffer);
bool LME_ChannelWindowAdjust(struct LMEConnection* module, unsigned int recipient, unsigned int len);
void LME_Deinit(struct LMEConnection* module);
void LME_Exit(struct LMEConnection* module);
unsigned int LME_GetMeiBufferSize(struct LMEConnection* module);

#define LME_UserAuthSuccess(module) LME_SendShortMessage(module, APF_USERAUTH_SUCCESS)
#define LME_TcpForwardReplyFailure(module) LME_SendShortMessage(module, APF_REQUEST_FAILURE)
#define LME_TcpForwardCancelReplySuccess(module) LME_SendShortMessage(module, APF_REQUEST_SUCCESS)
#define LME_TcpForwardCancelReplyFailure(module) LME_SendShortMessage(module, APF_REQUEST_FAILURE)

#endif

#endif
