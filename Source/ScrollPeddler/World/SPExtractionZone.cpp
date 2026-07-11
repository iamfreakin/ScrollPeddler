#include "World/SPExtractionZone.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Game/SPGameMode.h"
#include "Player/SPCharacter.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogSPExtractionZone, Log, All);

ASPExtractionZone::ASPExtractionZone()
{
	PrimaryActorTick.bCanEverTick = false;
	bAlwaysRelevant = true;
	SetReplicates(true);
	SetReplicateMovement(false);

	ExtractionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("ExtractionBounds"));
	SetRootComponent(ExtractionBounds);
	ExtractionBounds->SetBoxExtent(FVector(175.0f, 175.0f, 125.0f));
	ExtractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ExtractionBounds->SetCollisionObjectType(ECC_WorldDynamic);
	ExtractionBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	ExtractionBounds->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	ExtractionBounds->SetGenerateOverlapEvents(true);
	ExtractionBounds->OnComponentBeginOverlap.AddDynamic(this, &ASPExtractionZone::OnExtractionOverlap);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(ExtractionBounds);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -115.0f));
	VisualMesh->SetRelativeScale3D(FVector(3.0f, 3.0f, 0.12f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(
		TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMesh.Succeeded())
	{
		VisualMesh->SetStaticMesh(CylinderMesh.Object);
	}
}

FVector ASPExtractionZone::GetExtractionPoint() const
{
	return ExtractionBounds ? ExtractionBounds->GetComponentLocation() : GetActorLocation();
}

bool ASPExtractionZone::TryExtract(ASPCharacter* Character)
{
	if (!HasAuthority())
	{
		UE_LOG(LogSPExtractionZone, Warning,
			TEXT("SP_SPIKE_EXTRACTION_REJECTED reason=non_authority"));
		return false;
	}

	if (!IsValid(Character) || !Character->GetController())
	{
		UE_LOG(LogSPExtractionZone, Warning,
			TEXT("SP_SPIKE_EXTRACTION_REJECTED reason=invalid_character"));
		return false;
	}

	const float Distance = FVector::Distance(Character->GetActorLocation(), GetExtractionPoint());
	if (Distance > MaxExtractionDistance)
	{
		UE_LOG(LogSPExtractionZone, Warning,
			TEXT("SP_SPIKE_EXTRACTION_REJECTED reason=distance player=%s distance=%.1f max=%.1f"),
			*GetNameSafe(Character->GetController()), Distance, MaxExtractionDistance);
		return false;
	}

	ASPGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<ASPGameMode>() : nullptr;
	if (!GameMode)
	{
		UE_LOG(LogSPExtractionZone, Error,
			TEXT("SP_SPIKE_EXTRACTION_REJECTED reason=missing_game_mode"));
		return false;
	}

	UE_LOG(LogSPExtractionZone, Display,
		TEXT("SP_SPIKE_EXTRACTION_VALIDATED player=%s distance=%.1f"),
		*GetNameSafe(Character->GetController()), Distance);
	GameMode->HandlePlayerReachedExtraction(Character);
	return true;
}

void ASPExtractionZone::OnExtractionOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComponent,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (HasAuthority())
	{
		TryExtract(Cast<ASPCharacter>(OtherActor));
	}
}
