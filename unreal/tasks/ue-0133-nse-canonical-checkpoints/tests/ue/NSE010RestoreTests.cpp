// NSE010 portable checkpoints: restore equivalence, reference binding and pool slot order.
#include "NSE010TestCommon.h"

using namespace NSE010;

// R4: after an import the target follows the source's future exactly, forward or backward.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE010Restore, "UnrealBench.NSE010.Restore",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE010Restore::RunTest(const FString&)
{
	const FTape T1 = TapeT1(), T2 = TapeT2(), T3 = TapeT3();
	{
		// 4.1 backward jump: A ran T1 then a discarded T2, imports its own frame-40 checkpoint.
		FNSE010Battle A;
		FNSE010Battle C;
		RunAll({&A, &C}, T1);
		const FBytes Bytes = Export(A);
		ExpectPayload(*this, TEXT("R4.1 export at frame 40 has a payload"), Bytes);
		Run(A, T2);
		TestEqual(TEXT("R4.1 control: A ran 30 frames past the checkpoint"), A.Game->BattleState.FrameNumber, 70);
		ExpectResult(*this, TEXT("R4.1 backward import into the same battle is Restored"), Import(A, Bytes),
					 EBattleCheckpointResult::Restored);
		TestEqual(TEXT("R4.1 frame number after the backward import"), A.Game->BattleState.FrameNumber, 40);
		ExpectFollows(*this, TEXT("R4.1 backward jump then T2 equals control"), A, C, T2);
		TestEqual(TEXT("R4.1 control: P1 was hit by P2's close shot during T2"), C.Players[0]->CurrentHealth, 9900);

		// The same checkpoint with a different tail T3, against a second control.
		FNSE010Battle C2;
		Run(C2, T1);
		ExpectResult(*this, TEXT("R4.1 second backward import is Restored"), Import(A, Bytes), EBattleCheckpointResult::Restored);
		ExpectFollows(*this, TEXT("R4.1 backward jump then T3 equals a T1+T3 control"), A, C2, T3);
		TestTrue(TEXT("R4.1 control: the T3 tail differs from the T2 tail (P1 walked the other way)"),
				 A.Players[0]->PosX != C.Players[0]->PosX);
	}
	{
		// 4.2 forward jump into a stranger with its own shots, 4.3 round trip.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		RunAll({&A, &C}, Slice(T1, 0, 20));
		const FBytes Bytes = Export(A);
		TestTrue(TEXT("R4.2 control: A has one shot in slot 0 and slot 1 unused at frame 20"),
				 A.Game->Objects[0]->IsActive && !A.Game->Objects[1]->IsActive);
		B.Spawn(B.Players[0], -40000, ShotY);
		B.Spawn(B.Players[0], -40000, ShotY);
		Run(B, Constant(3));
		TestTrue(TEXT("R4.2 control: the stranger has two active shots aimed at P2"),
				 B.Game->Objects[0]->IsActive && B.Game->Objects[1]->IsActive);
		const int32 TraceB = B.Trace.Num();
		ExpectResult(*this, TEXT("R4.2 forward import into a stranger is Restored"), Import(B, Bytes),
					 EBattleCheckpointResult::Restored);
		TestEqual(TEXT("R4.2 frame number after the forward import"), B.Game->BattleState.FrameNumber, 20);
		TestFalse(TEXT("R4.2 the stranger's slot 1 is inactive after the import"), B.Game->Objects[1]->IsActive);
		ExpectSameBytes(*this, TEXT("R4.3 export right after the import reproduces the imported bytes"), Export(B), Bytes);
		// 4.11 import runs no gameplay and is idempotent.
		TestEqual(TEXT("R4.11 the import fired no fixture callback"), B.Trace.Num(), TraceB);
		const FObs Once = Observe(B);
		ExpectResult(*this, TEXT("R4.11 importing the same bytes again is Restored"), Import(B, Bytes), EBattleCheckpointResult::Restored);
		ExpectObsEqual(*this, TEXT("R4.11 a second import of the same bytes changes nothing"), Observe(B), Once);
		ExpectSameBytes(*this, TEXT("R4.11 export after the second import still reproduces the bytes"), Export(B), Bytes);
		TestEqual(TEXT("R4.11 the second import fired no fixture callback"), B.Trace.Num(), TraceB);
		FTape Tail = Slice(T1, 20, 40);
		Tail.Append(T2);
		ExpectFollows(*this, TEXT("R4.2 stranger follows control for the rest of T1 and T2"), B, C, Tail, &A);
		TestEqual(TEXT("R4.2 target P2 took exactly one hit (the stranger's own shots never hit)"), B.Players[1]->CurrentHealth, 9900);
		TestEqual(TEXT("R4.2 control: P2 took exactly one hit"), C.Players[1]->CurrentHealth, 9900);
	}
	{
		// 4.4 random walk.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		for (FNSE010Battle* X : {&A, &C})
		{
			X->Script(X->Players[0])->RandomWalk = 700;
			X->Script(X->Players[1])->RandomWalk = 700;
		}
		RunAll({&A, &C}, Constant(25));
		TestTrue(TEXT("R4.4 control: the random walk moved P1"), C.Players[0]->PosX != P1X);
		ExpectResult(*this, TEXT("R4.4 import mid random walk is Restored"), Import(B, Export(A)), EBattleCheckpointResult::Restored);
		ExpectFollows(*this, TEXT("R4.4 random walk continues like control after the import"), B, C, Constant(40), &A);
	}
	{
		// 4.5 mid super freeze.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		for (FNSE010Battle* X : {&A, &C})
		{
			UNSE010Script* P1 = X->Script(X->Players[0]);
			P1->FreezeAtCounter = 20;
			P1->FreezeDuration = 12;
			P1->FreezeSelfDuration = 0;
			P1->DriftX = 500;
			X->Script(X->Players[1])->DriftX = -300;
		}
		RunAll({&A, &C}, Constant(20));
		TestTrue(TEXT("R4.5 control: a super freeze is active at frame 20"), A.Game->BattleState.SuperFreezeDuration > 0);
		TestTrue(TEXT("R4.5 control: P1 is the freeze caller"), A.Game->BattleState.SuperFreezeCaller == A.Players[0]);
		const int32 TraceB = B.Trace.Num();
		ExpectResult(*this, TEXT("R4.5 import mid super freeze is Restored"), Import(B, Export(A)), EBattleCheckpointResult::Restored);
		TestEqual(TEXT("R4.5 the mid-freeze import fired no callback"), B.Trace.Num(), TraceB);
		TestTrue(TEXT("R4.5 the freeze caller is the target's own P1"), B.Game->BattleState.SuperFreezeCaller == B.Players[0]);
		const int32 CursorC = C.Trace.Num();
		ExpectFollows(*this, TEXT("R4.5 freeze, freeze end callbacks and timer resume like control"), B, C, Constant(20), &A);
		ExpectTraceIs(*this, TEXT("R4.5 control: freeze end callbacks fired for both players"), C, CursorC, {5001, 5002});
		TestTrue(TEXT("R4.5 control: P2 drifted after the freeze ended"), C.Players[1]->PosX != P2X);
	}
	{
		// 4.6 mid hitstop.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		for (FNSE010Battle* X : {&A, &B, &C}) SetTemplateTravel(*X, 0);
		for (FNSE010Battle* X : {&A, &C})
		{
			X->Spawn(X->Players[0], OnP2HurtboxX, ShotY);
			X->Step();
		}
		TestEqual(TEXT("R4.6 control: P2 is in hitstop 3 on the hit frame"), static_cast<int32>(A.Players[1]->Hitstop), 3);
		TestEqual(TEXT("R4.6 control: P2 took the hit"), A.Players[1]->CurrentHealth, 9900);
		const int32 TraceB = B.Trace.Num();
		ExpectResult(*this, TEXT("R4.6 import mid hitstop is Restored"), Import(B, Export(A)), EBattleCheckpointResult::Restored);
		TestEqual(TEXT("R4.6 the mid-hitstop import fired no callback (no hit replayed)"), B.Trace.Num(), TraceB);
		const int32 CursorC = C.Trace.Num();
		ExpectFollows(*this, TEXT("R4.6 hitstop counts down like control with no second hit"), B, C, Constant(8), &A);
		ExpectTraceIs(*this, TEXT("R4.6 control: no second hit after the checkpoint"), C, CursorC, {});
	}
	{
		// 4.7 battle extension state.
		FNSE010Lineup L;
		L.bWithExtension = true;
		FNSE010Battle A(L);
		FNSE010Battle B(L);
		FNSE010Battle C(L);
		for (FNSE010Battle* X : {&A, &C})
		{
			X->Extension()->LiftPeriod = 7;
			X->Extension()->LiftAmount = 1000;
		}
		RunAll({&A, &C}, Constant(10));
		ExpectResult(*this, TEXT("R4.7 import with extension state is Restored"), Import(B, Export(A)), EBattleCheckpointResult::Restored);
		TestEqual(TEXT("R4.7 extension lift period restored"), B.Extension()->LiftPeriod, 7);
		TestEqual(TEXT("R4.7 extension ticks restored"), B.Extension()->Ticks, C.Extension()->Ticks);
		ExpectFollows(*this, TEXT("R4.7 extension lift schedule continues like control"), B, C, Constant(30), &A);
		TestTrue(TEXT("R4.7 control: the extension lifted P1"), C.Players[0]->PosY > 0);
	}
	{
		// 4.8 the target keeps its own configuration.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		B.Game->BattleState.MaxRoundCount = 3;
		B.Game->BattleState.MaxFadeTimer = 20;
		RunAll({&A, &C}, Slice(T1, 0, 10));
		ExpectResult(*this, TEXT("R4.8 import into a target with other round limits is Restored"), Import(B, Export(A)),
					 EBattleCheckpointResult::Restored);
		TestEqual(TEXT("R4.8 target keeps its MaxRoundCount"), B.Game->BattleState.MaxRoundCount, 3);
		TestEqual(TEXT("R4.8 target keeps its MaxFadeTimer"), B.Game->BattleState.MaxFadeTimer, 20);
		ExpectFollows(*this, TEXT("R4.8 target follows control with its own configuration"), B, C, Slice(T1, 10, 30), &A);
	}
	{
		// 4.9 runtime collision box edit before a first hit.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		for (FNSE010Battle* X : {&A, &B, &C}) SetTemplateTravel(*X, 0);
		for (FNSE010Battle* X : {&A, &C})
		{
			ABattleObject* Shot = X->Spawn(X->Players[0], 45000, ShotY);
			X->Step();
			Shot->PosX += 20000;
			if (Shot->Boxes.Num()) Shot->Boxes[0].SizeX = 8000;
		}
		TestEqual(TEXT("R4.9 control: no hit before the edit"), A.Players[1]->CurrentHealth, 10000);
		TestEqual(TEXT("R4.9 control: the armed shot has one box"), A.Game->Objects[0]->Boxes.Num(), 1);
		ExpectResult(*this, TEXT("R4.9 import with an edited box is Restored"), Import(B, Export(A)), EBattleCheckpointResult::Restored);
		TestEqual(TEXT("R4.9 the edited box width is restored, not re-derived from the cel"),
				  B.Game->Objects[0]->Boxes.Num() ? B.Game->Objects[0]->Boxes[0].SizeX : -1, 8000);
		const int32 CursorC = C.Trace.Num();
		ExpectFollows(*this, TEXT("R4.9 shrunk box misses like control"), B, C, Constant(3), &A);
		ExpectTraceIs(*this, TEXT("R4.9 control: the shrunk box never hit"), C, CursorC, {});
		for (FNSE010Battle* X : {&A, &B, &C})
		{
			if (X->Game->Objects[0]->Boxes.Num()) X->Game->Objects[0]->Boxes[0].SizeX = 20000;
		}
		const int32 CursorB = B.Trace.Num(), CursorA = A.Trace.Num();
		RunAll({&A, &B, &C}, Constant(1));
		ExpectTraceIs(*this, TEXT("R4.9 control: the widened box hits"), C, CursorC, {100, -3});
		ExpectTraceIs(*this, TEXT("R4.9 widened box hits in the target"), B, CursorB, {100, -3});
		ExpectTraceIs(*this, TEXT("R4.9 widened box hits in the source"), A, CursorA, {100, -3});
	}
	{
		// 4.10 no residue in non-current registered states.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		for (FNSE010Battle* X : {&A, &C})
		{
			const FGameplayTag Idle = IdleTag(*X);
			X->Players[0]->JumpToStatePrimary(StandTag());
			Run(*X, Constant(3));
			X->Players[0]->JumpToStatePrimary(Idle);
			X->Step();
		}
		const FGameplayTag IdleB = IdleTag(B);
		B.Players[0]->JumpToStatePrimary(StandTag());
		Run(B, Constant(7));
		B.Players[0]->JumpToStatePrimary(IdleB);
		TestEqual(TEXT("R4.10 control: the stranger's Stand counter is 8 before the import"), RegisteredScript(B.Players[0], StandTag())->Counter, 8);
		ExpectResult(*this, TEXT("R4.10 import over a stranger with other non-current state is Restored"), Import(B, Export(A)),
					 EBattleCheckpointResult::Restored);
		TestEqual(TEXT("R4.10 non-current Stand counter equals control right after the import"),
				  RegisteredScript(B.Players[0], StandTag())->Counter, RegisteredScript(C.Players[0], StandTag())->Counter);
		TestEqual(TEXT("R4.10 control: Stand counter is 4 (one entry execution plus three frames)"),
				  RegisteredScript(C.Players[0], StandTag())->Counter, 4);
		TestEqual(TEXT("R4.10 the stranger's Idle counter equals control right after the import"), Counter(B.Players[0]),
				  Counter(C.Players[0]));
		ExpectFollows(*this, TEXT("R4.10 target follows control after the import"), B, C, Constant(3), &A);
		TestEqual(TEXT("R4.10 non-current Stand counter still equals control after three frames"),
				  RegisteredScript(B.Players[0], StandTag())->Counter, RegisteredScript(C.Players[0], StandTag())->Counter);
		// Entering a state resets its saved fields, so after a re-entry every battle agrees again.
		for (FNSE010Battle* X : {&A, &B, &C}) X->Players[0]->JumpToStatePrimary(StandTag());
		ExpectFollows(*this, TEXT("R4.10 re-entering Stand follows control"), B, C, Constant(2), &A);
		TestEqual(TEXT("R4.10 control: entering a state resets its counter (1 plus two frames)"), Counter(C.Players[0]), 3);
	}
	return !HasAnyErrors();
}

