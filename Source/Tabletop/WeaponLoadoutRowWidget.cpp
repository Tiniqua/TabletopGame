// WeaponLoadoutRowWidget.cpp
#include "WeaponLoadoutRowWidget.h"
#include "UnitRowWidget.h" // for UIFormat helpers
#include "Actors/UnitAbility.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Gamemodes/SetupGamemode.h"
#include "Controllers/SetupPlayerController.h"

namespace UIFormatForWeapon
{
	inline FString AbilityClassToDisplay(TSubclassOf<class UUnitAbility> C)
	{
		if (!*C) return TEXT("");
		const UClass* Cls = *C;

		// Prefer a FText property on CDO
		if (const UObject* CDO = Cls->GetDefaultObject())
		{
			if (const FTextProperty* P = FindFProperty<FTextProperty>(Cls, TEXT("DisplayName")))
			{
				const FText V = P->GetPropertyValue_InContainer(CDO);
				if (!V.IsEmpty()) return V.ToString();
			}
			if (const FTextProperty* P2 = FindFProperty<FTextProperty>(Cls, TEXT("AbilityName")))
			{
				const FText V = P2->GetPropertyValue_InContainer(CDO);
				if (!V.IsEmpty()) return V.ToString();
			}
		}
		// Fall back to class display name
		return Cls->GetDisplayNameText().ToString();
	}

	inline FString FormatAbilities(const TArray<TSubclassOf<class UUnitAbility>>& Abils)
	{
		if (Abils.Num() == 0) return TEXT("Abilities: —");
		TArray<FString> Parts; Parts.Reserve(Abils.Num());
		for (TSubclassOf<UUnitAbility> C : Abils)
			if (*C) Parts.Add(AbilityClassToDisplay(C));
		return FString::Printf(TEXT("Abilities: %s"), *FString::Join(Parts, TEXT(", ")));
	}

	// --- Keywords: robust enum-or-name reader ----

	inline bool TryGetKeywordEnumValue(const FWeaponKeywordData& K, int64& OutVal)
	{
		const UScriptStruct* SS = FWeaponKeywordData::StaticStruct();
		if (!SS) return false;

		static const FName Candidates[] = { TEXT("Keyword"), TEXT("Type"), TEXT("Id"), TEXT("Key"), TEXT("Enum") };

		for (const FName& FieldName : Candidates)
		{
			if (const FProperty* P = SS->FindPropertyByName(FieldName))
			{
				if (const FEnumProperty* EP = CastField<FEnumProperty>(P))
				{
					const void* Ptr = EP->ContainerPtrToValuePtr<void>(&K);
					OutVal = (int64)EP->GetUnderlyingProperty()->GetSignedIntPropertyValue(Ptr);
					return true;
				}
				if (const FByteProperty* BP = CastField<FByteProperty>(P))
				{
					const void* Ptr = BP->ContainerPtrToValuePtr<void>(&K);
					OutVal = (int64)BP->GetPropertyValue(Ptr);
					return true;
				}
				if (const FNumericProperty* NP = CastField<FNumericProperty>(P))
				{
					const void* Ptr = NP->ContainerPtrToValuePtr<void>(&K);
					OutVal = (int64)NP->GetSignedIntPropertyValue(Ptr);
					return true;
				}
			}
		}
		return false;
	}

	inline bool TryGetKeywordName(const FWeaponKeywordData& K, FString& OutName)
	{
		const UScriptStruct* SS = FWeaponKeywordData::StaticStruct();
		if (!SS) return false;

		static const FName Candidates[] = { TEXT("Name"), TEXT("Id"), TEXT("KeywordName"), TEXT("Tag") };

		for (const FName& FieldName : Candidates)
		{
			if (const FNameProperty* NP = FindFProperty<FNameProperty>(SS, FieldName))
			{
				const void* Ptr = NP->ContainerPtrToValuePtr<void>(&K);
				OutName = NP->GetPropertyValue(Ptr).ToString();
				return !OutName.IsEmpty();
			}
			if (const FStrProperty* SP = FindFProperty<FStrProperty>(SS, FieldName))
			{
				const void* Ptr = SP->ContainerPtrToValuePtr<void>(&K);
				OutName = SP->GetPropertyValue(Ptr);
				return !OutName.IsEmpty();
			}
		}
		return false;
	}

	inline int32 ReadKeywordValue(const FWeaponKeywordData& K)
	{
		const UScriptStruct* SS = FWeaponKeywordData::StaticStruct();
		if (!SS) return 0;
		if (const FIntProperty* IP = FindFProperty<FIntProperty>(SS, TEXT("Value")))
		{
			const void* Ptr = IP->ContainerPtrToValuePtr<void>(&K);
			return IP->GetPropertyValue(Ptr);
		}
		return 0;
	}

