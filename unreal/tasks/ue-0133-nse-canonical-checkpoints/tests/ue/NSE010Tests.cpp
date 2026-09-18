// NSE010 portable checkpoints: header, canonical equality, discrimination, rejection,
// rollback regression and the fresh-spawn activation rule.
#include "NSE010TestCommon.h"
#include "Sound/SoundWave.h"
#include "UObject/UnrealType.h"
#include "NightSkyEngine/UI/NightSkyBattleHudActor.h"
#include "NightSkyEngine/UI/NightSkyBattleWidget.h"

using namespace NSE010;

// R1: magic, version and the unsupported version rejection.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE010Header, "UnrealBench.NSE010.Header",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE010Header::RunTest(const FString&)
{
	FNSE010Battle A;
	FNSE010Battle B;
	FNSE010Battle C;
	const FBytes Fresh = Export(A);
	ExpectHeader(*this, TEXT("R1 fresh battle export starts with NSKYCKPT and little-endian version 1"), Fresh);
	ExpectPayload(*this, TEXT("R1 fresh battle export carries a payload after the header"), Fresh);

	const FTape Tape = TapeT1();
	RunAll({&A, &B, &C}, Slice(Tape, 0, 10));
	const FBytes Bytes = Export(A);
	ExpectHeader(*this, TEXT("R1 export at frame 10 starts with NSKYCKPT and version 1"), Bytes);
	for (const uint32 Version : {2u, 0u, 0x01000001u})
	{
		const FBytes Before = Export(B);
		const FBytes Patched = WithVersion(Bytes, Version);
		ExpectResult(*this, FString::Printf(TEXT("R1 import of a checkpoint with version %u is UnsupportedVersion"), Version),
					 Import(B, Patched), EBattleCheckpointResult::UnsupportedVersion);
		ExpectSameBytes(*this, FString::Printf(TEXT("R1 export unchanged after rejecting version %u"), Version), Export(B),
						Before);
	}
	// A synthetic header with no payload and a wrong version is still an unsupported version.
	const FBytes Synthetic = WithVersion(FBytes{'N', 'S', 'K', 'Y', 'C', 'K', 'P', 'T'}, 7u);
	ExpectResult(*this, TEXT("R1 synthetic 12 byte header with version 7 is UnsupportedVersion"), Import(B, Synthetic),
				 EBattleCheckpointResult::UnsupportedVersion);
	ExpectFollows(*this, TEXT("R1 battle continues like the control after version rejections"), B, C, Slice(Tape, 10, 40));
	return !HasAnyErrors();
}

// R2: twins export identical bytes, presentation and addresses do not matter, export is pure.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE010Equality, "UnrealBench.NSE010.Equality",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE010Equality::RunTest(const FString&)
{
	{
		FNSE010Battle A;
		FNSE010Battle B;
		FNSE010Battle C;
		const FTape Tape = TapeT90();
		int32 FirstHitFrame = -1;
		for (int32 F = 0; F <= Tape.Num(); ++F)
		{
			if (F > 0)
			{
				A.Step(Tape[F - 1].In1, Tape[F - 1].In2);
				B.Step(Tape[F - 1].In1, Tape[F - 1].In2);
				C.Step(Tape[F - 1].In1, Tape[F - 1].In2);
			}
			if (FirstHitFrame < 0 && C.Trace.Contains(-3)) FirstHitFrame = F;
			// A exports every frame; B only at the checkpoints below.
			const FBytes EveryFrame = Export(A);
			if (F == 0 || F == 1 || F == 20 || F == 60 || F == 90 || F == FirstHitFrame + 1)
			{
				const FString Label = FString::Printf(TEXT("R2 twins export identical bytes at frame %d"), F);
				ExpectPayload(*this, Label, EveryFrame);
				ExpectSameBytes(*this, Label, EveryFrame, Export(B));
				ExpectSameBytes(*this, FString::Printf(TEXT("R2 two consecutive exports agree at frame %d"), F), EveryFrame,
								Export(A));
			}
			if (!ExpectObsEqual(*this, FString::Printf(TEXT("R2 exporting every frame leaves A equal to control at frame %d"), F),
								A, C))
			{
				break;
			}
		}
		TestTrue(TEXT("R2 control: the first shot hit P2 within the tape"), FirstHitFrame > 0 && FirstHitFrame < 60);
		TestEqual(TEXT("R2 control: P2 took two hits over the tape"), C.Players[1]->CurrentHealth, 9800);
		ExpectTraceEqual(*this, TEXT("R2 exporting every frame leaves A's trace equal to control"), A, 0, C, 0);

		// Presentation state in A only, gameplay untouched, taken before the next step.
		FBattleState& S = A.Game->BattleState;
		S.CommonAudioChannels[0].SoundWave = NewObject<USoundWave>(A.World);
		S.CommonAudioChannels[0].Finished = false;
		S.CommonAudioChannels[0].StartingFrame = 7;
		S.CameraPosition = FVector(1, 2, 3);
		S.OrthoBlendActive = 0.5f;
		ABattleObject* Shot = nullptr;
		for (ABattleObject* Obj : A.Game->Objects)
		{
			if (Obj->IsActive) Shot = Obj;
		}
		TestNotNull(TEXT("R2 control: an active shot exists at frame 90"), Shot);
		if (Shot)
		{
			Shot->MulColor = FLinearColor(0.5f, 0.5f, 0.5f, 1);
			Shot->AddColor = FLinearColor(0.2f, 0, 0, 1);
			Shot->ObjectOffset = FVector(1, 2, 3);
			Shot->ObjectRotation = FRotator(10, 0, 0);
			Shot->ObjectScale = FVector(2, 2, 2);
		}
		A.Players[0]->MulColor = FLinearColor(0.3f, 0.3f, 0.3f, 1);
		A.Players[1]->ObjectOffset = FVector(4, 5, 6);
		FRollbackAnimation Anim;
		Anim.Anim = nullptr;
		Anim.Time = 0.5f;
		Anim.bPlaying = true;
		A.Game->BattleHudActor->TopWidget->WidgetAnimationRollback.Add(Anim);
		ExpectSameBytes(*this, TEXT("R2 presentation state (audio, camera, HUD, colours, drawing transforms) does not change the bytes"),
						Export(A), Export(B));

		// Non-triviality: one different input on A only changes the bytes.
		A.Step(INP_Left, INP_Neutral);
		B.Step(INP_Neutral, INP_Neutral);
		ExpectDifferentBytes(*this, TEXT("R2 one extra frame with a different input on A only changes the bytes"), Export(A),
							 Export(B));
	}
	{
		// Freed slot independence: A used and freed slot 1, B never used it.
		FNSE010Battle A;
		FNSE010Battle B;
		ABattleObject* S0 = A.Spawn(A.Players[0], P1X, FarY);
		ABattleObject* S1 = A.Spawn(A.Players[0], P1X, FarY + 50000);
		TestTrue(TEXT("R2 control: two shots spawned into slots 0 and 1"), S0 == A.Game->Objects[0] && S1 == A.Game->Objects[1]);
		if (S1) S1->DeactivateObject();
		A.Step();
		B.Spawn(B.Players[0], P1X, FarY);
		B.Step();
		TestTrue(TEXT("R2 control: A has slot 0 active and slot 1 freed"),
				 A.Game->Objects[0]->IsActive && !A.Game->Objects[1]->IsActive);
		TestEqual(TEXT("R2 control: slot order unchanged by freeing the last used slot"), IntArrayString(SlotOrder(A)),
				  IntArrayString(SlotOrder(B)));
		ExpectSameBytes(*this, TEXT("R2 a freed pool slot (used then reset) does not change the bytes"), Export(A), Export(B));
	}
	{
		// Address independence: reverse construction order.
		FNSE010Battle B;
		FNSE010Battle A;
		RunAll({&A, &B}, Slice(TapeT1(), 0, 12));
		const FBytes BytesA = Export(A);
		ExpectPayload(*this, TEXT("R2 export after 12 frames carries a payload"), BytesA);
		ExpectSameBytes(*this, TEXT("R2 battles built in the opposite order export identical bytes"), BytesA, Export(B));
	}
	return !HasAnyErrors();
}

