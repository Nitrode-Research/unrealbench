#pragma once
#include "CoreMinimal.h"

// Transport-independent description of real remote input records awaiting delivery.
struct FRemoteInputChange
{
    int32 Frame = 0;
    uint32 Pressed = 0;
};
