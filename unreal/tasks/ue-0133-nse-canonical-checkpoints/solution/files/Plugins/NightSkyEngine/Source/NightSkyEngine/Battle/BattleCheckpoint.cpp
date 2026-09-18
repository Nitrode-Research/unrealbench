#include "NightSkyGameState.h"

#include "Script/BattleExtension.h"
#include "Misc/Crc.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

// Records use UTF-8 keys and values, with explicit little-endian lengths and integers.
// The final CRC covers the header and every record, including their lengths.
struct FBattleCheckpoint
{
	ANightSkyGameState& Game;
	bool bReading = false;
	bool bMalformed = false;
	bool bMismatch = false;
	TMap<FString, TArray<uint8>> Records;
	TSet<FString> Used;
	TMap<FString, UObject*> References;
	TArray<TFunction<void()>> Apply;
	TArray<TStrongObjectPtr<UState>> PendingScripts;
	TMap<FString, FStateMachine*> Machines;

	explicit FBattleCheckpoint(ANightSkyGameState& InGame) : Game(InGame) {}

	static void Put(TArray<uint8>& Bytes, uint64 Value, int32 Size)
	{
		for (int32 i = 0; i < Size; i++) Bytes.Add(static_cast<uint8>(Value >> (i * 8)));
	}

	static uint64 Get(const TArray<uint8>& Bytes, int32 Offset, int32 Size)
	{
		uint64 Value = 0;
		for (int32 i = 0; i < Size; i++) Value |= uint64(Bytes[Offset + i]) << (i * 8);
		return Value;
	}