// The reflection sweep: the engine's own SaveForRollback serializes with ArIsSaveGame, so the
// CPF_SaveGame flag is the engine's statement of what is state. The sweep collects every
// SaveGame-tagged field of a class or struct and perturbs each on one twin battle only. The
// names the instruction already classes outside gameplay state stay out (actor handles,
// drawing offsets, collision-derived animation data); Boxes and ObjectsToIgnoreHitsFrom are
// state and are swept like everything else.
namespace
{
struct FNSE010SweepTarget
{
	FString Path;
	FProperty* Prop = nullptr;
	void* Data = nullptr;
};

bool NSE010SweepExcluded(const FName& Name)
{
	return Name == TEXT("LinkedActor") || Name == TEXT("StoredLinkActors") ||
		   Name == TEXT("ScreenSpaceDepthOffset") || Name == TEXT("AnimStructs");
}

void NSE010CollectSweep(const FString& Path, UStruct* Type, void* Data, TArray<FNSE010SweepTarget>& Out)
{
	for (TFieldIterator<FProperty> It(Type); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop->HasAnyPropertyFlags(CPF_SaveGame) || NSE010SweepExcluded(Prop->GetFName())) continue;
		void* Member = Prop->ContainerPtrToValuePtr<void>(Data);
		for (int32 Dim = 0; Dim < Prop->ArrayDim; ++Dim)
		{
			const FString Name = Prop->ArrayDim > 1
				? Path + Prop->GetName() + FString::Printf(TEXT("[%d]"), Dim)
				: Path + Prop->GetName();
			void* Elem = static_cast<uint8*>(Member) + Dim * Prop->ElementSize;
			if (const FStructProperty* Struct = CastField<FStructProperty>(Prop))
			{
				if (Struct->Struct == FGameplayTag::StaticStruct() ||
					Struct->Struct == FGameplayTagContainer::StaticStruct())
					Out.Add({Name, Prop, Elem});
				else NSE010CollectSweep(Name + TEXT("/"), Struct->Struct, Elem, Out);
			}
			else Out.Add({Name, Prop, Elem});
		}
	}
}

// +1 for integers, a flip for bools, one added element for arrays, a tag toggle for tags;
// an object reference toggles between the battle's own registered states.
bool NSE010Perturb(const FNSE010Battle& B, FProperty* Prop, void* Data)
{
	if (FBoolProperty* Bool = CastField<FBoolProperty>(Prop))
	{
		Bool->SetPropertyValue(Data, !Bool->GetPropertyValue(Data));
		return true;
	}
	if (FEnumProperty* Enum = CastField<FEnumProperty>(Prop))
		return NSE010Perturb(B, Enum->GetUnderlyingProperty(), Data);
	if (FNumericProperty* Numeric = CastField<FNumericProperty>(Prop))
	{
		if (Numeric->IsInteger())
			Numeric->SetIntPropertyValue(Data, Numeric->GetUnsignedIntPropertyValue(Data) + 1);
		else Numeric->SetFloatingPointPropertyValue(Data, Numeric->GetFloatingPointPropertyValue(Data) + 1);
		return true;
	}
	if (FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Prop))
	{
		const TArray<UState*>& States = B.Players[0]->PrimaryStateMachine.States;
		if (!States.Num()) return false;
		UState* Current = Cast<UState>(Object->GetObjectPropertyValue(Data));
		Object->SetObjectPropertyValue(Data, Current == States[0] && States.Num() > 1 ? States.Last() : States[0]);
		return true;
	}
	if (FArrayProperty* Array = CastField<FArrayProperty>(Prop))
	{
		FScriptArrayHelper Helper(Array, Data);
		const int32 Index = Helper.AddValue();
		if (const FStructProperty* Inner = CastField<FStructProperty>(Array->Inner))
		{
			if (Inner->Struct == FGameplayTag::StaticStruct())
				*reinterpret_cast<FGameplayTag*>(Helper.GetRawPtr(Index)) = StandTag();
			else if (Inner->Struct == FGameplayTagContainer::StaticStruct())
				reinterpret_cast<FGameplayTagContainer*>(Helper.GetRawPtr(Index))->AddTag(StandTag());
		}
		return true;
	}
	if (FStructProperty* Struct = CastField<FStructProperty>(Prop))
	{
		if (Struct->Struct == FGameplayTag::StaticStruct())
		{
			FGameplayTag& Target = *static_cast<FGameplayTag*>(Data);
			Target = Target == StandTag() ? TagInTag() : StandTag();
			return true;
		}
		if (Struct->Struct == FGameplayTagContainer::StaticStruct())
		{
			FGameplayTagContainer& Target = *static_cast<FGameplayTagContainer*>(Data);
			if (Target.HasTag(StandTag())) Target.RemoveTag(StandTag());
			else Target.AddTag(StandTag());
			return true;
		}
	}
	return false;
}

