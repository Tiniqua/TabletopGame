
#include "NetDebugTextActor.h"

#include "Components/TextRenderComponent.h"
#include "Kismet/GameplayStatics.h"

ANetDebugTextActor::ANetDebugTextActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false; // local cosmetic actor per client

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	Text = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Text"));
	Text->SetupAttachment(Root);
	Text->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	Text->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	Text->SetWorldSize(32.f);
	Text->SetTextRenderColor(FColor::White);
	Text->SetText(FText::GetEmpty());
	Text->SetGenerateOverlapEvents(false);
	Text->SetMobility(EComponentMobility::Movable);

	// keep on top of world stuff a bit
	Text->SetTranslucentSortPriority(10);
}

void ANetDebugTextActor::Init(const FString& InText, const FColor& InColor, float InWorldSize, float InLifetime)
{
	Text->SetText(FText::FromString(InText));
	Text->SetTextRenderColor(InColor);
	Text->SetWorldSize(FMath::Max(4.f, InWorldSize));
	SetLifeSpan(FMath::Max(0.01f, InLifetime));

	// Face the camera initially
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		FVector CamLoc; FRotator CamRot;
		PC->GetPlayerViewPoint(CamLoc, CamRot);
		SetActorRotation((CamLoc - GetActorLocation()).Rotation());
	}
}

void ANetDebugTextActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bFaceCamera) return;

	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		FVector CamLoc; FRotator CamRot;
		PC->GetPlayerViewPoint(CamLoc, CamRot);
		SetActorRotation((CamLoc - GetActorLocation()).Rotation());
	}
}