	static TArray<uint8> Encode(const FString& Text)
	{
		FTCHARToUTF8 UTF8(*Text, Text.Len());
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(UTF8.Get()), UTF8.Length());
		return Bytes;
	}

	static bool Decode(const TArray<uint8>& Bytes, FString& Text)
	{
		if (Bytes.IsEmpty()) { Text.Empty(); return true; }
		FUTF8ToTCHAR UTF8(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
		Text = FString(UTF8.Length(), UTF8.Get());
		return Encode(Text) == Bytes;
	}

	TArray<uint8> Value(const FString& Key, const TArray<uint8>& Bytes)
	{
		if (!bReading) { Records.Add(Key, Bytes); return Bytes; }
		Used.Add(Key);
		if (const auto Found = Records.Find(Key)) return *Found;
		bMismatch = true;
		return Bytes;
	}

	uint64 Number(const FString& Key, uint64 Number)
	{
		TArray<uint8> Bytes;
		Put(Bytes, Number, 8);
		Bytes = Value(Key, Bytes);
		if (Bytes.Num() != 8) { bMalformed = true; return Number; }
		return Get(Bytes, 0, 8);
	}

	FString Text(const FString& Key, const FString& Text)
	{
		FString Result;
		if (!Decode(Value(Key, Encode(Text)), Result)) bMalformed = true;
		return Result;
	}

	void Match(const FString& Key, const FString& Expected)
	{
		if (Text(Key, Expected) != Expected) bMismatch = true;
	}

	template<typename T> void Field(const FString& Key, T& Target)
	{
		const uint64 Result = Number(Key, static_cast<uint64>(Target));
		if (static_cast<uint64>(static_cast<T>(Result)) != Result) bMalformed = true;
		if (bReading) Apply.Add([&Target, Result]() { Target = static_cast<T>(Result); });
	}

	void Field(const FString& Key, bool& Target)
	{
		const uint64 Result = Number(Key, Target);
		if (Result > 1) bMalformed = true;
		if (bReading) Apply.Add([&Target, Result]() { Target = Result != 0; });
	}

	template<typename T> void Field(const FString& Key, TEnumAsByte<T>& Target)
	{
		const uint64 Result = Number(Key, Target.GetValue());
		if (Result > 255) bMalformed = true;
		if (bReading) Apply.Add([&Target, Result]() { Target = static_cast<T>(Result); });
	}

	void Field(const FString& Key, FName& Target)
	{
		const FString Result = Text(Key, Target.ToString().ToLower());
		if (Result.IsEmpty()) { bMalformed = true; return; }
		// FName construction asserts on oversized input, including otherwise valid UTF-8.
		if (Result.Len() >= NAME_SIZE || Encode(Result).Contains(0) || Result != Result.ToLower())
		{ bMalformed = true; return; }
		if (bReading) Apply.Add([&Target, Result]() { Target = FName(*Result); });
	}

	void Field(const FString& Key, FGameplayTag& Target)
	{
		const FString Result = Text(Key, Target.IsValid() ? Target.ToString().ToLower() : FString());
		// FName construction asserts on oversized input, including otherwise valid UTF-8.
		if (Result.Len() >= NAME_SIZE || Encode(Result).Contains(0) || Result != Result.ToLower())
		{ bMalformed = true; return; }
		const FGameplayTag Tag = Result.IsEmpty() ? FGameplayTag() : FGameplayTag::RequestGameplayTag(FName(*Result), false);
		if (!Result.IsEmpty() && !Tag.IsValid()) bMismatch = true;
		if (bReading) Apply.Add([&Target, Tag]() { Target = Tag; });
	}

	FString Reference(UObject* Object) const
	{
		if (!Object) return FString();
		TArray<FString> Keys;
		References.GetKeys(Keys);
		Keys.Sort();
		for (const auto& Key : Keys) if (References[Key] == Object) return Key;
		return FString();
	}

	UObject* Resolve(const FString& Key, UObject* Object)
	{
		const FString Name = Text(Key, Reference(Object));
		if (Name.IsEmpty()) return nullptr;
		if (auto Found = References.Find(Name)) return *Found;
		bMismatch = true;
		return nullptr;
	}

	template<typename T> void Field(const FString& Key, T*& Target)
	{
		UObject* Object = Resolve(Key, Target);
		if (Object && !Object->IsA(T::StaticClass())) bMalformed = true;
		if (bReading) Apply.Add([&Target, Object]() { Target = Cast<T>(Object); });
	}

	template<typename T, size_t N> void Field(const FString& Key, T (&Target)[N])
	{
		for (size_t i = 0; i < N; i++) Field(Key + TEXT("/") + FString::FromInt(static_cast<int32>(i)), Target[i]);
	}

	int32 Count(const FString& Key, int32 OldCount)
	{
		uint64 Result = Number(Key, OldCount);
		// Every array element must consume at least one record.
		if (Result > static_cast<uint64>(bReading ? Records.Num() : MAX_int32))
		{
			bMalformed = true;
			return 0;
		}
		return static_cast<int32>(Result);
	}

	template<typename T> void Field(const FString& Key, TArray<T>& Target)
	{
		if (!bReading)
		{
			Number(Key, Target.Num());
			for (int32 i = 0; i < Target.Num(); i++) Field(Key + TEXT("/") + FString::FromInt(i), Target[i]);
			return;
		}
		auto Copy = MakeShared<TArray<T>>();
		Copy->SetNum(Count(Key, Target.Num()));
		for (int32 i = 0; i < Copy->Num(); i++) Field(Key + TEXT("/") + FString::FromInt(i), (*Copy)[i]);
		Apply.Add([&Target, Copy]() { Target = *Copy; });
	}

	static bool BytesLess(const TArray<uint8>& A, const TArray<uint8>& B)
	{
		const int32 Common = FMath::Min(A.Num(), B.Num());
		const int32 Compare = Common ? FMemory::Memcmp(A.GetData(), B.GetData(), Common) : 0;
		return Compare < 0 || (Compare == 0 && A.Num() < B.Num());
	}

	// Hash tables have no gameplay iteration order. Sort by the portable encoding,
	// never by their native hash, pointer addresses, or FName pool indices.
	TArray<uint8> PropertyOrder(FProperty* Prop, void* Data)
	{
		FBattleCheckpoint Writer(Game);
		Writer.References = References;
		Writer.Property(TEXT("value"), Prop, Data);
		bMismatch |= Writer.bMismatch;
		bMalformed |= Writer.bMalformed;
		TArray<FString> Keys;
		Writer.Records.GetKeys(Keys);
		Keys.Sort();
		TArray<uint8> Bytes;
		for (const auto& Name : Keys)
		{
			const auto Encoded = Encode(Name);
			Put(Bytes, Encoded.Num(), 4);
			Bytes.Append(Encoded);
			const auto& ValueBytes = Writer.Records[Name];
			Put(Bytes, ValueBytes.Num(), 4);
			Bytes.Append(ValueBytes);
		}
		return Bytes;
	}

	// Only use for detached, staged property storage. Complete its nested writes
	// now so keys can be validated and rehashed before committing anything live.
	void ReadDetached(const FString& Key, FProperty* Prop, void* Data)
	{
		const int32 Begin = Apply.Num();
		Property(Key, Prop, Data);
		if (!bMalformed && !bMismatch)
			for (int32 i = Begin; i < Apply.Num(); ++i) Apply[i]();
		Apply.SetNum(Begin);
	}

	void Property(const FString& Key, FProperty* Prop, void* Data)
	{
		if (auto Bool = CastField<FBoolProperty>(Prop))
		{
			uint64 Result = Number(Key, Bool->GetPropertyValue(Data));
			if (Result > 1) bMalformed = true;
			if (bReading) Apply.Add([Bool, Data, Result]() { Bool->SetPropertyValue(Data, Result != 0); });
		}
		else if (auto Enum = CastField<FEnumProperty>(Prop)) Property(Key, Enum->GetUnderlyingProperty(), Data);
		else if (auto Numeric = CastField<FNumericProperty>(Prop))
		{
			uint64 Bits = 0;
			if (Numeric->IsInteger()) Bits = Numeric->GetUnsignedIntPropertyValue(Data);
			else if (Prop->GetElementSize() == sizeof(uint32))
			{
				uint32 Value;
				FMemory::Memcpy(&Value, Data, sizeof(Value));
				Bits = Value;
			}
			else FMemory::Memcpy(&Bits, Data, sizeof(Bits));
			Bits = Number(Key, Bits);
			if (Numeric->IsInteger())
			{
				alignas(uint64) uint8 Value[sizeof(uint64)] = {};
				Numeric->SetIntPropertyValue(Value, Bits);
				if (Numeric->GetUnsignedIntPropertyValue(Value) != Bits) bMalformed = true;
			}
			else if (Prop->GetElementSize() == sizeof(uint32) && Bits > MAX_uint32) bMalformed = true;
			if (bReading) Apply.Add([Numeric, Data, Bits]()
			{
				if (Numeric->IsInteger()) Numeric->SetIntPropertyValue(Data, Bits);
				else if (Numeric->GetElementSize() == sizeof(uint32))
				{
					const uint32 Value = static_cast<uint32>(Bits);
					FMemory::Memcpy(Data, &Value, sizeof(Value));
				}
				else FMemory::Memcpy(Data, &Bits, sizeof(Bits));
			});
		}
		else if (CastField<FNameProperty>(Prop)) Field(Key, *static_cast<FName*>(Data));
		else if (auto String = CastField<FStrProperty>(Prop))
		{
			FString Result = Text(Key, String->GetPropertyValue(Data));
			if (bReading) Apply.Add([String, Data, Result]() { String->SetPropertyValue(Data, Result); });
		}
		else if (auto Object = CastField<FObjectPropertyBase>(Prop))
		{
			UObject* Result = Resolve(Key, Object->GetObjectPropertyValue(Data));
			if (Result && !Result->IsA(Object->PropertyClass)) bMalformed = true;
			if (bReading && (Result || !Reference(Object->GetObjectPropertyValue(Data)).IsEmpty() ||
				!Object->GetObjectPropertyValue(Data)))
				Apply.Add([Object, Data, Result]() { Object->SetObjectPropertyValue(Data, Result); });
		}
		else if (auto Array = CastField<FArrayProperty>(Prop))
		{
			FScriptArrayHelper Helper(Array, Data);
			int32 Num = Count(Key, Helper.Num());
			if (bReading) Helper.Resize(Num);
			for (int32 i = 0; i < Num; i++) Property(Key + TEXT("/") + FString::FromInt(i), Array->Inner, Helper.GetRawPtr(i));
		}
		else if (auto Map = CastField<FMapProperty>(Prop))
		{
			FScriptMapHelper Helper(Map, Data);
			const int32 Num = Count(Key, Helper.Num());
			if (bReading)
			{
				Helper.EmptyValues(Num);
				TArray<uint8> Previous;
				for (int32 i = 0; i < Num; ++i)
				{
					const int32 Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
					const FString Entry = Key + TEXT("/") + FString::FromInt(i);
					ReadDetached(Entry + TEXT("/key"), Map->KeyProp, Helper.GetKeyPtr(Index));
					ReadDetached(Entry + TEXT("/value"), Map->ValueProp, Helper.GetValuePtr(Index));
					if (bMalformed || bMismatch) return;
					auto Order = PropertyOrder(Map->KeyProp, Helper.GetKeyPtr(Index));
					if (i && !BytesLess(Previous, Order)) { bMalformed = true; return; }
					Previous = MoveTemp(Order);
				}
				Helper.Rehash();
			}
			else
			{
				TArray<TPair<TArray<uint8>, int32>> Entries;
				for (int32 i = 0; i < Helper.GetMaxIndex(); ++i)
					if (Helper.IsValidIndex(i)) Entries.Emplace(PropertyOrder(Map->KeyProp, Helper.GetKeyPtr(i)), i);
				Entries.Sort([](const auto& A, const auto& B) { return BytesLess(A.Key, B.Key); });
				for (int32 i = 0; i < Entries.Num(); ++i)
				{
					if (i && !BytesLess(Entries[i - 1].Key, Entries[i].Key)) bMismatch = true;
					const FString Entry = Key + TEXT("/") + FString::FromInt(i);
					Property(Entry + TEXT("/key"), Map->KeyProp, Helper.GetKeyPtr(Entries[i].Value));
					Property(Entry + TEXT("/value"), Map->ValueProp, Helper.GetValuePtr(Entries[i].Value));
				}
			}
		}
		else if (auto Set = CastField<FSetProperty>(Prop))
		{
			FScriptSetHelper Helper(Set, Data);
			const int32 Num = Count(Key, Helper.Num());
			if (bReading)
			{
				Helper.EmptyElements(Num);
				TArray<uint8> Previous;
				for (int32 i = 0; i < Num; ++i)
				{
					const int32 Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
					ReadDetached(Key + TEXT("/") + FString::FromInt(i), Set->ElementProp, Helper.GetElementPtr(Index));
					if (bMalformed || bMismatch) return;
					auto Order = PropertyOrder(Set->ElementProp, Helper.GetElementPtr(Index));
					if (i && !BytesLess(Previous, Order)) { bMalformed = true; return; }
					Previous = MoveTemp(Order);
				}
				Helper.Rehash();
			}
			else
			{
				TArray<TPair<TArray<uint8>, int32>> Entries;
				for (int32 i = 0; i < Helper.GetMaxIndex(); ++i)
					if (Helper.IsValidIndex(i)) Entries.Emplace(PropertyOrder(Set->ElementProp, Helper.GetElementPtr(i)), i);
				Entries.Sort([](const auto& A, const auto& B) { return BytesLess(A.Key, B.Key); });
				for (int32 i = 0; i < Entries.Num(); ++i)
				{
					if (i && !BytesLess(Entries[i - 1].Key, Entries[i].Key)) bMismatch = true;
					Property(Key + TEXT("/") + FString::FromInt(i), Set->ElementProp, Helper.GetElementPtr(Entries[i].Value));
				}
			}
		}
		else if (auto Struct = CastField<FStructProperty>(Prop))
		{
			if (Struct->Struct == FGameplayTag::StaticStruct()) Field(Key, *static_cast<FGameplayTag*>(Data));
			else if (Struct->Struct == FGameplayTagContainer::StaticStruct())
			{
				auto* Container = static_cast<FGameplayTagContainer*>(Data);
				auto Tags = Container->GetGameplayTagArray();
				Tags.Sort([](const FGameplayTag& A, const FGameplayTag& B)
				{ return A.ToString().ToLower() < B.ToString().ToLower(); });
				const int32 Num = Count(Key, Tags.Num());
				if (bReading) Tags.SetNum(Num);
				FString Previous;
				for (int32 i = 0; i < Num; ++i)
				{
					const int32 Begin = Apply.Num();
					Field(Key + TEXT("/") + FString::FromInt(i), Tags[i]);
					if (bReading)
					{
						if (!bMalformed && !bMismatch)
							for (int32 j = Begin; j < Apply.Num(); ++j) Apply[j]();
						Apply.SetNum(Begin);
					}
					const FString Name = Tags[i].ToString().ToLower();
					if (!Tags[i].IsValid() || (i && Name <= Previous)) bMalformed = true;
					Previous = Name;
				}
				if (bReading && !bMalformed && !bMismatch)
				{
					Container->Reset();
					for (const auto& Tag : Tags) Container->AddTag(Tag);
				}
			}
			else Properties(Key, Struct->Struct, Data, false);
		}
		else bMismatch = true;
	}

	struct FPropertyCopy
	{
		FProperty* Prop;
		void* Data;
		explicit FPropertyCopy(FProperty* InProp, void* Source) : Prop(InProp)
		{
			Data = FMemory::Malloc(Prop->GetSize(), Prop->GetMinAlignment());
			Prop->InitializeValue(Data);
			Prop->CopyCompleteValue(Data, Source);
		}
		~FPropertyCopy() { Prop->DestroyValue(Data); FMemory::Free(Data); }
	};

	void Properties(const FString& Key, UStruct* Type, void* Data, bool bSavedOnly, bool bStage = false)
	{
		TArray<FProperty*> Props;
		for (TFieldIterator<FProperty> It(Type); It; ++It)
		{
			if (bSavedOnly && !It->HasAnyPropertyFlags(CPF_SaveGame)) continue;
			if (It->HasAnyPropertyFlags(CPF_EditorOnly)) continue;
			const FString Name = It->GetName();
			if (Type->IsChildOf(ABattleObject::StaticClass()) &&
				(Name == TEXT("PrimaryStateMachine") || Name == TEXT("SubStateMachines") ||
				 Name == TEXT("StoredLinkActors") || Name == TEXT("LinkedActor") ||
				 Name == TEXT("AnimStructs") || Name == TEXT("ScreenSpaceDepthOffset"))) continue;
			if (Type == FHitDataCommon::StaticStruct() &&
				(Name == TEXT("DamageColor") || Name == TEXT("DamageColor2") ||
				 Name == TEXT("SFXType") || Name == TEXT("VFXType") ||
				 Name == TEXT("GuardSFXOverride") || Name == TEXT("GuardVFXOverride") ||
				 Name == TEXT("HitSFXOverride") || Name == TEXT("HitVFXOverride"))) continue;
			Props.Add(*It);
		}
		Props.Sort([](const FProperty& A, const FProperty& B) { return A.GetName() < B.GetName(); });
		for (auto Prop : Props)
		{
			void* Original = Prop->ContainerPtrToValuePtr<void>(Data);
			TSharedPtr<FPropertyCopy> Copy;
			void* ValueData = Original;
			if (bReading && bStage) { Copy = MakeShared<FPropertyCopy>(Prop, Original); ValueData = Copy->Data; }
			for (int32 i = 0; i < Prop->ArrayDim; i++)
				Property(Key + TEXT("/") + Prop->GetName() + TEXT("/") + FString::FromInt(i), Prop,
					static_cast<uint8*>(ValueData) + i * Prop->GetElementSize());
			if (Copy) Apply.Add([Copy, Original]() { Copy->Prop->CopyCompleteValue(Original, Copy->Data); });
		}
	}

	template<typename T> void Struct(const FString& Key, T& Target)
	{
		auto Copy = MakeShared<T>(Target);
		Properties(Key, T::StaticStruct(), bReading ? &Copy.Get() : &Target, false);
		if (bReading) Apply.Add([&Target, Copy]() { Target = *Copy; });
	}

	void Machine(const FString& Key, FStateMachine& Machine)
	{
		Field(Key + TEXT("/primary"), Machine.bPrimary);
		Field(Key + TEXT("/name"), Machine.StateMachineName);
		Field(Key + TEXT("/enabled"), Machine.EnableFlags);
		Field(Key + TEXT("/custom"), Machine.EnabledCustomStateTypes);
		UState* Current = Cast<UState>(Resolve(Key + TEXT("/current"), Machine.CurrentState));
		if (Current && !Machine.States.Contains(Current)) bMismatch = true;
		if (bReading) Apply.Add([&Machine, Current]() { Machine.CurrentState = Current; });
		const int32 Num = Count(Key + TEXT("/states"), Machine.States.Num());
		if (Num != Machine.States.Num()) bMismatch = true;
		for (int32 i = 0; i < Num; i++)
		{
			const FString ExpectedName = Machine.StateNames.IsValidIndex(i)
				? Machine.StateNames[i].ToString().ToLower() : FString();
			const FString Name = Text(Key + TEXT("/states/") + FString::FromInt(i), ExpectedName);
			// Registration order determines move priority. Preserve the receiving
			// configuration and reject an order that cannot reproduce these bytes.
			if (Name != ExpectedName) bMismatch = true;
			UState* State = Cast<UState>(References.FindRef(Key + TEXT("/state/") + Name));
			if (!State) { bMismatch = true; continue; }
			Properties(Key + TEXT("/state/") + Name, State->GetClass(), State, true, true);
		}
	}

	void RegisterMachine(const FString& Key, FStateMachine& Machine)
	{
		Machines.Add(Key, &Machine);
		for (int32 i = 0; i < Machine.States.Num(); i++)
			References.Add(Key + TEXT("/state/") + Machine.StateNames[i].ToString().ToLower(), Machine.States[i]);
	}

	void Object(const FString& Key, ABattleObject& Object);
	void Player(const FString& Key, APlayerObject& Player);
	void Battle();
	void Walk();
};