// Saves one property value out of the twin so every case leaves the battle as it found it
// and one twin pair per lineup carries the whole sweep.
struct FNSE010PropBackup
{
	FProperty* Prop;
	void* Buffer;
	explicit FNSE010PropBackup(FProperty* InProp, void* Data) : Prop(InProp)
	{
		Buffer = FMemory::Malloc(Prop->GetSize(), Prop->GetMinAlignment());
		Prop->InitializeValue(Buffer);
		Prop->CopyCompleteValue(Buffer, Data);
	}
	~FNSE010PropBackup()
	{
		Prop->DestroyValue(Buffer);
		FMemory::Free(Buffer);
	}
	void Restore(void* Data) const { Prop->CopyCompleteValue(Data, Buffer); }
};

int32 NSE010SweepGroup(FAutomationTestBase& T, const FString& Group, FNSE010Battle& A, FNSE010Battle& B,
					   UStruct* Type, void* DataB)
{
	const FBytes Base = Export(A);
	if (!ExpectPayload(T, TEXT("R3 sweep control (") + Group + TEXT("): the twin exports a payload"), Base))
	{
		T.AddInfo(TEXT("R3 sweep (") + Group + TEXT(") skipped: the export carries no payload"));
		return 0;
	}
	ExpectSameBytes(T, TEXT("R3 sweep control (") + Group + TEXT("): twins agree before the sweep"), Base, Export(B));
	TArray<FNSE010SweepTarget> Targets;
	NSE010CollectSweep(TEXT(""), Type, DataB, Targets);
	int32 Cases = 0;
	for (const FNSE010SweepTarget& Target : Targets)
	{
		const FString Label = TEXT("R3 sweep ") + Group + TEXT("/") + Target.Path;
		FNSE010PropBackup Backup(Target.Prop, Target.Data);
		if (!NSE010Perturb(B, Target.Prop, Target.Data))
		{
			T.AddError(Label + TEXT(": no perturbation available for this property"));
			continue;
		}
		++Cases;
		ExpectDifferentBytes(T, Label + TEXT(" differs"), Base, Export(B));
		Backup.Restore(Target.Data);
		ExpectSameBytes(T, Label + TEXT(" undone restores the twin bytes"), Base, Export(B));
	}
	T.AddInfo(FString::Printf(TEXT("R3 sweep (%s): %d SaveGame properties"), *Group, Cases));
	return Cases;
}
} // namespace

