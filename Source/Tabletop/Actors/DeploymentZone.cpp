#include "DeploymentZone.h"
#include "Components/BoxComponent.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "Components/DecalComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Tabletop/Gamemodes/MatchGameMode.h"
#include "Tabletop/PlayerStates/TabletopPlayerState.h"

// Optional: tighten or loosen this if you want more/less spam
static constexpr float GDeployDebugThrottleSeconds = 5.f;

// Helper to stringify your EDeployOwner (adjust enum name if different)
static FString DeployOwnerToString(EDeployOwner Owner)
{
    if (const UEnum* Enum = StaticEnum<EDeployOwner>())
    {
        return Enum->GetDisplayNameTextByValue(static_cast<int64>(Owner)).ToString();
    }
    return TEXT("UnknownOwner");
}

static FString FactionDisplay(EFaction F)
{
    if (const UEnum* E = StaticEnum<EFaction>())
        return E->GetDisplayNameTextByValue(static_cast<int64>(F)).ToString();
    return TEXT("None");
}

static FString Cap8(const FString& In)
{
    const FString T = In.TrimStartAndEnd();
    return (T.Len() > 8) ? T.Left(8) : T;
}

ADeploymentZone::ADeploymentZone()
{
    PrimaryActorTick.bCanEverTick = false;

    Zone = CreateDefaultSubobject<UBoxComponent>(TEXT("Zone"));
    SetRootComponent(Zone);

    Zone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Zone->SetCollisionResponseToAllChannels(ECR_Ignore);  // purely logical volume
    Zone->SetGenerateOverlapEvents(false);

    OwnerText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("OwnerText"));
    OwnerText->SetupAttachment(RootComponent);

    // IMPORTANT: prevent actor scaling stretching the text
    OwnerText->SetAbsolute(false, false, true);         // absolute scale
    OwnerText->SetWorldScale3D(FVector(1.f));           // sane unit scale
    OwnerText->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
    OwnerText->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
    OwnerText->SetTextRenderColor(FColor::Cyan);
    OwnerText->SetWorldSize(OwnerTextWorldSize);
    OwnerText->SetVisibility(false);      

    bReplicates = false;         // level-placed actors load on all clients; we only replicate bEnabled
    SetReplicateMovement(false);
}

void ADeploymentZone::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADeploymentZone, bEnabled);
    DOREPLIFETIME(ADeploymentZone, CurrentOwner);
}

void ADeploymentZone::BeginPlay()
{
    Super::BeginPlay();
    
    if (UWorld* W = GetWorld())
    {
        if (AMatchGameState* S = W->GetGameState<AMatchGameState>())
        {
            S->OnDeploymentChanged.AddDynamic(this, &ADeploymentZone::OnMatchChanged);
        }
    }
    UpdateOwnerText();
}

void ADeploymentZone::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UWorld* W = GetWorld())
    {
        if (AMatchGameState* S = W->GetGameState<AMatchGameState>())
        {
            S->OnDeploymentChanged.RemoveDynamic(this, &ADeploymentZone::OnMatchChanged);
        }
    }
       
    Super::EndPlay(Reason);
}

void ADeploymentZone::OnMatchSignalChanged()
{
    RefreshVisuals();
}

void ADeploymentZone::OnMatchChanged()
{
    UpdateOwnerText();
}

void ADeploymentZone::UpdateOwnerText()
{
    if (!OwnerText || !Zone) return;

    // Hide if disabled, hidden by setting, or after battle begins
    bool bShow = bEnabled && bShowOwnerText;
    if (const UWorld* W = GetWorld())
        if (const AMatchGameState* S = W->GetGameState<AMatchGameState>())
        {
            if (S->Phase == EMatchPhase::Battle) bShow = false;
        }

    if (!bShow)
    {
        OwnerText->SetVisibility(false);
        return;
    }

    // Center of the box, plus a small world-Z lift so it’s not z-fighting
    const FVector Center = Zone->Bounds.Origin;
    const float   LiftZ  = Zone->Bounds.BoxExtent.Z + OwnerTextZOffset;
    const FVector Where  = Center + FVector(0,0,LiftZ);

    OwnerText->SetWorldLocation(Where);
    OwnerText->SetWorldSize(OwnerTextWorldSize);
    OwnerText->SetText(FText::FromString(OwnerDisplayText()));
    OwnerText->SetVisibility(true);

    // Face the local camera (yaw-only so text stays upright)
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
        if (PC->PlayerCameraManager)
        {
            const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
            FRotator Face = UKismetMathLibrary::FindLookAtRotation(Where, CamLoc);
            Face.Pitch = 0.f; Face.Roll = 0.f;
            OwnerText->SetWorldRotation(Face);
        }
}