void FBattleCheckpoint::Object(const FString& Key, ABattleObject& Object)
{
	Field(Key + TEXT("/PosX"), Object.PosX);
	Field(Key + TEXT("/PosY"), Object.PosY);
	Field(Key + TEXT("/PosZ"), Object.PosZ);
	Field(Key + TEXT("/PrevPosX"), Object.PrevPosX);
	Field(Key + TEXT("/PrevPosY"), Object.PrevPosY);
	Field(Key + TEXT("/PrevPosZ"), Object.PrevPosZ);
	Field(Key + TEXT("/PrevRootMotionX"), Object.PrevRootMotionX);
	Field(Key + TEXT("/PrevRootMotionY"), Object.PrevRootMotionY);
	Field(Key + TEXT("/PrevRootMotionZ"), Object.PrevRootMotionZ);
	Field(Key + TEXT("/AnglePitch_x1000"), Object.AnglePitch_x1000);
	Field(Key + TEXT("/AngleYaw_x1000"), Object.AngleYaw_x1000);
	Field(Key + TEXT("/AngleRoll_x1000"), Object.AngleRoll_x1000);
	Field(Key + TEXT("/BlendOffset"), Object.BlendOffset);
	Field(Key + TEXT("/PrevOffsetX"), Object.PrevOffsetX);
	Field(Key + TEXT("/PrevOffsetY"), Object.PrevOffsetY);
	Field(Key + TEXT("/NextOffsetX"), Object.NextOffsetX);
	Field(Key + TEXT("/NextOffsetY"), Object.NextOffsetY);
	Field(Key + TEXT("/SpeedX"), Object.SpeedX);
	Field(Key + TEXT("/SpeedY"), Object.SpeedY);
	Field(Key + TEXT("/SpeedZ"), Object.SpeedZ);
	Field(Key + TEXT("/SpeedXRate"), Object.SpeedXRate);
	Field(Key + TEXT("/SpeedXRatePerFrame"), Object.SpeedXRatePerFrame);
	Field(Key + TEXT("/SpeedYRate"), Object.SpeedYRate);
	Field(Key + TEXT("/SpeedYRatePerFrame"), Object.SpeedYRatePerFrame);
	Field(Key + TEXT("/SpeedZRate"), Object.SpeedZRate);
	Field(Key + TEXT("/SpeedZRatePerFrame"), Object.SpeedZRatePerFrame);
	Field(Key + TEXT("/Gravity"), Object.Gravity);
	Field(Key + TEXT("/Inertia"), Object.Inertia);
	Field(Key + TEXT("/GroundHeight"), Object.GroundHeight);
	Field(Key + TEXT("/Direction"), Object.Direction);
	Field(Key + TEXT("/Pushback"), Object.Pushback);
	Struct(Key + TEXT("/HitCommon"), Object.HitCommon);
	Struct(Key + TEXT("/NormalHit"), Object.NormalHit);
	Struct(Key + TEXT("/CounterHit"), Object.CounterHit);
	Field(Key + TEXT("/AttackFlags"), Object.AttackFlags);
	Struct(Key + TEXT("/ReceivedHitCommon"), Object.ReceivedHitCommon);
	Struct(Key + TEXT("/ReceivedHit"), Object.ReceivedHit);
	Field(Key + TEXT("/StunTime"), Object.StunTime);
	Field(Key + TEXT("/StunTimeMax"), Object.StunTimeMax);
	Field(Key + TEXT("/Hitstop"), Object.Hitstop);
	Field(Key + TEXT("/ActionReg1"), Object.ActionReg1);
	Field(Key + TEXT("/ActionReg2"), Object.ActionReg2);
	Field(Key + TEXT("/ActionReg3"), Object.ActionReg3);
	Field(Key + TEXT("/ActionReg4"), Object.ActionReg4);
	Field(Key + TEXT("/ActionReg5"), Object.ActionReg5);
	Field(Key + TEXT("/ActionReg6"), Object.ActionReg6);
	Field(Key + TEXT("/ActionReg7"), Object.ActionReg7);
	Field(Key + TEXT("/ActionReg8"), Object.ActionReg8);
	Field(Key + TEXT("/ObjectReg1"), Object.ObjectReg1);
	Field(Key + TEXT("/ObjectReg2"), Object.ObjectReg2);
	Field(Key + TEXT("/ObjectReg3"), Object.ObjectReg3);
	Field(Key + TEXT("/ObjectReg4"), Object.ObjectReg4);
	Field(Key + TEXT("/ObjectReg5"), Object.ObjectReg5);
	Field(Key + TEXT("/ObjectReg6"), Object.ObjectReg6);
	Field(Key + TEXT("/ObjectReg7"), Object.ObjectReg7);
	Field(Key + TEXT("/ObjectReg8"), Object.ObjectReg8);
	Field(Key + TEXT("/SubroutineReg1"), Object.SubroutineReg1);
	Field(Key + TEXT("/SubroutineReg2"), Object.SubroutineReg2);
	Field(Key + TEXT("/SubroutineReg3"), Object.SubroutineReg3);
	Field(Key + TEXT("/SubroutineReg4"), Object.SubroutineReg4);
	Field(Key + TEXT("/SubroutineReturnVal1"), Object.SubroutineReturnVal1);
	Field(Key + TEXT("/SubroutineReturnVal2"), Object.SubroutineReturnVal2);
	Field(Key + TEXT("/SubroutineReturnVal3"), Object.SubroutineReturnVal3);
	Field(Key + TEXT("/SubroutineReturnVal4"), Object.SubroutineReturnVal4);
	Field(Key + TEXT("/ActionTime"), Object.ActionTime);
	Field(Key + TEXT("/CelName"), Object.CelName);
	Field(Key + TEXT("/BlendCelName"), Object.BlendCelName);
	Field(Key + TEXT("/LabelName"), Object.LabelName);
	Field(Key + TEXT("/GotoLabelActive"), Object.GotoLabelActive);
	Field(Key + TEXT("/AnimFrame"), Object.AnimFrame);
	Field(Key + TEXT("/BlendAnimFrame"), Object.BlendAnimFrame);
	Field(Key + TEXT("/CelIndex"), Object.CelIndex);
	Field(Key + TEXT("/TimeUntilNextCel"), Object.TimeUntilNextCel);
	Field(Key + TEXT("/MaxCelTime"), Object.MaxCelTime);
	Field(Key + TEXT("/ObjectStateName"), Object.ObjectStateName);
	Field(Key + TEXT("/ObjectID"), Object.ObjectID);
	Field(Key + TEXT("/PushHeight"), Object.PushHeight);
	Field(Key + TEXT("/PushHeightLow"), Object.PushHeightLow);
	Field(Key + TEXT("/PushWidth"), Object.PushWidth);
	Field(Key + TEXT("/PushWidthExtend"), Object.PushWidthExtend);
	Field(Key + TEXT("/L"), Object.L);
	Field(Key + TEXT("/R"), Object.R);
	Field(Key + TEXT("/T"), Object.T);
	Field(Key + TEXT("/B"), Object.B);
	Field(Key + TEXT("/ColPosX"), Object.ColPosX);
	Field(Key + TEXT("/ColPosY"), Object.ColPosY);
	Field(Key + TEXT("/MiscFlags"), Object.MiscFlags);
	Field(Key + TEXT("/Timer0"), Object.Timer0);
	Field(Key + TEXT("/Timer1"), Object.Timer1);
	Field(Key + TEXT("/IsPlayer"), Object.IsPlayer);
	Struct(Key + TEXT("/HomingParams"), Object.HomingParams);
	Struct(Key + TEXT("/SuperArmorData"), Object.SuperArmorData);
	Field(Key + TEXT("/UpdateTime"), Object.UpdateTime);
	Field(Key + TEXT("/Player"), Object.Player);
	Field(Key + TEXT("/AttackOwner"), Object.AttackOwner);
	Field(Key + TEXT("/AttackTarget"), Object.AttackTarget);
	Field(Key + TEXT("/PositionLinkObj"), Object.PositionLinkObj);
	Field(Key + TEXT("/StopLinkObj"), Object.StopLinkObj);
	for (int32 i = 0; i < EVT_NUM; i++)
	{
		Field(Key + TEXT("/event/function/") + FString::FromInt(i), Object.EventHandlers[i].FunctionName);
		Field(Key + TEXT("/event/subroutine/") + FString::FromInt(i), Object.EventHandlers[i].SubroutineName);
	}
	Properties(Key + TEXT("/saved"), Object.GetClass(), &Object, true, true);
}

