#include "World/SPScrollPickup.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "Player/SPCharacter.h"
#include "ScrollPeddler.h"
#include "UObject/ConstructorHelpers.h"

ASPScrollPickup::ASPScrollPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	PickupMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupMesh"));
	SetRootComponent(PickupMesh);
	PickupMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickupMesh->SetCollisionObjectType(ECC_WorldDynamic);
	PickupMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PickupMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PickupMesh->SetGenerateOverlapEvents(false);
	PickupMesh->SetRelativeScale3D(FVector(0.30f, 0.22f, 0.08f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		PickupMesh->SetStaticMesh(CubeMesh.Object);
	}
}

void ASPScrollPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASPScrollPickup, ScrollInstance);
	DOREPLIFETIME(ASPScrollPickup, bClaimed);
}

void ASPScrollPickup::InitializeScroll(const FSPScrollInstance& InScrollInstance)
{
	if (!HasAuthority() || bClaimed)
	{
		return;
	}

	ScrollInstance = InScrollInstance;
	ForceNetUpdate();
	UE_LOG(LogScrollPeddler, Log, TEXT("[SP_TECH_SPIKE_PICKUP_INITIALIZED] Pickup=%s InstanceId=%s Valid=%d"),
		*GetNameSafe(this), *ScrollInstance.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower), ScrollInstance.IsValid() ? 1 : 0);
}

bool ASPScrollPickup::IsAvailable() const
{
	return !bClaimed && ScrollInstance.IsValid() && (!HasAuthority() || !ReservedBy.IsValid());
}

bool ASPScrollPickup::TryReserve(ASPCharacter* Claimant)
{
	if (!HasAuthority() || bClaimed || !ScrollInstance.IsValid() || !IsValid(Claimant))
	{
		return false;
	}

	if (ReservedBy.IsValid() && ReservedBy.Get() != Claimant)
	{
		return false;
	}

	ReservedBy = Claimant;
	return true;
}

void ASPScrollPickup::ReleaseReservation(ASPCharacter* Claimant)
{
	if (HasAuthority() && ReservedBy.Get() == Claimant)
	{
		ReservedBy.Reset();
	}
}

bool ASPScrollPickup::CommitClaim(ASPCharacter* Claimant)
{
	if (!HasAuthority() || bClaimed || !IsValid(Claimant) || ReservedBy.Get() != Claimant)
	{
		return false;
	}

	bClaimed = true;
	ReservedBy.Reset();
	ApplyClaimedPresentation();
	ForceNetUpdate();
	SetLifeSpan(0.10f);
	return true;
}

void ASPScrollPickup::OnRep_Claimed()
{
	ApplyClaimedPresentation();
}

void ASPScrollPickup::ApplyClaimedPresentation()
{
	if (bClaimed)
	{
		SetActorEnableCollision(false);
		SetActorHiddenInGame(true);
	}
}