// R3: every gameplay difference changes the bytes.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE010Discrimination, "UnrealBench.NSE010.Discrimination",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE010Discrimination::RunTest(const FString&)
{
	FTape Prefix = Constant(30);
	for (int32 I = 0; I < 10; ++I)
	{
		Prefix[I].In1 = INP_Right;
		Prefix[I].In2 = INP_Right;
	}
	int32 Cases = 0;
	// Setup runs on both twins in turn, Perturb on B only.
	auto Case = [&](const TCHAR* Name, TFunctionRef<void(FNSE010Battle&)> Setup, TFunctionRef<void(FNSE010Battle&)> Perturb,
					const FNSE010Lineup& Lineup = FNSE010Lineup()) {
		FNSE010Battle A(Lineup);
		FNSE010Battle B(Lineup);
		Run(A, Prefix);
		Run(B, Prefix);
		Setup(A);
		Setup(B);
		const FBytes Same = Export(A);
		ExpectSameBytes(*this, FString::Printf(TEXT("R3 control: twins agree before the perturbation (%s)"), Name), Same,
						Export(B));
		Perturb(B);
		const FBytes BytesA = Export(A);
		ExpectPayload(*this, FString::Printf(TEXT("R3 export has a payload (%s)"), Name), BytesA);
		ExpectDifferentBytes(*this, FString::Printf(TEXT("R3 bytes change when %s"), Name), BytesA, Export(B));
		++Cases;
	};
	auto Nothing = [](FNSE010Battle&) {};

	{
		// The input-history case needs the twins to diverge mid-prefix, so it is built by hand.
		FNSE010Battle A;
		FNSE010Battle B;
		for (int32 F = 0; F < 12; ++F)
		{
			A.Step(Prefix[F].In1, Prefix[F].In2);
			B.Step(Prefix[F].In1, Prefix[F].In2);
		}
		A.Step(INP_Neutral, INP_Neutral);
		B.Step(INP_Down, INP_Neutral);
		RunAll({&A, &B}, Constant(10));
		TestTrue(TEXT("R3 control: the Down input left positions and health equal"),
				 A.Players[0]->PosX == B.Players[0]->PosX && A.Players[0]->PosY == B.Players[0]->PosY &&
					 A.Players[1]->PosX == B.Players[1]->PosX && A.Players[1]->CurrentHealth == B.Players[1]->CurrentHealth &&
					 A.Players[0]->Inputs == B.Players[0]->Inputs);
		bool bHistoryDiffers = false;
		for (int32 I = 0; I < InputBufferSize; ++I)
		{
			bHistoryDiffers |= A.Players[0]->StoredInputBuffer.InputBufferInternal[I] !=
							   B.Players[0]->StoredInputBuffer.InputBufferInternal[I];
		}
		TestTrue(TEXT("R3 control: the twins differ in the input history"), bHistoryDiffers);
		ExpectDifferentBytes(*this, TEXT("R3 bytes change when one input reached only the input history"), Export(A), Export(B));
	}
	Case(TEXT("P2 health differs by 1"), Nothing, [](FNSE010Battle& B) { B.Players[1]->CurrentHealth -= 1; });
	Case(TEXT("P1 PosX differs by 1"), Nothing, [](FNSE010Battle& B) { B.Players[0]->PosX += 1; });
	Case(TEXT("P1 pitch angle differs"), Nothing, [](FNSE010Battle& B) { B.Players[0]->AnglePitch_x1000 += 1000; });
	Case(TEXT("random seed differs"), Nothing, [](FNSE010Battle& B) {
		B.Game->BattleState.RandomManager.Reseed(B.Game->BattleState.RandomManager.GetSeed() + 1);
	});
	Case(TEXT("round timer differs"), Nothing, [](FNSE010Battle& B) { B.Game->BattleState.RoundTimer -= 1; });
	Case(TEXT("frame number differs"), Nothing, [](FNSE010Battle& B) { B.Game->BattleState.FrameNumber += 1; });
	Case(TEXT("P1 rounds won differs"), Nothing, [](FNSE010Battle& B) { B.Game->BattleState.P1RoundsWon += 1; });
	Case(TEXT("P2 meter differs"), Nothing, [](FNSE010Battle& B) { B.Game->BattleState.Meter[1] = 5; });
	Case(TEXT("a runtime collision box edit on an armed shot"),
		 [](FNSE010Battle& X) {
			 SetTemplateTravel(X, 0);
			 X.Spawn(X.Players[0], P1X, FarY);
			 X.Step();
		 },
		 [this](FNSE010Battle& B) {
			 TestEqual(TEXT("R3 control: the armed shot carries one box"), B.Game->Objects[0]->Boxes.Num(), 1);
			 if (B.Game->Objects[0]->Boxes.Num()) B.Game->Objects[0]->Boxes[0].SizeX -= 1;
		 });
	Case(TEXT("one extra active shot"), Nothing, [](FNSE010Battle& B) { B.Spawn(B.Players[0], P1X, FarY); });
	Case(TEXT("a shot spawned from a template with different damage"), Nothing, [](FNSE010Battle& B) {
		B.ShotTemplate(B.Players[0])->Damage = 250;
		B.Spawn(B.Players[0], P1X, FarY);
	});
	Case(TEXT("a stored child reference exists"), [](FNSE010Battle& X) { X.Spawn(X.Players[0], P1X, FarY); },
		 [](FNSE010Battle& B) { B.Store(B.Players[0], B.Game->Objects[0], 0); });
	Case(TEXT("a camera target was added"), [](FNSE010Battle& X) { X.Spawn(X.Players[0], P1X, FarY); },
		 [](FNSE010Battle& B) { B.Game->BattleState.ScreenData.TargetObjects.Add(B.Game->Objects[0]); });
	Case(TEXT("screen world center differs"), Nothing, [](FNSE010Battle& B) { B.Game->BattleState.ScreenData.ScreenWorldCenterX += 1; });
	Case(TEXT("an ignore-hit entry exists"),
		 [](FNSE010Battle& X) {
			 SetTemplateTravel(X, 0);
			 X.Spawn(X.Players[0], P1X, FarY);
			 X.Step();
		 },
		 [](FNSE010Battle& B) { B.Players[1]->ObjectsToIgnoreHitsFrom.Add(B.Game->Objects[0]); });
	Case(TEXT("a shot's position link differs"), [](FNSE010Battle& X) { X.Spawn(X.Players[0], P1X, FarY); },
		 [](FNSE010Battle& B) { B.Game->Objects[0]->PositionLinkObj = B.Players[0]; });
	Case(TEXT("P2 is off screen"), Nothing, [](FNSE010Battle& B) { B.Players[1]->SetOnScreen(false); });
	{
		// Pool slot order: A freed slot 0 before slot 1 (order O1, O0), B never spawned.
		FNSE010Battle A;
		FNSE010Battle B;
		Run(A, Prefix);
		Run(B, Prefix);
		ABattleObject* S0 = A.Spawn(A.Players[0], P1X, FarY);
		ABattleObject* S1 = A.Spawn(A.Players[0], P1X, FarY + 50000);
		S0->DeactivateObject();
		A.Step();
		B.Step();
		S1->DeactivateObject();
		A.Step();
		B.Step();
		TestEqual(TEXT("R3 control: A's pool order is [1, 0, 2, 3] with nothing active"), IntArrayString(SlotOrder(A)),
				  FString(TEXT("[1, 0, 2, 3]")));
		TestEqual(TEXT("R3 control: A has no active object"), A.Game->BattleState.ActiveObjectCount, 2);
		ExpectDifferentBytes(*this, TEXT("R3 bytes change when the update order of inactive pool slots differs"), Export(A),
							 Export(B));
	}
	// The exhaustive part: every CPF_SaveGame field of the checkpoint's own state classes must
	// change the bytes, not just the handful the cases above sample. Two twin pairs (one per
	// lineup) carry all sweep groups; every perturbation is undone before the next case, so the
	// sweep adds a handful of battles rather than one per field.
	int32 Swept = 0;
	{
		FNSE010Battle A;
		FNSE010Battle B;
		RunAll({&A, &B}, Prefix);
		for (FNSE010Battle* X : {&A, &B})
		{
			X->Players[0]->JumpToStatePrimary(StandTag());
			X->Step();
			X->Step();
			X->Players[0]->JumpToStatePrimary(IdleTag(*X));
			X->Step();
			X->Players[1]->SetOnScreen(false);
			RunAll({X}, Constant(5));
			SetTemplateTravel(*X, 0);
			X->Spawn(X->Players[0], P1X, FarY);
			X->Step();
		}
		UNSE010Script* StandA = RegisteredScript(A.Players[0], StandTag());
		UNSE010Script* StandB = RegisteredScript(B.Players[0], StandTag());
		UNSE010Script* ShotA = ScriptOf(A.Game->Objects[0]);
		UNSE010Script* ShotB = ScriptOf(B.Game->Objects[0]);
		TestTrue(TEXT("R3 control: the armed shot is active with a live script"),
				 A.Game->Objects[0]->IsActive && ShotA && ShotB);
		TestTrue(TEXT("R3 control: a non-current registered state exists"), StandA && StandB);
		TestFalse(TEXT("R3 control: P2 is off screen"), (B.Players[1]->PlayerFlags & PLF_IsOnScreen) != 0);
		if (ShotA && ShotB && StandA && StandB)
		{
			Swept += NSE010SweepGroup(*this, TEXT("UNSE010Script of P1's current state"), A, B,
									  UNSE010Script::StaticClass(), ScriptOf(B.Players[0]));
			Swept += NSE010SweepGroup(*this, TEXT("UNSE010Script of a non-current registered state"), A, B,
									  UNSE010Script::StaticClass(), StandB);
			Swept += NSE010SweepGroup(*this, TEXT("UNSE010Script of an off-screen player's state"), A, B,
									  UNSE010Script::StaticClass(), ScriptOf(B.Players[1]));
			Swept += NSE010SweepGroup(*this, TEXT("UNSE010Script of an active object"), A, B,
									  UNSE010Script::StaticClass(), ShotB);
			Swept += NSE010SweepGroup(*this, TEXT("APlayerObject of P1"), A, B,
									  APlayerObject::StaticClass(), B.Players[0]);
			Swept += NSE010SweepGroup(*this, TEXT("ABattleObject of an active object"), A, B,
									  ABattleObject::StaticClass(), B.Game->Objects[0]);
			Swept += NSE010SweepGroup(*this, TEXT("FBattleState"), A, B,
									  FBattleState::StaticStruct(), &B.Game->BattleState);
		}
	}
	{
		FNSE010Lineup WithExtension;
		WithExtension.bWithExtension = true;
		FNSE010Battle E(WithExtension);
		FNSE010Battle F(WithExtension);
		RunAll({&E, &F}, Prefix);
		UNSE010Extension* ExtE = E.Extension();
		UNSE010Extension* ExtF = F.Extension();
		TestTrue(TEXT("R3 control: the extension battle carries an extension"), ExtE && ExtF);
		if (ExtE && ExtF)
		{
			Swept += NSE010SweepGroup(*this, TEXT("UNSE010Extension"), E, F,
									  UNSE010Extension::StaticClass(), ExtF);
		}
	}
	AddInfo(FString::Printf(TEXT("R3 ran %d twin cases and swept %d SaveGame properties"), Cases, Swept));
	return !HasAnyErrors();
}

