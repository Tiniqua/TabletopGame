#include "WorldspaceUnitStatusActor.h"

#include "Components/SceneComponent.h"
#include "Components/WidgetComponent.h"
#include "Tabletop/WorldspaceUnitStatusWidget.h"
#include "Tabletop/Actors/UnitBase.h"
#include "Tabletop/Controllers/MatchPlayerController.h"
#include "Tabletop/Actors/CoverVolume.h"

AWorldspaceUnitStatusActor::AWorldspaceUnitStatusActor()
{
    PrimaryActorTick.bCanEverTick = true;
    SetActorTickEnabled(false);
    bReplicates = false;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    WidgetComponent = CreateDefaultSubobject<UWidgetComponent>(TEXT("UnitStatusWidget"));
    WidgetComponent->SetupAttachment(RootComponent);
    WidgetComponent->SetWidgetSpace(EWidgetSpace::World);
    WidgetComponent->SetDrawAtDesiredSize(true);
    WidgetComponent->SetTwoSided(true);
    WidgetComponent->SetPivot(FVector2D(0.5f, 0.f));
    WidgetComponent->SetVisibility(false);

    SetActorHiddenInGame(true);
    CachedCover = ECoverType::None;
}

void AWorldspaceUnitStatusActor::InitializeIndicator(AMatchPlayerController* InOwner, TSubclassOf<UWorldspaceUnitStatusWidget> WidgetClass)
{
    OwnerPC = InOwner;
    if (!WidgetComponent)
    {
        return;
    }

    if (*WidgetClass && WidgetComponent->GetWidgetClass() != WidgetClass)
    {
        WidgetComponent->SetWidgetClass(WidgetClass);
        WidgetComponent->InitWidget();
    }

    WidgetComponent->SetVisibility(false);
}

void AWorldspaceUnitStatusActor::SetObservedUnit(AUnitBase* Unit)
{
    if (ObservedUnit.Get() == Unit)
    {
        return;
    }

    if (AUnitBase* Previous = ObservedUnit.Get())
    {
        Previous->OnUnitStatusChanged.RemoveDynamic(this, &AWorldspaceUnitStatusActor::HandleObservedUnitStatusChanged);
    }

    ObservedUnit = Unit;

    if (AUnitBase* NewUnit = ObservedUnit.Get())
    {
        NewUnit->OnUnitStatusChanged.AddDynamic(this, &AWorldspaceUnitStatusActor::HandleObservedUnitStatusChanged);
        SetActorHiddenInGame(false);
        SetActorTickEnabled(true);
        if (WidgetComponent)
        {
            WidgetComponent->SetVisibility(true);
        }
        UpdateTransform();
        bPendingRefresh = true;
        RefreshWidget();
    }
    else
    {
        if (WidgetComponent)
        {
            WidgetComponent->SetVisibility(false);
        }
        SetActorHiddenInGame(true);
        SetActorTickEnabled(false);
        bPendingRefresh = false;
        RefreshWidget();
    }
}

void AWorldspaceUnitStatusActor::SetCoverInfo(ECoverType CoverType, bool bHasCoverInfo)
{
    if (CachedCover != CoverType || bCoverIsKnown != bHasCoverInfo)
    {
        CachedCover = CoverType;
        bCoverIsKnown = bHasCoverInfo;
        bPendingRefresh = true;
    }
}

void AWorldspaceUnitStatusActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (!ObservedUnit.IsValid())
    {
        return;
    }

    UpdateTransform();

    if (bPendingRefresh)
    {
        RefreshWidget();
    }
}

void AWorldspaceUnitStatusActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (AUnitBase* Unit = ObservedUnit.Get())
    {
        Unit->OnUnitStatusChanged.RemoveDynamic(this, &AWorldspaceUnitStatusActor::HandleObservedUnitStatusChanged);
    }
    ObservedUnit = nullptr;
    Super::EndPlay(EndPlayReason);
}

void AWorldspaceUnitStatusActor::HandleObservedUnitStatusChanged()
{
    bPendingRefresh = true;
    UpdateTransform();
    RefreshWidget();
}

void AWorldspaceUnitStatusActor::RefreshWidget()
{
    bPendingRefresh = false;

    if (!WidgetComponent)
    {
        return;
    }

    UWorldspaceUnitStatusWidget* Widget = Cast<UWorldspaceUnitStatusWidget>(WidgetComponent->GetUserWidgetObject());
    if (!Widget)
    {
        return;
    }

    Widget->ApplyUnitStatus(ObservedUnit.Get(), CachedCover, bCoverIsKnown);
}

void AWorldspaceUnitStatusActor::UpdateTransform() const
{
    if (!ObservedUnit.IsValid())
    {
        return;
    }

    const FVector BaseLocation = ObservedUnit->GetActorLocation();
    SetActorLocation(BaseLocation + WorldOffset);
}
