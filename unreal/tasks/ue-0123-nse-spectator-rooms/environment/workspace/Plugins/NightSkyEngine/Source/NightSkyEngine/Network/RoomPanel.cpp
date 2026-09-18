#include "RoomPanel.h"

// The public widget type is available, but room controls are left to the solver.
void URoomActionButton::Activate()
{
}

void URoomPanel::Run(const FString &Operation)
{
}

void URoomPanel::UpdateDelivery(const FRoomDelivery &Delivery)
{
}

TSharedRef<SWidget> URoomPanel::RebuildWidget()
{
    return Super::RebuildWidget();
}

void URoomPanel::NativeConstruct()
{
    Super::NativeConstruct();
}

void URoomPanel::NativeDestruct()
{
    Super::NativeDestruct();
}