// R7: malformed, unsupported and mismatched checkpoints are rejected without touching the battle.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE010Rejection, "UnrealBench.NSE010.Rejection",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE010Rejection::RunTest(const FString&)
{
	FNSE010Battle A;
	FNSE010Battle B;
	FNSE010Battle C;
	RunAll({&A, &B, &C}, Slice(TapeT1(), 0, 10));
	const FBytes Bytes = Export(A);
	const bool bPayload = ExpectPayload(*this, TEXT("R7 the source export has a payload"), Bytes);

	const EBattleCheckpointResult Malformed = EBattleCheckpointResult::Malformed;
	const EBattleCheckpointResult Unsupported = EBattleCheckpointResult::UnsupportedVersion;
	const EBattleCheckpointResult Mismatch = EBattleCheckpointResult::LineupMismatch;
	// Damage to the payload is rejected; whether an implementation notices the damage before or
	// after reading the lineup is its own business, so either rejection code is accepted.
	const TArray<EBattleCheckpointResult> Damaged = {Malformed, Mismatch};
	auto Reject = [&](const FString& Label, FNSE010Battle& Target, const FBytes& Attempt,
					  const TArray<EBattleCheckpointResult>& Accepted) {
		const FBytes Before = Export(Target);
		ExpectResultIn(*this, Label, Import(Target, Attempt), Accepted);
		return ExpectSameBytes(*this, Label + TEXT(" leaves the export unchanged"), Export(Target), Before);
	};

	Reject(TEXT("R7 empty array is Malformed"), B, FBytes(), {Malformed});
	Reject(TEXT("R7 one byte array is Malformed"), B, FBytes{'N'}, {Malformed});
	Reject(TEXT("R7 eleven byte array with the right prefix is Malformed"), B,
		   FBytes{'N', 'S', 'K', 'Y', 'C', 'K', 'P', 'T', 1, 0, 0}, {Malformed});
	Reject(TEXT("R7 wrong magic (NSKYCKPX) with version 1 is Malformed"), B,
		   FBytes{'N', 'S', 'K', 'Y', 'C', 'K', 'P', 'X', 1, 0, 0, 0}, {Malformed});
	if (bPayload)
	{
		FBytes Magic = Bytes;
		Magic[3] ^= 0x20;
		Reject(TEXT("R7 magic altered at byte 3 is Malformed"), B, Magic, {Malformed});
		Reject(TEXT("R7 version bytes altered to 2 is UnsupportedVersion"), B, WithVersion(Bytes, 2), {Unsupported});
		Reject(TEXT("R7 version bytes altered to 0 is UnsupportedVersion"), B, WithVersion(Bytes, 0), {Unsupported});

		// Single byte alterations at fixed and seeded random positions.
		TArray<int32> Positions = {0, 3, 7, 8, 11, 12, 13, Bytes.Num() / 2, Bytes.Num() - 2, Bytes.Num() - 1};
		FRandomStream Rand(10133);
		for (int32 I = 0; I < 200; ++I)
		{
			Positions.Add(Rand.GetFraction() < 0.25f ? Rand.RandRange(0, FMath::Min(31, Bytes.Num() - 1))
													 : Rand.RandRange(0, Bytes.Num() - 1));
		}
		int32 Failures = 0;
		for (int32 K = 0; K < Positions.Num() && Failures < 8; ++K)
		{
			const int32 Pos = Positions[K];
			FBytes Altered = Bytes;
			const uint8 Mask = static_cast<uint8>(Rand.RandRange(1, 255));
			Altered[Pos] ^= Mask;
			const TArray<EBattleCheckpointResult> Expected = (Pos >= 8 && Pos <= 11) ? TArray<EBattleCheckpointResult>{Unsupported}
															 : Pos < 8 ? TArray<EBattleCheckpointResult>{Malformed} : Damaged;
			const FBytes Before = Export(B);
			if (!ExpectResultIn(*this, FString::Printf(TEXT("R7 byte %d of %d xor 0x%02x is rejected"), Pos, Bytes.Num(), Mask),
								Import(B, Altered), Expected))
			{
				++Failures;
			}
			if (!ExpectSameBytes(*this, FString::Printf(TEXT("R7 export unchanged after altering byte %d"), Pos), Export(B), Before))
			{
				++Failures;
			}
		}
		ExpectFollows(*this, TEXT("R7 battle continues like the control after single byte alterations"), B, C, Constant(30));

		FBytes Truncated1 = Bytes;
		Truncated1.RemoveAt(Truncated1.Num() - 1);
		Reject(TEXT("R7 truncated by one byte is rejected"), B, Truncated1, Damaged);
		FBytes Half = Bytes;
		Half.SetNum(Bytes.Num() / 2);
		Reject(TEXT("R7 truncated to half is rejected"), B, Half, Damaged);
		FBytes HeaderOnly = Bytes;
		HeaderOnly.SetNum(12);
		Reject(TEXT("R7 truncated to the header is rejected"), B, HeaderOnly, Damaged);
		FBytes Plus1 = Bytes;
		Plus1.Add(0);
		Reject(TEXT("R7 one appended byte is rejected"), B, Plus1, Damaged);
		FBytes Plus64 = Bytes;
		for (int32 I = 0; I < 64; ++I) Plus64.Add(static_cast<uint8>(I));
		Reject(TEXT("R7 64 appended bytes is rejected"), B, Plus64, Damaged);
		ExpectFollows(*this, TEXT("R7 battle continues like the control after truncations and extensions"), B, C, Constant(30));
	}

	// Lineups.
	{
		FNSE010Lineup L12;
		L12.TeamCountP2 = 2;
		FNSE010Battle T(L12);
		FNSE010Battle TC(L12);
		Reject(TEXT("R7 1v1 checkpoint into a 1v2 target is LineupMismatch"), T, Bytes, {Mismatch});
		ExpectFollows(*this, TEXT("R7 1v2 target continues like its control after the mismatch"), T, TC, Slice(TapeT1(), 0, 30));
	}
	{
		FNSE010Lineup L21;
		L21.TeamCountP1 = 2;
		FNSE010Battle S(L21);
		Run(S, Slice(TapeT1(), 0, 10));
		Reject(TEXT("R7 2v1 checkpoint into a 1v1 target is LineupMismatch"), B, Export(S), {Mismatch});
	}
	{
		FNSE010Lineup L5;
		L5.PoolCapacity = 5;
		FNSE010Battle T(L5);
		Reject(TEXT("R7 capacity 4 checkpoint into a capacity 5 target is LineupMismatch"), T, Bytes, {Mismatch});
		Run(T, Slice(TapeT1(), 0, 10));
		Reject(TEXT("R7 capacity 5 checkpoint into a capacity 4 target is LineupMismatch"), B, Export(T), {Mismatch});
	}
	{
		FNSE010Lineup LAlt;
		LAlt.bAltClassP2 = true;
		FNSE010Battle T(LAlt);
		Reject(TEXT("R7 default class checkpoint into an alternate class target is LineupMismatch"), T, Bytes, {Mismatch});
		Run(T, Slice(TapeT1(), 0, 10));
		Reject(TEXT("R7 alternate class checkpoint into a default class target is LineupMismatch"), B, Export(T), {Mismatch});
	}
	{
		FNSE010Lineup LExt;
		LExt.bWithExtension = true;
		FNSE010Battle T(LExt);
		Reject(TEXT("R7 checkpoint without extension into a target with one is LineupMismatch"), T, Bytes, {Mismatch});
		Run(T, Slice(TapeT1(), 0, 10));
		const FBytes OneExtension = Export(T);
		Reject(TEXT("R7 checkpoint with extension into a target without is LineupMismatch"), B, OneExtension, {Mismatch});

		// Extension identity: a second extension with another name, and registration order.
		const FGameplayTag Second = Tag(TEXT("BattleExtension.RoundInit"));
		FNSE010Battle T2(LExt);
		UNSE010Extension* Extra = NewObject<UNSE010Extension>(T2.Game);
		Extra->Parent = T2.Game;
		Extra->Name = Second;
		T2.Game->BattleExtensions.Add(Extra);
		T2.Game->BattleExtensionNames.Add(Second);
		Reject(TEXT("R7 one-extension checkpoint into a two-extension target is LineupMismatch"), T2, OneExtension, {Mismatch});
		FNSE010Battle S2(LExt);
		UNSE010Extension* ExtraS = NewObject<UNSE010Extension>(S2.Game);
		ExtraS->Parent = S2.Game;
		ExtraS->Name = Second;
		S2.Game->BattleExtensions.Insert(ExtraS, 0);
		S2.Game->BattleExtensionNames.Insert(Second, 0);
		Run(S2, Slice(TapeT1(), 0, 10));
		Run(T2, Slice(TapeT1(), 0, 10));
		const FBytes Reversed = Export(S2);
		Reject(TEXT("R7 two extensions registered in the opposite order is LineupMismatch"), T2, Reversed, {Mismatch});
		FNSE010Battle S3(LExt);
		UNSE010Extension* ExtraS3 = NewObject<UNSE010Extension>(S3.Game);
		ExtraS3->Parent = S3.Game;
		ExtraS3->Name = Second;
		S3.Game->BattleExtensions.Add(ExtraS3);
		S3.Game->BattleExtensionNames.Add(Second);
		Run(S3, Slice(TapeT1(), 0, 10));
		ExpectResult(*this, TEXT("R7 two extensions registered in the same order is Restored"), Import(T2, Export(S3)),
					 EBattleCheckpointResult::Restored);
	}
	{
		// Missing registration: the target lacks the last registered state (TagIn, never entered in 1v1).
		FNSE010Battle T;
		FNSE010Battle TC;
		for (FNSE010Battle* X : {&T, &TC})
		{
			FStateMachine& M = X->Players[0]->PrimaryStateMachine;
			M.States.RemoveAt(M.States.Num() - 1);
			M.StateNames.RemoveAt(M.StateNames.Num() - 1);
		}
		Reject(TEXT("R7 checkpoint naming a state the target has not registered is LineupMismatch"), T, Bytes, {Mismatch});
		ExpectFollows(*this, TEXT("R7 target without the registration continues like its control"), T, TC, Slice(TapeT1(), 0, 30));
	}
	{
		// A rejection must not touch the live random generator either: the seed is the
		// generator's whole state, so an import that fails may not reseed it on the way out.
		// T and TC random-walk their seeds off the checkpoint's, and T lacks the source's last
		// registered state, so the mismatch fires only after the battle record, seed included,
		// has been read.
		FNSE010Battle T;
		FNSE010Battle TC;
		for (FNSE010Battle* X : {&T, &TC})
		{
			X->Script(X->Players[0])->RandomWalk = 700;
			FStateMachine& M = X->Players[0]->PrimaryStateMachine;
			M.States.RemoveAt(M.States.Num() - 1);
			M.StateNames.RemoveAt(M.StateNames.Num() - 1);
		}
		RunAll({&T, &TC}, Slice(TapeT1(), 0, 10));
		const uint32 Seed = T.Game->BattleState.RandomManager.GetSeed();
		TestTrue(TEXT("R7 control: the target and its control share one seed"),
				 Seed == TC.Game->BattleState.RandomManager.GetSeed());
		TestTrue(TEXT("R7 control: the random walk moved the target seed off the checkpoint's"),
				 Seed != A.Game->BattleState.RandomManager.GetSeed());
		if (bPayload)
		{
			FBytes Altered = Bytes;
			Altered[Altered.Num() / 2] ^= 0x40;
			Reject(TEXT("R7 altered payload byte into the seed-drifted target is rejected"), T, Altered, Damaged);
			TestTrue(TEXT("R7 the RNG seed is unchanged after the malformed rejection"),
					 T.Game->BattleState.RandomManager.GetSeed() == Seed);
		}
		Reject(TEXT("R7 checkpoint into the seed-drifted target without the registration is LineupMismatch"), T, Bytes, {Mismatch});
		TestTrue(TEXT("R7 the RNG seed is unchanged after the lineup mismatch"),
				 T.Game->BattleState.RandomManager.GetSeed() == Seed);
		ExpectFollows(*this, TEXT("R7 later random draws follow the no-import control"), T, TC, Constant(20));
	}
	{
		// The reverse: the target carries an extra registered state. The instruction does not
		// decide this case, so both readings are accepted; a restored target must still follow
		// a control at the checkpoint's frame, a rejecting one must be untouched.
		FNSE010Battle T;
		FNSE010Battle TC;
		Run(TC, Slice(TapeT1(), 0, 10));
		UNSE010Script* Extra = NewObject<UNSE010Script>(T.Players[0]);
		Extra->Name = Tag(TEXT("State.Universal.Crouch"));
		Extra->bHumanUsable = false;
		Extra->Parent = T.Players[0];
		T.Players[0]->AddState(Extra->Name, Extra, FGameplayTag::EmptyTag);
		const FBytes Before = Export(T);
		const EBattleCheckpointResult Result = Import(T, Bytes);
		ExpectResultIn(*this, TEXT("R7 checkpoint into a target with an extra registered state is Restored or LineupMismatch"), Result,
					   {EBattleCheckpointResult::Restored, Mismatch});
		TestTrue(TEXT("R7 the extra registration survives the import attempt"),
				 T.Players[0]->PrimaryStateMachine.StateNames.Contains(Extra->Name));
		if (Result == EBattleCheckpointResult::Restored)
		{
			ExpectFollows(*this, TEXT("R7 restored target with an extra registered state follows a control at the checkpoint frame"), T, TC,
						  Slice(TapeT1(), 10, 30));
		}
		else
		{
			ExpectSameBytes(*this, TEXT("R7 rejected import over an extra registration leaves the export unchanged"), Export(T), Before);
		}
	}
	{
		FNSE010Battle T;
		const FBytes Before = Export(T);
		ExpectResult(*this, TEXT("R7 same-lineup checkpoint is Restored"), Import(T, Bytes), EBattleCheckpointResult::Restored);
		ExpectDifferentBytes(*this, TEXT("R7 the same-lineup import changed the target's export"), Export(T), Before);
	}

	// Arbitrary bytes never crash and never change a battle when rejected.
	{
		FRandomStream Rand(20133);
		int32 Restored = 0, Rejected = 0;
		for (int32 I = 0; I < 300; ++I)
		{
			const int32 Len = Rand.RandRange(0, 4096);
			FBytes Junk;
			Junk.SetNumUninitialized(Len);
			for (int32 J = 0; J < Len; ++J) Junk[J] = static_cast<uint8>(Rand.RandRange(0, 255));
			if (I % 2 == 0 && Len >= 12)
			{
				const uint8 Header[12] = {'N', 'S', 'K', 'Y', 'C', 'K', 'P', 'T', 1, 0, 0, 0};
				for (int32 J = 0; J < 12; ++J) Junk[J] = Header[J];
			}
			const FBytes Before = Export(B);
			const EBattleCheckpointResult Result = Import(B, Junk);
			if (Result == EBattleCheckpointResult::Restored)
			{
				++Restored;
				AddInfo(FString::Printf(TEXT("R7 random array %d (seed 20133, %d bytes) was Restored; re-arming the target"), I, Len));
				B.Reset();
				C.Reset();
				continue;
			}
			++Rejected;
			if (!ExpectSameBytes(*this, FString::Printf(TEXT("R7 export unchanged after random array %d (%d bytes)"), I, Len), Export(B),
								 Before))
			{
				break;
			}
			if (I % 30 == 29)
			{
				if (!ExpectFollows(*this, FString::Printf(TEXT("R7 battle continues like the control after random array %d"), I), B, C,
								   Constant(10)))
				{
					break;
				}
			}
		}
		AddInfo(FString::Printf(TEXT("R7 arbitrary bytes: %d rejected, %d restored"), Rejected, Restored));
	}
	return !HasAnyErrors();
}

