#include "BattleObject.h"

void ABattleObject::ConfigureSweptProjectile(bool Enabled, bool Piercing, int32 ContactLimit)
{
	// Implement opt-in swept contacts here. The starter retains endpoint collision.
}

int32 ABattleObject::GetSweptContactLimit() const
{
	// Report the effective limit once swept contacts are implemented.
	return 0;
}

void ABattleObject::SetContactOrderKey(int32 Key)
{
	ContactOrderKey = FMath::Max(0, Key);
}

void ABattleObject::TeleportBattlePosition(int32 X, int32 Y)
{
	PosX = X;
	PosY = Y;
}