void FBattleCheckpoint::Player(const FString& Key, APlayerObject& Player)
{
	Field(Key + TEXT("/IsActive"), Player.IsActive);
	Field(Key + TEXT("/FWalkSpeed"), Player.FWalkSpeed);
	Field(Key + TEXT("/BWalkSpeed"), Player.BWalkSpeed);
	Field(Key + TEXT("/FDashInitSpeed"), Player.FDashInitSpeed);
	Field(Key + TEXT("/FDashAccel"), Player.FDashAccel);
	Field(Key + TEXT("/FDashMaxSpeed"), Player.FDashMaxSpeed);
	Field(Key + TEXT("/FDashFriction"), Player.FDashFriction);
	Field(Key + TEXT("/BDashSpeed"), Player.BDashSpeed);
	Field(Key + TEXT("/BDashHeight"), Player.BDashHeight);
	Field(Key + TEXT("/BDashGravity"), Player.BDashGravity);
	Field(Key + TEXT("/JumpHeight"), Player.JumpHeight);
	Field(Key + TEXT("/FJumpSpeed"), Player.FJumpSpeed);
	Field(Key + TEXT("/BJumpSpeed"), Player.BJumpSpeed);
	Field(Key + TEXT("/JumpGravity"), Player.JumpGravity);
	Field(Key + TEXT("/SuperJumpHeight"), Player.SuperJumpHeight);
	Field(Key + TEXT("/FSuperJumpSpeed"), Player.FSuperJumpSpeed);
	Field(Key + TEXT("/BSuperJumpSpeed"), Player.BSuperJumpSpeed);
	Field(Key + TEXT("/SuperJumpGravity"), Player.SuperJumpGravity);
	Field(Key + TEXT("/AirDashMinimumHeight"), Player.AirDashMinimumHeight);
	Field(Key + TEXT("/FAirDashSpeed"), Player.FAirDashSpeed);
	Field(Key + TEXT("/BAirDashSpeed"), Player.BAirDashSpeed);
	Field(Key + TEXT("/FAirDashTime"), Player.FAirDashTime);
	Field(Key + TEXT("/BAirDashTime"), Player.BAirDashTime);
	Field(Key + TEXT("/FAirDashNoAttackTime"), Player.FAirDashNoAttackTime);
	Field(Key + TEXT("/BAirDashNoAttackTime"), Player.BAirDashNoAttackTime);
	Field(Key + TEXT("/AirJumpCount"), Player.AirJumpCount);
	Field(Key + TEXT("/AirDashCount"), Player.AirDashCount);
	Field(Key + TEXT("/StandPushWidth"), Player.StandPushWidth);
	Field(Key + TEXT("/StandPushHeight"), Player.StandPushHeight);
	Field(Key + TEXT("/CrouchPushWidth"), Player.CrouchPushWidth);
	Field(Key + TEXT("/CrouchPushHeight"), Player.CrouchPushHeight);
	Field(Key + TEXT("/AirPushWidth"), Player.AirPushWidth);
	Field(Key + TEXT("/AirPushHeight"), Player.AirPushHeight);
	Field(Key + TEXT("/AirPushHeightLow"), Player.AirPushHeightLow);
	Field(Key + TEXT("/CloseNormalRange"), Player.CloseNormalRange);
	Field(Key + TEXT("/MaxHealth"), Player.MaxHealth);
	Field(Key + TEXT("/MaxMeter"), Player.MaxMeter);
	Field(Key + TEXT("/ComboRate"), Player.ComboRate);
	Field(Key + TEXT("/OtgProration"), Player.OtgProration);
	Field(Key + TEXT("/ForwardWalkMeterGain"), Player.ForwardWalkMeterGain);
	Field(Key + TEXT("/ForwardJumpMeterGain"), Player.ForwardJumpMeterGain);
	Field(Key + TEXT("/ForwardDashMeterGain"), Player.ForwardDashMeterGain);
	Field(Key + TEXT("/ForwardAirDashMeterGain"), Player.ForwardAirDashMeterGain);
	Field(Key + TEXT("/MeterPercentOnHit"), Player.MeterPercentOnHit);
	Field(Key + TEXT("/MeterPercentOnHitGuard"), Player.MeterPercentOnHitGuard);
	Field(Key + TEXT("/MeterPercentOnReceiveHitGuard"), Player.MeterPercentOnReceiveHitGuard);
	Field(Key + TEXT("/MeterPercentOnReceiveHit"), Player.MeterPercentOnReceiveHit);
	Field(Key + TEXT("/CanReverseBeat"), Player.CanReverseBeat);
	Field(Key + TEXT("/CanProximityThrow"), Player.CanProximityThrow);
	Struct(Key + TEXT("/ProximityThrowInput"), Player.ProximityThrowInput);
	Field(Key + TEXT("/ThrowTechWindow"), Player.ThrowTechWindow);
	Field(Key + TEXT("/ThrowResistAfterWakeUp"), Player.ThrowResistAfterWakeUp);
	Field(Key + TEXT("/PlayerReg1"), Player.PlayerReg1);
	Field(Key + TEXT("/PlayerReg2"), Player.PlayerReg2);
	Field(Key + TEXT("/PlayerReg3"), Player.PlayerReg3);
	Field(Key + TEXT("/PlayerReg4"), Player.PlayerReg4);
	Field(Key + TEXT("/PlayerReg5"), Player.PlayerReg5);
	Field(Key + TEXT("/PlayerReg6"), Player.PlayerReg6);
	Field(Key + TEXT("/PlayerReg7"), Player.PlayerReg7);
	Field(Key + TEXT("/PlayerReg8"), Player.PlayerReg8);
	Field(Key + TEXT("/CmnPlayerReg1"), Player.CmnPlayerReg1);
	Field(Key + TEXT("/CmnPlayerReg2"), Player.CmnPlayerReg2);
	Field(Key + TEXT("/CmnPlayerReg3"), Player.CmnPlayerReg3);
	Field(Key + TEXT("/CmnPlayerReg4"), Player.CmnPlayerReg4);
	Field(Key + TEXT("/CmnPlayerReg5"), Player.CmnPlayerReg5);
	Field(Key + TEXT("/CmnPlayerReg6"), Player.CmnPlayerReg6);
	Field(Key + TEXT("/CmnPlayerReg7"), Player.CmnPlayerReg7);
	Field(Key + TEXT("/CmnPlayerReg8"), Player.CmnPlayerReg8);
	Field(Key + TEXT("/IntroEndFlag"), Player.IntroEndFlag);
	Field(Key + TEXT("/RoundEndFlag"), Player.RoundEndFlag);
	Field(Key + TEXT("/Inputs"), Player.Inputs);
	Field(Key + TEXT("/FlipInputs"), Player.FlipInputs);
	Field(Key + TEXT("/MaxOTGCount"), Player.MaxOTGCount);
	Field(Key + TEXT("/bLimitCrumple"), Player.bLimitCrumple);
	Field(Key + TEXT("/PlayerIndex"), Player.PlayerIndex);
	Field(Key + TEXT("/TeamIndex"), Player.TeamIndex);
	Field(Key + TEXT("/Stance"), Player.Stance);
	Field(Key + TEXT("/CurrentHealth"), Player.CurrentHealth);
	Field(Key + TEXT("/RecoverableHealth"), Player.RecoverableHealth);
	Field(Key + TEXT("/ComboCounter"), Player.ComboCounter);
	Field(Key + TEXT("/TotalProration"), Player.TotalProration);
	Field(Key + TEXT("/ComboTimer"), Player.ComboTimer);
	Field(Key + TEXT("/InvulnFlags"), Player.InvulnFlags);
	Field(Key + TEXT("/PlayerFlags"), Player.PlayerFlags);
	Field(Key + TEXT("/StrikeInvulnerableTimer"), Player.StrikeInvulnerableTimer);
	Field(Key + TEXT("/ThrowInvulnerableTimer"), Player.ThrowInvulnerableTimer);
	Field(Key + TEXT("/ThrowResistTimer"), Player.ThrowResistTimer);
	Field(Key + TEXT("/AirDashTimer"), Player.AirDashTimer);
	Field(Key + TEXT("/OTGCount"), Player.OTGCount);
	Field(Key + TEXT("/bCrumpled"), Player.bCrumpled);
	Field(Key + TEXT("/RoundWinTimer"), Player.RoundWinTimer);
	Field(Key + TEXT("/WallTouchTimer"), Player.WallTouchTimer);
	Field(Key + TEXT("/Enemy"), Player.Enemy);
	Field(Key + TEXT("/StoredBattleObjects"), Player.StoredBattleObjects);
	Field(Key + TEXT("/StateEntryName"), Player.StateEntryName);
	Field(Key + TEXT("/IntroName"), Player.IntroName);
	Field(Key + TEXT("/CurrentAirJumpCount"), Player.CurrentAirJumpCount);
	Field(Key + TEXT("/CurrentAirDashCount"), Player.CurrentAirDashCount);
	Field(Key + TEXT("/AirDashTimerMax"), Player.AirDashTimerMax);
	Field(Key + TEXT("/CancelFlags"), Player.CancelFlags);
	Field(Key + TEXT("/AirDashNoAttackTime"), Player.AirDashNoAttackTime);
	Field(Key + TEXT("/InstantBlockLockoutTimer"), Player.InstantBlockLockoutTimer);
	Field(Key + TEXT("/MeterCooldownTimer"), Player.MeterCooldownTimer);
	Field(Key + TEXT("/ThrowRange"), Player.ThrowRange);
	Field(Key + TEXT("/ThrowTechTimer"), Player.ThrowTechTimer);
	Field(Key + TEXT("/AutoComboCancels"), Player.AutoComboCancels);
	Field(Key + TEXT("/bIsAutoCombo"), Player.bIsAutoCombo);
	Field(Key + TEXT("/LastStateName"), Player.LastStateName);
	Field(Key + TEXT("/ExeStateName"), Player.ExeStateName);
	Field(Key + TEXT("/BufferedStateName"), Player.BufferedStateName);
	Field(Key + TEXT("/input/history"), Player.StoredInputBuffer.InputBufferInternal);
	Field(Key + TEXT("/input/valid"), Player.StoredInputBuffer.InputBufferValid);
	Field(Key + TEXT("/input/time"), Player.StoredInputBuffer.InputTime);
	Machine(Key + TEXT("/primary"), Player.PrimaryStateMachine);
	const int32 Num = Count(Key + TEXT("/machines"), Player.SubStateMachines.Num());
	if (Num != Player.SubStateMachines.Num()) bMismatch = true;
	for (int32 i = 0; i < Num; i++)
	{
		const FString ExpectedName = Player.SubStateMachines.IsValidIndex(i)
			? Player.SubStateMachines[i].StateMachineName.ToString().ToLower() : FString();
		const FString Name = Text(Key + TEXT("/machines/") + FString::FromInt(i), ExpectedName);
		if (Name != ExpectedName) bMismatch = true;
		const FString MachineKey = Key + TEXT("/machine/") + Name;
		if (auto Found = Machines.Find(MachineKey)) Machine(MachineKey, **Found);
		else bMismatch = true;
	}
}

