
#include "CoverVolume.h"

#include "Components/BoxComponent.h"

ACoverVolume::ACoverVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // static map prop

	// Root visual mesh (you can assign a mesh in BP or defaults)
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	RootComponent = Visual;

	// Visual is just for looks; Box drives cover logic
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetGenerateOverlapEvents(false);
	Visual->SetMobility(EComponentMobility::Static);

	// Box used for traces — scale/position it in editor relative to mesh
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("CoverBox"));
	Box->SetupAttachment(Visual);
	Box->SetMobility(EComponentMobility::Static);

	// Query-only: it should block your cover trace, nothing else
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetGenerateOverlapEvents(false);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	// If you’re using a custom trace channel, change this to that channel
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	// Starter size; adjust per instance in the level
	Box->SetBoxExtent(FVector(100.f, 100.f, 75.f));
	Box->SetRelativeLocation(FVector::ZeroVector);
}