FLinearColor ADeploymentZone::OwnerColor() const
{
    switch (CurrentOwner)
    {
    case EDeployOwner::Team1: return Team1Color;
    case EDeployOwner::Team2: return Team2Color;
    default:                  return EitherColor;
    }
}

// “Deploy: <DisplayName>” or “Deploy: Either side”
FString ADeploymentZone::OwnerDisplayText() const
{
    UWorld* W = GetWorld();
    if (!W) return TEXT("Deploy: —");
    
    auto Cap8 = [](const FString& In)->FString
    {
        const int32 Max = 8;
        const FString Trimmed = In.TrimStartAndEnd();
        return (Trimmed.Len() > Max) ? Trimmed.Left(Max) : Trimmed;
    };

    const AMatchGameState* S = GetWorld() ? GetWorld()->GetGameState<AMatchGameState>() : nullptr;
    auto Nice = [&](APlayerState* PS)->FString
    {
        if (!PS) return TEXT("---");

        if (const ATabletopPlayerState* TPS = Cast<ATabletopPlayerState>(PS))
        {
            const FString RawName = TPS->DisplayName.IsEmpty() ? PS->GetPlayerName() : TPS->DisplayName;
            const FString Name8   = Cap8(RawName);

            const FString Fac     = (TPS->SelectedFaction != EFaction::None)
                ? FactionDisplay(TPS->SelectedFaction)
                : FString();

            return Fac.IsEmpty()
                ? Name8
                : FString::Printf(TEXT("%s (%s)"), *Name8, *Fac);
        }

        return Cap8(PS->GetPlayerName());
    };

    switch (CurrentOwner)
    {
    case EDeployOwner::Team1: return FString::Printf(TEXT("Deploy: %s"), *Nice(S ? S->GetPSForTeam(1) : nullptr));
    case EDeployOwner::Team2: return FString::Printf(TEXT("Deploy: %s"), *Nice(S ? S->GetPSForTeam(2) : nullptr));

    default:                  return TEXT("Deploy: Either side");
    }
}


void ADeploymentZone::RefreshVisuals()
{
    // Hide visuals once the match enters Battle (or if disabled)
    bool bShow = bEnabled;
    if (const AMatchGameState* S = GetWorld() ? GetWorld()->GetGameState<AMatchGameState>() : nullptr)
    {
        if (S->Phase != EMatchPhase::Deployment)
            bShow = false; // auto-hide after battle starts
    }

    const FLinearColor Tint = OwnerColor();

    // --- Decal ---
    if (!ZoneDecal)
    {
        ZoneDecal = NewObject<UDecalComponent>(this, TEXT("ZoneDecal"));
        ZoneDecal->SetupAttachment(RootComponent);
        ZoneDecal->RegisterComponent();
    }
    if (ZoneDecal)
    {
        ZoneDecal->SetVisibility(bShow, true);
        ZoneDecal->SetHiddenInGame(!bShow);

        if (DecalMaterial)
        {
            ZoneDecal->SetDecalMaterial(DecalMaterial);
            if (UMaterialInstanceDynamic* MID = ZoneDecal->CreateDynamicMaterialInstance())
            {
                MID->SetVectorParameterValue(TEXT("Tint"), Tint); // your decal material should expose "Tint"
            }
        }

        const FVector Ext = Zone->GetScaledBoxExtent();
        ZoneDecal->DecalSize = FVector(Ext.X, Ext.Y, 1.f);
        const FVector Center = Zone->GetComponentLocation();
        ZoneDecal->SetWorldLocation(Center + FVector(0,0, Ext.Z + 5.f));
        ZoneDecal->SetWorldRotation(FRotator(-90.f, 0.f, 0.f));
    }

    // --- 3D Text label ---
    if (!Label3D)
    {
        Label3D = NewObject<UTextRenderComponent>(this, TEXT("ZoneLabel"));
        Label3D->SetupAttachment(RootComponent);
        Label3D->RegisterComponent();
        Label3D->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
        Label3D->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
        Label3D->SetWorldSize(48.f);
    }
    if (Label3D)
    {
        Label3D->SetVisibility(bShow, true);
        Label3D->SetHiddenInGame(!bShow);

        Label3D->SetText(FText::FromString(OwnerDisplayText()));
        Label3D->SetTextRenderColor(Tint.ToFColor(true));
        Label3D->SetWorldLocation(Zone->GetComponentLocation() + FVector(0,0, LabelHeight));
    }
}

