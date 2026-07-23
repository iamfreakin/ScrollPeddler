#include "World/SPScrollPickup.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/SPScrollDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Player/SPCharacter.h"
#include "ScrollPeddler.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName PickupBundleName(TEXT("Pickup"));
	const FVector FallbackVisualScale(0.30f, 0.22f, 0.08f);
}

ASPScrollPickup::ASPScrollPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	InteractionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBounds"));
	SetRootComponent(InteractionBounds);
	InteractionBounds->SetBoxExtent(FVector(26.0f, 10.0f, 10.0f));
	InteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBounds->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBounds->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionBounds->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractionBounds->SetGenerateOverlapEvents(false);
	InteractionBounds->SetCanEverAffectNavigation(false);

	PickupVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupVisual"));
	PickupVisual->SetupAttachment(InteractionBounds);
	PickupVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PickupVisual->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupVisual->SetGenerateOverlapEvents(false);
	PickupVisual->SetCanEverAffectNavigation(false);
	PickupVisual->SetRelativeScale3D(FallbackVisualScale);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		FallbackVisualMesh = CubeMesh.Object;
		PickupVisual->SetStaticMesh(FallbackVisualMesh);
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
	RequestPickupVisual();
	ForceNetUpdate();
	UE_LOG(LogScrollPeddler, Log, TEXT("[SP_TECH_SPIKE_PICKUP_INITIALIZED] Pickup=%s InstanceId=%s Valid=%d"),
		*GetNameSafe(this), *ScrollInstance.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower), ScrollInstance.IsValid() ? 1 : 0);
}

void ASPScrollPickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelPickupVisualLoad();
	Super::EndPlay(EndPlayReason);
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
	CancelPickupVisualLoad();
	ApplyClaimedPresentation();
	ForceNetUpdate();
	// 패키지 클라이언트가 contested/replay 권위 경로를 검증하는 동안 fixture 주소를 유지한다.
	// 일반 플레이에서는 짧은 정리 시간을 사용한다.
	float ClaimedLifeSpan = 0.10f;
#if !UE_BUILD_SHIPPING
	if (FParse::Param(FCommandLine::Get(), TEXT("SPAutoContestedPickup")) ||
		FParse::Param(FCommandLine::Get(), TEXT("SPAdversarialSuite")))
	{
		ClaimedLifeSpan = 5.0f;
	}
#endif
	SetLifeSpan(ClaimedLifeSpan);
	return true;
}

void ASPScrollPickup::OnRep_ScrollInstance()
{
	RequestPickupVisual();
}

void ASPScrollPickup::OnRep_Claimed()
{
	if (bClaimed)
	{
		CancelPickupVisualLoad();
	}
	ApplyClaimedPresentation();
}

void ASPScrollPickup::RequestPickupVisual()
{
	CancelPickupVisualLoad();
	ApplyFallbackVisual();

	if (bClaimed)
	{
		return;
	}

	if (!ScrollInstance.IsValid())
	{
		LogVisualFallback(TEXT("Scroll instance is invalid"), ScrollInstance.BaseDefinitionId);
		return;
	}

	const FPrimaryAssetId RequestedDefinitionId = ScrollInstance.BaseDefinitionId;
	const FGuid RequestedInstanceId = ScrollInstance.InstanceId;
	const uint32 RequestId = PickupVisualRequestId;
	UAssetManager& AssetManager = UAssetManager::Get();
	if (!AssetManager.GetPrimaryAssetPath(RequestedDefinitionId).IsValid())
	{
		LogVisualFallback(TEXT("Primary asset id is not registered"), RequestedDefinitionId);
		return;
	}

	const TArray<FName> BundlesToLoad{ PickupBundleName };
	FAssetManagerLoadParams LoadParams;
	LoadParams.OnComplete = FStreamableDelegateWithHandle::CreateUObject(
		this,
		&ASPScrollPickup::HandlePickupVisualLoaded,
		RequestId,
		RequestedInstanceId,
		RequestedDefinitionId);

	TSharedPtr<FStreamableHandle> NewHandle = AssetManager.PreloadPrimaryAssets(
		TArray<FPrimaryAssetId>{ RequestedDefinitionId },
		BundlesToLoad,
		false,
		MoveTemp(LoadParams));
	// 완료된 요청은 대기 callback에 전달된 handle을 소유한다.
	// claim/EndPlay에서 취소할 수 있도록 진행 중인 작업만 보관한다.
	if (NewHandle.IsValid() && !NewHandle->HasLoadCompleted())
	{
		PickupVisualLoadHandle = MoveTemp(NewHandle);
	}
}

