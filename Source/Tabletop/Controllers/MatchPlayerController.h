
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/UserWidget.h"
#include "Tabletop/DeploymentWidget.h"
#include "Tabletop/GameplayWidget.h"
#include "Tabletop/Actors/UnitAction.h"

#include "MatchPlayerController.generated.h"

class AUnitBase;
class AMatchGameState;
class UDeploymentWidget;
class UGameplayWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectedChanged, class AUnitBase*, NewSelection);


UCLASS()
class TABLETOP_API AMatchPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	UPROPERTY()
	AUnitBase* SelectedUnit = nullptr;
	UPROPERTY()
	bool bTargetMode = false;

	UPROPERTY(EditDefaultsOnly, Category="UI")
	TSubclassOf<class UMatchSummaryWidget> SummaryWidgetClass;

	UPROPERTY() UMatchSummaryWidget* SummaryWidgetInstance = nullptr;

	UFUNCTION(Client, Reliable) void Client_ShowSummary();
	UFUNCTION(Client, Reliable) void Client_HideSummary();
	UFUNCTION(Server, Reliable) void Server_ExitToMainMenu();

	
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSelectedChanged, AUnitBase*, NewSel);
	UPROPERTY(BlueprintAssignable) FOnSelectedChanged OnSelectedChanged;

	UFUNCTION(Client, Reliable)
	void Client_OnUnitMoved(class AUnitBase* Unit, float SpentTTIn, float NewBudgetTTIn);

	UFUNCTION(Client, Reliable)
	void Client_OnMoveDenied_OverBudget(class AUnitBase* Unit, float AttemptTTIn, float BudgetTTIn);

	UFUNCTION(Server, Reliable)
	void Server_RequestAdvance(AUnitBase* Unit);

	UFUNCTION(Client, Reliable)
	void Client_OnAdvanced(AUnitBase* Unit, int32 BonusInches);

	UFUNCTION(Client, Reliable)
	void Client_OnOverwatchArmed(class AUnitBase* Unit);

	UFUNCTION(Server, Reliable)
	void Server_SetGlobalSelectedUnit(AUnitBase* NewSel);


	// Expose helpers for the widget to call
	UFUNCTION(BlueprintCallable) void EnterTargetMode() { bTargetMode = true; }
	
	UFUNCTION(Client, Reliable)
	void Client_KickUIRefresh();
	
	virtual void BeginPlay() override;
	void SetSelectedUnit(AUnitBase* NewSel);
	virtual void SetupInputComponent() override;

	UFUNCTION(BlueprintCallable)
	void BeginDeployForUnit(FName UnitId,int32 InWeaponIndex);
	
	UFUNCTION(Server, Reliable)
	void Server_RequestDeploy(FName UnitId, const FTransform& Where, int32 WeaponIndex);
	//void Server_RequestDeploy(FName UnitId, FTransform Where, int32 WeaponIndex);

	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_StartBattle();

	UFUNCTION(Server, Reliable, BlueprintCallable)
	void Server_EndPhase();

	// Server RPCs
	UFUNCTION(Server, Reliable)
	void Server_MoveUnit(AUnitBase* Unit, FVector Dest);
	UFUNCTION(Server, Reliable)
	void Server_SelectTarget(AUnitBase* Attacker, AUnitBase* Target);
	UFUNCTION(Server, Reliable)
	void Server_CancelPreview(AUnitBase* Attacker);
	UFUNCTION(Server, Reliable)
	void Server_ConfirmShoot(AUnitBase* Attacker, AUnitBase* Target);
	UFUNCTION(Server, Reliable)
	void Server_SelectFriendly(class AUnitBase* Attacker, class AUnitBase* Target);

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

	UFUNCTION(Client, Reliable)
	void Client_ClearSelectionAfterConfirm();
	
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	
	AMatchGameState* GS() const;

	FTimerHandle BindGSTimer;
	
	UPROPERTY()
	FName PendingGroundActionId = NAME_None;

	UPROPERTY()
	int32 PendingWeaponIndex = INDEX_NONE;

	UPROPERTY()
	TObjectPtr<AUnitBase> PendingActionUnit = nullptr;

	UPROPERTY() bool bFriendlyTargetMode = false; // NEW

	UFUNCTION(BlueprintCallable) 
	void EnterFriendlyTargetMode() { bTargetMode = true; bFriendlyTargetMode = true; } 
	
	UFUNCTION(BlueprintCallable) 
	void ExitTargetMode() { bTargetMode = false; bFriendlyTargetMode = false; } // UPDATE
	
	UFUNCTION(Server, Reliable)
	void Server_ExecuteAction(AUnitBase* Unit, FName ActionId, FActionRuntimeArgs Args);

	UFUNCTION() void HandleSelectedChanged_Internal(class AUnitBase* NewSel);
	void UpdateTurnContextVisibility();
	void CacheTurnContextIfNeeded(bool bForce = false);
	
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

	UPROPERTY()
	UTurnContextWidget* TurnContextRef = nullptr;

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

	UPROPERTY(EditDefaultsOnly, Category="Trace")
	TEnumAsByte<ECollisionChannel> BattleGroundTraceChannel = ECC_GameTraceChannel3;

	bool TraceGround(FHitResult& OutHit, TEnumAsByte<ECollisionChannel> Channel) const;
	
	bool TraceGround_Deploy(FHitResult& OutHit) const { return TraceGround(OutHit, DeployTraceChannel); }
	bool TraceGround_Battle(FHitResult& OutHit) const { return TraceGround(OutHit, BattleGroundTraceChannel); }

	

	UFUNCTION()
	void OnUnStuckPressed();

	/** Run on server so teleport replicates to everyone */
	UFUNCTION(Server, Reliable)
	void Server_UnStuck();

	/** Shared utility the server calls to do the actual teleport */
	bool TeleportPawnToFirstPlayerStart();

protected:
	// NEW: channel to click units (matches UnitBase::SelectCollision setup)
	UPROPERTY(EditDefaultsOnly, Category="Trace")
	TEnumAsByte<ECollisionChannel> UnitTraceChannel = ECC_GameTraceChannel2;

	// Helpers
	//bool TraceGround(FHitResult& OutHit) const;
	class AUnitBase* TraceUnit() const;

	void SelectUnit(class AUnitBase* U);
	void ClearSelection();
};
