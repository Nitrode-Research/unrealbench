// Fill out your copyright notice in the Description page of Project Settings.


#include "RpcConnectionManager.h"


RpcConnectionManager::RpcConnectionManager()
{
	playerIndex = 0;
}

RpcConnectionManager::~RpcConnectionManager()
{
}

int RpcConnectionManager::SendTo(const char* buffer, int len, int flags, int connection_id)
{
	const TArray scheduledMessage((int8*)buffer,len);
	sendSchedule.AddTail(scheduledMessage);

	return 0;
}

int RpcConnectionManager::RecvFrom(char* buffer, int len, int flags, int* connection_id)
{
	if (receiveSchedule.Num() == 0)
		return -1;
    auto msg = receiveSchedule.GetHead();
    if ((ReceiveGate && !ReceiveGate(msg->GetValue())) ||
        !ShouldDeliverInputPacket(msg->GetValue()))
    {
        return -1;
    }

    auto msgVal = msg->GetValue();
    auto rec = (char *)msgVal.GetData();
    auto leng = msgVal.Num(); // int* to char* size
    if (leng == 0 || leng > len)
    {
        return -1;
    }
    memcpy(buffer, rec, leng);
    receiveSchedule.RemoveNode(msg);
    *connection_id = playerIndex;
    return leng;
}

bool RpcConnectionManager::ShouldDeliverInputPacket(const TArray<int8>& Packet) const
{
    if (!InputDeliveryGate || Packet.Num() < 32 || uint8(Packet[4]) != 3 ||
        (uint8(Packet[29]) == 0 && uint8(Packet[30]) == 0))
    {
        return true;
    }
    // GGPO adapter only: translate its wire records to the public semantic gate.
    uint32 StartFrame = 0;
    for (int32 Byte = 0; Byte < 4; ++Byte)
    {
        StartFrame |= uint32(uint8(Packet[21 + Byte])) << (Byte * 8);
    }
    const int32 NumBits = uint8(Packet[29]) | (int32(uint8(Packet[30])) << 8);
    if (NumBits > (Packet.Num() - 32) * 8)
    {
        return true;
    }
    int32 Offset = 0;
    int32 InputFrame = int32(StartFrame);
    auto ReadBit = [&]() {
        const int32 Bit = (uint8(Packet[32 + Offset / 8]) >> (Offset % 8)) & 1;
        ++Offset;
        return Bit;
    };
    TArray<FRemoteInputChange> Inputs;
    while (Offset < NumBits)
    {
        uint32 Pressed = 0;
        while (ReadBit())
        {
            if (Offset + 9 > NumBits)
            {
                return true;
            }
            const int32 On = ReadBit();
            int32 Button = 0;
            for (int32 Bit = 0; Bit < 8; ++Bit)
            {
                Button |= ReadBit() << Bit;
            }
            if (On && Button < 32)
            {
                Pressed |= uint32(1) << Button;
            }
            if (Offset >= NumBits)
            {
                return true;
            }
        }
        Inputs.Add({InputFrame, Pressed});
        ++InputFrame;
    }
    return InputDeliveryGate(Inputs);
}