// R5: references and scripts are bound to the target's own objects and registered states.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE010References, "UnrealBench.NSE010.References",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE010References::RunTest(const FString&)
{
	{
		// 5.1 ignore-hit list and 5.6 attack links.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		for (FNSE010Battle* X : {&A, &B, &C}) SetTemplateTravel(*X, 0);
		for (FNSE010Battle* X : {&A, &C})
		{
			X->Spawn(X->Players[0], OnP2HurtboxX, ShotY);
			X->Step();
		}
		ExpectTraceIs(*this, TEXT("R5.1 control: the shot hit P2 once"), A, 0, {100, -3});
		TestTrue(TEXT("R5.6 control: P2's attack owner is the shot"), A.Players[1]->AttackOwner == A.Game->Objects[0]);
		ExpectResult(*this, TEXT("R5.1 import after a hit is Restored"), Import(B, Export(A)), EBattleCheckpointResult::Restored);
		TestTrue(TEXT("R5.6 P2's attack owner is the target's own slot 0 object"),
				 B.Players[1]->AttackOwner == B.Game->Objects[0] && B.Game->Objects[0]->IsActive);
		TestEqual(TEXT("R5.6 the restored shot carries the source trace id"), Counter(B.Game->Objects[0]) >= 0 ? ScriptOf(B.Game->Objects[0])->TraceId : -1, 100);
		TestTrue(TEXT("R5.6 the shot's attack target is the target's own P2"), B.Game->Objects[0]->AttackTarget == B.Players[1]);
		TestTrue(TEXT("R5.1 P2's ignore-hit list names the target's own slot 0 object"),
				 B.Players[1]->ObjectsToIgnoreHitsFrom.Num() == 1 && B.Players[1]->ObjectsToIgnoreHitsFrom[0] == B.Game->Objects[0]);
		const int32 CursorB = B.Trace.Num();
		ExpectFollows(*this, TEXT("R5.1 no second hit after the import"), B, C, Constant(10), &A);
		ExpectTraceIs(*this, TEXT("R5.1 target trace gains nothing after the import"), B, CursorB, {});
		TestEqual(TEXT("R5.1 target health stays at one hit"), B.Players[1]->CurrentHealth, 9900);
	}
	{
		// 5.2 stored child followed by the owner.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		for (FNSE010Battle* X : {&A, &B, &C}) SetTemplateTravel(*X, 0);
		for (FNSE010Battle* X : {&A, &C})
		{
			ABattleObject* Shot = X->Spawn(X->Players[0], P1X, 200000);
			X->Store(X->Players[0], Shot, 3);
			X->Script(X->Players[0])->FollowStoredIndex = 3;
			X->Script(Shot)->DriftY = 200;
			Run(*X, Constant(5));
		}
		TestTrue(TEXT("R5.2 control: P1 follows the stored shot's height"), C.Players[0]->PosY > 200000);
		ExpectResult(*this, TEXT("R5.2 import with a stored child is Restored"), Import(B, Export(A)), EBattleCheckpointResult::Restored);
		TestTrue(TEXT("R5.2 stored child slot 3 is the target's own slot 0 object"),
				 B.Players[0]->StoredBattleObjects[3] == B.Game->Objects[0]);
		ExpectFollows(*this, TEXT("R5.2 the owner keeps following the stored child like control"), B, C, Constant(20), &A);
	}
	{
		// 5.3 camera targets and scroll state with a wall-clamped player.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		for (FNSE010Battle* X : {&A, &B, &C}) SetTemplateTravel(*X, 0);
		for (FNSE010Battle* X : {&A, &C})
		{
			ABattleObject* Shot = X->Spawn(X->Players[0], -1500000, ShotY);
			X->Game->BattleState.ScreenData.TargetObjects.Add(Shot);
			X->Players[1]->MiscFlags |= MISC_WallCollisionActive;
			Run(*X, Constant(20));
		}
		TestTrue(TEXT("R5.3 control: the screen scrolled toward the far shot"), C.Game->BattleState.ScreenData.ScreenWorldCenterX < 0);
		ExpectResult(*this, TEXT("R5.3 import with a camera target is Restored"), Import(B, Export(A)), EBattleCheckpointResult::Restored);
		TestTrue(TEXT("R5.3 the third camera target is the target's own slot 0 object"),
				 B.Game->BattleState.ScreenData.TargetObjects.Num() == 3 &&
					 B.Game->BattleState.ScreenData.TargetObjects[2] == B.Game->Objects[0]);
		ExpectFollows(*this, TEXT("R5.3 scroll and wall clamp continue like control"), B, C, Constant(30), &A);
		TestTrue(TEXT("R5.3 control: P2 was pushed by the scrolling screen edge"), C.Players[1]->PosX != P2X);
	}
	{
		// 5.4 main player identity and off-screen state in 1v2, then 5.8 registry binding.
		FNSE010Lineup L;
		L.TeamCountP2 = 2;
		FNSE010Battle A(L);
		FNSE010Battle B(L);
		FNSE010Battle C(L);
		for (FNSE010Battle* X : {&A, &C})
		{
			Run(*X, Constant(5));
			APlayerObject* Switched = X->Game->SwitchMainPlayer(X->Players[1], 1, true);
			TestTrue(TEXT("R5.4 control: the switch made the second team member main"), Switched == X->Players[2]);
			X->Players[1]->SetOnScreen(false);
			Run(*X, Constant(3));
		}
		TestEqual(TEXT("R5.4 control: the off-screen player's Idle counter froze at 6"), Counter(C.Players[1]), 6);
		TestEqual(TEXT("R5.4 control: the new main player's TagIn counter is 4"), Counter(C.Players[2]), 4);
		ExpectResult(*this, TEXT("R5.4 import of a 1v2 battle after a switch is Restored"), Import(B, Export(A)),
					 EBattleCheckpointResult::Restored);
		TestTrue(TEXT("R5.4 main player of side 2 is the target's own third player"), B.Game->GetMainPlayer(false) == B.Players[2]);
		TestTrue(TEXT("R5.4 P1's enemy is the target's own third player"), B.Players[0]->Enemy == B.Players[2]);
		TestFalse(TEXT("R5.4 the switched-out player is off screen"), (B.Players[1]->PlayerFlags & PLF_IsOnScreen) != 0);
		TestEqual(TEXT("R5.4 the switched-out player is in Idle"), StateName(B.Players[1]), IdleTag(B).ToString());
		TestEqual(TEXT("R5.4 the off-screen player's Idle counter is restored"), Counter(B.Players[1]), 6);
		TestEqual(TEXT("R5.4 the new main player is in TagIn"), StateName(B.Players[2]), TagInTag().ToString());
		TestEqual(TEXT("R5.4 the new main player's TagIn counter is restored"), Counter(B.Players[2]), 4);
		ExpectFollows(*this, TEXT("R5.4 side 2 inputs move the third player like control"), B, C, Constant(3, INP_Neutral, INP_Right), &A);
		TestTrue(TEXT("R5.4 control: side 2 input moved the new main player"), C.Players[2]->PosX != P2X);
		for (FNSE010Battle* X : {&A, &B, &C}) X->Players[1]->SetOnScreen(true);
		ExpectFollows(*this, TEXT("R5.4 the returned player resumes its Idle counter like control"), B, C, Constant(4), &A);
		TestEqual(TEXT("R5.4 control: the returned player's counter is 10"), Counter(C.Players[1]), 10);
		for (int32 I = 0; I < B.Players.Num(); ++I)
		{
			const APlayerObject* P = B.Players[I];
			const UState* Current = P->PrimaryStateMachine.CurrentState;
			const int32 Index = Current ? P->PrimaryStateMachine.StateNames.Find(Current->Name) : INDEX_NONE;
			TestTrue(FString::Printf(TEXT("R5.8 player %d's current state (%s) is the target's registered instance of that name"), I, *StateName(P)),
					 Index != INDEX_NONE && P->PrimaryStateMachine.States[Index] == Current);
		}
		TestEqual(TEXT("R5.8 the registered TagIn instance carries the current counter"),
				  RegisteredScript(B.Players[2], TagInTag())->Counter, Counter(C.Players[2]));
		for (FNSE010Battle* X : {&A, &B, &C}) X->Players[2]->JumpToStatePrimary(StandTag());
		ExpectFollows(*this, TEXT("R5.8 third player in Stand follows control"), B, C, Constant(2), &A);
		for (FNSE010Battle* X : {&A, &B, &C}) X->Players[2]->JumpToStatePrimary(TagInTag());
		ExpectFollows(*this, TEXT("R5.8 re-entering TagIn follows control"), B, C, Constant(2), &A);
		TestEqual(TEXT("R5.8 control: entering TagIn resets its counter (1 plus two frames)"), Counter(C.Players[2]), 3);
	}
	{
		// 5.5 object scripts carry the checkpoint's saved fields, not the template's.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		for (FNSE010Battle* X : {&A, &C})
		{
			X->ShotTemplate(X->Players[0])->Damage = 250;
			X->Spawn(X->Players[0], 40000, ShotY);
		}
		const FBytes Bytes = Export(A);
		for (FNSE010Battle* X : {&A, &B, &C}) X->ShotTemplate(X->Players[0])->Damage = 999;
		ExpectResult(*this, TEXT("R5.5 import of an unexecuted shot is Restored"), Import(B, Bytes), EBattleCheckpointResult::Restored);
		TestEqual(TEXT("R5.5 the restored shot's saved Damage comes from the checkpoint"),
				  ScriptOf(B.Game->Objects[0]) ? ScriptOf(B.Game->Objects[0])->Damage : -1, 250);
		ExpectFollows(*this, TEXT("R5.5 the shot arms and hits with the checkpoint's damage"), B, C, Constant(10), &A);
		TestEqual(TEXT("R5.5 control: P2 took 250 damage"), C.Players[1]->CurrentHealth, 9750);
		TestEqual(TEXT("R5.5 target P2 took 250 damage, not the template's 999"), B.Players[1]->CurrentHealth, 9750);
	}
	{
		// 5.7 a camera target that refers to an inactive pool slot.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		for (FNSE010Battle* X : {&A, &B, &C}) SetTemplateTravel(*X, 0);
		for (FNSE010Battle* X : {&A, &C})
		{
			X->Players[0]->PosX = 200000;
			X->Players[1]->PosX = 300000;
			ABattleObject* Shot = X->Spawn(X->Players[0], 280000, ShotY);
			X->Script(Shot)->bDeactivateOnHit = true;
			X->Game->BattleState.ScreenData.TargetObjects.Add(Shot);
			int32 Steps = 0;
			while (Shot->IsActive && Steps < 8)
			{
				X->Step();
				++Steps;
			}
			TestFalse(TEXT("R5.7 control: the shot deactivated after its hit"), Shot->IsActive);
			Run(*X, Constant(5));
		}
		ExpectResult(*this, TEXT("R5.7 import with a camera target in an inactive slot is Restored"), Import(B, Export(A)),
					 EBattleCheckpointResult::Restored);
		TestTrue(TEXT("R5.7 the camera target list still names the target's own inactive slot 0 object"),
				 B.Game->BattleState.ScreenData.TargetObjects.Num() == 3 &&
					 B.Game->BattleState.ScreenData.TargetObjects[2] == B.Game->Objects[0] && !B.Game->Objects[0]->IsActive);
		ExpectFollows(*this, TEXT("R5.7 scroll toward the reset object continues like control"), B, C, Constant(20), &A);
	}
	{
		// 5.9 position and stop links.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		for (FNSE010Battle* X : {&A, &B, &C}) SetTemplateTravel(*X, 0);
		for (FNSE010Battle* X : {&A, &C})
		{
			ABattleObject* S = X->Spawn(X->Players[0], P1X, 150000);
			S->PositionLinkObj = X->Players[0];
			Run(*X, Constant(3, INP_Right, INP_Neutral));
			TestEqual(TEXT("R5.9 control: the linked shot follows P1"), S->PosX, X->Players[0]->PosX);
			ABattleObject* H = X->Spawn(X->Players[0], OnP2HurtboxX, ShotY);
			X->Script(H)->HitstopFrames = 6;
			S->StopLinkObj = X->Players[1];
			X->Step(INP_Right, INP_Neutral);
		}
		TestEqual(TEXT("R5.9 control: P2 is in hitstop 6 on the export frame"), static_cast<int32>(A.Players[1]->Hitstop), 6);
		ExpectResult(*this, TEXT("R5.9 import with position and stop links is Restored"), Import(B, Export(A)),
					 EBattleCheckpointResult::Restored);
		TestTrue(TEXT("R5.9 the position link points at the target's own P1"), B.Game->Objects[0]->PositionLinkObj == B.Players[0]);
		TestTrue(TEXT("R5.9 the stop link points at the target's own P2"), B.Game->Objects[0]->StopLinkObj == B.Players[1]);
		ExpectFollows(*this, TEXT("R5.9 linked shot copies P1's position and P2's hitstop like control"), B, C,
					  Constant(10, INP_Right, INP_Neutral), &A);
	}
	{
		// 5.10 a restored script keeps its saved values while the target's template stays its own:
		// the restored shot hits for its saved damage, and after it dies the next spawn into the same
		// slot hits for the target's template damage, not the source's and not the restored value.
		// The oracle for the second phase is arithmetic on the health, not the source battle.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		for (FNSE010Battle* X : {&A, &C})
		{
			X->ShotTemplate(X->Players[0])->Damage = 250;
			ABattleObject* Shot = X->Spawn(X->Players[0], 40000, ShotY);
			X->Script(Shot)->Damage = 150;
			X->Script(Shot)->bDeactivateOnHit = true;
		}
		const FBytes Bytes = Export(A);
		B.ShotTemplate(B.Players[0])->Damage = 999;
		ExpectResult(*this, TEXT("R5.10 import of an unexecuted shot into a target with another template is Restored"), Import(B, Bytes),
					 EBattleCheckpointResult::Restored);
		TestEqual(TEXT("R5.10 the restored shot's saved Damage is the checkpoint's 150, not either template"),
				  ScriptOf(B.Game->Objects[0]) ? ScriptOf(B.Game->Objects[0])->Damage : -1, 150);
		TestEqual(TEXT("R5.10 the target's template is untouched by the import"), B.ShotTemplate(B.Players[0])->Damage, 999);
		ExpectFollows(*this, TEXT("R5.10 the restored shot hits for its saved damage"), B, C, Constant(10), &A);
		TestEqual(TEXT("R5.10 control: P2 took 150"), C.Players[1]->CurrentHealth, 9850);
		TestEqual(TEXT("R5.10 target P2 took 150"), B.Players[1]->CurrentHealth, 9850);
		for (int32 Steps = 0; C.Game->Objects[0]->IsActive && Steps < 10; ++Steps) RunAll({&A, &B, &C}, Constant(1));
		TestFalse(TEXT("R5.10 control: the restored shot deactivated after its hit"), C.Game->Objects[0]->IsActive);
		TestFalse(TEXT("R5.10 the target's slot 0 is free again"), B.Game->Objects[0]->IsActive);
		// A rejected incompatible import must not disturb the template either.
		FNSE010Lineup L21;
		L21.TeamCountP1 = 2;
		FNSE010Battle S(L21);
		ExpectResult(*this, TEXT("R5.10 a 2v1 checkpoint into the 1v1 target is LineupMismatch"), Import(B, Export(S)),
					 EBattleCheckpointResult::LineupMismatch);
		TestEqual(TEXT("R5.10 the template survives the rejected import"), B.ShotTemplate(B.Players[0])->Damage, 999);
		TestEqual(TEXT("R5.10 control: health before the second shot"), C.Players[1]->CurrentHealth, 9850);
		FTape Tape = Constant(35);
		Tape[0].In1 = INP_A;
		RunAll({&A, &B, &C}, Tape);
		TestTrue(TEXT("R5.10 control: the later shot reused slot 0"), C.Game->Objects[0]->IsActive && C.Game->Objects[0]->Player == C.Players[0]);
		TestEqual(TEXT("R5.10 control: the source's later shot hit for the source template's 250"), C.Players[1]->CurrentHealth, 9600);
		TestEqual(TEXT("R5.10 the source agrees with its control"), A.Players[1]->CurrentHealth, 9600);
		TestEqual(TEXT("R5.10 the target's later shot into the restored slot hit for the target template's 999"),
				  B.Players[1]->CurrentHealth, 9850 - 999);
	}
	return !HasAnyErrors();
}

