// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "Sockets.h"
#include "SocketSubsystem.h"
#include "IPAddress.h"

// MAVLink headers (C code)
extern "C" {
#include "common/mavlink.h"
}

#include "MavlinkCommunicator.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMavlinkTextMessage, const FString&, Message);

UCLASS()
class MAVLINKBRIDGE_API AMavlinkCommunicator : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AMavlinkCommunicator();

	UPROPERTY(BlueprintAssignable, Category = "MAVLinkBridge")
	FOnMavlinkTextMessage OnMavlinkTextMessage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MAVLinkBridge")
	FString ServerIP = "127.0.0.1";

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MAVLinkBridge")
	int32 ServerPort = 5763;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FSocket* Socket;
	TArray<uint8> ReceiveBuffer;

	bool ConnectToServer(FString IP, int32 Port);
	void ReceiveMavlinkData();
	void ProcessMavlinkMessage(const mavlink_message_t& Msg);
	void RequestMessageInterval(uint32 MessageID, int32 IntervalUs);
	void SendMavlinkMessage(const mavlink_message_t& Msg);
	void SendHeartbeat();
};
