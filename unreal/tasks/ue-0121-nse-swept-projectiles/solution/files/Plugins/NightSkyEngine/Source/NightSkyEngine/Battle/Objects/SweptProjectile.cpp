#include "BattleObject.h"
#include "PlayerObject.h"
#include "../Misc/NightSkyBlueprintFunctionLibrary.h"

namespace NightSkySweep
{
// Projections fit in int64 at the published coordinate bound. Compare fractions
// by continued fractions so their cross products never overflow, including on MSVC.
bool PositiveFractionLess(uint64 Left, uint64 LeftDenominator, uint64 Right, uint64 RightDenominator)
{
	bool Reversed = false;
	for (;;)
	{
		const uint64 LeftQuotient = Left / LeftDenominator, RightQuotient = Right / RightDenominator;
		if (LeftQuotient != RightQuotient)
		{
			return Reversed ? LeftQuotient > RightQuotient : LeftQuotient < RightQuotient;
		}
		const uint64 LeftRemainder = Left % LeftDenominator, RightRemainder = Right % RightDenominator;
		if (!LeftRemainder || !RightRemainder)
		{
			return Reversed ? LeftRemainder > RightRemainder : LeftRemainder < RightRemainder;
		}
		// Equal integer parts leave the remainders. Reciprocation reverses ordering.
		Left = LeftDenominator;
		LeftDenominator = LeftRemainder;
		Right = RightDenominator;
		RightDenominator = RightRemainder;
		Reversed = !Reversed;
	}
}
struct Time
{
	int64 Numerator = 0, Denominator = 1;
	Time() = default;
	Time(int64 Num, int64 Den) : Numerator(Den < 0 ? -Num : Num), Denominator(Den < 0 ? -Den : Den)
	{
	}
	bool operator<(const Time& Right) const
	{
		if ((Numerator < 0) != (Right.Numerator < 0))
		{
			return Numerator < 0;
		}
		const uint64 LeftMagnitude = Numerator < 0 ? uint64(-(Numerator + 1)) + 1 : uint64(Numerator);
		const uint64 RightMagnitude =
			Right.Numerator < 0 ? uint64(-(Right.Numerator + 1)) + 1 : uint64(Right.Numerator);
		return Numerator < 0 ? PositiveFractionLess(RightMagnitude, uint64(Right.Denominator), LeftMagnitude,
													uint64(Denominator))
							 : PositiveFractionLess(LeftMagnitude, uint64(Denominator), RightMagnitude,
													uint64(Right.Denominator));
	}
};
struct Point
{
	int64 X, Y;
};
int64 Dot(Point Left, Point Right)
{
	return Left.X * Right.X + Left.Y * Right.Y;
}
void BuildVertices(const ABattleObject* Object, const FCollisionBox& Box, bool bEndpointOnly,
				   Point (&OutVertices)[4])
{
	const int64 HalfWidth = Box.SizeX / 2, HalfHeight = Box.SizeY / 2;
	const int64 DirectedAngle =
		Object->Direction == DIR_Right ? int64(Object->AnglePitch_x1000) : -int64(Object->AnglePitch_x1000);
	const int32 Angle = int32((DirectedAngle % 360000 + 360000) % 360000);
	const int64 Cosine = UNightSkyBlueprintFunctionLibrary::Cos_x1000(Angle / 100);
	const int64 Sine = UNightSkyBlueprintFunctionLibrary::Sin_x1000(Angle / 100);
	const int64 X = bEndpointOnly ? Object->PosX : Object->SweepStartX;
	const int64 Y = bEndpointOnly ? Object->PosY : Object->SweepStartY;
	const int64 FacingOffsetX = Object->Direction == DIR_Right ? Box.PosX : -int64(Box.PosX);
	Point Local[4] = {{-HalfWidth, -HalfHeight},
					  {HalfWidth, -HalfHeight},
					  {HalfWidth, HalfHeight},
					  {-HalfWidth, HalfHeight}};
	for (int VertexIndex = 0; VertexIndex < 4; ++VertexIndex)
	{
		const int64 LocalX = Local[VertexIndex].X + FacingOffsetX, LocalY = Local[VertexIndex].Y + Box.PosY;
		OutVertices[VertexIndex] = {LocalX * Cosine / 1000 - LocalY * Sine / 1000 + X,
									LocalX * Sine / 1000 + LocalY * Cosine / 1000 + Y};
	}
}
bool FindPairContactTime(const ABattleObject* Left, const FCollisionBox& LeftBox, const ABattleObject* Right,
						 const FCollisionBox& RightBox, bool bEndpointOnly, Time& First)
{
	Point LeftVertices[4], RightVertices[4];
	BuildVertices(Left, LeftBox, bEndpointOnly, LeftVertices);
	BuildVertices(Right, RightBox, bEndpointOnly, RightVertices);
	Point Velocity = bEndpointOnly
						 ? Point{0, 0}
						 : Point{int64(Left->PosX) - Left->SweepStartX - Right->PosX + Right->SweepStartX,
								 int64(Left->PosY) - Left->SweepStartY - Right->PosY + Right->SweepStartY};
	TArray<Point, TInlineAllocator<18>> Axes;
	Axes.Add({1, 0});
	Axes.Add({0, 1});
	// Edge normals separate full polygons. Edge directions also separate collinear
	// segments. Coordinate axes cover the point/point case with no surviving edges.
	for (const Point* V : {LeftVertices, RightVertices})
	{
		for (int VertexIndex = 0; VertexIndex < 4; ++VertexIndex)
		{
			Point Edge{V[(VertexIndex + 1) % 4].X - V[VertexIndex].X,
					   V[(VertexIndex + 1) % 4].Y - V[VertexIndex].Y};
			if (Edge.X || Edge.Y)
			{
				Axes.Add({-Edge.Y, Edge.X});
				Axes.Add(Edge);
			}
		}
	}
	Time Enter(0, 1), Exit(1, 1);
	for (Point Axis : Axes)
	{
		int64 LeftMinimum = Dot(LeftVertices[0], Axis), LeftMaximum = LeftMinimum,
			  RightMinimum = Dot(RightVertices[0], Axis), RightMaximum = RightMinimum;
		for (int VertexIndex = 1; VertexIndex < 4; ++VertexIndex)
		{
			LeftMinimum = FMath::Min(LeftMinimum, Dot(LeftVertices[VertexIndex], Axis));
			LeftMaximum = FMath::Max(LeftMaximum, Dot(LeftVertices[VertexIndex], Axis));
			RightMinimum = FMath::Min(RightMinimum, Dot(RightVertices[VertexIndex], Axis));
			RightMaximum = FMath::Max(RightMaximum, Dot(RightVertices[VertexIndex], Axis));
		}
		const int64 Speed = Dot(Velocity, Axis);
		if (!Speed)
		{
			if (LeftMaximum < RightMinimum || RightMaximum < LeftMinimum)
			{
				return false;
			}
			continue;
		}
		Time LowerBound(RightMinimum - LeftMaximum, Speed), UpperBound(RightMaximum - LeftMinimum, Speed);
		if (UpperBound < LowerBound)
		{
			Swap(LowerBound, UpperBound);
		}
		if (Enter < LowerBound)
		{
			Enter = LowerBound;
		}
		if (UpperBound < Exit)
		{
			Exit = UpperBound;
		}
		if (Exit < Enter)
		{
			return false;
		}
	}
	First = bEndpointOnly ? Time(1, 1) : Enter;
	return true;
}
} // namespace NightSkySweep