	inline FString FormatKeywords(const TArray<FWeaponKeywordData>& Ks)
	{
		if (Ks.Num() == 0) return TEXT("Keywords: —");
		TArray<FString> Parts; Parts.Reserve(Ks.Num());

		const UEnum* Enum = StaticEnum<EWeaponKeyword>();

		for (const FWeaponKeywordData& K : Ks)
		{
			FString Label;
			int64 EnumVal = 0;

			if (Enum && TryGetKeywordEnumValue(K, EnumVal))
			{
				Label = Enum->GetDisplayNameTextByValue(EnumVal).ToString();
				if (Label.IsEmpty())
					Label = Enum->GetNameStringByValue(EnumVal);
			}
			else
			{
				if (!TryGetKeywordName(K, Label))
					Label = TEXT("Keyword");
			}

			const int32 V = ReadKeywordValue(K);
			if (V != 0) Label += FString::Printf(TEXT(" %d"), V);

			Parts.Add(Label);
		}
		return FString::Printf(TEXT("Keywords: %s"), *FString::Join(Parts, TEXT(", ")));
	}
}


ASetupGameState* UWeaponLoadoutRowWidget::GS() const { return GetWorld()? GetWorld()->GetGameState<ASetupGameState>() : nullptr; }
ASetupPlayerController* UWeaponLoadoutRowWidget::PC() const { return GetOwningPlayer<ASetupPlayerController>(); }

void UWeaponLoadoutRowWidget::Init(FName InUnitId, int32 InWeaponIndex, const FWeaponProfile& W)
{
	UnitId = InUnitId;
	WeaponIndex = InWeaponIndex;
	CachedWeapon = W;

	if (WL_NameText)      WL_NameText     ->SetText(FText::FromName(W.WeaponId));
	if (WL_StatsText)     WL_StatsText    ->SetText(FText::FromString(FormatWeaponStats(W)));
	if (WL_KeywordsText)  WL_KeywordsText ->SetText(FText::FromString(UIFormatForWeapon::FormatKeywords(W.Keywords)));
	if (WL_AbilitiesText) WL_AbilitiesText->SetText(FText::FromString(UIFormatForWeapon::FormatAbilities(W.AbilityClasses)));

	// If you want per-loadout points, set them here (or reuse unit points)
	if (WL_PointsText)
	{
		// Replace with per-loadout cost if you have it; using 0 or unit cost as placeholder:
		WL_PointsText->SetText(FText::GetEmpty());
	}

	RefreshFromState();
}

void UWeaponLoadoutRowWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (WL_MinusBtn) WL_MinusBtn->OnClicked.AddDynamic(this, &UWeaponLoadoutRowWidget::HandleMinus);
	if (WL_PlusBtn)  WL_PlusBtn ->OnClicked.AddDynamic(this, &UWeaponLoadoutRowWidget::HandlePlus);

	if (ASetupGameState* S = GS())
		S->OnRosterChanged.AddDynamic(this, &UWeaponLoadoutRowWidget::HandleRosterChanged);

	UE_LOG(LogTemp, Warning, TEXT("[LoadoutRow] Construct %p  UnitId=%s  WIdx=%d  Parent=%s"),
		this, *UnitId.ToString(), WeaponIndex, *GetNameSafe(GetParent()));
}

void UWeaponLoadoutRowWidget::NativeDestruct()
{
	if (ASetupGameState* S = GS())
		S->OnRosterChanged.RemoveDynamic(this, &UWeaponLoadoutRowWidget::HandleRosterChanged);
	Super::NativeDestruct();
}

void UWeaponLoadoutRowWidget::HandleMinus()
{
	const int32 Curr = GetLocalSeatCount();
	if (Curr > 0) SendCountToServer(Curr - 1);
}

void UWeaponLoadoutRowWidget::HandlePlus()
{
	const int32 Curr = GetLocalSeatCount();
	SendCountToServer(Curr + 1);
}

void UWeaponLoadoutRowWidget::HandleRosterChanged()
{
	RefreshFromState();
}

void UWeaponLoadoutRowWidget::RefreshFromState()
{
	if (WL_CountText)
		WL_CountText->SetText(FText::AsNumber(FMath::Max(0, GetLocalSeatCount())));
}

int32 UWeaponLoadoutRowWidget::GetLocalSeatCount() const
{
	if (const ASetupGameState* S = GS())
		if (const ASetupPlayerController* LPC = PC())
			return S->GetCountFor(UnitId, WeaponIndex, LPC->PlayerState == S->Player1);
	return 0;
}

void UWeaponLoadoutRowWidget::SendCountToServer(int32 NewCount) const
{
	if (ASetupPlayerController* LPC = PC())
		LPC->Server_SetUnitCount(UnitId, WeaponIndex, FMath::Clamp(NewCount, 0, 99));
}

FString UWeaponLoadoutRowWidget::FormatWeaponStats(const FWeaponProfile& W)
{
	return FString::Printf(TEXT("Rng %d\"  A %d  S %d  AP %d  D %d"),
		W.RangeInches, W.Attacks, W.Strength, W.AP, W.Damage);
}
