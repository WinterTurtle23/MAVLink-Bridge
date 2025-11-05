// Fill out your copyright notice in the Description page of Project Settings.


#include "MavlinkCommunicator.h"
#include "Common/TcpSocketBuilder.h"
#include "Common/TcpListener.h"

// Sets default values
AMavlinkCommunicator::AMavlinkCommunicator()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;
	Socket=nullptr;
}

// Called when the game starts or when spawned
void AMavlinkCommunicator::BeginPlay()
{
	Super::BeginPlay();

	if (ConnectToServer(ServerIP, ServerPort))
	{
		UE_LOG(LogTemp, Display, TEXT("Connected to MAVLink server at %s:%d"), *ServerIP, ServerPort);

		// Request STATUSTEXT (ID 253) every 1 second
		RequestMessageInterval(MAVLINK_MSG_ID_STATUSTEXT, 1000000);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to connect to MAVLink server"));
	}
}

// Called every frame
void AMavlinkCommunicator::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	static float HeartbeatTimer = 0.0f;
	HeartbeatTimer += DeltaTime;

	if (HeartbeatTimer >= 1.0f)
	{
		SendHeartbeat();
		HeartbeatTimer = 0.0f;
	}

	if (Socket && Socket->GetConnectionState() == ESocketConnectionState::SCS_Connected)
	{
		ReceiveMavlinkData();
	}
}

void AMavlinkCommunicator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Socket)
	{
		Socket->Close();
		ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Socket);
		Socket = nullptr;
	}
	
	Super::EndPlay(EndPlayReason);
}

bool AMavlinkCommunicator::ConnectToServer(FString IP, int32 Port)
{
	ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM);
	Socket = SocketSubsystem->CreateSocket(NAME_Stream, TEXT("MAVLinkTCP"), false);

	FIPv4Address Addr;
	FIPv4Address::Parse(IP, Addr);

	TSharedRef<FInternetAddr> InternetAddr = SocketSubsystem->CreateInternetAddr();
	InternetAddr->SetIp(Addr.Value);
	InternetAddr->SetPort(Port);

	return Socket->Connect(*InternetAddr);
}

void AMavlinkCommunicator::ReceiveMavlinkData()
{
	uint32 PendingSize = 0;

	while (Socket->HasPendingData(PendingSize))
	{
		ReceiveBuffer.SetNumUninitialized(PendingSize);
		int32 BytesRead = 0;
		Socket->Recv(ReceiveBuffer.GetData(), ReceiveBuffer.Num(), BytesRead);

		mavlink_message_t Msg;
		mavlink_status_t Status;

		for (int32 i = 0; i < BytesRead; ++i)
		{
			if (mavlink_parse_char(MAVLINK_COMM_0, ReceiveBuffer[i], &Msg, &Status))
			{
				ProcessMavlinkMessage(Msg);
			}
		}
	}
}

void AMavlinkCommunicator::ProcessMavlinkMessage(const mavlink_message_t& Msg)
{
	switch (Msg.msgid)
	{
	case MAVLINK_MSG_ID_HEARTBEAT:
	{
	    mavlink_heartbeat_t hb;
	    mavlink_msg_heartbeat_decode(&Msg, &hb);

	    FString ModeName = TEXT("UNKNOWN");

	    // Map ArduPilot custom_mode values to names
	    switch (hb.custom_mode)
	    {
	    case 0:  ModeName = TEXT("STABILIZE"); break;
	    case 1:  ModeName = TEXT("ACRO"); break;
	    case 2:  ModeName = TEXT("ALT_HOLD"); break;
	    case 3:  ModeName = TEXT("AUTO"); break;
	    case 4:  ModeName = TEXT("GUIDED"); break;
	    case 5:  ModeName = TEXT("LOITER"); break;
	    case 6:  ModeName = TEXT("RTL"); break;
	    case 7:  ModeName = TEXT("CIRCLE"); break;
	    case 9:  ModeName = TEXT("LAND"); break;
	    case 11: ModeName = TEXT("DRIFT"); break;
	    case 13: ModeName = TEXT("SPORT"); break;
	    case 16: ModeName = TEXT("POSHOLD"); break;
	    case 17: ModeName = TEXT("BRAKE"); break;
	    case 18: ModeName = TEXT("THROW"); break;
	    case 19: ModeName = TEXT("AVOID_ADSB"); break;
	    case 20: ModeName = TEXT("GUIDED_NOGPS"); break;
	    case 21: ModeName = TEXT("SMART_RTL"); break;
	    default: ModeName = FString::Printf(TEXT("UNKNOWN (%d)"), hb.custom_mode); break;
	    }
	    if (hb.type == 2) {
	        UE_LOG(LogTemp, Display,
	            TEXT("Heartbeat | Type=%d | Autopilot=%d | Mode=%s | base_mode=%d | system_status=%d"),
	            hb.type, hb.autopilot, *ModeName, hb.base_mode, hb.system_status);
	        OnMavlinkTextMessage.Broadcast(*ModeName);
	    }
			
	    break;
	}
	case MAVLINK_MSG_ID_STATUSTEXT:
	{
	    mavlink_statustext_t text;
	    mavlink_msg_statustext_decode(&Msg, &text);
	    FString Message = ANSI_TO_TCHAR(text.text);

	    UE_LOG(LogTemp, Warning, TEXT("GCS Message: %s"), *Message);

	    // Broadcast to Blueprint
	    OnMavlinkTextMessage.Broadcast(*Message);
	    break;
	}
	default:
	    // Uncomment this if you want to see every message ID
	    //UE_LOG(LogTemp, Display, TEXT("MsgID: %d"), Msg.msgid);
	    break;
	}
}

void AMavlinkCommunicator::RequestMessageInterval(uint32 MessageID, int32 IntervalUs)
{
	if (!Socket)
		return;

	mavlink_message_t Msg;
	mavlink_command_long_t Cmd = {};

	Cmd.target_system = 1;
	Cmd.target_component = 1;
	Cmd.command = MAV_CMD_SET_MESSAGE_INTERVAL;
	Cmd.param1 = (float)MessageID;
	Cmd.param2 = (float)IntervalUs;
	Cmd.param7 = 1; // respond to sender

	mavlink_msg_command_long_encode(255, 0, &Msg, &Cmd);
	SendMavlinkMessage(Msg);

	UE_LOG(LogTemp, Display, TEXT("📡 Requested MsgID %d every %d µs"), MessageID, IntervalUs);
}

void AMavlinkCommunicator::SendMavlinkMessage(const mavlink_message_t& Msg)
{
	if (!Socket)
		return;

	uint8 Buffer[MAVLINK_MAX_PACKET_LEN];
	uint16 Len = mavlink_msg_to_send_buffer(Buffer, &Msg);

	int32 BytesSent = 0;
	Socket->Send(Buffer, Len, BytesSent);
}

void AMavlinkCommunicator::SendHeartbeat()
{
	mavlink_message_t Msg;
	mavlink_heartbeat_t Heartbeat = {};

	Heartbeat.type = MAV_TYPE_GCS;
	Heartbeat.autopilot = MAV_AUTOPILOT_INVALID;
	Heartbeat.base_mode = MAV_MODE_FLAG_CUSTOM_MODE_ENABLED;
	Heartbeat.system_status = MAV_STATE_ACTIVE;

	mavlink_msg_heartbeat_encode(255, 0, &Msg, &Heartbeat);
	SendMavlinkMessage(Msg);

	UE_LOG(LogTemp, Display, TEXT("Sent Heartbeat"));
}

