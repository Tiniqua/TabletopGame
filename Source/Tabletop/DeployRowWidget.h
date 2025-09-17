
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Controllers/MatchPlayerController.h"
#include "Gamemodes/MatchGameMode.h"
#include "Controllers/MatchPlayerController.h"
#include "Gamemodes/MatchGameMode.h"

#include "DeployRowWidget.generated.h"

class UWeaponPickerWidget;
class UTextBlock;
class UImage;
class UButton;

UCLASS()
class TABLETOP_API UDeployRowWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	AMatchGameState* GS() const;
	AMatchPlayerController* MPC() const;
	
	void InitDisplay(const FText& Name, UTexture2D* Icon, int32 Count);
	void SetDeployPayload(FName InUnitId, int32 InWeaponIdx)
	{
		UnitId = InUnitId;
		WeaponIndex = InWeaponIdx;
	}


	UPROPERTY(meta=(BindWidget)) UTextBlock* NameText = nullptr;
	UPROPERTY(meta=(BindWidget)) UImage*     IconImg  = nullptr;
	UPROPERTY(meta=(BindWidget)) UTextBlock* CountText= nullptr;
	UPROPERTY(meta=(BindWidget)) UButton*    SelectBtn= nullptr;

	FName UnitId = NAME_None;
	int32 WeaponIndex;

	UPROPERTY(BlueprintReadWrite,EditAnywhere)
	TSubclassOf<UWeaponPickerWidget> WeaponPickerClass;

protected:
	virtual void NativeConstruct() override;

private:
	UFUNCTION() void OnSelectClicked();
};
