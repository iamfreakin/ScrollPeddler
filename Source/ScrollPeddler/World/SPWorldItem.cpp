#include "World/SPWorldItem.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/SPItemDefinition.h"
#include "Data/SPScrollDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "ScrollPeddler.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName WorldItemPickupBundleName(TEXT("Pickup"));
	const FVector WorldItemFallbackVisualScale(0.25f);
}

FPrimaryAssetType SPGetWorldItemPickupDefinitionType(const ESPItemKind Kind)
{
	return Kind == ESPItemKind::Scroll
		? USPScrollDefinition::PrimaryAssetType
		: USPItemDefinition::PrimaryAssetType;
}

ESPDropPlacementResult SPValidateDropPlacement(
	const FVector& SourceLocation,
	const FTransform& ProposedTransform,
	const float MaxDropDistance,
	const bool bCollisionAllowsPlacement)
{
	if (SourceLocation.ContainsNaN())
	{
		return ESPDropPlacementResult::InvalidSource;
	}
	if (!ProposedTransform.IsValid())
	{
		return ESPDropPlacementResult::InvalidTransform;
	}

	const FVector ProposedScale = ProposedTransform.GetScale3D();
	if (ProposedScale.X <= UE_SMALL_NUMBER
		|| ProposedScale.Y <= UE_SMALL_NUMBER
		|| ProposedScale.Z <= UE_SMALL_NUMBER)
	{
		return ESPDropPlacementResult::InvalidTransform;
	}
	if (!FMath::IsFinite(MaxDropDistance) || MaxDropDistance <= 0.0f)
	{
		return ESPDropPlacementResult::InvalidDistanceLimit;
	}

	const FVector Delta = ProposedTransform.GetLocation() - SourceLocation;
	if (Delta.ContainsNaN()
		|| Delta.SizeSquared() > FMath::Square(static_cast<double>(MaxDropDistance)))
	{
		return ESPDropPlacementResult::OutOfRange;
	}
	if (!bCollisionAllowsPlacement)
	{
		return ESPDropPlacementResult::Blocked;
	}
	return ESPDropPlacementResult::Success;
}

bool FSPWorldItemClaimState::Initialize()
{
	if (Lifecycle != ESPWorldItemLifecycleState::Uninitialized || Revision != 0)
	{
		return false;
	}

	Lifecycle = ESPWorldItemLifecycleState::Available;
	Revision = 1;
	return true;
}

ESPInteractionResultCode FSPWorldItemClaimState::TryClaim(
	UObject* Claimant,
	const FGuid& ProposedToken,
	const int32 ExpectedRevision,
	FGuid& OutActiveToken)
{
	if (!IsValid(Claimant) || !ProposedToken.IsValid())
	{
		return ESPInteractionResultCode::InvalidRequest;
	}
	if (Lifecycle == ESPWorldItemLifecycleState::Uninitialized)
	{
		return ESPInteractionResultCode::InvalidState;
	}
	if (Lifecycle == ESPWorldItemLifecycleState::Committed)
	{
		return ESPInteractionResultCode::Unavailable;
	}
	if (Lifecycle == ESPWorldItemLifecycleState::Claimed)
	{
		if (ActiveClaimant.Get() == Claimant)
		{
			OutActiveToken = ActiveClaimToken;
			return ESPInteractionResultCode::Success;
		}
		return ESPInteractionResultCode::Contested;
	}
	if (ExpectedRevision != INDEX_NONE && ExpectedRevision != Revision)
	{
		return ESPInteractionResultCode::StaleRevision;
	}
	if (ProposedToken == LastRollbackClaimToken)
	{
		return ESPInteractionResultCode::RequestConflict;
	}

	ActiveClaimant = Claimant;
	ActiveClaimToken = ProposedToken;
	LastRollbackClaimant.Reset();
	LastRollbackClaimToken.Invalidate();
	Lifecycle = ESPWorldItemLifecycleState::Claimed;
	AdvanceRevision();
	OutActiveToken = ActiveClaimToken;
	return ESPInteractionResultCode::Success;
}