void FBattleCheckpoint::Battle()
{
	auto& State = Game.BattleState;
	Field(TEXT("battle/frame"), State.FrameNumber);
	Field(TEXT("battle/start"), State.TimeUntilRoundStart);
	Field(TEXT("battle/timer"), State.RoundTimer);
	Field(TEXT("battle/pause"), State.PauseTimer);
	Field(TEXT("battle/sequence"), State.IsPlayingSequence);
	const uint64 SeedValue = Number(TEXT("battle/random"), State.RandomManager.GetSeed());
	if (SeedValue > MAX_uint32) bMalformed = true;
	const uint32 Seed = static_cast<uint32>(SeedValue);
	if (bReading) Apply.Add([this, Seed]() { Game.BattleState.RandomManager.Reseed(Seed); });
	Field(TEXT("battle/meter"), State.Meter);
	Field(TEXT("battle/maxMeter"), State.MaxMeter);
	Field(TEXT("battle/maxGauge"), State.MaxGauge);
	Field(TEXT("battle/freeze"), State.SuperFreezeDuration);
	Field(TEXT("battle/selfFreeze"), State.SuperFreezeSelfDuration);
	Field(TEXT("battle/caller"), State.SuperFreezeCaller);
	Field(TEXT("battle/main"), State.MainPlayer);
	Field(TEXT("battle/rounds1"), State.P1RoundsWon);
	Field(TEXT("battle/rounds2"), State.P2RoundsWon);
	Field(TEXT("battle/round"), State.RoundCount);
	Field(TEXT("battle/fade"), State.FadeTimer);
	Field(TEXT("battle/intro"), State.CurrentIntroSide);
	Field(TEXT("battle/winner"), State.CurrentWinSide);
	Field(TEXT("battle/phase"), State.BattlePhase);
	Field(TEXT("battle/active"), State.ActiveObjectCount);
	Field(TEXT("battle/sequenceTime"), State.CurrentSequenceTime);
	Field(TEXT("battle/gauge1"), State.GaugeP1);
	Field(TEXT("battle/gauge2"), State.GaugeP2);
	for (int32 i = 0; i < 2; i++)
	{
		Field(TEXT("battle/team/") + FString::FromInt(i), State.TeamData[i].CooldownTimer);
	}
	const int32 OrderCount = Count(TEXT("battle/order"), Game.SortedObjects.Num());
	TArray<ABattleObject*> Order;
	if (OrderCount != Game.Players.Num() + Game.Objects.Num()) bMalformed = true;
	for (int32 i = 0; i < OrderCount; i++)
	{
		ABattleObject* Entry = Cast<ABattleObject>(Resolve(TEXT("battle/order/") + FString::FromInt(i),
			Game.SortedObjects.IsValidIndex(i) ? Game.SortedObjects[i] : nullptr));
		if (!Entry || Order.Contains(Entry) || (i < Game.Players.Num() && Entry != Game.Players[i])) bMalformed = true;
		Order.Add(Entry);
	}
	if (bReading) Apply.Add([this, Order]() { Game.SortedObjects = Order; });
	Field(TEXT("battle/sequenceTarget"), Game.SequenceTarget);
	Field(TEXT("battle/sequenceEnemy"), Game.SequenceEnemy);
	Field(TEXT("battle/playingSequence"), Game.bIsPlayingSequence);
	Field(TEXT("battle/paused"), Game.bPauseGame);
	auto& Screen = State.ScreenData;
	Field(TEXT("screen/Flags"), Screen.Flags);
	Field(TEXT("screen/bTouchingWorldSide"), Screen.bTouchingWorldSide);
	Field(TEXT("screen/MaxZoomOutWidth"), Screen.MaxZoomOutWidth);
	Field(TEXT("screen/ZoomOutBeginX"), Screen.ZoomOutBeginX);
	Field(TEXT("screen/ZoomOutBeginY"), Screen.ZoomOutBeginY);
	Field(TEXT("screen/ZoomOutBeginH"), Screen.ZoomOutBeginH);
	Field(TEXT("screen/TargetObjects"), Screen.TargetObjects);
	Field(TEXT("screen/ObjTop"), Screen.ObjTop);
	Field(TEXT("screen/ObjBottom"), Screen.ObjBottom);
	Field(TEXT("screen/HigherObjBottom"), Screen.HigherObjBottom);
	Field(TEXT("screen/ObjLeft"), Screen.ObjLeft);
	Field(TEXT("screen/ObjRight"), Screen.ObjRight);
	Field(TEXT("screen/ObjLength"), Screen.ObjLength);
	Field(TEXT("screen/ObjHeight"), Screen.ObjHeight);
	Field(TEXT("screen/ObjDistanceY"), Screen.ObjDistanceY);
	Field(TEXT("screen/ScreenWorldCenterX"), Screen.ScreenWorldCenterX);
	Field(TEXT("screen/ScreenWorldCenterY"), Screen.ScreenWorldCenterY);
	Field(TEXT("screen/ScreenWorldWidth"), Screen.ScreenWorldWidth);
	Field(TEXT("screen/TargetCenterX"), Screen.TargetCenterX);
	Field(TEXT("screen/TargetCenterY"), Screen.TargetCenterY);
	Field(TEXT("screen/TargetWidth"), Screen.TargetWidth);
	Field(TEXT("screen/CenterXVelocity"), Screen.CenterXVelocity);
	Field(TEXT("screen/CenterYVelocity"), Screen.CenterYVelocity);
	Field(TEXT("screen/WidthVelocity"), Screen.WidthVelocity);
	Field(TEXT("screen/FinalScreenX"), Screen.FinalScreenX);
	Field(TEXT("screen/FinalScreenY"), Screen.FinalScreenY);
	Field(TEXT("screen/FinalScreenWidth"), Screen.FinalScreenWidth);
	Field(TEXT("screen/TargetOffsetY"), Screen.TargetOffsetY);
	Field(TEXT("screen/TargetOffsetLandYMax"), Screen.TargetOffsetLandYMax);
	Field(TEXT("screen/TargetOffsetLandYAdd"), Screen.TargetOffsetLandYAdd);
	Field(TEXT("screen/TargetOffsetAirYMax"), Screen.TargetOffsetAirYMax);
	Field(TEXT("screen/TargetOffsetAirYAdd"), Screen.TargetOffsetAirYAdd);
	Field(TEXT("screen/TargetOffsetAirYPos"), Screen.TargetOffsetAirYPos);
	Field(TEXT("screen/TargetOffsetAirYDist"), Screen.TargetOffsetAirYDist);
	Field(TEXT("screen/ScreenBoundsLeft"), Screen.ScreenBoundsLeft);
	Field(TEXT("screen/ScreenBoundsRight"), Screen.ScreenBoundsRight);
	Field(TEXT("screen/ScreenBoundsTop"), Screen.ScreenBoundsTop);
}