// R8: the rollback snapshot still round-trips after a checkpoint import.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE010Rollback, "UnrealBench.NSE010.Rollback",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE010Rollback::RunTest(const FString&)
{
	FNSE010Battle A;
	FNSE010Battle C;
	const FTape T1 = TapeT1();
	RunAll({&A, &C}, Slice(T1, 0, 10));
	const FBytes Bytes = Export(A);
	ExpectPayload(*this, TEXT("R8 export at frame 10 has a payload"), Bytes);
	ExpectResult(*this, TEXT("R8 importing a battle's own export at frame 10 is Restored"), Import(A, Bytes),
				 EBattleCheckpointResult::Restored);
	ExpectObsEqual(*this, TEXT("R8 self import leaves the battle equal to control"), A, C);
	RunAll({&A, &C}, Slice(T1, 10, 40));
	ExpectObsEqual(*this, TEXT("R8 battle equals control at frame 40 after the self import"), A, C);
	TestEqual(TEXT("R8 control: P2 was hit once by frame 40"), C.Players[1]->CurrentHealth, 9900);

	FRollbackData Snapshot;
	int32 Checksum = 0;
	A.Game->SaveGameState(Snapshot, &Checksum);
	Run(A, Constant(20, INP_Right, INP_Right));
	TestTrue(TEXT("R8 control: the discarded future moved P1"), A.Players[0]->PosX != C.Players[0]->PosX);
	A.Game->LoadGameState(Snapshot);
	TestEqual(TEXT("R8 rollback restores the frame number"), A.Game->BattleState.FrameNumber, C.Game->BattleState.FrameNumber);
	ExpectFollows(*this, TEXT("R8 rollback snapshot replays T2 like the control"), A, C, TapeT2());
	TestEqual(TEXT("R8 control: P1 was hit by P2's close shot during T2"), C.Players[0]->CurrentHealth, 9900);
	return !HasAnyErrors();
}

