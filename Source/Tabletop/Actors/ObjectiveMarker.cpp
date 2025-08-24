
#include "ObjectiveMarker.h"

#include "EngineUtils.h"
#include "UnitBase.h"
#include "Components/SphereComponent.h"
#include "Net/UnrealNetwork.h"

AObjectiveMarker::AObjectiveMarker()
{
	bReplicates = true;

	Sphere = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	RootComponent = Sphere;
	Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sphere->SetGenerateOverlapEvents(false);
}

void AObjectiveMarker::BeginPlay()
{
	Super::BeginPlay();
	RecalcRadius();

#if !(UE_BUILD_SHIPPING)
	if (bDrawDebug && HasAuthority())
	{
		DrawDebugSphere(GetWorld(), GetActorLocation(), RadiusCm, 24, FColor::Emerald, /*bPersistent*/true, /*LifeTime*/0.f, 0, 2.f);
	}
#endif
}

#if WITH_EDITOR
void AObjectiveMarker::PostEditChangeProperty(FPropertyChangedEvent& E)
{
	Super::PostEditChangeProperty(E);
	RecalcRadius();
}
#endif

void AObjectiveMarker::RecalcRadius()
{
	if (Sphere) Sphere->SetSphereRadius(RadiusCm);
	RadiusSq = RadiusCm * RadiusCm;
}

void AObjectiveMarker::RecalculateControl()
{
	if (!HasAuthority()) return;

	// Sum OC per PlayerState on the server
	TMap<APlayerState*, int32> Sum;
	for (TActorIterator<AUnitBase> It(GetWorld()); It; ++It)
	{
		AUnitBase* Unit = *It;
		if (!Unit) continue;

		// Replace 'OwningPS' with whatever you store for unit ownership
		APlayerState* PS = Unit->OwningPS;
		if (!PS) continue;

		const int32 OC = Unit->GetObjectiveControlAt(this); // per-model OC from the unit
		if (OC <= 0) continue;

		Sum.FindOrAdd(PS) += OC;
	}

	// Build replicated snapshot
	Contestants.Reset(Sum.Num());
	for (const TPair<APlayerState*, int32>& KV : Sum)
	{
		FObjectiveContestant C;
		C.PlayerState = KV.Key;
		C.ObjectiveControl = KV.Value;
		Contestants.Add(C);
	}

	// Sort by OC desc for nice UI (top[0] is the leader)
	Contestants.Sort([](const FObjectiveContestant& A, const FObjectiveContestant& B)
	{
		return A.ObjectiveControl > B.ObjectiveControl;
	});

	// Decide controller: highest OC strictly greater than second highest
	if (Contestants.Num() == 0 || Contestants[0].ObjectiveControl <= 0)
	{
		ControllingPS = nullptr;
	}
	else if (Contestants.Num() >= 2 && Contestants[0].ObjectiveControl == Contestants[1].ObjectiveControl)
	{
		ControllingPS = nullptr; // contested tie
	}
	else
	{
		ControllingPS = Contestants[0].PlayerState;
	}

	// Net notify
	ForceNetUpdate();

#if !(UE_BUILD_SHIPPING)
	// Log for clarity
	if (Contestants.Num() > 0)
	{
		const int32 Top = Contestants[0].ObjectiveControl;
		const TCHAR* Who = ControllingPS ? TEXT("Controller chosen") : TEXT("Contested/None");
		UE_LOG(LogTemp, Log, TEXT("[OBJ %s] top OC=%d -> %s, contestants=%d"),
			*ObjectiveId.ToString(), Top, Who, Contestants.Num());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[OBJ %s] no contestants"), *ObjectiveId.ToString());
	}
#endif
}

void AObjectiveMarker::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AObjectiveMarker, Contestants);
	DOREPLIFETIME(AObjectiveMarker, ControllingPS);
}