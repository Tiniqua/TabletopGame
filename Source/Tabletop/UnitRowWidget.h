// UnitRowWidget.h
#pragma once

#include "CoreMinimal.h"
#include "ArmyData.h"
#include "Blueprint/UserWidget.h"
#include "UnitRowWidget.generated.h"

// (helpers namespace UIFormat is at the very top of this header; see earlier block)

class UTextBlock;
class UPanelWidget;

UCLASS()
class TABLETOP_API UUnitRowWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// New single init path: full data from DT
	void InitFull(FName InRowName, const FUnitRow& Row);

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// Header fields
	UPROPERTY(meta=(BindWidget)) UTextBlock* NameText         = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* PointsText       = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* CountText        = nullptr; // total across loadouts
	UPROPERTY(meta=(BindWidget)) UTextBlock* UnitStatsText    = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* UnitAbilitiesText= nullptr;

	// Container for loadout rows
	UPROPERTY(meta=(BindWidget)) UPanelWidget* LoadoutsList   = nullptr;

private:
	// Data
	FName    UnitId   = NAME_None;
	int32    UnitPoints = 0;
	FUnitRow CachedRow;

	UPROPERTY(EditDefaultsOnly, Category="UI")
	TSubclassOf<class UWeaponLoadoutRowWidget> LoadoutRowClass;

	// Callbacks
	UFUNCTION() void HandleRosterChanged();

	// Helpers
	class ASetupGameState* GS() const;
	bool  IsLocalP1() const;
	void  RebuildLoadouts();
	void  RefreshHeader();

	// Formatters
	static FString FormatUnitStats(const FUnitRow& U);
};