ESPInteractionResultCode FSPWorldItemClaimState::Commit(
	UObject* Claimant,
	const FGuid& ClaimToken,
	const int32 ExpectedRevision)
{
	if (!IsValid(Claimant) || !ClaimToken.IsValid())
	{
		return ESPInteractionResultCode::InvalidRequest;
	}
	if (Lifecycle == ESPWorldItemLifecycleState::Committed)
	{
		if (TerminalClaimant.Get() != Claimant)
		{
			return ESPInteractionResultCode::NotOwner;
		}
		return TerminalClaimToken == ClaimToken
			? ESPInteractionResultCode::Success
			: ESPInteractionResultCode::RequestConflict;
	}
	if (Lifecycle != ESPWorldItemLifecycleState::Claimed)
	{
		return ESPInteractionResultCode::InvalidState;
	}
	if (ActiveClaimant.Get() != Claimant)
	{
		return ESPInteractionResultCode::NotOwner;
	}
	if (ActiveClaimToken != ClaimToken)
	{
		return ESPInteractionResultCode::RequestConflict;
	}
	if (ExpectedRevision != INDEX_NONE && ExpectedRevision != Revision)
	{
		return ESPInteractionResultCode::StaleRevision;
	}

	TerminalClaimant = ActiveClaimant;
	TerminalClaimToken = ActiveClaimToken;
	ActiveClaimant.Reset();
	ActiveClaimToken.Invalidate();
	LastRollbackClaimant.Reset();
	LastRollbackClaimToken.Invalidate();
	Lifecycle = ESPWorldItemLifecycleState::Committed;
	AdvanceRevision();
	return ESPInteractionResultCode::Success;
}

ESPInteractionResultCode FSPWorldItemClaimState::Rollback(
	UObject* Claimant,
	const FGuid& ClaimToken,
	const int32 ExpectedRevision)
{
	if (!IsValid(Claimant) || !ClaimToken.IsValid())
	{
		return ESPInteractionResultCode::InvalidRequest;
	}
	if (Lifecycle == ESPWorldItemLifecycleState::Available)
	{
		if (LastRollbackClaimant.Get() != Claimant)
		{
			return ESPInteractionResultCode::InvalidState;
		}
		return LastRollbackClaimToken == ClaimToken
			? ESPInteractionResultCode::Success
			: ESPInteractionResultCode::RequestConflict;
	}
	if (Lifecycle != ESPWorldItemLifecycleState::Claimed)
	{
		return ESPInteractionResultCode::InvalidState;
	}
	if (ActiveClaimant.Get() != Claimant)
	{
		return ESPInteractionResultCode::NotOwner;
	}
	if (ActiveClaimToken != ClaimToken)
	{
		return ESPInteractionResultCode::RequestConflict;
	}
	if (ExpectedRevision != INDEX_NONE && ExpectedRevision != Revision)
	{
		return ESPInteractionResultCode::StaleRevision;
	}

	LastRollbackClaimant = ActiveClaimant;
	LastRollbackClaimToken = ActiveClaimToken;
	ActiveClaimant.Reset();
	ActiveClaimToken.Invalidate();
	Lifecycle = ESPWorldItemLifecycleState::Available;
	AdvanceRevision();
	return ESPInteractionResultCode::Success;
}

bool FSPWorldItemClaimState::IsClaimedBy(const UObject* Candidate) const
{
	return Lifecycle == ESPWorldItemLifecycleState::Claimed
		&& IsValid(Candidate)
		&& ActiveClaimant.Get() == Candidate;
}

bool FSPWorldItemClaimState::HasAbandonedClaim() const
{
	return Lifecycle == ESPWorldItemLifecycleState::Claimed
		&& !ActiveClaimant.IsValid();
}

bool FSPWorldItemClaimState::RollbackAbandonedClaim()
{
	if (!HasAbandonedClaim())
	{
		return false;
	}

	ActiveClaimant.Reset();
	ActiveClaimToken.Invalidate();
	LastRollbackClaimant.Reset();
	LastRollbackClaimToken.Invalidate();
	Lifecycle = ESPWorldItemLifecycleState::Available;
	AdvanceRevision();
	return true;
}