void FBattleCheckpoint::Walk()
{
	Match(TEXT("lineup/players"), FString::FromInt(Game.Players.Num()));
	Match(TEXT("lineup/pool"), FString::FromInt(Game.Objects.Num()));
	Match(TEXT("lineup/extensions"), FString::FromInt(Game.BattleExtensions.Num()));
	for (int32 i = 0; i < 2; i++)
		Match(TEXT("lineup/team/") + FString::FromInt(i), FString::FromInt(Game.BattleState.TeamData[i].TeamCount));
	for (int32 i = 0; i < Game.Players.Num(); i++)
	{
		APlayerObject* Player = Game.Players[i];
		const FString Key = TEXT("player/") + FString::FromInt(i);
		References.Add(Key, Player);
		Match(Key + TEXT("/class"), Player->GetClass()->GetPathName());
		RegisterMachine(Key + TEXT("/primary"), Player->PrimaryStateMachine);
		for (auto& Machine : Player->SubStateMachines)
			RegisterMachine(Key + TEXT("/machine/") + Machine.StateMachineName.ToString().ToLower(), Machine);
		for (int32 j = 0; j < Player->CommonObjectStates.Num(); j++)
			References.Add(Key + TEXT("/common/") + Player->CommonObjectStateNames[j].ToString().ToLower(), Player->CommonObjectStates[j]);
		for (int32 j = 0; j < Player->ObjectStates.Num(); j++)
			References.Add(Key + TEXT("/template/") + Player->ObjectStateNames[j].ToString().ToLower(), Player->ObjectStates[j]);
	}
	for (int32 i = 0; i < Game.Objects.Num(); i++)
		References.Add(TEXT("object/") + FString::FromInt(i), Game.Objects[i]);
	for (int32 i = 0; i < Game.BattleExtensions.Num(); i++)
	{
		const FString Key = TEXT("extension/") + FString::FromInt(i);
		Match(Key + TEXT("/class"), Game.BattleExtensions[i]->GetClass()->GetPathName());
		Match(Key + TEXT("/name"), Game.BattleExtensionNames[i].ToString().ToLower());
		References.Add(Key, Game.BattleExtensions[i]);
	}
	if (bMismatch || bMalformed) return;

	// Resolve every script before walking saved properties, which may reference it.
	TArray<UState*> Scripts;
	Scripts.SetNumZeroed(Game.Objects.Num());
	for (int32 i = 0; i < Game.Objects.Num(); i++)
	{
		ABattleObject* Object = Game.Objects[i];
		const FString Key = TEXT("object/") + FString::FromInt(i);
		uint64 Active = Number(Key + TEXT("/active"), Object->IsActive);
		if (Active > 1) bMalformed = true;
		if (bReading) Apply.Add([Object, Active]() { Object->IsActive = Active != 0; });
		if (!Active) continue;
		FString TemplateName;
		if (Object->IsActive && Object->Player)
		{
			const auto& Templates = Object->bIsCommonState ? Object->Player->CommonObjectStates : Object->Player->ObjectStates;
			if (Templates.IsValidIndex(Object->ObjectStateIndex)) TemplateName = Reference(Templates[Object->ObjectStateIndex]);
		}
		TemplateName = Text(Key + TEXT("/template"), TemplateName);
		UState* Template = Cast<UState>(References.FindRef(TemplateName));
		APlayerObject* Owner = nullptr;
		int32 TemplateIndex = INDEX_NONE;
		bool bCommon = false;
		for (auto Player : Game.Players)
		{
			TemplateIndex = Player->CommonObjectStates.Find(Template);
			if (TemplateIndex != INDEX_NONE) { Owner = Player; bCommon = true; break; }
			TemplateIndex = Player->ObjectStates.Find(Template);
			if (TemplateIndex != INDEX_NONE) { Owner = Player; break; }
		}
		if (!Template || !Owner) { bMismatch = true; continue; }
		if (bReading)
		{
			Scripts[i] = DuplicateObject(Template, Object);
			PendingScripts.Emplace(Scripts[i]);
			Scripts[i]->Parent = Object;
			UState* Script = Scripts[i];
			Apply.Add([Object, Script, TemplateIndex, bCommon, Owner]()
			{
				Object->ObjectState = Script;
				Object->ObjectStateIndex = TemplateIndex;
				Object->bIsCommonState = bCommon;
				Object->Player = Owner;
			});
		}
		else Scripts[i] = Object->ObjectState;
		References.Add(Key + TEXT("/script"), Scripts[i]);
	}
	if (bMismatch || bMalformed) return;
	Battle();
	for (int32 i = 0; i < Game.Players.Num(); i++)
	{
		const FString Key = TEXT("player/") + FString::FromInt(i);
		Object(Key, *Game.Players[i]);
		Player(Key, *Game.Players[i]);
	}
	for (int32 i = 0; i < Game.Objects.Num(); i++)
	{
		if (!Scripts[i]) continue;
		const FString Key = TEXT("object/") + FString::FromInt(i);
		Object(Key, *Game.Objects[i]);
		Properties(Key + TEXT("/script"), Scripts[i]->GetClass(), Scripts[i], true, true);
	}
	for (int32 i = 0; i < Game.BattleExtensions.Num(); i++)
		Properties(TEXT("extension/") + FString::FromInt(i), Game.BattleExtensions[i]->GetClass(), Game.BattleExtensions[i], true, true);
}