bool ADeploymentZone::ContainsLocation(const FVector& WorldLocation) const
{
    if (!bEnabled || !Zone) return false;

    // Transform the world point into the box's local space
    const FTransform T = Zone->GetComponentTransform().Inverse();
    FVector Local = T.TransformPosition(WorldLocation);

    FVector Extent = Zone->GetUnscaledBoxExtent();
    FVector Center = FVector::ZeroVector;

    if (bUse2DCheck)
    {
        // Ignore Z by clamping to center.z and a small tolerance
        Local.Z = 0.f;
        Center.Z = 0.f;
        Extent.Z = 1.f; // thin slab to simulate 2D check
    }

    const FBox LocalBox = FBox(Center - Extent, Center + Extent);
    return LocalBox.IsInside(Local);
}

FVector ADeploymentZone::GetRandomPointInside() const
{
    const FTransform T = Zone->GetComponentTransform();
    const FVector Extent = Zone->GetUnscaledBoxExtent();

    FVector LocalRand(
        FMath::FRandRange(-Extent.X, Extent.X),
        FMath::FRandRange(-Extent.Y, Extent.Y),
        bUse2DCheck ? 0.f : FMath::FRandRange(-Extent.Z, Extent.Z)
    );

    return T.TransformPosition(LocalRand);
}

int32 ADeploymentZone::ResolvePlayerSlot(const APlayerController* PC)
{
    if (!PC || !PC->PlayerState) return 0;
    const int32 Id = PC->PlayerState->GetPlayerId();
    return (Id <= 0) ? 1 : 2; // host → 1, everyone else → 2
}

bool ADeploymentZone::IsLocationAllowedForTeam(const UWorld* World, int32 TeamNum, const FVector& WorldLocation)
{
    // ——— Debug throttle state ———
    static float LastPrintTime = -1000.f;
    const float Now = World ? World->GetTimeSeconds() : 0.f;

    auto Print = [&](const FString& Msg, const FColor& Color = FColor::Cyan)
    {
#if !(UE_BUILD_SHIPPING)
        if (GEngine && World && (Now - LastPrintTime) > GDeployDebugThrottleSeconds)
        {
            GEngine->AddOnScreenDebugMessage(
                /*Key*/ 991234, /*Time*/ GDeployDebugThrottleSeconds, Color, Msg);
            LastPrintTime = Now;
        }
        UE_LOG(LogTemp, Log, TEXT("%s"), *Msg);
#endif
    };

    if (!World)
    {
        Print(TEXT("IsLocationAllowedForTeam: World is null → DENY"), FColor::Red);
        return false;
    }
    if (TeamNum <= 0)
    {
        Print(FString::Printf(TEXT("IsLocationAllowedForTeam: Bad TeamNum=%d → DENY"), TeamNum), FColor::Red);
        return false;
    }

    int32 ZonesVisited = 0;
    int32 ZonesConsidered = 0;

    for (TActorIterator<ADeploymentZone> It(World); It; ++It)
    {
        const ADeploymentZone* Z = *It;
        ++ZonesVisited;

        if (!Z)
        {
            Print(TEXT("Zone: <null> → skip"));
            continue;
        }

        const FString ZoneName = GetNameSafe(Z);

        if (!Z->bEnabled)
        {
            Print(FString::Printf(TEXT("Zone: %s disabled → skip"), *ZoneName));
            continue;
        }

        const bool OwnerMatch =
            (Z->CurrentOwner == EDeployOwner::Either) ||
            (Z->CurrentOwner == EDeployOwner::Team1 && TeamNum == 1) ||
            (Z->CurrentOwner == EDeployOwner::Team2 && TeamNum == 2);

        ++ZonesConsidered;

        const bool bInside = Z->ContainsLocation(WorldLocation);

        Print(FString::Printf(
            TEXT("Zone: %s | Owner=%s | TeamNum=%d | OwnerMatch=%s | Contains=%s"),
            *ZoneName,
            *DeployOwnerToString(Z->CurrentOwner),
            TeamNum,
            OwnerMatch ? TEXT("YES") : TEXT("NO"),
            bInside ? TEXT("YES") : TEXT("NO")));

        if (OwnerMatch && bInside)
        {
            Print(FString::Printf(
                TEXT("IsLocationAllowedForTeam: ALLOW in zone %s for Team %d"),
                *ZoneName, TeamNum), FColor::Green);
            DrawDebugSphere(World, WorldLocation, 50.f, 12, FColor::Green, false, 5.f, 0, 2.f);
            return true;
        }
    }

    Print(FString::Printf(
        TEXT("IsLocationAllowedForTeam: DENY | Team=%d | ZonesVisited=%d | ZonesConsidered=%d"),
        TeamNum, ZonesVisited, ZonesConsidered), FColor::Red);

    DrawDebugSphere(World, WorldLocation, 50.f, 12, FColor::Red, false, 5.f, 0, 2.f);

    
    return false;
}