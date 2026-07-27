#include "World/SPDungeonLayoutActor.h"

#include "Components/SceneComponent.h"
#include "Net/UnrealNetwork.h"
#include "World/SPDungeonLayoutGenerator.h"

ASPDungeonLayoutActor::ASPDungeonLayoutActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(1.0f);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
}

void ASPDungeonLayoutActor::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASPDungeonLayoutActor, Layout);
}

bool ASPDungeonLayoutActor::AuthorityGenerateLayout(
	const USPDungeonSeedDefinition* Definition,
	FSPDungeonLayoutValidationReport& OutReport)
{
	if (!HasAuthority())
	{
		OutReport.Result =
			ESPDungeonLayoutValidationResult::NotAuthority;
		OutReport.Message = FText::FromString(
			TEXT("Only the server may generate the replicated dungeon layout."));
		return false;
	}

	FSPDungeonLayout Candidate;
	if (!FSPDungeonLayoutGenerator::Generate(
		Definition,
		Candidate,
		OutReport))
	{
		return false;
	}

	Layout = MoveTemp(Candidate);
	ForceNetUpdate();
	OnLayoutChanged.Broadcast(Layout);
	return true;
}

void ASPDungeonLayoutActor::OnRep_Layout()
{
	OnLayoutChanged.Broadcast(Layout);
}
