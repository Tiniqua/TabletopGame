#include "DeploymentZone.h"
#include "Components/BoxComponent.h"
#include "Net/UnrealNetwork.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"

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

ADeploymentZone::ADeploymentZone()
{
    PrimaryActorTick.bCanEverTick = false;

    Zone = CreateDefaultSubobject<UBoxComponent>(TEXT("Zone"));
    SetRootComponent(Zone);

    Zone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Zone->SetCollisionResponseToAllChannels(ECR_Ignore);  // purely logical volume
    Zone->SetGenerateOverlapEvents(false);

    bReplicates = false;         // level-placed actors load on all clients; we only replicate bEnabled
    SetReplicateMovement(false);
}

void ADeploymentZone::GetLifetimeReplicatedProps(TArray< FLifetimeProperty >& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(ADeploymentZone, bEnabled);
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