bool FSPWorldItemClaimState::AdvanceAvailableRevision()
{
	if (Lifecycle != ESPWorldItemLifecycleState::Available)
	{
		return false;
	}

	AdvanceRevision();
	return true;
}

void FSPWorldItemClaimState::AdvanceRevision()
{
	Revision = Revision >= MAX_int32 ? 1 : Revision + 1;
}

ASPWorldItem::ASPWorldItem()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	InteractionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBounds"));
	SetRootComponent(InteractionBounds);
	InteractionBounds->SetBoxExtent(FVector(24.0f));
	InteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBounds->SetCollisionObjectType(ECC_WorldDynamic);
	InteractionBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractionBounds->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractionBounds->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	InteractionBounds->SetGenerateOverlapEvents(false);
	InteractionBounds->SetCanEverAffectNavigation(false);
	InteractionBounds->SetSimulatePhysics(false);

	PickupVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PickupVisual"));
	PickupVisual->SetupAttachment(InteractionBounds);
	PickupVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PickupVisual->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupVisual->SetGenerateOverlapEvents(false);
	PickupVisual->SetCanEverAffectNavigation(false);
	PickupVisual->SetRelativeScale3D(WorldItemFallbackVisualScale);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		FallbackVisualMesh = CubeMesh.Object;
		PickupVisual->SetStaticMesh(FallbackVisualMesh);
	}

	SetActorHiddenInGame(true);
}

void ASPWorldItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASPWorldItem, ItemInstance);
	DOREPLIFETIME(ASPWorldItem, ClaimState);
}

void ASPWorldItem::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelPickupVisualLoad();
	Super::EndPlay(EndPlayReason);
}

bool ASPWorldItem::InitializeItem(const FSPItemInstance& InItemInstance)
{
	if (!HasAuthority()
		|| ClaimState.Lifecycle != ESPWorldItemLifecycleState::Uninitialized
		|| !InItemInstance.IsValid()
		|| !ClaimState.Initialize())
	{
		return false;
	}

	ItemInstance = InItemInstance;
	RequestPickupVisual();
	ApplyLifecyclePresentation();
	NotifyStateChanged(TEXT("INITIALIZED"));
	return true;
}

ESPInteractionResultCode ASPWorldItem::InspectItem(
	const APawn* RequestingPawn,
	const FSPInteractionRequest& Request,
	FSPItemInstance& OutItemSnapshot,
	int32& OutAuthoritativeRevision) const
{
	OutAuthoritativeRevision = ClaimState.Revision;
	const ESPInteractionResultCode Result = ValidatePickupRequest(
		RequestingPawn,
		Request,
		false);
	if (Result == ESPInteractionResultCode::Success)
	{
		OutItemSnapshot = ItemInstance;
	}
	return Result;
}

ESPInteractionResultCode ASPWorldItem::TryClaimItem(
	APawn* Claimant,
	const FSPInteractionRequest& Request,
	FGuid& OutClaimToken,
	FSPItemInstance& OutItemSnapshot,
	int32& OutAuthoritativeRevision)
{
	OutAuthoritativeRevision = ClaimState.Revision;
	if (!HasAuthority())
	{
		return ESPInteractionResultCode::InvalidState;
	}

	RefreshAbandonedClaim();
	OutAuthoritativeRevision = ClaimState.Revision;
	const ESPInteractionResultCode ValidationResult = ValidatePickupRequest(
		Claimant,
		Request,
		true);
	if (ValidationResult != ESPInteractionResultCode::Success)
	{
		return ValidationResult;
	}

	const int32 PreviousRevision = ClaimState.Revision;
	const ESPInteractionResultCode ClaimResult = ClaimState.TryClaim(
		Claimant,
		FGuid::NewGuid(),
		Request.ExpectedRevision,
		OutClaimToken);
	OutAuthoritativeRevision = ClaimState.Revision;
	if (ClaimResult != ESPInteractionResultCode::Success)
	{
		return ClaimResult;
	}

	OutItemSnapshot = ItemInstance;
	if (ClaimState.Revision != PreviousRevision)
	{
		ApplyLifecyclePresentation();
		NotifyStateChanged(TEXT("CLAIMED"));
	}
	return ESPInteractionResultCode::Success;
}