void ASPScrollPickup::HandlePickupVisualLoaded(
	TSharedPtr<FStreamableHandle> CompletedHandle,
	const uint32 RequestId,
	const FGuid RequestedInstanceId,
	const FPrimaryAssetId RequestedDefinitionId)
{
	if (RequestId != PickupVisualRequestId)
	{
		return;
	}

	if (PickupVisualLoadHandle == CompletedHandle)
	{
		PickupVisualLoadHandle.Reset();
	}
	// callback 파라미터는 mesh component가 hard reference를 얻을 때까지 preload를 유지한다.
	// 호출자가 반환 handle을 저장하기 전에 완료됐더라도 유효하다.
	if (!CompletedHandle.IsValid())
	{
		ApplyFallbackVisual();
		LogVisualFallback(TEXT("Pickup preload completed without a valid handle"), RequestedDefinitionId);
		return;
	}
	if (bClaimed
		|| !ScrollInstance.IsValid()
		|| ScrollInstance.InstanceId != RequestedInstanceId
		|| ScrollInstance.BaseDefinitionId != RequestedDefinitionId)
	{
		return;
	}

	const USPScrollDefinition* Definition = UAssetManager::Get().GetPrimaryAssetObject<USPScrollDefinition>(
		RequestedDefinitionId);
	if (!Definition)
	{
		ApplyFallbackVisual();
		LogVisualFallback(TEXT("Primary asset did not load as a scroll definition"), RequestedDefinitionId);
		return;
	}

	UStaticMesh* LoadedMesh = Definition->PickupMesh.Get();
	if (!LoadedMesh)
	{
		ApplyFallbackVisual();
		LogVisualFallback(TEXT("Pickup bundle completed without a loadable mesh"), RequestedDefinitionId);
		return;
	}

	PickupVisual->SetStaticMesh(LoadedMesh);
	PickupVisual->SetRelativeScale3D(FVector::OneVector);
	UE_LOG(LogScrollPeddler, Log,
		TEXT("[SP_PICKUP_VISUAL_APPLIED] Pickup=%s Definition=%s Mesh=%s"),
		*GetNameSafe(this),
		*RequestedDefinitionId.ToString(),
		*GetNameSafe(LoadedMesh));
}

void ASPScrollPickup::CancelPickupVisualLoad()
{
	++PickupVisualRequestId;
	if (PickupVisualLoadHandle.IsValid())
	{
		PickupVisualLoadHandle->CancelHandle();
		PickupVisualLoadHandle.Reset();
	}
}

void ASPScrollPickup::ApplyFallbackVisual()
{
	PickupVisual->SetStaticMesh(FallbackVisualMesh);
	PickupVisual->SetRelativeScale3D(FallbackVisualScale);
}

void ASPScrollPickup::LogVisualFallback(const TCHAR* Reason, const FPrimaryAssetId& DefinitionId) const
{
	UE_LOG(LogScrollPeddler, Warning,
		TEXT("[SP_PICKUP_VISUAL_FALLBACK] Pickup=%s Definition=%s Reason=%s"),
		*GetNameSafe(this),
		*DefinitionId.ToString(),
		Reason);
}

void ASPScrollPickup::ApplyClaimedPresentation()
{
	if (bClaimed)
	{
		SetActorEnableCollision(false);
		SetActorHiddenInGame(true);
	}
}