// R9: a newly activated pooled object has no collision boxes until its script sets a cel.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FNSE010FreshSpawn, "UnrealBench.NSE010.FreshSpawn",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FNSE010FreshSpawn::RunTest(const FString&)
{
	for (const bool bFreedSlot : {true, false})
	{
		const FString Kind = bFreedSlot ? TEXT("freed slot") : TEXT("never-used slot");
		FNSE010Battle A;
		ABattleObject* S0 = bFreedSlot ? A.Spawn(A.Players[0], P1X, 150000) : nullptr;
		ABattleObject* E = A.Spawn(A.Players[1], -40000, ShotY);
		A.Script(E)->Travel = 0;
		A.Script(E)->bDeactivateOnHit = true;
		A.Step();
		TestEqual(FString::Printf(TEXT("R9 control (%s): the enemy shot is armed with a hitbox"), *Kind), E->Boxes.Num(), 1);
		if (S0)
		{
			S0->DeactivateObject();
			A.Step();
			TestFalse(FString::Printf(TEXT("R9 control (%s): slot 0 is free before the spawn"), *Kind), A.Game->Objects[0]->IsActive);
		}
		const int32 NewSlot = bFreedSlot ? 0 : 1;
		A.ShotTemplate(A.Players[0])->Travel = 30000;
		const int32 Cursor = A.Trace.Num();
		A.Step(INP_A, INP_Neutral);
		ABattleObject* N = A.Game->Objects[NewSlot];
		TestTrue(FString::Printf(TEXT("R9 control (%s): P1's press spawned into slot %d"), *Kind, NewSlot),
				 N->IsActive && N->Player == A.Players[0]);
		TestTrue(FString::Printf(TEXT("R9 (%s): the enemy shot did not hit the fresh spawn on its spawn frame (no 110 in trace)"), *Kind),
				 !TraceSince(A, Cursor).Contains(110));
		TestEqual(FString::Printf(TEXT("R9 (%s): the fresh spawn has no hitstop on its spawn frame"), *Kind), static_cast<int32>(N->Hitstop), 0);
		TestTrue(FString::Printf(TEXT("R9 (%s): the enemy shot is still active after the spawn frame"), *Kind), E->IsActive);
		A.Step();
		A.Step();
		TestTrue(FString::Printf(TEXT("R9 (%s): no hit between the shots two frames later"), *Kind),
				 !TraceSince(A, Cursor).Contains(110) && E->IsActive);
		TestTrue(FString::Printf(TEXT("R9 (%s): the fresh spawn travelled after arming"), *Kind), N->PosX > -40000);
		TestEqual(FString::Printf(TEXT("R9 (%s): the fresh spawn carries its own hitbox after arming"), *Kind), N->Boxes.Num(), 1);
	}
	{
		// The engine's own activation path, driven directly: right after activation into a freed
		// slot and before any update, the object carries no collision box at all.
		FNSE010Battle A;
		APlayerObject* P1 = A.Players[0];
		ABattleObject* S0 = A.Spawn(P1, P1X, 150000);
		A.Step();
		TestEqual(TEXT("R9 control: the first shot armed with a hitbox"), S0->Boxes.Num(), 1);
		S0->DeactivateObject();
		A.Step();
		TestFalse(TEXT("R9 control: slot 0 is free again"), A.Game->Objects[0]->IsActive);
		UNSE010Script* Template = A.ShotTemplate(P1);
		const int32 Index = P1->ObjectStates.Find(Template);
		ABattleObject* N = A.Game->AddBattleObject(Template, P1X, 150000, P1->Direction, Index, false, P1);
		TestTrue(TEXT("R9 control: the engine activated the freed slot 0"), N == A.Game->Objects[0] && N->IsActive);
		TestEqual(TEXT("R9 a newly activated object has no collision boxes before its script sets a cel"), N->Boxes.Num(), 0);
		A.Step();
		TestEqual(TEXT("R9 after its first execution the object carries its own hitbox"), N->Boxes.Num(), 1);
	}
	return !HasAnyErrors();
}