void ABattleObject::HandleSweptContacts(const TArray<ABattleObject*>& Candidates)
{
	using namespace NightSkySweep;
	if (IsPlayer || !IsActive || (MiscFlags & MISC_DeactivateOnNextUpdate))
	{
		return;
	}
	// Reconfiguration preserves consumed contacts, so a lowered limit may already
	// be exhausted before another candidate is accepted.
	if (SweepConsumed >= SweepLimit)
	{
		ResetObject();
		return;
	}
	struct FPendingContact
	{
		ABattleObject* Target;
		Time ContactTime;
		int32 Key;
		uint64 TargetActivation;
	};
	TArray<FPendingContact> Contacts;
	for (auto* Target : Candidates)
	{
		if (!Target || Target == this || (!Target->IsPlayer && !Target->IsActive))
		{
			continue;
		}
		const bool bEndpointOnly =
			!SweepFrameReady || !Target->SweepFrameReady || SweepTeleported || Target->SweepTeleported;
		bool bFoundContact = false;
		Time Earliest;
		for (const auto& Hit : Boxes)
		{
			if (Hit.Type == BOX_Hit && Hit.SizeX > 0 && Hit.SizeY > 0)
			{
				for (const auto& Hurt : Target->Boxes)
				{
					if (Hurt.Type == BOX_Hurt && Hurt.SizeX > 0 && Hurt.SizeY > 0)
					{
						Time PairTime;
						if (FindPairContactTime(this, Hit, Target, Hurt, bEndpointOnly, PairTime) &&
							(!bFoundContact || PairTime < Earliest))
						{
							bFoundContact = true;
							Earliest = PairTime;
						}
					}
				}
			}
		}
		if (bFoundContact)
		{
			Contacts.Add({Target, Earliest, Target->ContactOrderKey, Target->SweepActivation});
		}
	}
	Contacts.Sort(
		[](const FPendingContact& Left, const FPendingContact& Right)
		{
			if (Left.ContactTime < Right.ContactTime)
			{
				return true;
			}
			if (Right.ContactTime < Left.ContactTime)
			{
				return false;
			}
			return Left.Key < Right.Key;
		});
	const uint64 ProjectileActivation = SweepActivation;
	for (const FPendingContact& Contact : Contacts)
	{
		if (SweepActivation != ProjectileActivation || !IsActive || (MiscFlags & MISC_DeactivateOnNextUpdate) || !(AttackFlags & ATK_IsAttacking) ||
			!(AttackFlags & ATK_HitActive))
		{
			break;
		}
		ABattleObject* Target = Contact.Target;
		if (Target->SweepActivation != Contact.TargetActivation ||
			(!Target->IsPlayer && !Target->IsActive) || (Target->MiscFlags & MISC_DeactivateOnNextUpdate) ||
			!Player || !Target->Player)
		{
			continue;
		}
		if (Player->PlayerIndex == Target->Player->PlayerIndex || !(Player->PlayerFlags & PLF_IsOnScreen) ||
			!(Target->Player->PlayerFlags & PLF_IsOnScreen))
		{
			continue;
		}
		FSweptTargetIdentity TargetIdentity;
		TargetIdentity.ObjectSlot = Target->ObjNumber;
		TargetIdentity.Activation = Target->SweepActivation;
		if (SweepVictims.Contains(TargetIdentity))
		{
			continue;
		}
		if (Target->ObjectsToIgnoreHitsFrom.Contains(this) || Target->Player->IsInvulnerable(this))
		{
			continue;
		}
		// Geometry is only a candidate. Ordinary scripted hit-collision vetoes
		// must not consume lifetime capacity or mark the target as accepted.
		const bool bAcceptedContact = TryHandleHitCollision(Target, true);
		if (SweepActivation != ProjectileActivation || !IsActive) break;
		Target->ObjectsToIgnoreHitsFrom.Remove(this);
		if (!bAcceptedContact) continue;
		SweepVictims.Add(TargetIdentity);
		++SweepConsumed;
		if (!SweepPiercing || SweepConsumed >= SweepLimit)
		{
			ResetObject();
			break;
		}
	}
}
