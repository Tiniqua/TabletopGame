#include "CoverVolume.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY(LogCoverNet);

ACoverVolume::ACoverVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

#if WITH_EDITOR
	// nicer editor UX (rename/undo/etc.)
	SetFlags(RF_Transactional);
#endif

	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	RootComponent = Visual;
	Visual->SetMobility(EComponentMobility::Movable);
	Visual->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Visual->SetGenerateOverlapEvents(false);
	Visual->SetCollisionResponseToAllChannels(ECR_Ignore);
	Visual->SetCanEverAffectNavigation(false);

	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("CoverBox"));
	Box->SetupAttachment(Visual);
	Box->SetMobility(EComponentMobility::Movable);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetBoxExtent(FVector(100.f, 100.f, 75.f));
	Box->SetRelativeLocation(FVector::ZeroVector);
	Box->SetCanEverAffectNavigation(false);

	MaxHealth = FMath::Max(0.01f, MaxHealth);
	Health    = MaxHealth;
}

bool ACoverVolume::HaveValidComponents() const
{
	return IsValid(Visual) && IsValid(Box);
}

bool ACoverVolume::IsGameWorld() const
{
	const UWorld* W = GetWorld();
	return (W && (W->IsGameWorld() || W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE));
}

void ACoverVolume::OnConstruction(const FTransform& Transform)
{
	// Keep visuals coherent in the editor; never destroy here.
	RecomputeFromHealth();
}

void ACoverVolume::BeginPlay()
{
	Super::BeginPlay();

	MaxHealth = FMath::Max(0.01f, MaxHealth);
	Health    = FMath::Clamp(Health, 0.f, MaxHealth);

	if (HasAuthority() && IsGameWorld() && bPreferLowCover && LowMesh)
	{
		const float Eps = kHealthEpsilonPct;
		const float TargetPct = FMath::Clamp(HighToLowPct - Eps, 0.f, 1.f);
		Health = FMath::Clamp(TargetPct * MaxHealth, 0.f, MaxHealth);
	}

	// Establish a sane baseline; not a damage recompute.
	bRecomputeAfterDamage = false;
	LastAppliedType = ComputeTypeFromHealth();
	ApplyStateVisuals(LastAppliedType);
}

void ACoverVolume::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACoverVolume, Health);
	DOREPLIFETIME(ACoverVolume, HighMesh);
	DOREPLIFETIME(ACoverVolume, LowMesh);
	DOREPLIFETIME(ACoverVolume, NoneMesh);
	DOREPLIFETIME(ACoverVolume, HighToLowPct);
}

float ACoverVolume::GetLowStateHealthFraction() const
{
	return FMath::Clamp(HighToLowPct, 0.f, 1.f);
}

bool ACoverVolume::IsHighDefault() const
{
	if (HighMesh && (!bPreferLowCover || !LowMesh)) return true;
	if (!HighMesh && !LowMesh) return true; // no meshes → treat as "high by default"
	return false;
}

ECoverType ACoverVolume::GetCurrentCoverType() const
{
	return ComputeTypeFromHealth();
}

bool ACoverVolume::BlocksCoverTrace() const
{
	const ECoverType T = ComputeTypeFromHealth();
	return (T == ECoverType::High || T == ECoverType::Low);
}

ECoverType ACoverVolume::ComputeTypeFromHealth() const
{
	const bool bHasHigh = (HighMesh != nullptr);
	const bool bHasLow  = (LowMesh  != nullptr);
	const bool bHasAny  = bHasHigh || bHasLow;

	if (!bHasAny)
		return ECoverType::None;

	const float HF = (MaxHealth > 0.f) ? (Health / MaxHealth) : 0.f;
	const float Threshold = FMath::Clamp(HighToLowPct, 0.f, 1.f);

	if (!bHasHigh && bHasLow)
		return (Health > 0.f) ? ECoverType::Low : ECoverType::None;

	if (bHasHigh && !bHasLow)
		return (Health > 0.f) ? ECoverType::High : ECoverType::None;

	if (Health <= 0.f)
		return ECoverType::None;

	if (HF + KINDA_SMALL_NUMBER >= Threshold)
		return ECoverType::High;

	return ECoverType::Low;
}