ESPInteractionResultCode ASPWorldItem::CommitClaim(
	APawn* Claimant,
	const FGuid& ClaimToken,
	const int32 ExpectedRevision)
{
	if (!HasAuthority())
	{
		return ESPInteractionResultCode::InvalidState;
	}

	const int32 PreviousRevision = ClaimState.Revision;
	const ESPInteractionResultCode Result = ClaimState.Commit(
		Claimant,
		ClaimToken,
		ExpectedRevision);
	if (Result == ESPInteractionResultCode::Success
		&& ClaimState.Revision != PreviousRevision)
	{
		CancelPickupVisualLoad();
		ApplyLifecyclePresentation();
		NotifyStateChanged(TEXT("COMMITTED"));
	}
	return Result;
}

ESPInteractionResultCode ASPWorldItem::RollbackClaim(
	APawn* Claimant,
	const FGuid& ClaimToken,
	const int32 ExpectedRevision)
{
	if (!HasAuthority())
	{
		return ESPInteractionResultCode::InvalidState;
	}

	const int32 PreviousRevision = ClaimState.Revision;
	const ESPInteractionResultCode Result = ClaimState.Rollback(
		Claimant,
		ClaimToken,
		ExpectedRevision);
	if (Result == ESPInteractionResultCode::Success
		&& ClaimState.Revision != PreviousRevision)
	{
		ApplyLifecyclePresentation();
		NotifyStateChanged(TEXT("ROLLED_BACK"));
	}
	return Result;
}

FText ASPWorldItem::GetInteractionPrompt_Implementation(const APawn* Viewer) const
{
	return IsAvailable()
		? NSLOCTEXT("ScrollPeddler", "WorldItemPickupPrompt", "PICK UP")
		: FText::GetEmpty();
}

FVector ASPWorldItem::GetInteractionLocation_Implementation() const
{
	return GetActorLocation();
}

ESPInteractionResultCode ASPWorldItem::ValidateInteraction(
	const APawn* RequestingPawn,
	const FSPInteractionRequest& Request) const
{
	return ValidatePickupRequest(RequestingPawn, Request, false);
}

bool ASPWorldItem::IsAvailableToPaperEater_Implementation() const
{
	return HasAuthority() && IsAvailable();
}

ESPThreatItemTargetKind
ASPWorldItem::GetThreatItemTargetKind_Implementation() const
{
	return ItemInstance.Kind == ESPItemKind::Scroll
		? ESPThreatItemTargetKind::Scroll
		: ESPThreatItemTargetKind::WorldItem;
}

FVector ASPWorldItem::GetThreatItemTargetLocation_Implementation() const
{
	return GetActorLocation();
}

bool ASPWorldItem::RequestPaperEaterCorruption_Implementation(
	const FSPPaperCorruptionIntent& Intent)
{
	if (!HasAuthority() || !IsAvailable() || !Intent.IsValid()
		|| Intent.TargetActor != this
		|| !Intent.ThreatActor || !Intent.ThreatActor->HasAuthority())
	{
		return false;
	}

	const FString Fingerprint = FString::Printf(
		TEXT("%s|%s|%.3f|%d"),
		*GetNameSafe(Intent.ThreatActor),
		*GetNameSafe(Intent.TargetActor),
		Intent.ContaminationDelta,
		Intent.bRequestItemDamage ? 1 : 0);
	if (const FString* Existing =
		ProcessedCorruptionRequests.Find(Intent.RequestId))
	{
		return Existing->Equals(Fingerprint, ESearchCase::CaseSensitive);
	}

	bool bMutated = false;
	switch (ItemInstance.Kind)
	{
	case ESPItemKind::Scroll:
	{
		const float Previous = ItemInstance.ScrollRoll.Contamination;
		ItemInstance.ScrollRoll.Contamination = FMath::Clamp(
			Previous + Intent.ContaminationDelta,
			0.0f,
			100.0f);
		bMutated = !FMath::IsNearlyEqual(
			Previous,
			ItemInstance.ScrollRoll.Contamination);
		break;
	}
	case ESPItemKind::Material:
		if (Intent.ContaminationDelta > 0.0f
			&& ItemInstance.MaterialContamination
				!= ESPContaminationTier::HighlyContaminated)
		{
			ItemInstance.MaterialContamination =
				static_cast<ESPContaminationTier>(
					static_cast<uint8>(
						ItemInstance.MaterialContamination) + 1);
			bMutated = true;
		}
		break;
	case ESPItemKind::Equipment:
		if (Intent.bRequestItemDamage
			&& ItemInstance.EquipmentCondition
				!= ESPEquipmentCondition::Broken)
		{
			ItemInstance.EquipmentCondition =
				ItemInstance.EquipmentCondition
					== ESPEquipmentCondition::Good
				? ESPEquipmentCondition::Worn
				: ESPEquipmentCondition::Broken;
			bMutated = true;
		}
		break;
	case ESPItemKind::LargeCargo:
	default:
		break;
	}

	if (!bMutated || !ClaimState.AdvanceAvailableRevision())
	{
		return false;
	}

	ProcessedCorruptionRequests.Add(Intent.RequestId, Fingerprint);
	NotifyStateChanged(TEXT("PAPER_CORRUPTED"));
	return true;
}

