
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Tabletop/DeploymentWidget.h"
#include "Tabletop/GameplayWidget.h"

#include "MatchPlayerController.generated.h"

class AMatchGameState;
class UDeploymentWidget;
class UGameplayWidget;

UCLASS()
class TABLETOP_API AMatchPlayerController : public APlayerController
{
	GENERATED_BODY()
public:

	UFUNCTION(Client, Reliable)
	void Client_KickUIRefresh();
	
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

	UFUNCTION(BlueprintCallable)
	void BeginDeployForUnit(FName UnitId);
	
	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_RequestDeploy(FName UnitId, FTransform Where);

	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_StartBattle();

	/** Class reference to the deployment widget */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI")
	TSubclassOf<UDeploymentWidget> DeploymentWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI")
	TSubclassOf<UGameplayWidget>  GameplayWidgetClass;
	
	UPROPERTY() UDeploymentWidget* DeploymentWidgetInstance = nullptr;
	UPROPERTY() UGameplayWidget*   GameplayWidgetInstance   = nullptr;
	
	// ——— Phase / GS plumbing ———
	UFUNCTION() void OnPhaseSignalChanged(); // bound to GS
	void TryBindToGameState();
	void RefreshPhaseUI();

	UFUNCTION(Client, Reliable)
	void Client_KickPhaseRefresh();
	
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	
	AMatchGameState* GS() const;

	FTimerHandle BindGSTimer;

	template<typename TWidget>
	void ShowWidgetTyped(TWidget*& Instance, TSubclassOf<TWidget> Class, bool bShow)
	{
		if (bShow)
		{
			if (!Instance && Class.Get() && IsLocalController())
			{
				// NOTE: use the UClass* overload
				Instance = CreateWidget<TWidget>(this, Class.Get());
				if (Instance) Instance->AddToViewport();
			}
			if (Instance) Instance->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			if (Instance) Instance->SetVisibility(ESlateVisibility::Hidden);
		}
	}

private:
	TWeakObjectPtr<AMatchGameState> BoundGS;
	
	FTimerHandle DeployCursorTimer;
	EMouseCursor::Type DefaultCursorBackup = EMouseCursor::Default;

	EMouseCursor::Type BackedUpCursor = EMouseCursor::Default;

	void StartDeployCursorFeedback();
	void StopDeployCursorFeedback();
	void UpdateDeployCursor();
	void SetCursorType(EMouseCursor::Type Type);

	FName PendingDeployUnit = NAME_None;

	void OnLeftClick();       // confirms a deployment if we have a pending unit
	void OnRightClickCancel();

	bool TraceDeployLocation(FHitResult& OutHit) const;

	// Choose which trace channel to use (set in defaults or leave as Visibility if you prefer)
	UPROPERTY(EditDefaultsOnly, Category="Deploy")
	TEnumAsByte<ECollisionChannel> DeployTraceChannel = ECC_GameTraceChannel1;

	UFUNCTION()
	void OnUnStuckPressed();

	/** Run on server so teleport replicates to everyone */
	UFUNCTION(Server, Reliable)
	void Server_UnStuck();

	/** Shared utility the server calls to do the actual teleport */
	bool TeleportPawnToFirstPlayerStart();
};
