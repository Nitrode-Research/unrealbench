#include "CoreMinimal.h"
#include "Engine/World.h"
#include "Components/LineBatchComponent.h"
#include "Misc/AutomationTest.h"
#include "NightSkyEngine/Fixtures/NSE005Fixture.h"
#include "NightSkyEngine/Battle/NightSkyGameState.h"
#include "NightSkyEngine/Battle/Objects/PlayerObject.h"

namespace
{
TArray<int32> Accepted(const FNSE005Battle& Battle)
{
	TArray<int32> Result;
	for (int32 Key : Battle.Trace)
	{
		if (Key >= 0)
		{
			Result.Add(Key);
		}
	}
	return Result;
}
void AssertContacts(FAutomationTestBase& Test, const FNSE005Battle& Battle, const TArray<int32>& Expected)
{
	const auto Actual = Accepted(Battle);
	Test.TestEqual(TEXT("accepted callback count"), Actual.Num(), Expected.Num());
	for (int I = 0; I < FMath::Min(Actual.Num(), Expected.Num()); ++I)
	{
		Test.TestEqual(FString::Printf(TEXT("contact %d"), I), Actual[I], Expected[I]);
	}
	for (auto* Target : Battle.Targets)
	{
		const bool Hit = Expected.Contains(Target->ContactOrderKey);
		Test.TestEqual(TEXT("ordinary damage"), Target->CurrentHealth, Hit ? 9900 : 10000);
		int Receives = 0;
		for (int Key : Battle.Trace)
		{
			if (Key == -Target->ContactOrderKey - 1)
			{
				++Receives;
			}
		}
		Test.TestEqual(TEXT("receive callbacks are not duplicated"), Receives, Hit ? 1 : 0);
		if (Hit)
		{
			Test.TestEqual(TEXT("ordinary hitstop"), Target->Hitstop, 3);
		}
	}
}
} // namespace
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE005V1, "UnrealBench.NSE005.V1",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE005V1::RunTest(const FString&)
{
	{
		FNSE005Battle Battle;
		Battle.Projectile->ConfigureSweptProjectile(false, false, 1);
		Battle.Move(Battle.Projectile, 50);
		Battle.Step();
		AssertContacts(*this, Battle, {1});
		AddInfo(FString::Printf(TEXT("Endpoint control health=%d hitstop=%d hit callbacks=%d"),
								Battle.Targets[0]->CurrentHealth, Battle.Targets[0]->Hitstop,
								Accepted(Battle).Num()));
	}
	{
		FNSE005Battle Battle;
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {1});
		TestFalse(TEXT("nonpiercing deactivates"), Battle.Projectile->IsActive);
	}
	{
		FNSE005Battle Battle;
		Battle.Projectile->ConfigureSweptProjectile(false, false, 1);
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {});
	}
	{
		FNSE005Battle Battle;
		Battle.Targets[0]->PosX = 100;
		Battle.Move(Battle.Projectile, 100);
		Battle.Move(Battle.Targets[0], -100);
		Battle.Step();
		AssertContacts(*this, Battle, {1});
	}
	{
		FNSE005Battle Battle;
		Battle.Move(Battle.Projectile, 100);
		Battle.Move(Battle.Targets[0], 100);
		Battle.Step();
		AssertContacts(*this, Battle, {});
	}
	{
		FNSE005Battle Battle;
		Battle.Targets[0]->PosY = 103;
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {});
	}
	return !HasAnyErrors();
}
namespace Oracle
{
struct FOraclePoint
{
	int64 X, Y;
	FOraclePoint operator-(FOraclePoint B) const
	{
		return {X - B.X, Y - B.Y};
	}
};
int64 Cross(FOraclePoint A, FOraclePoint B)
{
	return A.X * B.Y - A.Y * B.X;
}
// The oracle uses exact two-word multiplication. Every partial product uses standard uint64.
struct WideProduct
{
	uint64 High, Low;
	WideProduct(uint64 A, uint64 B)
	{
		const uint64 Mask = 0xffffffffULL;
		const uint64 A0 = A & Mask, A1 = A >> 32, B0 = B & Mask, B1 = B >> 32;
		const uint64 W0 = A0 * B0, T = A1 * B0 + (W0 >> 32);
		const uint64 W1 = (T & Mask) + A0 * B1;
		High = A1 * B1 + (T >> 32) + (W1 >> 32);
		Low = (W1 << 32) | (W0 & Mask);
	}
	bool operator<(const WideProduct& B) const
	{
		return High == B.High ? Low < B.Low : High < B.High;
	}
};
struct FOracleFraction
{
	int64 Numerator = 0, Denominator = 1;
	FOracleFraction() = default;
	FOracleFraction(int64 A, int64 B) : Numerator(B < 0 ? -A : A), Denominator(B < 0 ? -B : B)
	{
	}
	bool operator<(FOracleFraction B) const
	{
		if ((Numerator < 0) != (B.Numerator < 0))
		{
			return Numerator < 0;
		}
		const uint64 LeftMagnitude = Numerator < 0 ? uint64(-(Numerator + 1)) + 1 : uint64(Numerator);
		const uint64 RightMagnitude = B.Numerator < 0 ? uint64(-(B.Numerator + 1)) + 1 : uint64(B.Numerator);
		const WideProduct Left(LeftMagnitude, uint64(B.Denominator)),
			Right(RightMagnitude, uint64(Denominator));
		return Numerator < 0 ? Right < Left : Left < Right;
	}
};
TArray<FOraclePoint> Hull(TArray<FOraclePoint> Points)
{
	Points.Sort([](FOraclePoint A, FOraclePoint B) { return A.X == B.X ? A.Y < B.Y : A.X < B.X; });
	TArray<FOraclePoint> Unique;
	for (FOraclePoint X : Points)
	{
		if (Unique.IsEmpty() || X.X != Unique.Last().X || X.Y != Unique.Last().Y)
		{
			Unique.Add(X);
		}
	}
	if (Unique.Num() < 3)
	{
		return Unique;
	}
	TArray<FOraclePoint> ConvexHull;
	for (FOraclePoint X : Unique)
	{
		while (ConvexHull.Num() > 1 &&
			   Cross(ConvexHull.Last() - ConvexHull[ConvexHull.Num() - 2], X - ConvexHull.Last()) <= 0)
		{
			ConvexHull.Pop(EAllowShrinking::No);
		}
		ConvexHull.Add(X);
	}
	const int LowerHullSize = ConvexHull.Num();
	for (int VertexIndex = Unique.Num() - 2; VertexIndex >= 0; --VertexIndex)
	{
		FOraclePoint X = Unique[VertexIndex];
		while (ConvexHull.Num() > LowerHullSize &&
			   Cross(ConvexHull.Last() - ConvexHull[ConvexHull.Num() - 2], X - ConvexHull.Last()) <= 0)
		{
			ConvexHull.Pop(EAllowShrinking::No);
		}
		ConvexHull.Add(X);
	}
	ConvexHull.Pop(EAllowShrinking::No);
	return ConvexHull;
}
bool Contact(TArray<FOraclePoint> A, TArray<FOraclePoint> B, FOraclePoint Velocity, FOracleFraction& First)
{
	TArray<FOraclePoint> Difference;
	for (FOraclePoint Y : B)
	{
		for (FOraclePoint X : A)
		{
			Difference.Add(Y - X);
		}
	}
	auto ConvexHull = Hull(Difference);
	FOracleFraction LowerBound(0, 1), UpperBound(1, 1);
	auto AtLeast = [&](int64 Coefficient, int64 Value)
	{
		if (Coefficient == 0)
		{
			return Value <= 0;
		}
		FOracleFraction Bound(Value, Coefficient);
		if (Coefficient > 0)
		{
			if (LowerBound < Bound)
			{
				LowerBound = Bound;
			}
		}
		else if (Bound < UpperBound)
		{
			UpperBound = Bound;
		}
		return !(UpperBound < LowerBound);
	};
	if (ConvexHull.Num() == 1)
	{
		if (!AtLeast(Velocity.X, ConvexHull[0].X) || !AtLeast(-Velocity.X, -ConvexHull[0].X) ||
			!AtLeast(Velocity.Y, ConvexHull[0].Y) || !AtLeast(-Velocity.Y, -ConvexHull[0].Y))
		{
			return false;
		}
	}
	else if (ConvexHull.Num() == 2)
	{
		FOraclePoint E = ConvexHull[1] - ConvexHull[0];
		int64 C = Cross(E, Velocity), R = Cross(E, ConvexHull[0]);
		if (!AtLeast(C, R) || !AtLeast(-C, -R))
		{
			return false;
		}
		if (!AtLeast(Velocity.X, FMath::Min(ConvexHull[0].X, ConvexHull[1].X)) ||
			!AtLeast(-Velocity.X, -FMath::Max(ConvexHull[0].X, ConvexHull[1].X)) ||
			!AtLeast(Velocity.Y, FMath::Min(ConvexHull[0].Y, ConvexHull[1].Y)) ||
			!AtLeast(-Velocity.Y, -FMath::Max(ConvexHull[0].Y, ConvexHull[1].Y)))
		{
			return false;
		}
	}
	else
	{
		for (int VertexIndex = 0; VertexIndex < ConvexHull.Num(); ++VertexIndex)
		{
			FOraclePoint E = ConvexHull[(VertexIndex + 1) % ConvexHull.Num()] - ConvexHull[VertexIndex];
			if (!AtLeast(Cross(E, Velocity), Cross(E, ConvexHull[VertexIndex])))
			{
				return false;
			}
		}
	}
	First = LowerBound;
	return true;
}
// Independently published table-sampled coefficients. Values are floor(1000*sin(theta))
// at the pinned table's tenth-degree samples; no production geometry/trig is invoked.
const int Angles[] = {0, 17300, 31700, -62400, 391700, -422400};
const int Coefficients[][4] = {{999, 0, 999, 0},	  {954, 297, 954, -295}, {849, 525, 850, -523},
							   {463, -885, 461, 886}, {849, 525, 850, -523}, {463, -885, 461, 886}};
TArray<FOraclePoint> Shape(int X, int Y, const FCollisionBox& Box, int Rotation, bool Left)
{
	const int64 Cosine = Coefficients[Rotation][Left ? 2 : 0], Sine = Coefficients[Rotation][Left ? 3 : 1];
	TArray<FOraclePoint> Result;
	for (FOraclePoint Corner : TArray<FOraclePoint>{{-Box.SizeX / 2, -Box.SizeY / 2},
													{Box.SizeX / 2, -Box.SizeY / 2},
													{Box.SizeX / 2, Box.SizeY / 2},
													{-Box.SizeX / 2, Box.SizeY / 2}})
	{
		const int64 LocalX = Corner.X + (Left ? -int64(Box.PosX) : Box.PosX), LocalY = Corner.Y + Box.PosY;
		Result.Add({X + LocalX * Cosine / 1000 - LocalY * Sine / 1000,
					Y + LocalX * Sine / 1000 + LocalY * Cosine / 1000});
	}
	return Result;
}
TArray<FOraclePoint> Rect(int X, int Y, int Width, int Height)
{
	const int HalfWidth = (Width / 2) * 999 / 1000, HalfHeight = (Height / 2) * 999 / 1000;
	return {{X - HalfWidth, Y - HalfHeight},
			{X + HalfWidth, Y - HalfHeight},
			{X + HalfWidth, Y + HalfHeight},
			{X - HalfWidth, Y + HalfHeight}};
}
} // namespace Oracle
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE005V2, "UnrealBench.NSE005.V2",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE005V2::RunTest(const FString&)
{
	struct Scenario
	{
		int AttackWidth, AttackHeight, HurtWidth, HurtHeight, X, Y, PendingMoveX, PendingMoveY, TargetMoveX,
			TargetMoveY;
	};
	const Scenario Cases[] = {{4, 4, 4, 4, 102, 100, 100, 0, 0, 0},	 {4, 4, 4, 4, 50, 102, 100, 0, 0, 0},
							  {4, 4, 4, 4, 50, 103, 100, 0, 0, 0},	 {4, 4, 4, 4, 102, 102, 100, 100, 0, 0},
							  {5, 7, 7, 5, 50, 103, 100, 0, 0, 0},	 {5, 7, 7, 5, 50, 104, 100, 0, 0, 0},
							  {2, 2, 2, 2, 0, 100, 0, 0, 0, 0},		 {2, 2, 2, 2, 102, 100, 100, 0, 0, 0},
							  {2, 2, 2, 2, 50, 102, 100, 0, 0, 0},	 {2, 2, 2, 2, 50, 103, 100, 0, 0, 0},
							  {3, 5, 5, 3, 50, 101, 100, 0, 0, 0},	 {3, 5, 5, 3, 50, 102, 100, 0, 0, 0},
							  {5, 3, 3, 5, 50, 101, 100, 0, 0, 0},	 {5, 3, 3, 5, 50, 102, 100, 0, 0, 0},
							  {3, 5, 5, 3, 50, 103, 100, 0, 0, 0},	 {3, 5, 5, 3, 50, 104, 100, 0, 0, 0},
							  {1, 1, 1, 1, 5, 100, 10, 0, 0, 0},	 {1, 1, 1, 1, 5, 101, 10, 0, 0, 0},
							  {5, 1, 5, 1, 2, 100, 0, 0, 0, 0},		 {5, 1, 5, 1, 3, 100, 1, 0, 0, 0},
							  {5, 1, 5, 1, 4, 100, 1, 0, 0, 0},		 {5, 1, 5, 1, 5, 100, 0, 0, 0, 0},
							  {5, 1, 5, 1, 5, 100, 1, 0, 0, 0},		 {1, 5, 5, 1, 5, 100, 10, 0, 0, 0},
							  {5, 1, 1, 5, 5, 100, 10, 0, 0, 0},	 {1, 1, 1, 5, 0, 101, 0, 0, 0, 0},
							  {1, 1, 1, 5, 0, 102, 0, 0, 0, 0},		 {1, 1, 1, 5, 0, 103, 0, 0, 0, 0},
							  {1, 5, 1, 5, 1, 100, 0, 0, 0, 0},		 {1, 5, 1, 5, 0, 100, 0, 0, 0, 0},
							  {2, 2, 1, 1, 50, 100, 0, 0, -100, 0},	 {1, 1, 2, 2, 50, 100, 0, 0, -100, 0},
							  {1, 5, 2, 2, 50, 100, 0, 0, -100, 0},	 {2, 2, 5, 1, 50, 100, 0, 0, -100, 0},
							  {2, 2, 2, 2, 50, 100, 100, 100, 0, 0}, {2, 2, 2, 2, 50, 150, 100, 100, 0, 0}};
	for (const auto& C : Cases)
	{
		for (int Shift : {0, 9999700, -9999700})
		{
			FNSE005Battle Battle;
			Battle.Projectile->PosX = Shift;
			Battle.Targets[0]->PosX = C.X + Shift;
			Battle.Targets[0]->PosY = C.Y;
			Battle.Boxes(Battle.Projectile, {Battle.Box(C.AttackWidth, C.AttackHeight, BOX_Hit)});
			Battle.Boxes(Battle.Targets[0], {Battle.Box(C.HurtWidth, C.HurtHeight)});
			Battle.Move(Battle.Projectile, C.PendingMoveX, C.PendingMoveY);
			Battle.Move(Battle.Targets[0], C.TargetMoveX, C.TargetMoveY);
			Oracle::FOracleFraction Time;
			const bool Hit =
				Oracle::Contact(Oracle::Rect(Shift, 100, C.AttackWidth, C.AttackHeight),
								Oracle::Rect(C.X + Shift, C.Y, C.HurtWidth, C.HurtHeight),
								{C.PendingMoveX - C.TargetMoveX, C.PendingMoveY - C.TargetMoveY}, Time);
			Battle.Step();
			AssertContacts(*this, Battle, Hit ? TArray<int32>{1} : TArray<int32>{});
		}
	}
	for (int X : {1, 13})
	{
		FNSE005Battle Battle;
		Battle.Projectile->AnglePitch_x1000 = 45000;
		Battle.Boxes(Battle.Projectile, {Battle.Box(20, 20, BOX_Hit)});
		Battle.Boxes(Battle.Targets[0], {Battle.Box(1, 1)});
		Battle.Targets[0]->PosX = X;
		Battle.Targets[0]->PosY = 113;
		Battle.Projectile->PosX = -30;
		Battle.Move(Battle.Projectile, 30);
		// Published 45 degree fixture has local vertices (0,-14),(14,0),(0,14),(-14,0).
		Battle.Step();
		AssertContacts(*this, Battle, X == 1 ? TArray<int32>{1} : TArray<int32>{});
	}
	for (int Distance : {40, 50})
	{
		FNSE005Battle Battle;
		Battle.Targets[0]->AnglePitch_x1000 = 45000;
		Battle.Boxes(Battle.Targets[0], {Battle.Box(20, 20)});
		Battle.Boxes(Battle.Projectile, {Battle.Box(1, 1, BOX_Hit)});
		Battle.Projectile->PosY = 113;
		Battle.Move(Battle.Projectile, Distance);
		Battle.Step();
		AssertContacts(*this, Battle, Distance == 50 ? TArray<int32>{1} : TArray<int32>{});
	}
	for (bool Left : {false, true})
	{
		FNSE005Battle Battle;
		Battle.Projectile->Direction = Left ? DIR_Left : DIR_Right;
		Battle.Projectile->AnglePitch_x1000 = 90000;
		Battle.Boxes(Battle.Projectile, {Battle.Box(1, 5, BOX_Hit, 10, 0)});
		Battle.Targets[0]->PosY = 109;
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {1});
	}
	// Automatically shrunk seed91873 episode0 offset-mutant counterexample.
	for (int OffsetY : {-2, 0})
	{
		FNSE005Battle Battle;
		Battle.Projectile->AnglePitch_x1000 = -422400;
		Battle.Targets[0]->AnglePitch_x1000 = 391700;
		Battle.Targets[0]->PosX = 0;
		Battle.Targets[0]->PosY = 100;
		const auto A = Battle.Box(1, 1, BOX_Hit), H = Battle.Box(1, 1, BOX_Hurt, 0, OffsetY);
		Battle.Boxes(Battle.Projectile, {A});
		Battle.Boxes(Battle.Targets[0], {H});
		Oracle::FOracleFraction Time;
		const bool Hit = Oracle::Contact(Oracle::Shape(0, 100, A, 5, false),
										 Oracle::Shape(0, 100, H, 4, false), {0, 0}, Time);
		Battle.Step();
		AssertContacts(*this, Battle, Hit ? TArray<int32>{1} : TArray<int32>{});
		TestEqual(FString::Printf(TEXT("shrunk seed91873 episode0 replay offsetY=%d contact count"), OffsetY),
				  Accepted(Battle).Num(), Hit ? 1 : 0);
		TestEqual(TEXT("shrunk scene and reachable companion"), Hit, OffsetY == 0);
	}
	// Short paths make offsets, facing, and either rotation independently observable.
	int RotatedHits = 0, RotatedMisses = 0;
	for (int AttackRotation = 1; AttackRotation < 6; ++AttackRotation)
	{
		for (int HurtRotation = 1; HurtRotation < 6; ++HurtRotation)
		{
			for (bool bAttackFacingLeft : {false, true})
			{
				for (bool bHurtFacingLeft : {false, true})
				{
					for (int Y : {75, 90, 105, 120})
					{
						FNSE005Battle Battle;
						const auto A = Battle.Box(17, 7, BOX_Hit, 19, -11),
								   H = Battle.Box(9, 23, BOX_Hurt, -13, 17);
						Battle.Projectile->AnglePitch_x1000 = Oracle::Angles[AttackRotation];
						Battle.Targets[0]->AnglePitch_x1000 = Oracle::Angles[HurtRotation];
						Battle.Projectile->Direction = bAttackFacingLeft ? DIR_Left : DIR_Right;
						Battle.Targets[0]->Direction = bHurtFacingLeft ? DIR_Left : DIR_Right;
						Battle.Targets[0]->PosX = 20;
						Battle.Targets[0]->PosY = Y;
						Battle.Boxes(Battle.Projectile, {A});
						Battle.Boxes(Battle.Targets[0], {H});
						if (AttackRotation == HurtRotation && !bAttackFacingLeft && bHurtFacingLeft &&
							Y == 90)
						{
							Battle.Projectile->CollisionView();
							Battle.Targets[0]->CollisionView();
							// Capture the real diagnostic renderer's submitted lines, not a test drawing
							// substitute.
							if (const auto* Batch =
									Battle.World->GetLineBatcher(UWorld::ELineBatcherType::World))
							{
								for (int I = 0; I < Batch->BatchedLines.Num(); ++I)
								{
									const auto& Line = Batch->BatchedLines[I];
									AddInfo(FString::Printf(
										TEXT("NSE005_VIS angle=%d line=%d from=%.8f,%.8f to=%.8f,%.8f"),
										Oracle::Angles[AttackRotation], I, Line.Start.X, Line.Start.Z,
										Line.End.X, Line.End.Z));
								}
							}
							for (const auto P : Oracle::Shape(0, 100, A, AttackRotation, bAttackFacingLeft))
							{
								AddInfo(
									FString::Printf(TEXT("NSE005_ORACLE angle=%d object=A vertex=%lld,%lld"),
													Oracle::Angles[AttackRotation], P.X, P.Y));
							}
							for (const auto P : Oracle::Shape(20, Y, H, HurtRotation, bHurtFacingLeft))
							{
								AddInfo(
									FString::Printf(TEXT("NSE005_ORACLE angle=%d object=B vertex=%lld,%lld"),
													Oracle::Angles[AttackRotation], P.X, P.Y));
							}
						}
						Oracle::FOracleFraction Time;
						const bool Hit = Oracle::Contact(
							Oracle::Shape(0, 100, A, AttackRotation, bAttackFacingLeft),
							Oracle::Shape(20, Y, H, HurtRotation, bHurtFacingLeft), {30, 3}, Time);
						Hit ? ++RotatedHits : ++RotatedMisses;
						Battle.Move(Battle.Projectile, 23, 5);
						Battle.Move(Battle.Targets[0], -7, 2);
						Battle.Step();
						AssertContacts(*this, Battle, Hit ? TArray<int32>{1} : TArray<int32>{});
					}
				}
			}
		}
	}
	TestTrue(TEXT("arbitrary rotated offset controls include hits"), RotatedHits > 0);
	TestTrue(TEXT("arbitrary rotated offset controls include misses"), RotatedMisses > 0);
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE005V3, "UnrealBench.NSE005.V3",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE005V3::RunTest(const FString&)
{
	// Authored keys must determine exact-time ties independently of allocation
	// order. Exercise every permutation, key zero, and partial lifetime capacity.
	for (const TArray<int32>& Keys : {TArray<int32>{91, 0, 7}, TArray<int32>{91, 7, 0},
									TArray<int32>{0, 91, 7}, TArray<int32>{0, 7, 91},
									TArray<int32>{7, 91, 0}, TArray<int32>{7, 0, 91}})
	{
		for (int Limit : {1, 2, 3, 4})
		{
			FNSE005Battle Battle(3);
			// Keep the owner's key distinct from all target keys as well.
			Battle.Game->Players[0]->SetContactOrderKey(101);
			for (int I = 0; I < 3; ++I)
			{
				Battle.Targets[I]->SetContactOrderKey(Keys[I]);
			}
			Battle.Projectile->ConfigureSweptProjectile(true, true, Limit);
			Battle.Move(Battle.Projectile, 100);
			Battle.Step();
			const TArray<int32> All = {0, 7, 91};
			TArray<int32> Expected;
			for (int I = 0; I < FMath::Min(3, Limit); ++I)
			{
				Expected.Add(All[I]);
			}
			AssertContacts(*this, Battle, Expected);
			TestEqual(TEXT("authored-key tie capacity survival"), Battle.Projectile->IsActive, Limit > 3);
		}
	}
	// A positive authored lifetime limit must not silently cap at a small fixed
	// history size. Check both sides of 32 and survival with spare capacity.
	for (int Limit : {32, 33, 34})
	{
		FNSE005Battle Battle(33);
		Battle.Projectile->ConfigureSweptProjectile(true, true, Limit);
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		TArray<int32> Expected;
		for (int Key = 1; Key <= FMath::Min(33, Limit); ++Key)
		{
			Expected.Add(Key);
		}
		AssertContacts(*this, Battle, Expected);
		TestEqual(TEXT("authored lifetime limit above 32"), Battle.Projectile->IsActive, Limit > 33);
	}
	// A cap at a larger fixed size is invisible to small budgets alone. A limit far above any
	// plausible internal bound must still accept the ordinary contacts and leave spare capacity,
	// so a fixed-size backing store cannot pass by exhausting the projectile on configuration.
	for (int Limit : {257, 4096, MAX_int32})
	{
		FNSE005Battle Battle(3);
		Battle.Projectile->ConfigureSweptProjectile(true, true, Limit);
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {1, 2, 3});
		TestTrue(TEXT("large lifetime limit retains spare capacity"), Battle.Projectile->IsActive);
	}
	// An internal maximum is otherwise unobservable: reaching a cap of N needs N+1 targets, so any
	// fixed probe can be evaded by capping higher. The configured limit is read back instead.
	for (int Limit : {1, 2, 34, 257, 4096, MAX_int32})
	{
		FNSE005Battle Battle(1);
		Battle.Projectile->ConfigureSweptProjectile(true, true, Limit);
		TestEqual(TEXT("configured lifetime limit is retained"), Battle.Projectile->GetSweptContactLimit(), Limit);
	}
	for (int Limit : {0, -1, MIN_int32})
	{
		FNSE005Battle Battle(1);
		Battle.Projectile->ConfigureSweptProjectile(true, true, Limit);
		TestEqual(TEXT("invalid lifetime limit reports one"), Battle.Projectile->GetSweptContactLimit(), 1);
	}
	// Large edges make rational cross products exceed 64 bits. The one-unit time
	// difference must beat the authored key, while exact ties still use that key.
	for (bool Rotated : {false, true})
	{
		for (int Limit : {1, 2, 3})
		{
			FNSE005Battle Battle(3);
			Battle.Projectile->ConfigureSweptProjectile(true, true, Limit);
			const int Start = Rotated ? -6000000 : -9000000, Distance = -2 * Start;
			Battle.Projectile->PosX = Start;
			Battle.Projectile->AnglePitch_x1000 = Rotated ? 45000 : 0;
			Battle.Boxes(Battle.Projectile, {Battle.Box(4, 8000000, BOX_Hit)});
			for (int I = 0; I < 3; ++I)
			{
				Battle.Targets[I]->PosX = I == 0 ? 1 : 0;
				Battle.Targets[I]->AnglePitch_x1000 = Rotated ? 45000 : 0;
				Battle.Boxes(Battle.Targets[I], {Battle.Box(4, 8000000)});
			}
			auto Geometry = [Rotated](int X) -> TArray<Oracle::FOraclePoint>
			{
				if (!Rotated)
				{
					return Oracle::Rect(X, 100, 4, 8000000);
				}
				// Published 45-degree coefficients 705/707, truncating each product.
				// These long rotated edges also force >64-bit products during time sorting.
				return {{X - 1 + 2828000, 100 - 1 - 2820000},
						{X + 1 + 2828000, 100 + 1 - 2820000},
						{X + 1 - 2828000, 100 + 1 + 2820000},
						{X - 1 - 2828000, 100 - 1 + 2820000}};
			};
			Oracle::FOracleFraction Near, Far;
			TestTrue(TEXT("large projection near contact"),
					 Oracle::Contact(Geometry(Start), Geometry(0), {Distance, 0}, Near));
			TestTrue(TEXT("large projection far contact"),
					 Oracle::Contact(Geometry(Start), Geometry(1), {Distance, 0}, Far));
			TestTrue(TEXT("exact close times at large projections"), Near < Far);
			Battle.Move(Battle.Projectile, Distance);
			Battle.Step();
			const TArray<int32> All = {2, 3, 1};
			TArray<int32> Expected;
			for (int I = 0; I < Limit; ++I)
			{
				Expected.Add(All[I]);
			}
			AssertContacts(*this, Battle, Expected);
		}
	}
	for (int Limit : {1, 2, 5})
	{
		FNSE005Battle Battle(3);
		Battle.Projectile->ConfigureSweptProjectile(true, true, Limit);
		Battle.Targets[0]->PosX = 70;
		Battle.Targets[1]->PosX = 30;
		Battle.Targets[2]->PosX = 50;
		Battle.Boxes(Battle.Projectile, {Battle.Box(2, 2, BOX_Hit), Battle.Box(4, 2, BOX_Hit)});
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		const TArray<int32> All = {2, 3, 1};
		TArray<int32> Expected;
		for (int I = 0; I < FMath::Min(3, Limit); ++I)
		{
			Expected.Add(All[I]);
		}
		AssertContacts(*this, Battle, Expected);
		TestEqual(TEXT("capacity survival"), Battle.Projectile->IsActive, Limit > 3);
	}
	{
		FNSE005Battle Battle(3);
		Battle.Projectile->ConfigureSweptProjectile(true, true, 3);
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {1, 2, 3});
	}
	{
		FNSE005Battle Battle(2);
		Battle.Projectile->ConfigureSweptProjectile(true, true, 2);
		Battle.Targets[0]->PosX = 51;
		Battle.Targets[1]->PosX = 50;
		Battle.Move(Battle.Projectile, 2000000);
		Battle.Step();
		AssertContacts(*this, Battle, {2, 1});
	}
	{
		FNSE005Battle Battle(2);
		Battle.Projectile->ConfigureSweptProjectile(true, true, 3);
		Battle.Targets[1]->PosX = 200;
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {1});
		for (int I = 0; I < 5; ++I)
		{
			Battle.Step();
		}
		Battle.Boxes(Battle.Targets[0], {Battle.Box(1000, 1000)});
		Battle.Move(Battle.Projectile, 150);
		Battle.Step();
		TestTrue(TEXT("lifetime distinct identities"), Accepted(Battle) == TArray<int32>({1, 2}));
		TestTrue(TEXT("one capacity remains"), Battle.Projectile->IsActive);
	}
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE005V4, "UnrealBench.NSE005.V4",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE005V4::RunTest(const FString&)
{
	for (int Exclusion = 0; Exclusion < 3; ++Exclusion)
	{
		for (bool Exclude : {true, false})
		{
			FNSE005Battle Battle(2);
			Battle.Targets[0]->PosX = 25;
			Battle.Targets[1]->PosX = 75;
			if (Exclude && Exclusion == 0)
			{
				Battle.Targets[0]->SetStrikeInvulnerable(true);
			}
			if (Exclude && Exclusion == 1)
			{
				Battle.Targets[0]->PlayerFlags &= ~PLF_IsOnScreen;
			}
			if (Exclude && Exclusion == 2)
			{
				Battle.Targets[0]->PlayerIndex = 0;
			}
			Battle.Move(Battle.Projectile, 100);
			Battle.Step();
			AssertContacts(*this, Battle, Exclude ? TArray<int32>{2} : TArray<int32>{1});
		}
	}
	for (ENSE005ContactAction Action :
		 {ENSE005ContactAction::None, ENSE005ContactAction::DeactivateProjectile,
		  ENSE005ContactAction::DisableHit, ENSE005ContactAction::MakeTargetInvulnerable})
	{
		FNSE005Battle Battle(3);
		Battle.Projectile->ConfigureSweptProjectile(true, true, 3);
		for (int I = 0; I < 3; ++I)
		{
			Battle.Targets[I]->PosX = 20 + I * 20;
		}
		Battle.Script(Battle.Projectile)->OnContactAction = Action;
		Battle.Script(Battle.Projectile)->ChangeTarget = Battle.Targets[1];
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle,
					   Action == ENSE005ContactAction::None						? TArray<int32>{1, 2, 3}
					   : Action == ENSE005ContactAction::MakeTargetInvulnerable ? TArray<int32>{1, 3}
																				: TArray<int32>{1});
	}
	for (bool Sweep : {false, true})
	{
		FNSE005Battle Battle;
		auto* Target = Battle.Targets[0];
		Target->Direction = DIR_Left;
		Target->EnableState(ENB_Block, FGameplayTag::RequestGameplayTag(TEXT("StateMachine.Primary")));
		Battle.Projectile->ConfigureSweptProjectile(Sweep, false, 1);
		Battle.Move(Battle.Projectile, Sweep ? 100 : 50);
		Battle.Step(INP_Neutral, INP_Right);
		TestEqual(TEXT("guard chip"), Target->CurrentHealth, 9990);
		TestEqual(TEXT("guard hitstop"), Target->Hitstop, 3);
		TestEqual(TEXT("block callback count"), Accepted(Battle).Num(), 1);
		if (Sweep)
		{
			TestFalse(TEXT("guard consumes capacity"), Battle.Projectile->IsActive);
		}
	}
	for (bool Chip : {false, true})
	{
		for (bool Sweep : {false, true})
		{
			FNSE005Battle Battle;
			auto* Target = Battle.Targets[0];
			Target->SuperArmorData.Type = ARM_Guard;
			Target->SuperArmorData.ArmorHits = 2;
			Target->SuperArmorData.bArmorStrike = true;
			Target->SuperArmorData.bArmorProjectile = true;
			Target->SuperArmorData.bArmorMid = true;
			Target->SuperArmorData.bArmorLow = true;
			Target->SuperArmorData.bArmorOverhead = true;
			Target->SuperArmorData.bArmorTakeChipDamage = Chip;
			Target->SuperArmorData.ArmorDamagePercent = 0;
			Battle.Projectile->ConfigureSweptProjectile(Sweep, false, 1);
			Battle.Move(Battle.Projectile, Sweep ? 100 : 50);
			Battle.Step();
			TestEqual(TEXT("armor health"), Target->CurrentHealth, Chip ? 9990 : 10000);
			TestEqual(TEXT("armor ordinary hitstop"), Target->Hitstop, 3);
			TestEqual(TEXT("armor consumes one contact"), Accepted(Battle).Num(), 1);
			if (Sweep)
			{
				TestFalse(TEXT("armor consumes capacity"), Battle.Projectile->IsActive);
			}
		}
	}
	{
		FNSE005Battle Battle;
		Battle.Projectile->ConfigureSweptProjectile(true, true, 2);
		Battle.Move(Battle.Projectile, 50);
		Battle.Step();
		for (int I = 0; I < 5; ++I)
		{
			Battle.Step();
		}
		TestEqual(TEXT("no repeated endpoint damage"), Battle.Targets[0]->CurrentHealth, 9900);
		TestEqual(TEXT("no duplicate callback through hitstop"), Accepted(Battle).Num(), 1);
	}
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE005V5, "UnrealBench.NSE005.V5",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE005V5::RunTest(const FString&)
{
	for (bool Endpoint : {false, true})
	{
		FNSE005Battle Battle;
		Battle.Projectile->ResetObject();
		auto* SpawnScript = Battle.Script(Battle.Game->Players[0]);
		SpawnScript->bSpawnPending = true;
		SpawnScript->SpawnX = Endpoint ? 50 : 100;
		Battle.Step();
		AssertContacts(*this, Battle, Endpoint ? TArray<int32>{1} : TArray<int32>{});
	}
	for (bool Endpoint : {false, true})
	{
		FNSE005Battle Battle;
		Battle.Targets[0]->PosX = 500;
		auto* SpawnScript = Battle.Script(Battle.Targets[0]);
		SpawnScript->bSpawnTargetPending = true;
		SpawnScript->SpawnX = Endpoint ? 100 : 50;
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		TestTrue(TEXT("new target only participates at endpoint"),
				 Accepted(Battle) == (Endpoint ? TArray<int32>{99} : TArray<int32>{}));
	}
	for (bool Inactive : {false, true})
	{
		FNSE005Battle Battle;
		Battle.Targets[0]->PosX = 500;
		auto* Target = Battle.SpawnTarget(50);
		if (!Target)
		{
			AddError(TEXT("fixture could not spawn a target (object activation failed)"));
			return false;
		}
		if (Inactive)
		{
			Target->DeactivateObject();
		}
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		TestTrue(TEXT("precollision target deactivation"),
				 Accepted(Battle) == (Inactive ? TArray<int32>{} : TArray<int32>{99}));
	}
	for (bool Target : {false, true})
	{
		for (bool Endpoint : {false, true})
		{
			FNSE005Battle Battle;
			if (Target)
			{
				Battle.Targets[0]->PosX = 100;
				Battle.Teleport(Battle.Targets[0], Endpoint ? 0 : -100, 100);
			}
			else
			{
				Battle.Teleport(Battle.Projectile, Endpoint ? 50 : 100, 100);
			}
			Battle.Step();
			AssertContacts(*this, Battle, Endpoint ? TArray<int32>{1} : TArray<int32>{});
		}
	}
	{
		FNSE005Battle Battle;
		Battle.Teleport(Battle.Projectile, 90, 100, 10);
		Battle.Step();
		AssertContacts(*this, Battle, {});
		Battle.Move(Battle.Projectile, -100);
		Battle.Step();
		AssertContacts(*this, Battle, {1});
	}
	{
		FNSE005Battle Battle;
		Battle.Projectile->DeactivateObject();
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {});
	}
	{
		FNSE005Battle Battle;
		auto* Slot = Battle.Projectile;
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {1});
		for (int I = 0; I < 5; ++I)
		{
			Battle.Step();
		}
		Battle.Trace.Reset();
		Battle.Targets[0]->CurrentHealth = 10000;
		Battle.Spawn(500, 100, true, true, 2);
		TestTrue(TEXT("same pool slot reused"), Slot == Battle.Projectile);
		Battle.Step();
		AssertContacts(*this, Battle, {});
		Battle.Move(Battle.Projectile, -500);
		Battle.Step();
		AssertContacts(*this, Battle, {1});
		TestTrue(TEXT("new activation has full capacity"), Battle.Projectile->IsActive);
	}
	{
		FNSE005Battle Battle(3);
		Battle.Projectile->ConfigureSweptProjectile(true, true, 2);
		Battle.Targets[0]->PosX = 100;
		Battle.Targets[1]->PosX = 102;
		Battle.Targets[2]->PosX = 25;
		Battle.Teleport(Battle.Targets[0], 102, 100);
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {3, 1});
	}
	{
		// Reusing a projectile that was explicitly deactivated before exhaustion must
		// permit its old victim again. Cleanup may happen at hit time or activation.

		FNSE005Battle Battle(2);
		Battle.Projectile->ConfigureSweptProjectile(true, true, 5);
		Battle.Targets[0]->PosX = 50;
		Battle.Targets[1]->PosX = 500;
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {1});
		TestTrue(TEXT("piercing shot survives a non-exhausting contact"), Battle.Projectile->IsActive);
		Battle.Projectile->DeactivateObject();
		for (int Frame = 0; Frame < 5; ++Frame)
		{
			Battle.Step();
		}
		Battle.Trace.Reset();
		Battle.Targets[0]->CurrentHealth = 10000;
		Battle.Spawn(100, 100, true, true, 2);
		Battle.Step();
		AssertContacts(*this, Battle, {});
		Battle.Move(Battle.Projectile, -100);
		Battle.Step();
		AssertContacts(*this, Battle, {1});
		TestTrue(TEXT("reused live piercing activation has fresh capacity"), Battle.Projectile->IsActive);
	}
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE005V6, "UnrealBench.NSE005.V6",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE005V6::RunTest(const FString&)
{
	FNSE005Battle Battle(3);
	Battle.Projectile->ConfigureSweptProjectile(true, true, 2);
	Battle.Targets[0]->PosX = 50;
	Battle.Targets[1]->PosX = 150;
	Battle.Targets[2]->PosX = -50;
	FRollbackData Before;
	int32 Checksum = 0;
	Battle.Game->SaveGameState(Before, &Checksum);
	Battle.Move(Battle.Projectile, 100);
	Battle.Step();
	AssertContacts(*this, Battle, {1});
	FRollbackData DuringStop;
	Battle.Game->SaveGameState(DuringStop, &Checksum);
	for (int I = 0; I < 5; ++I)
	{
		Battle.Step();
	}
	FRollbackData After;
	Battle.Game->SaveGameState(After, &Checksum);
	Battle.Move(Battle.Projectile, 100);
	Battle.Step();
	TestFalse(TEXT("future exhausts"), Battle.Projectile->IsActive);
	Battle.Spawn(500, 100, true, true, 2);
	for (FRollbackData* Snapshot : {&Before, &DuringStop, &After})
	{
		Battle.Game->LoadGameState(*Snapshot);
		Battle.Trace.Reset();
		if (Snapshot == &Before)
		{
			Battle.Move(Battle.Projectile, 100);
			Battle.Step();
			TestTrue(TEXT("restored first contact"), Accepted(Battle) == TArray<int32>({1}));
		}
		for (int I = 0; I < 5; ++I)
		{
			Battle.Step();
		}
		Battle.Trace.Reset();
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		TestTrue(TEXT("restored remaining contact"), Accepted(Battle) == TArray<int32>({2}));
		TestEqual(TEXT("restored health"), Battle.Targets[1]->CurrentHealth, 9900);
		TestFalse(TEXT("restored capacity exhausts"), Battle.Projectile->IsActive);
	}
	Battle.Game->LoadGameState(After);
	Battle.Trace.Reset();
	Battle.Move(Battle.Projectile, -200);
	Battle.Step();
	TestTrue(TEXT("restored lifetime memory skips old target before fresh one"),
			 Accepted(Battle) == TArray<int32>({3}));
	TestEqual(TEXT("old target remains singly damaged"), Battle.Targets[0]->CurrentHealth, 9900);
	TestEqual(TEXT("fresh target takes damage after restored old-target skip"),
			  Battle.Targets[2]->CurrentHealth, 9900);
	Battle.Game->LoadGameState(Before);
	Battle.Trace.Reset();
	Battle.Move(Battle.Projectile, -100);
	Battle.Step();
	AssertContacts(*this, Battle, {3});
	TestTrue(TEXT("discarded capacity removed"), Battle.Projectile->IsActive);
	Battle.Game->LoadGameState(Before);
	Battle.Trace.Reset();
	Battle.Move(Battle.Projectile, 0, 100);
	Battle.Step();
	AssertContacts(*this, Battle, {});
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE005V7, "UnrealBench.NSE005.V7",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE005V7::RunTest(const FString&)
{
	for (int Distance : {0, 50, 100})
	{
		for (bool Sweep : {false, true})
		{
			FNSE005Battle Battle;
			Battle.Projectile->ConfigureSweptProjectile(Sweep, false, 1);
			if (Distance == 0)
			{
				Battle.Targets[0]->PosX = 0;
			}
			Battle.Move(Battle.Projectile, Distance);
			Battle.Step();
			AssertContacts(*this, Battle, (Sweep || Distance <= 50) ? TArray<int32>{1} : TArray<int32>{});
		}
	}
	return !HasAnyErrors();
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE005V8, "UnrealBench.NSE005.V8",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE005V8::RunTest(const FString&)
{
	TSet<int32> EarliestPairs;
	for (int Seed : {5005, 5006, 5007, 91873})
	{
		FRandomStream Random(Seed);
		for (int Episode = 0; Episode < 200; ++Episode)
		{
			const int Count = Random.RandRange(1, 5), Limit = Random.RandRange(1, 5);
			const int Sizes[] = {1, 2, 3, 5, 8, 13};
			const int AttackWidth = Sizes[Random.RandRange(0, 5)],
					  AttackHeight = Sizes[Random.RandRange(0, 5)];
			const int PendingMoveX = Random.RandRange(-100, 100), PendingMoveY = Random.RandRange(-10, 10);
			struct ExpectedContact
			{
				int Key;
				Oracle::FOracleFraction Time;
			};
			TArray<ExpectedContact> Contacts;
			TArray<int32> OriginalX, OriginalY, TargetDX, TargetDY;
			const bool AxisControl = Episode % 4 == 0;
			const int AttackRotation = AxisControl ? 0 : Random.RandRange(1, 5);
			const bool bAttackFacingLeft = AxisControl ? false : Random.RandRange(0, 1) != 0;
			FString Scene = FString::Printf(TEXT("projectile angle=%d left=%d"),
											Oracle::Angles[AttackRotation], bAttackFacingLeft);
			FNSE005Battle Battle(Count);
			Battle.Projectile->ConfigureSweptProjectile(true, true, Limit);
			Battle.Projectile->AnglePitch_x1000 = Oracle::Angles[AttackRotation];
			Battle.Projectile->Direction = bAttackFacingLeft ? DIR_Left : DIR_Right;
			TArray<FCollisionBox> AttackBoxes;
			for (int J = 0, N = Random.RandRange(1, 3); J < N; ++J)
			{
				AttackBoxes.Add(Battle.Box(J == 0 ? AttackWidth : Sizes[Random.RandRange(0, 5)],
										   J == 0 ? AttackHeight : Sizes[Random.RandRange(0, 5)], BOX_Hit,
										   AxisControl ? 0 : Random.RandRange(-25, 25),
										   AxisControl ? 0 : Random.RandRange(-20, 20)));
			}
			for (const auto& Box : AttackBoxes)
			{
				Scene += FString::Printf(TEXT(" attack(size=%d,%d offset=%d,%d)"), Box.SizeX, Box.SizeY,
										 Box.PosX, Box.PosY);
			}
			Battle.Boxes(Battle.Projectile, AttackBoxes);
			Battle.Move(Battle.Projectile, PendingMoveX, PendingMoveY);
			for (int I = 0; I < Count; ++I)
			{
				auto* Target = Battle.Targets[I];
				const int HurtRotation = AxisControl ? 0 : Random.RandRange(1, 5);
				const bool bHurtFacingLeft = AxisControl ? false : Random.RandRange(0, 1) != 0;
				Target->AnglePitch_x1000 = Oracle::Angles[HurtRotation];
				Target->Direction = bHurtFacingLeft ? DIR_Left : DIR_Right;
				const int W = Sizes[Random.RandRange(0, 5)], H = Sizes[Random.RandRange(0, 5)];
				Target->PosX = Random.RandRange(-100, 100);
				Target->PosY = Random.RandRange(90, 110);
				TArray<FCollisionBox> HurtBoxes;
				for (int J = 0, N = Random.RandRange(1, 3); J < N; ++J)
				{
					HurtBoxes.Add(Battle.Box(J == 0 ? W : Sizes[Random.RandRange(0, 5)],
											 J == 0 ? H : Sizes[Random.RandRange(0, 5)], BOX_Hurt,
											 AxisControl ? 0 : Random.RandRange(-25, 25),
											 AxisControl ? 0 : Random.RandRange(-20, 20)));
				}
				Battle.Boxes(Target, HurtBoxes);
				const int TargetVelocityX = Random.RandRange(-100, 100),
						  TargetVelocityY = Random.RandRange(-10, 10);
				Scene += FString::Printf(TEXT(" target%d(pos=%d,%d motion=%d,%d angle=%d left=%d)"), I + 1,
										 Target->PosX, Target->PosY, TargetVelocityX, TargetVelocityY,
										 Oracle::Angles[HurtRotation], bHurtFacingLeft);
				for (const auto& Box : HurtBoxes)
				{
					Scene += FString::Printf(TEXT(" hurt(size=%d,%d offset=%d,%d)"), Box.SizeX, Box.SizeY,
											 Box.PosX, Box.PosY);
				}
				Battle.Move(Target, TargetVelocityX, TargetVelocityY);
				OriginalX.Add(Target->PosX);
				OriginalY.Add(Target->PosY);
				TargetDX.Add(TargetVelocityX);
				TargetDY.Add(TargetVelocityY);
				Oracle::FOracleFraction Earliest;
				bool Found = false;
				int WinningPair = -1;
				for (int AttackBoxIndex = 0; AttackBoxIndex < AttackBoxes.Num(); ++AttackBoxIndex)
				{
					for (int HurtBoxIndex = 0; HurtBoxIndex < HurtBoxes.Num(); ++HurtBoxIndex)
					{
						const auto& A = AttackBoxes[AttackBoxIndex];
						const auto& HBox = HurtBoxes[HurtBoxIndex];
						Oracle::FOracleFraction Time;
						if (Oracle::Contact(Oracle::Shape(0, 100, A, AttackRotation, bAttackFacingLeft),
											Oracle::Shape(Target->PosX, Target->PosY, HBox, HurtRotation,
														  bHurtFacingLeft),
											{PendingMoveX - TargetVelocityX, PendingMoveY - TargetVelocityY},
											Time) &&
							(!Found || Time < Earliest))
						{
							Found = true;
							Earliest = Time;
							WinningPair = AttackBoxIndex * 3 + HurtBoxIndex;
						}
					}
				}
				if (Found)
				{
					Contacts.Add({I + 1, Earliest});
					EarliestPairs.Add(WinningPair);
				}
			}
			Contacts.Sort(
				[](const ExpectedContact& A, const ExpectedContact& C)
				{
					if (A.Time < C.Time)
					{
						return true;
					}
					if (C.Time < A.Time)
					{
						return false;
					}
					return A.Key < C.Key;
				});
			TArray<int32> Expected;
			for (int I = 0; I < FMath::Min(Limit, Contacts.Num()); ++I)
			{
				Expected.Add(Contacts[I].Key);
			}
			FRollbackData Saved;
			int32 Checksum = 0;
			Battle.Game->SaveGameState(Saved, &Checksum);
			Battle.Step();
			AddInfo(FString::Printf(TEXT("seed=%d episode=%d displacement=(%d,%d) capacity=%d"), Seed,
									Episode, PendingMoveX, PendingMoveY, Limit));
			if (Accepted(Battle) != Expected)
			{
				AddInfo(Scene);
				for (int Key : Expected)
				{
					AddInfo(FString::Printf(TEXT("expected=%d"), Key));
				}
				for (int Key : Battle.Trace)
				{
					AddInfo(FString::Printf(TEXT("actual callback=%d"), Key));
				}
			}
			AssertContacts(*this, Battle, Expected);
			TestEqual(TEXT("oracle capacity"), Battle.Projectile->IsActive, Expected.Num() < Limit);
			for (int Relation = AxisControl ? 0 : 1; Relation < 4; ++Relation)
			{
				Battle.Game->LoadGameState(Saved);
				Battle.Trace.Reset();
				const int Sign = Relation == 0 ? -1 : 1, Shift = Relation == 1 ? 100000 : 0,
						  CommonVelocity = Relation == 2 ? 17 : 0;
				Battle.Projectile->PosX = Shift;
				if (Relation == 0)
				{
					Battle.Projectile->Direction = DIR_Left;
				}
				Battle.Move(Battle.Projectile, Sign * PendingMoveX + CommonVelocity, PendingMoveY);
				for (int I = 0; I < Count; ++I)
				{
					Battle.Targets[I]->PosX = Sign * OriginalX[I] + Shift;
					Battle.Targets[I]->PosY = OriginalY[I];
					if (Relation == 0)
					{
						Battle.Targets[I]->Direction = DIR_Left;
					}
					Battle.Move(Battle.Targets[I], Sign * TargetDX[I] + CommonVelocity, TargetDY[I]);
				}
				TArray<int32> RelationExpected = Expected;
				if (Relation == 3)
				{
					Battle.Projectile->ConfigureSweptProjectile(true, true, 5);
					RelationExpected.Reset();
					for (const auto& C : Contacts)
					{
						RelationExpected.Add(C.Key);
					}
				}
				Battle.Step();
				AssertContacts(*this, Battle, RelationExpected);
				TestEqual(TEXT("restored capacity and metamorphic prefix"), Battle.Projectile->IsActive,
						  RelationExpected.Num() < (Relation == 3 ? 5 : Limit));
			}
			if (Seed == 5005 && Episode < 6)
			{
				// Exhaust the six authored actions once, independent of geometry sampling.
				// Stateful episode suffix: the model owns the accepted set and remaining capacity.
				FNSE005Battle Control(2);
				Control.Targets[1]->PosX = 150;
				Control.Game->BattleState.MainPlayer[1] = Control.Targets[1];
				Control.Projectile->ConfigureSweptProjectile(true, true, 2);
				Control.Move(Control.Projectile, 100);
				Control.Step();
				AssertContacts(*this, Control, {1});
				for (int Frame = 0; Frame < 5; ++Frame)
				{
					Control.Step();
				}
				TestTrue(TEXT("fuzz lifetime set survives hitstop"), Accepted(Control) == TArray<int32>({1}));
				FRollbackData Partial;
				Control.Game->SaveGameState(Partial, &Checksum);
				const int Action = Episode;
				Control.Trace.Reset();
				AddInfo(FString::Printf(TEXT("stateful seed=%d episode=%d action=%d "
											 "frames=contact,5wait,action,restore,old-cross,fresh-cross"),
										Seed, Episode, Action));
				if (Action == 0)
				{
					Control.Targets[1]->SetStrikeInvulnerable(true);
				}
				if (Action == 1)
				{
					Control.Teleport(Control.Projectile, 200, 100);
				}
				if (Action == 2)
				{
					// A bad contact implementation may already have exhausted and reset
					// the projectile. Report that failure without dereferencing its cleared owner.
					if (TestTrue(TEXT("fuzz projectile active before explicit deactivation"),
								 Control.Projectile->IsActive))
					{
						Control.Projectile->DeactivateObject();
					}
				}
				if (Action == 3)
				{
					Control.Targets[1]->Direction = DIR_Left;
					Control.Targets[1]->EnableState(
						ENB_Block, FGameplayTag::RequestGameplayTag(TEXT("StateMachine.Primary")));
				}
				if (Action == 4)
				{
					Control.Spawn(100, 100, true, true, 2);
				}
				if (Action != 1)
				{
					Control.Move(Control.Projectile, 100);
				}
				Control.Step(INP_Neutral, Action == 3 ? INP_Right : INP_Neutral);
				const bool HitSecond = Action >= 3;
				TestTrue(TEXT("fuzz action expected contact set"),
						 Accepted(Control) == (HitSecond ? TArray<int32>{2} : TArray<int32>{}));
				TestEqual(TEXT("fuzz action target health"), Control.Targets[1]->CurrentHealth,
						  Action == 3 ? 9990
						  : HitSecond ? 9900
									  : 10000);
				TestEqual(TEXT("fuzz lifetime capacity after action"), Control.Projectile->IsActive,
						  Action == 0 || Action == 1 || Action == 4);
				if (HitSecond)
				{
					TestEqual(TEXT("fuzz ordinary action hitstop"), Control.Targets[1]->Hitstop, 3);
				}
				Control.Game->LoadGameState(Partial);
				Control.Trace.Reset();
				// Returning across the old victim must skip it; replay forward must consume the fresh victim.
				Control.Move(Control.Projectile, -100);
				Control.Step();
				TestTrue(TEXT("fuzz restored lifetime dedup"), Accepted(Control).IsEmpty());
				TestEqual(TEXT("fuzz restored old victim health"), Control.Targets[0]->CurrentHealth, 9900);
				Control.Move(Control.Projectile, 200);
				Control.Step();
				TestTrue(TEXT("fuzz restored continuation oracle"), Accepted(Control) == TArray<int32>({2}));
				TestEqual(TEXT("fuzz restored fresh victim damage"), Control.Targets[1]->CurrentHealth, 9900);
				TestFalse(TEXT("fuzz restored remaining capacity"), Control.Projectile->IsActive);
			}
			if (HasAnyErrors())
			{
				return false;
			}
		}
	}
	TestTrue(TEXT("different offset pairs produce earliest contact"), EarliestPairs.Num() >= 4);
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE005V9, "UnrealBench.NSE005.V9",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE005V9::RunTest(const FString&)
{
	for (int Distance : {50, 100})
	{
		// Skip the fixture's authoring override on a newly created projectile.
		FNSE005Battle Battle(1, false);
		Battle.Step();
		Battle.Move(Battle.Projectile, Distance);
		Battle.Step();
		AssertContacts(*this, Battle, Distance == 50 ? TArray<int32>{1} : TArray<int32>{});
	}

	// Invalid limits have a specified public fallback, even for piercing projectiles.
	for (int Limit : {0, -1, MIN_int32})
	{
		FNSE005Battle Battle(2);
		Battle.Projectile->ConfigureSweptProjectile(true, true, Limit);
		Battle.Targets[0]->PosX = 25;
		Battle.Targets[1]->PosX = 75;
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {1});
		TestFalse(TEXT("invalid limit clamps to one contact"), Battle.Projectile->IsActive);
	}
	{
		FNSE005Battle Battle(2);
		Battle.Projectile->ConfigureSweptProjectile(true, false, 5);
		Battle.Targets[0]->PosX = 25;
		Battle.Targets[1]->PosX = 75;
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {1});
		TestFalse(TEXT("nonpiercing accepts one even with a larger limit"), Battle.Projectile->IsActive);
	}
	{
		FNSE005Battle Battle(3);
		Battle.Projectile->ConfigureSweptProjectile(true, true, 3);
		// Authored key order differs from creation and actor order; zero is valid.
		Battle.Targets[0]->SetContactOrderKey(42);
		Battle.Targets[1]->SetContactOrderKey(0);
		Battle.Targets[2]->SetContactOrderKey(7);
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {0, 7, 42});
		TestFalse(TEXT("key-ordered contacts exhaust lifetime"), Battle.Projectile->IsActive);
	}
	{
		FNSE005Battle Battle(3);
		Battle.Targets[0]->PosX = 50;
		Battle.Targets[1]->PosX = 150;
		Battle.Targets[2]->PosX = 250;
		Battle.Projectile->ConfigureSweptProjectile(true, true, 3);
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {1});
		for (int Frame = 0; Frame < 5; ++Frame) Battle.Step();
		Battle.Projectile->ConfigureSweptProjectile(false, false, 3);
		Battle.Projectile->ConfigureSweptProjectile(true, true, 3);
		Battle.Move(Battle.Projectile, -100);
		Battle.Step();
		TestTrue(TEXT("configuration retains the accepted target"), Accepted(Battle) == TArray<int32>({1}));
		TestEqual(TEXT("configuration cannot damage old target again"), Battle.Targets[0]->CurrentHealth, 9900);
		Battle.Move(Battle.Projectile, 200);
		Battle.Step();
		TestTrue(TEXT("configuration permits remaining target"), Accepted(Battle) == TArray<int32>({1, 2}));
		TestTrue(TEXT("one lifetime contact remains after configuration"), Battle.Projectile->IsActive);
		for (int Frame = 0; Frame < 5; ++Frame) Battle.Step();
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		TestTrue(TEXT("configuration keeps lifetime count"), Accepted(Battle) == TArray<int32>({1, 2, 3}));
		TestEqual(TEXT("last target ordinary damage"), Battle.Targets[2]->CurrentHealth, 9900);
		TestFalse(TEXT("original lifetime limit exhausts"), Battle.Projectile->IsActive);
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE005V10, "UnrealBench.NSE005.V10",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE005V10::RunTest(const FString&)
{
	// Empty and wrong-type collision lists cannot supply a hit/hurt pair.
	for (int Case = 0; Case < 4; ++Case)
	{
		FNSE005Battle Battle;
		if (Case == 0) Battle.Boxes(Battle.Projectile, {});
		if (Case == 1) Battle.Boxes(Battle.Targets[0], {});
		if (Case == 2) Battle.Boxes(Battle.Projectile, {Battle.Box(4, 4, BOX_Hurt)});
		if (Case == 3) Battle.Boxes(Battle.Targets[0], {Battle.Box(4, 4, BOX_Hit)});
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		AssertContacts(*this, Battle, {});
		TestTrue(TEXT("absent collision pair consumes no capacity"), Battle.Projectile->IsActive);
	}
	for (bool ReverseBoxes : {false, true})
	{
		FNSE005Battle Battle(3);
		Battle.Projectile->ConfigureSweptProjectile(true, true, 3);
		Battle.Targets[0]->PosX = 70;
		Battle.Targets[1]->PosX = 40;
		Battle.Targets[2]->PosX = 90;
		TArray<FCollisionBox> Attacks = {Battle.Box(4, 4, BOX_Hit, -20), Battle.Box(4, 4, BOX_Hit)};
		TArray<FCollisionBox> Hurts = {Battle.Box(4, 4), Battle.Box(4, 4, BOX_Hurt, -60)};
		if (ReverseBoxes)
		{
			Swap(Attacks[0], Attacks[1]);
			Swap(Hurts[0], Hurts[1]);
		}
		Battle.Boxes(Battle.Projectile, Attacks);
		Battle.Boxes(Battle.Targets[0], Hurts);
		Battle.Boxes(Battle.Targets[2], Hurts);
		Battle.Move(Battle.Projectile, 120);
		Battle.Step();
		// Earliest target regions are near x=10, x=30, x=40, not actor origins.
		AssertContacts(*this, Battle, {1, 3, 2});
	}
	{
		FNSE005Battle Battle;
		Battle.Targets[0]->PosX = 100;
		// Post-teleport movement crosses the projectile, but ends clear of it.
		Battle.Teleport(Battle.Targets[0], 10, 100, -20);
		Battle.Step();
		AssertContacts(*this, Battle, {});
		// The next frame starts at -10 and crosses continuously to +10.
		Battle.Move(Battle.Targets[0], 20);
		Battle.Step();
		AssertContacts(*this, Battle, {1});
	}
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE005V11, "UnrealBench.NSE005.V11",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE005V11::RunTest(const FString&)
{
	for (int ReusedKey : {99, 17})
	{
		FNSE005Battle Battle;
		Battle.Targets[0]->PosX = 500;
		Battle.Projectile->ConfigureSweptProjectile(true, true, 2);
		auto* Target = Battle.SpawnTarget(50);
		if (!Target)
		{
			AddError(TEXT("fixture could not spawn a target (object activation failed)"));
			return false;
		}
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		TestTrue(TEXT("first target activation accepted"), Accepted(Battle) == TArray<int32>({99}));
		TestTrue(TEXT("projectile survives first target activation"), Battle.Projectile->IsActive);
		for (int Frame = 0; Frame < 5; ++Frame) Battle.Step();

		// A teleport and a new hurtbox asset do not create a target activation.
		Battle.Teleport(Target, 100, 100);
		Battle.Step();
		Battle.Boxes(Target, {Battle.Box(8, 8)});
		Battle.Step();
		TestTrue(TEXT("teleport and hurtbox edit retain target identity"),
		         Accepted(Battle) == TArray<int32>({99}));
		TestTrue(TEXT("same activation cannot consume remaining capacity"), Battle.Projectile->IsActive);

		FRollbackData BeforeReuse;
		int32 Checksum = 0;
		Battle.Game->SaveGameState(BeforeReuse, &Checksum);
		Target->DeactivateObject();
		Battle.Step(); // Complete the normal deferred deactivation before pool reuse.
		auto* Reused = Battle.SpawnTarget(50);
		if (!Reused)
		{
			AddError(TEXT("fixture could not spawn a target (object activation failed)"));
			return false;
		}
		TestTrue(TEXT("fixture reuses target pool slot"), Reused == Target);
		Reused->SetContactOrderKey(ReusedKey);
		Battle.Step();
		Battle.Move(Battle.Projectile, -100);
		Battle.Step();
		TestTrue(TEXT("new target activation accepted regardless of old key"),
		         Accepted(Battle) == TArray<int32>({99, ReusedKey}));
		TestFalse(TEXT("two target activations exhaust two lifetime contacts"), Battle.Projectile->IsActive);

		Battle.Game->LoadGameState(BeforeReuse);
		Battle.Trace.Reset();
		Battle.Move(Battle.Projectile, -100);
		Battle.Step();
		TestTrue(TEXT("restored original target activation remains accepted"), Accepted(Battle).IsEmpty());
		TestTrue(TEXT("discarded reactivation capacity is restored"), Battle.Projectile->IsActive);
		Battle.Move(Battle.Projectile, 100);
		Battle.Step();
		TestTrue(TEXT("return across original activation stays deduplicated"), Accepted(Battle).IsEmpty());
		Target->DeactivateObject();
		Battle.Step();
		Reused = Battle.SpawnTarget(50);
		if (!Reused)
		{
			AddError(TEXT("fixture could not spawn a target (object activation failed)"));
			return false;
		}
		Reused->SetContactOrderKey(ReusedKey);
		Battle.Step();
		Battle.Move(Battle.Projectile, -100);
		Battle.Step();
		TestTrue(TEXT("reactivation after restore accepts its own contact"),
		         Accepted(Battle) == TArray<int32>({ReusedKey}));
		TestFalse(TEXT("reactivation after restore consumes remaining contact"), Battle.Projectile->IsActive);
	}
	return !HasAnyErrors();
}