void ASPWorldItem::OnRep_ItemInstance()
{
	RequestPickupVisual();
	ApplyLifecyclePresentation();
}

void ASPWorldItem::OnRep_ClaimState()
{
	if (ClaimState.Lifecycle == ESPWorldItemLifecycleState::Committed)
	{
		CancelPickupVisualLoad();
	}
	ApplyLifecyclePresentation();
}

void ASPWorldItem::RequestPickupVisual()
{
	CancelPickupVisualLoad();
	ApplyFallbackVisual();

	if (ClaimState.Lifecycle == ESPWorldItemLifecycleState::Committed)
	{
		return;
	}
	if (!ItemInstance.IsValid())
	{
		LogVisualFallback(
			TEXT("Item instance is invalid"),
			ItemInstance.DefinitionId,
			ItemInstance.Kind);
		return;
	}

	const FPrimaryAssetId RequestedDefinitionId = ItemInstance.DefinitionId;
	const FGuid RequestedInstanceId = ItemInstance.InstanceId;
	const ESPItemKind RequestedKind = ItemInstance.Kind;
	const uint32 RequestId = PickupVisualRequestId;
	if (RequestedDefinitionId.PrimaryAssetType
		!= SPGetWorldItemPickupDefinitionType(RequestedKind))
	{
		LogVisualFallback(
			TEXT("Primary asset type does not match item kind"),
			RequestedDefinitionId,
			RequestedKind);
		return;
	}

	UAssetManager& AssetManager = UAssetManager::Get();
	if (!AssetManager.GetPrimaryAssetPath(RequestedDefinitionId).IsValid())
	{
		LogVisualFallback(
			TEXT("Primary asset id is not registered"),
			RequestedDefinitionId,
			RequestedKind);
		return;
	}

	const TArray<FName> BundlesToLoad{ WorldItemPickupBundleName };
	FAssetManagerLoadParams LoadParams;
	LoadParams.OnComplete = FStreamableDelegateWithHandle::CreateUObject(
		this,
		&ASPWorldItem::HandlePickupVisualLoaded,
		RequestId,
		RequestedInstanceId,
		RequestedDefinitionId,
		RequestedKind);

	TSharedPtr<FStreamableHandle> NewHandle = AssetManager.PreloadPrimaryAssets(
		TArray<FPrimaryAssetId>{ RequestedDefinitionId },
		BundlesToLoad,
		false,
		MoveTemp(LoadParams));
	// The callback owns the completed handle long enough for PickupVisual to
	// establish a hard mesh reference. Retain only pending work for cancellation.
	if (NewHandle.IsValid() && !NewHandle->HasLoadCompleted())
	{
		PickupVisualLoadHandle = MoveTemp(NewHandle);
	}
}