// R6: update order of every pool slot, slot identity, capacity.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE010Order, "UnrealBench.NSE010.Order",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE010Order::RunTest(const FString&)
{
	{
		// 6.1 active order: A's slot 1 updates before slot 0, the stranger's does not.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		for (FNSE010Battle* X : {&A, &B, &C}) SetTemplateTravel(*X, 0);
		for (FNSE010Battle* X : {&A, &C})
		{
			UNSE010Script* Template = X->ShotTemplate(X->Players[0]);
			Template->TraceId = 100;
			ABattleObject* S0 = X->Spawn(X->Players[0], P1X, FarY);
			Template->TraceId = 110;
			ABattleObject* S1 = X->Spawn(X->Players[0], P1X, FarY + 50000);
			X->Script(S1)->HitstopFrames = 6;
			S0->DeactivateObject();
			X->Step();
			S1->PosX = OnP2HurtboxX;
			S1->PosY = ShotY;
			Template->TraceId = 100;
			ABattleObject* S0b = X->Spawn(X->Players[0], OnP2HurtboxX, ShotY);
			TestTrue(TEXT("R6.1 control: the second spawn reused slot 0"), S0b == X->Game->Objects[0]);
		}
		TestEqual(TEXT("R6.1 control: A's pool order is [1, 0, 2, 3]"), IntArrayString(SlotOrder(A)), FString(TEXT("[1, 0, 2, 3]")));
		{
			UNSE010Script* Template = B.ShotTemplate(B.Players[0]);
			Template->TraceId = 100;
			B.Spawn(B.Players[0], OnP2HurtboxX, ShotY);
			Template->TraceId = 110;
			B.Spawn(B.Players[0], OnP2HurtboxX, ShotY);
			Template->TraceId = 100;
		}
		TestEqual(TEXT("R6.1 control: the stranger's pool order is [0, 1, 2, 3]"), IntArrayString(SlotOrder(B)), FString(TEXT("[0, 1, 2, 3]")));
		ExpectResult(*this, TEXT("R6.1 import over a stranger with two shots is Restored"), Import(B, Export(A)),
					 EBattleCheckpointResult::Restored);
		TestEqual(TEXT("R6.1 pool update order restored"), IntArrayString(SlotOrder(B)), IntArrayString(SlotOrder(C)));
		const int32 CursorB = B.Trace.Num(), CursorC = C.Trace.Num();
		ExpectFollows(*this, TEXT("R6.1 both shots hit in the restored order"), B, C, Constant(1), &A);
		ExpectTraceIs(*this, TEXT("R6.1 control: slot 1 hits before slot 0"), C, CursorC, {110, -3, 100, -3});
		ExpectTraceIs(*this, TEXT("R6.1 target: slot 1 hits before slot 0"), B, CursorB, {110, -3, 100, -3});
		TestEqual(TEXT("R6.1 the last hit's hitstop wins on P2"), static_cast<int32>(B.Players[1]->Hitstop), 3);
	}
	{
		// 6.2 capacity is part of the lineup and of the bytes.
		FNSE010Lineup L2, L40;
		L2.PoolCapacity = 2;
		L40.PoolCapacity = 40;
		FNSE010Battle A2(L2);
		FNSE010Battle B2(L2);
		FNSE010Battle A40(L40);
		RunAll({&A2, &B2, &A40}, Constant(5));
		const FBytes Bytes2 = Export(A2), Bytes40 = Export(A40);
		ExpectPayload(*this, TEXT("R6.2 capacity 2 export has a payload"), Bytes2);
		ExpectDifferentBytes(*this, TEXT("R6.2 exports of capacity 2 and capacity 40 differ"), Bytes2, Bytes40);
		ExpectResult(*this, TEXT("R6.2 same capacity import is Restored"), Import(B2, Bytes2), EBattleCheckpointResult::Restored);
		ExpectResult(*this, TEXT("R6.2 capacity 40 checkpoint into capacity 2 is LineupMismatch"), Import(B2, Bytes40),
					 EBattleCheckpointResult::LineupMismatch);
	}
	{
		// 6.3 inactive order decides which of two same-frame shots updates first.
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		for (FNSE010Battle* X : {&A, &C})
		{
			ABattleObject* S0 = X->Spawn(X->Players[0], P1X, FarY);
			ABattleObject* S1 = X->Spawn(X->Players[0], P1X, FarY + 50000);
			S0->DeactivateObject();
			X->Step();
			S1->DeactivateObject();
			X->Step();
		}
		TestEqual(TEXT("R6.3 control: nothing active and order [1, 0, 2, 3]"), IntArrayString(SlotOrder(A)), FString(TEXT("[1, 0, 2, 3]")));
		TestEqual(TEXT("R6.3 control: no active object"), A.Game->BattleState.ActiveObjectCount, 2);
		ExpectResult(*this, TEXT("R6.3 import with nothing active is Restored"), Import(B, Export(A)), EBattleCheckpointResult::Restored);
		TestEqual(TEXT("R6.3 inactive slot order restored"), IntArrayString(SlotOrder(B)), IntArrayString(SlotOrder(C)));
		for (FNSE010Battle* X : {&A, &B, &C}) X->Players[1]->PosY = 50000;
		const int32 CursorB = B.Trace.Num(), CursorC = C.Trace.Num();
		FTape Tape = Constant(30);
		Tape[0].In1 = INP_A;
		Tape[0].In2 = INP_A;
		ExpectFollows(*this, TEXT("R6.3 same-frame shots update and hit in the restored order"), B, C, Tape, &A);
		TestTrue(TEXT("R6.3 control: P1's shot took slot 0 and P2's slot 1"),
				 C.Game->Objects[0]->Player == C.Players[0] && C.Game->Objects[1]->Player == C.Players[1]);
		ExpectTraceIs(*this, TEXT("R6.3 control: P2's shot (slot 1) hits first"), C, CursorC, {110, -2, 100, -3});
		ExpectTraceIs(*this, TEXT("R6.3 target: P2's shot (slot 1) hits first"), B, CursorB, {110, -2, 100, -3});
	}
	{
		// 6.4 slot identity: an active object returns to its own slot, not the first free one.
		FNSE010Battle A;
		FNSE010Battle B;
		ABattleObject* S0 = A.Spawn(A.Players[0], P1X, FarY);
		A.Spawn(A.Players[0], P1X, FarY + 50000);
		S0->DeactivateObject();
		A.Step();
		TestTrue(TEXT("R6.4 control: slot 0 inactive, slot 1 active"), !A.Game->Objects[0]->IsActive && A.Game->Objects[1]->IsActive);
		ExpectResult(*this, TEXT("R6.4 import with a hole in the pool is Restored"), Import(B, Export(A)), EBattleCheckpointResult::Restored);
		TestFalse(TEXT("R6.4 slot 0 stays inactive in the target"), B.Game->Objects[0]->IsActive);
		TestTrue(TEXT("R6.4 slot 1 is active in the target"), B.Game->Objects[1]->IsActive);
		TestEqual(TEXT("R6.4 slot 1's script counter equals the source's"), Counter(B.Game->Objects[1]), Counter(A.Game->Objects[1]));
		TestEqual(TEXT("R6.4 control: the surviving shot executed once"), Counter(A.Game->Objects[1]), 1);
	}
	return !HasAnyErrors();
}