TArray<uint8> ANightSkyGameState::ExportCheckpoint()
{
	FBattleCheckpoint Writer(*this);
	Writer.Walk();
	// Unsupported authored property types must never produce a silently incomplete save.
	if (Writer.bMalformed || Writer.bMismatch) return {};
	TArray<uint8> Bytes = {'N', 'S', 'K', 'Y', 'C', 'K', 'P', 'T'};
	FBattleCheckpoint::Put(Bytes, BattleCheckpointVersion, 4);
	TArray<FString> Keys;
	Writer.Records.GetKeys(Keys);
	Keys.Sort();
	FBattleCheckpoint::Put(Bytes, Keys.Num(), 4);
	for (const auto& Key : Keys)
	{
		const auto Name = FBattleCheckpoint::Encode(Key);
		const auto& Value = Writer.Records[Key];
		FBattleCheckpoint::Put(Bytes, Name.Num(), 4);
		Bytes.Append(Name);
		FBattleCheckpoint::Put(Bytes, Value.Num(), 4);
		Bytes.Append(Value);
	}
	FBattleCheckpoint::Put(Bytes, FCrc::MemCrc32(Bytes.GetData(), Bytes.Num()), 4);
	return Bytes;
}

EBattleCheckpointResult ANightSkyGameState::ImportCheckpoint(const TArray<uint8>& Checkpoint)
{
	static const uint8 Magic[] = {'N', 'S', 'K', 'Y', 'C', 'K', 'P', 'T'};
	if (Checkpoint.Num() < 12 || FMemory::Memcmp(Checkpoint.GetData(), Magic, 8) != 0)
		return EBattleCheckpointResult::Malformed;
	if (FBattleCheckpoint::Get(Checkpoint, 8, 4) != BattleCheckpointVersion)
		return EBattleCheckpointResult::UnsupportedVersion;
	if (Checkpoint.Num() < 20 || FBattleCheckpoint::Get(Checkpoint, Checkpoint.Num() - 4, 4) !=
		FCrc::MemCrc32(Checkpoint.GetData(), Checkpoint.Num() - 4))
		return EBattleCheckpointResult::Malformed;

	FBattleCheckpoint Reader(*this);
	Reader.bReading = true;
	int32 Offset = 16;
	const int32 End = Checkpoint.Num() - 4;
	const uint64 Count = FBattleCheckpoint::Get(Checkpoint, 12, 4);
	if (Count > static_cast<uint64>((End - Offset) / 8)) return EBattleCheckpointResult::Malformed;
	FString Previous;
	for (uint64 i = 0; i < Count; i++)
	{
		TArray<uint8> Parts[2];
		for (auto& Part : Parts)
		{
			if (End - Offset < 4) return EBattleCheckpointResult::Malformed;
			const uint64 Size = FBattleCheckpoint::Get(Checkpoint, Offset, 4);
			Offset += 4;
			if (Size > static_cast<uint64>(End - Offset)) return EBattleCheckpointResult::Malformed;
			Part.Append(Checkpoint.GetData() + Offset, static_cast<int32>(Size));
			Offset += static_cast<int32>(Size);
		}
		FString Key;
		if (Parts[0].Contains(0) || !FBattleCheckpoint::Decode(Parts[0], Key) || Key.IsEmpty() || (i && Key <= Previous))
			return EBattleCheckpointResult::Malformed;
		Previous = Key;
		Reader.Records.Add(Key, MoveTemp(Parts[1]));
	}
	if (Offset != End) return EBattleCheckpointResult::Malformed;
	Reader.Walk();
	if (Reader.bMalformed) return EBattleCheckpointResult::Malformed;
	if (Reader.bMismatch || Reader.Used.Num() != Reader.Records.Num()) return EBattleCheckpointResult::LineupMismatch;
	// Reset pooled slots before assigning any references or ignore-hit lists.
	for (auto Object : Objects)
	{
		// Presentation teardown can dispatch authored callbacks. Import does not run them.
		Object->LinkedActor = nullptr;
		Object->LinkedParticle = nullptr;
		Object->ResetObject();
		Object->ObjectState = nullptr;
		Object->Boxes.Reset();
	}
	for (auto& Apply : Reader.Apply) Apply();
	// Animation assets are configuration, but their root-motion lookup follows the cel.
	for (auto Object : SortedObjects)
	{
		if (!Object->IsPlayer && !Object->IsActive) continue;
		Object->AnimStructs.Reset();
		if (!Object->Player) continue;
		UCollisionData* Data[] = {Object->Player->CommonCollisionData, Object->Player->CollisionData};
		for (auto CollisionData : Data)
		{
			if (!CollisionData) continue;
			for (const auto& Frame : CollisionData->CollisionFrames)
			{
				if (Frame.CelName == Object->CelName) Object->AnimStructs = Frame.Anim;
			}
		}
	}
	return EBattleCheckpointResult::Restored;
}