void ACoverVolume::ApplyStateVisuals(ECoverType NewType)
{
	// Only handles meshes + collision; no destruction logic here.
	if (!HaveValidComponents())
		return;

	UStaticMesh* MeshToUse = nullptr;
	switch (NewType)
	{
	case ECoverType::High: MeshToUse = HighMesh; break;
	case ECoverType::Low:  MeshToUse = LowMesh;  break;
	default:               MeshToUse = NoneMesh; break;
	}

	const bool bHasAnyCover = (NewType == ECoverType::High || NewType == ECoverType::Low);

	// Collision on the simple box and the visual
	Box->SetCollisionEnabled(bHasAnyCover ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Visibility, bHasAnyCover ? ECR_Block : ECR_Ignore);
	Box->SetCollisionResponseToChannel(CoverTraceECC,  bHasAnyCover ? ECR_Block : ECR_Ignore);

	Visual->SetCollisionEnabled(bHasAnyCover ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	Visual->SetCollisionResponseToAllChannels(ECR_Ignore);
	Visual->SetCollisionResponseToChannel(ECC_Visibility, bHasAnyCover ? ECR_Block : ECR_Ignore);
	Visual->SetCollisionResponseToChannel(CoverTraceECC,  bHasAnyCover ? ECR_Block : ECR_Ignore);

	// Mesh
	if (Visual->GetStaticMesh() != MeshToUse)
	{
		Visual->SetStaticMesh(MeshToUse);
	}

	// Clear any forced materials
	if (Visual->IsRegistered())
	{
		const int32 Slots = Visual->GetNumMaterials();
		for (int32 i = 0; i < Slots; ++i)
		{
			if (Visual->GetMaterial(i) != nullptr)
			{
				Visual->SetMaterial(i, nullptr);
			}
		}
		Visual->MarkRenderStateDirty();
	}

	LastAppliedType = NewType;
}

void ACoverVolume::RecomputeFromHealth()
{
	const ECoverType NewType = ComputeTypeFromHealth();

	// Only the server in a real game world may destroy, and only immediately after damage.
	const bool bServerCanDestroy =
		HasAuthority() && IsGameWorld() && bDestroyOnZero && bRecomputeAfterDamage;

	// We only auto-destroy if we were LOW and dropped to NONE due to damage (i.e., 0 HP).
	// This prevents preset/streaming nulls from ever deleting actors.
	if (bServerCanDestroy &&
		LastAppliedType == ECoverType::Low &&
		NewType == ECoverType::None &&
		Health <= 0.f)
	{
		UE_LOG(LogCoverNet, Verbose, TEXT("[CV %s] Destroying after damage (Low -> None at 0 HP)"), *GetName());

		// Be nice to physics/queries for a frame
		if (Box)    Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (Visual) Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		// Optional: show a None mesh briefly if you have one (purely cosmetic)
		if (Visual && NoneMesh)
		{
			Visual->SetStaticMesh(NoneMesh);
			Visual->MarkRenderStateDirty();
		}

		Destroy();
		bRecomputeAfterDamage = false; // clear the flag even though we're going away
		return;
	}

	// Otherwise just update visuals/collision.
	ApplyStateVisuals(NewType);

	// Done with a damage-driven recompute (if any).
	bRecomputeAfterDamage = false;
}

float ACoverVolume::ClampDamageToSingleStateDrop(float Incoming) const
{
	if (Incoming <= 0.f || MaxHealth <= 0.f || Health <= 0.f)
		return 0.f;

	const ECoverType Cur = ComputeTypeFromHealth();

	if (Cur == ECoverType::High && LowMesh)
	{
		const float ThresholdHP = FMath::Clamp(HighToLowPct, 0.f, 1.f) * MaxHealth;
		const float EpsHP = kHealthEpsilonPct * MaxHealth;
		const float ClampTo = FMath::Max(0.f, ThresholdHP - EpsHP);
		const float MaxAllowed = FMath::Max(0.f, Health - ClampTo);
		return FMath::Min(Incoming, MaxAllowed);
	}

	return Incoming; // from Low: allow full drop to zero
}

void ACoverVolume::SetHealthPercentImmediate(float Pct)
{
	if (!HasAuthority()) return;

	Pct = FMath::Clamp(Pct, 0.f, 1.f);
	Health = FMath::Clamp(MaxHealth * Pct, 0.f, MaxHealth);
	RecomputeFromHealth();

	if (IsGameWorld())
	{
		ForceNetUpdate();
	}
}

void ACoverVolume::ApplyPresetMeshes(UStaticMesh* InHigh, UStaticMesh* InLow, UStaticMesh* InNone)
{
	HighMesh = InHigh;
	LowMesh  = InLow;
	NoneMesh = InNone;

	bPresetInitialized = true;
	bInitialized = true;

	if (!HaveValidComponents())
	{
		if (Visual && !Visual->IsRegistered()) Visual->RegisterComponent();
		if (Box    && !Box->IsRegistered())    Box->RegisterComponent();
	}

	// If Health arrived first, apply it now
	if (PendingHealth >= 0.f)
	{
		Health = FMath::Clamp(PendingHealth, 0.f, MaxHealth);
		PendingHealth = -1.f;
	}

	RecomputeFromHealth();
	UE_LOG(LogCoverNet, Log, TEXT("[CV %s] ApplyPresetMeshes ..."), *GetName());

	if (HasAuthority() && IsGameWorld()) { ForceNetUpdate(); }
}


void ACoverVolume::ApplyCoverDamage(float Incoming)
{
	if (!HasAuthority() || Incoming <= 0.f || MaxHealth <= 0.f || Health <= 0.f)
		return;

	const float Clamped = ClampDamageToSingleStateDrop(Incoming);
	if (Clamped <= 0.f)
		return;

	Health = FMath::Clamp(Health - Clamped, 0.f, MaxHealth);

	// This recompute is damage-driven; allows Low->None destroy path.
	bRecomputeAfterDamage = true;
	RecomputeFromHealth();

	if (IsGameWorld())
	{
		ForceNetUpdate();
	}
}

void ACoverVolume::OnRep_Health()
{
	if (!bInitialized)
	{
		PendingHealth = Health;
		return;
	}
	RecomputeFromHealth();
}

void ACoverVolume::OnRep_Preset()
{
	UE_LOG(LogCoverNet, Verbose, TEXT("[CV %s] OnRep_Preset High=%s Low=%s None=%s Thr=%.2f"),
		*GetName(),
		HighMesh? *HighMesh->GetName():TEXT("null"),
		LowMesh ? *LowMesh ->GetName():TEXT("null"),
		NoneMesh?*NoneMesh->GetName():TEXT("null"),
		HighToLowPct);

	ApplyPresetMeshes(HighMesh, LowMesh, NoneMesh);
}

void ACoverVolume::OnRep_Threshold()
{
	RecomputeFromHealth();
}