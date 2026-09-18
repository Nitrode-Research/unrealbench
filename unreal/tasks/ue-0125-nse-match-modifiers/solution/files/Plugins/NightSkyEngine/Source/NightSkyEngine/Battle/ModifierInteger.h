#pragma once
#include "CoreMinimal.h"

// Signed, arbitrary-width integer for authored arithmetic. Only commits impose bounds.
class FModifierInteger
{
	TArray<uint32> Words;
	bool Negative = false;
	void Normalize()
	{
		while (!Words.IsEmpty() && Words.Last() == 0) Words.Pop();
		if (Words.IsEmpty()) Negative = false;
	}
	int CompareMagnitude(const FModifierInteger& Other) const
	{
		if (Words.Num() != Other.Words.Num()) return Words.Num() < Other.Words.Num() ? -1 : 1;
		for (int32 I = Words.Num() - 1; I >= 0; --I)
			if (Words[I] != Other.Words[I]) return Words[I] < Other.Words[I] ? -1 : 1;
		return 0;
	}
public:
	FModifierInteger(int32 Value = 0)
	{
		Negative = Value < 0;
		const uint32 Magnitude = uint32(Negative ? -int64(Value) : Value);
		if (Magnitude) Words.Add(Magnitude);
	}
	bool IsNegative() const { return Negative; }
	bool IsZero() const { return Words.IsEmpty(); }
	FModifierInteger Negated() const
	{
		auto Result = *this;
		if (!Result.IsZero()) Result.Negative = !Negative;
		return Result;
	}
	int Compare(const FModifierInteger& Other) const
	{
		if (Negative != Other.Negative) return Negative ? -1 : 1;
		return CompareMagnitude(Other) * (Negative ? -1 : 1);
	}
	void Add(const FModifierInteger& Other)
	{
		if (Negative == Other.Negative)
		{
			const int32 Count = FMath::Max(Words.Num(), Other.Words.Num());
			Words.SetNumZeroed(Count);
			uint64 Carry = 0;
			for (int32 I = 0; I < Count; ++I)
			{
				const uint64 Sum = uint64(Words[I]) + (Other.Words.IsValidIndex(I) ? Other.Words[I] : 0) + Carry;
				Words[I] = uint32(Sum);
				Carry = Sum >> 32;
			}
			if (Carry) Words.Add(uint32(Carry));
		}
		else
		{
			if (CompareMagnitude(Other) < 0)
			{
				auto Result = Other;
				Result.Add(*this);
				*this = MoveTemp(Result);
				return;
			}
			uint64 Borrow = 0;
			for (int32 I = 0; I < Words.Num(); ++I)
			{
				const uint64 Subtrahend = uint64(Other.Words.IsValidIndex(I) ? Other.Words[I] : 0) + Borrow;
				const uint64 Value = Words[I];
				Words[I] = uint32(Value - Subtrahend);
				Borrow = Value < Subtrahend;
			}
		}
		Normalize();
	}
	void Multiply(int32 Factor)
	{
		if (Factor < 0) Negative = !Negative;
		const uint32 Magnitude = uint32(Factor < 0 ? -int64(Factor) : Factor);
		uint64 Carry = 0;
		for (auto& Word : Words)
		{
			const uint64 Product = uint64(Word) * Magnitude + Carry;
			Word = uint32(Product);
			Carry = Product >> 32;
		}
		if (Carry) Words.Add(uint32(Carry));
		Normalize();
	}
	void Divide(int32 Divisor, bool Floor = true)
	{
		check(Divisor > 0);
		uint64 Remainder = 0;
		for (int32 I = Words.Num() - 1; I >= 0; --I)
		{
			const uint64 Value = (Remainder << 32) | Words[I];
			Words[I] = uint32(Value / uint32(Divisor));
			Remainder = Value % uint32(Divisor);
		}
		const bool RoundDown = Negative && Remainder && Floor;
		Normalize();
		if (RoundDown) Add(-1);
	}
	int32 Bounded(int32 Lower, int32 Upper) const
	{
		if (Compare(Lower) < 0) return Lower;
		if (Compare(Upper) > 0) return Upper;
		const int64 Magnitude = Words.IsEmpty() ? 0 : Words[0];
		return int32(Negative ? -Magnitude : Magnitude);
	}
};