void ASPWorldItem::HandlePickupVisualLoaded(
	TSharedPtr<FStreamableHandle> CompletedHandle,
	const uint32 RequestId,
	const FGuid RequestedInstanceId,
	const FPrimaryAssetId RequestedDefinitionId,
	const ESPItemKind RequestedKind)
{
	if (RequestId != PickupVisualRequestId)
	{
		return;
	}

	if (PickupVisualLoadHandle == CompletedHandle)
	{
		PickupVisualLoadHandle.Reset();
	}
	if (!CompletedHandle.IsValid())
	{
		ApplyFallbackVisual();
		LogVisualFallback(
			TEXT("Pickup preload completed without a valid handle"),
			RequestedDefinitionId,
			RequestedKind);
		return;
	}
	if (ClaimState.Lifecycle == ESPWorldItemLifecycleState::Committed
		|| !ItemInstance.IsValid()
		|| ItemInstance.InstanceId != RequestedInstanceId
		|| ItemInstance.DefinitionId != RequestedDefinitionId
		|| ItemInstance.Kind != RequestedKind)
	{
		return;
	}

	UStaticMesh* LoadedMesh = nullptr;
	if (RequestedKind == ESPItemKind::Scroll)
	{
		const USPScrollDefinition* Definition =
			UAssetManager::Get().GetPrimaryAssetObject<USPScrollDefinition>(
				RequestedDefinitionId);
		if (!Definition)
		{
			ApplyFallbackVisual();
			LogVisualFallback(
				TEXT("Primary asset did not load as a scroll definition"),
				RequestedDefinitionId,
				RequestedKind);
			return;
		}
		LoadedMesh = Definition->PickupMesh.Get();
	}
	else
	{
		const USPItemDefinition* Definition =
			UAssetManager::Get().GetPrimaryAssetObject<USPItemDefinition>(
				RequestedDefinitionId);
		if (!Definition)
		{
			ApplyFallbackVisual();
			LogVisualFallback(
				TEXT("Primary asset did not load as an item definition"),
				RequestedDefinitionId,
				RequestedKind);
			return;
		}
		if (Definition->Kind != RequestedKind)
		{
			ApplyFallbackVisual();
			LogVisualFallback(
				TEXT("Item definition kind does not match the instance"),
				RequestedDefinitionId,
				RequestedKind);
			return;
		}
		LoadedMesh = Definition->PickupMesh.Get();
	}

	if (!LoadedMesh)
	{
		ApplyFallbackVisual();
		LogVisualFallback(
			TEXT("Pickup bundle completed without a loadable mesh"),
			RequestedDefinitionId,
			RequestedKind);
		return;
	}

	PickupVisual->SetStaticMesh(LoadedMesh);
	PickupVisual->SetRelativeScale3D(FVector::OneVector);
	UE_LOG(LogScrollPeddler, Log,
		TEXT("[SP_WORLD_ITEM_VISUAL_APPLIED] Item=%s Definition=%s Kind=%s Mesh=%s"),
		*GetNameSafe(this),
		*RequestedDefinitionId.ToString(),
		*StaticEnum<ESPItemKind>()->GetNameStringByValue(
			static_cast<int64>(RequestedKind)),
		*GetNameSafe(LoadedMesh));
}

void ASPWorldItem::CancelPickupVisualLoad()
{
	++PickupVisualRequestId;
	if (PickupVisualLoadHandle.IsValid())
	{
		PickupVisualLoadHandle->CancelHandle();
		PickupVisualLoadHandle.Reset();
	}
}

void ASPWorldItem::ApplyFallbackVisual()
{
	PickupVisual->SetStaticMesh(FallbackVisualMesh);
	PickupVisual->SetRelativeScale3D(WorldItemFallbackVisualScale);
}

void ASPWorldItem::LogVisualFallback(
	const TCHAR* Reason,
	const FPrimaryAssetId& DefinitionId,
	const ESPItemKind Kind) const
{
	UE_LOG(LogScrollPeddler, Warning,
		TEXT("[SP_WORLD_ITEM_VISUAL_FALLBACK] Item=%s Definition=%s Kind=%s Reason=%s"),
		*GetNameSafe(this),
		*DefinitionId.ToString(),
		*StaticEnum<ESPItemKind>()->GetNameStringByValue(
			static_cast<int64>(Kind)),
		Reason);
}

