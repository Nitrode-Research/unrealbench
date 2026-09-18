// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "NightSkyEngine/Network/InputDelivery.h"

#include "CoreMinimal.h"
#include "include/connection_manager.h"

/**
 * 
 */
class NIGHTSKYENGINE_API RpcConnectionManager: public ConnectionManager
{
public:
	RpcConnectionManager();
	virtual ~RpcConnectionManager() override;
	
	virtual int SendTo(const char* buffer, int len, int flags, int connection_id);
	virtual int RecvFrom(char* buffer, int len, int flags, int* connection_id);

    // Optional transport fixture gate: false retains the exact head packet.
    TFunction<bool(const TArray<int8> &)> ReceiveGate;
    TFunction<bool(const TArray<FRemoteInputChange>&)> InputDeliveryGate;
    bool ShouldDeliverInputPacket(const TArray<int8>& Packet) const;
    int playerIndex;
    TDoubleLinkedList<TArray<int8>> sendSchedule;
    TDoubleLinkedList<TArray<int8>> receiveSchedule;
};