ESPInteractionResultCode ASPWorldItem::ValidatePickupRequest(
	const APawn* RequestingPawn,
	const FSPInteractionRequest& Request,
	const bool bAllowCurrentClaimantReplay) const
{
	if (!Request.IsValid()
		|| Request.Action != ESPInteractionAction::Pickup
		|| !IsValid(RequestingPawn)
		|| !Request.TargetInstanceId.IsValid()
		|| !ItemInstance.IsValid())
	{
		return ESPInteractionResultCode::InvalidRequest;
	}
	if (Request.TargetInstanceId != ItemInstance.InstanceId)
	{
		return ESPInteractionResultCode::RequestConflict;
	}
	if (ClaimState.Lifecycle == ESPWorldItemLifecycleState::Uninitialized)
	{
		return ESPInteractionResultCode::InvalidState;
	}
	if (ClaimState.Lifecycle == ESPWorldItemLifecycleState::Committed)
	{
		return ESPInteractionResultCode::Unavailable;
	}

	const bool bCurrentClaimantReplay = bAllowCurrentClaimantReplay
		&& ClaimState.IsClaimedBy(RequestingPawn);
	if (!bCurrentClaimantReplay
		&& Request.ExpectedRevision != INDEX_NONE
		&& Request.ExpectedRevision != ClaimState.Revision)
	{
		return ESPInteractionResultCode::StaleRevision;
	}
	if (RequestingPawn->GetWorld() != GetWorld())
	{
		return ESPInteractionResultCode::InvalidRequest;
	}

	const FVector ViewLocation = RequestingPawn->GetPawnViewLocation();
	const FVector InteractionLocation = GetInteractionLocation_Implementation();
	if (ViewLocation.ContainsNaN()
		|| InteractionLocation.ContainsNaN()
		|| FVector::DistSquared(ViewLocation, InteractionLocation)
			> FMath::Square(static_cast<double>(MaxInteractionDistance)))
	{
		return ESPInteractionResultCode::OutOfRange;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SPWorldItemInteraction), false);
	QueryParams.AddIgnoredActor(RequestingPawn);
	QueryParams.AddIgnoredActor(this);
	FHitResult BlockingHit;
	if (GetWorld()
		&& GetWorld()->LineTraceSingleByChannel(
			BlockingHit,
			ViewLocation,
			InteractionLocation,
			ECC_Visibility,
			QueryParams))
	{
		return ESPInteractionResultCode::Obstructed;
	}

	if (ClaimState.Lifecycle == ESPWorldItemLifecycleState::Claimed
		&& !ClaimState.IsClaimedBy(RequestingPawn))
	{
		return ESPInteractionResultCode::Contested;
	}
	return ESPInteractionResultCode::Success;
}

bool ASPWorldItem::RefreshAbandonedClaim()
{
	if (!ClaimState.RollbackAbandonedClaim())
	{
		return false;
	}

	ApplyLifecyclePresentation();
	NotifyStateChanged(TEXT("ABANDONED_ROLLBACK"));
	return true;
}

void ASPWorldItem::ApplyLifecyclePresentation()
{
	const bool bShouldBeVisible = ItemInstance.IsValid()
		&& ClaimState.Lifecycle != ESPWorldItemLifecycleState::Uninitialized
		&& ClaimState.Lifecycle != ESPWorldItemLifecycleState::Committed;
	SetActorHiddenInGame(!bShouldBeVisible);
	InteractionBounds->SetCollisionEnabled(
		bShouldBeVisible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
}

void ASPWorldItem::NotifyStateChanged(const TCHAR* Operation)
{
	check(HasAuthority());
	ForceNetUpdate();
	UE_LOG(LogScrollPeddler, Log,
		TEXT("[SP_WORLD_ITEM_%s] Item=%s InstanceId=%s Lifecycle=%s Revision=%d"),
		Operation,
		*GetNameSafe(this),
		*ItemInstance.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower),
		*StaticEnum<ESPWorldItemLifecycleState>()->GetNameStringByValue(
			static_cast<int64>(ClaimState.Lifecycle)),
		ClaimState.Revision);
}
