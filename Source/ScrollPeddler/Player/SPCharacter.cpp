#include "Player/SPCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/SPRunTypes.h"
#include "Data/SPScrollFamilyDefinition.h"
#include "Data/SPScrollDefinition.h"
#include "Data/SPScrollEngravingDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/SPGameMode.h"
#include "Game/SPGameState.h"
#include "Game/SPPlayerState.h"
#include "Game/SPScrollUseResolver.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"
#include "Player/SPInventoryComponent.h"
#include "ScrollPeddler.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "World/SPExtractionZone.h"
#include "World/SPGrayboxThreat.h"
#include "World/SPNoiseSubsystem.h"
#include "World/SPScrollPickup.h"
#include "World/SPWorldItem.h"

namespace
{
	FSPInteractionRequest MakePickupInteractionRequest(
		const ASPScrollPickup* Pickup,
		const uint32 RequestId)
	{
		FSPInteractionRequest Request;
		Request.RequestId = RequestId;
		Request.Action = ESPInteractionAction::Pickup;
		Request.TargetInstanceId = IsValid(Pickup)
			? Pickup->GetScrollInstance().InstanceId
			: FGuid();
		return Request;
	}

	ESPInteractionResultCode ToInteractionResultCode(const ESPPickupResultCode ResultCode)
	{
		switch (ResultCode)
		{
		case ESPPickupResultCode::Success:
			return ESPInteractionResultCode::Success;
		case ESPPickupResultCode::OutOfRange:
			return ESPInteractionResultCode::OutOfRange;
		case ESPPickupResultCode::InventoryFull:
			return ESPInteractionResultCode::InventoryFull;
		case ESPPickupResultCode::Unavailable:
			return ESPInteractionResultCode::Unavailable;
		case ESPPickupResultCode::Obstructed:
			return ESPInteractionResultCode::Obstructed;
		case ESPPickupResultCode::Contested:
			return ESPInteractionResultCode::Contested;
		case ESPPickupResultCode::ServerError:
			return ESPInteractionResultCode::ServerError;
		case ESPPickupResultCode::InvalidRequest:
		default:
			return ESPInteractionResultCode::InvalidRequest;
		}
	}

	ESPPickupResultCode ToPickupResultCode(const ESPInteractionResultCode ResultCode)
	{
		switch (ResultCode)
		{
		case ESPInteractionResultCode::Success:
			return ESPPickupResultCode::Success;
		case ESPInteractionResultCode::OutOfRange:
			return ESPPickupResultCode::OutOfRange;
		case ESPInteractionResultCode::InventoryFull:
			return ESPPickupResultCode::InventoryFull;
		case ESPInteractionResultCode::Unavailable:
			return ESPPickupResultCode::Unavailable;
		case ESPInteractionResultCode::Obstructed:
			return ESPPickupResultCode::Obstructed;
		case ESPInteractionResultCode::Contested:
			return ESPPickupResultCode::Contested;
		case ESPInteractionResultCode::ServerError:
		case ESPInteractionResultCode::SaveFailed:
			return ESPPickupResultCode::ServerError;
		default:
			return ESPPickupResultCode::InvalidRequest;
		}
	}

	FSPInteractionResult MakePickupInteractionResult(
		const FSPInteractionRequest& Request,
		const ESPPickupResultCode ResultCode)
	{
		FSPInteractionResult Result;
		Result.RequestId = Request.RequestId;
		Result.Action = Request.Action;
		Result.Code = ToInteractionResultCode(ResultCode);
		return Result;
	}

	FSPInteractionResult MakeInteractionResult(
		const FSPInteractionRequest& Request,
		const ESPInteractionResultCode ResultCode,
		const int32 AuthoritativeRevision)
	{
		FSPInteractionResult Result;
		Result.RequestId = Request.RequestId;
		Result.Action = Request.Action;
		Result.Code = ResultCode;
		Result.AuthoritativeRevision = AuthoritativeRevision;
		return Result;
	}

	ESPInteractionResultCode ToInteractionResultCode(
		const ESPInventoryMutationResult ResultCode)
	{
		switch (ResultCode)
		{
		case ESPInventoryMutationResult::Success:
			return ESPInteractionResultCode::Success;
		case ESPInventoryMutationResult::StaleRevision:
			return ESPInteractionResultCode::StaleRevision;
		case ESPInventoryMutationResult::InventoryFull:
			return ESPInteractionResultCode::InventoryFull;
		case ESPInventoryMutationResult::NotFound:
			return ESPInteractionResultCode::Unavailable;
		case ESPInventoryMutationResult::DuplicateInstance:
			return ESPInteractionResultCode::RequestConflict;
		case ESPInventoryMutationResult::InvalidSlot:
		case ESPInventoryMutationResult::SlotRestricted:
			return ESPInteractionResultCode::InvalidState;
		case ESPInventoryMutationResult::InvalidItem:
		default:
			return ESPInteractionResultCode::InvalidRequest;
		}
	}

	template <typename TDefinition>
	TDefinition* ResolvePrimaryAsset(UAssetManager& AssetManager, const FPrimaryAssetId& AssetId)
	{
		if (!AssetId.IsValid())
		{
			return nullptr;
		}

		if (TDefinition* LoadedDefinition = Cast<TDefinition>(AssetManager.GetPrimaryAssetObject(AssetId)))
		{
			return LoadedDefinition;
		}

		const FSoftObjectPath AssetPath = AssetManager.GetPrimaryAssetPath(AssetId);
		return AssetPath.IsValid() ? Cast<TDefinition>(AssetPath.TryLoad()) : nullptr;
	}
}

ASPCharacter::ASPCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	BaseEyeHeight = 64.0f;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = true;
	bUseControllerRotationRoll = false;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	Movement->bOrientRotationToMovement = false;
	Movement->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	Movement->JumpZVelocity = 700.0f;
	Movement->AirControl = 0.35f;
	Movement->MaxWalkSpeed = WalkSpeed;
	Movement->MaxWalkSpeedCrouched = CrouchSpeed;
	Movement->GetNavAgentPropertiesRef().bCanCrouch = true;

	FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
	FirstPersonCamera->SetRelativeLocation(FVector(0.0f, 0.0f, BaseEyeHeight));
	FirstPersonCamera->SetFieldOfView(90.0f);
	FirstPersonCamera->bUsePawnControlRotation = true;

	FirstPersonHands = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("FirstPersonHands"));
	FirstPersonHands->SetupAttachment(FirstPersonCamera);
	FirstPersonHands->SetOnlyOwnerSee(true);
	FirstPersonHands->SetOwnerNoSee(false);
	FirstPersonHands->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FirstPersonHands->SetGenerateOverlapEvents(false);
	FirstPersonHands->SetCastShadow(false);
	FirstPersonHands->SetIsReplicated(false);

	Inventory = CreateDefaultSubobject<USPInventoryComponent>(TEXT("Inventory"));

	RemoteBodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RemoteBodyMesh"));
	RemoteBodyMesh->SetupAttachment(RootComponent);
	RemoteBodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RemoteBodyMesh->SetGenerateOverlapEvents(false);
	RemoteBodyMesh->SetRelativeScale3D(FVector(0.55f, 0.55f, 1.75f));
	RemoteBodyMesh->SetOwnerNoSee(true);
	RemoteBodyMesh->SetOnlyOwnerSee(false);
	RemoteBodyMesh->SetIsReplicated(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> DebugCubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (DebugCubeMesh.Succeeded())
	{
		RemoteBodyMesh->SetStaticMesh(DebugCubeMesh.Object);
	}
}

void ASPCharacter::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority() || IsLocallyControlled())
	{
		UpdateStamina(DeltaSeconds);
		ApplyMovementTuning();
	}

	if (HasAuthority())
	{
		UpdateMovementNoise();
		if (bSelfTreatmentInProgress && GetVelocity().SizeSquared2D() > 25.0f)
		{
			CancelSelfTreatment();
		}
	}
}

void ASPCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	check(PlayerInputComponent);

	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &ASPCharacter::HandleJumpPressed);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Released, this, &ASPCharacter::HandleJumpReleased);
	PlayerInputComponent->BindAction(TEXT("Sprint"), IE_Pressed, this, &ASPCharacter::HandleSprintPressed);
	PlayerInputComponent->BindAction(TEXT("Sprint"), IE_Released, this, &ASPCharacter::HandleSprintReleased);
	PlayerInputComponent->BindAction(TEXT("Crouch"), IE_Pressed, this, &ASPCharacter::HandleCrouchPressed);
	PlayerInputComponent->BindAction(TEXT("Crouch"), IE_Released, this, &ASPCharacter::HandleCrouchReleased);
	PlayerInputComponent->BindAction(TEXT("Interact"), IE_Pressed, this, &ASPCharacter::HandleInteract);
	PlayerInputComponent->BindAction(TEXT("UseScroll"), IE_Pressed, this, &ASPCharacter::HandleUseScroll);
	PlayerInputComponent->BindAction(TEXT("BagSlot1"), IE_Pressed, this, &ASPCharacter::HandleSwapBag0);
	PlayerInputComponent->BindAction(TEXT("BagSlot2"), IE_Pressed, this, &ASPCharacter::HandleSwapBag1);
	PlayerInputComponent->BindAction(TEXT("BagSlot3"), IE_Pressed, this, &ASPCharacter::HandleSwapBag2);
	PlayerInputComponent->BindAction(TEXT("BagSlot4"), IE_Pressed, this, &ASPCharacter::HandleSwapBag3);
	PlayerInputComponent->BindAction(TEXT("SelfTreat"), IE_Pressed, this, &ASPCharacter::HandleSelfTreatment);
	PlayerInputComponent->BindAction(TEXT("DropHandItem"), IE_Pressed, this, &ASPCharacter::HandleDropHandItem);
	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &ASPCharacter::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &ASPCharacter::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("Turn"), this, &ASPCharacter::Turn);
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &ASPCharacter::LookUp);
}

void ASPCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASPCharacter, SilenceEndServerTime);
	DOREPLIFETIME(ASPCharacter, ProtectionEndServerTime);
	DOREPLIFETIME(ASPCharacter, RevelationEndServerTime);
	DOREPLIFETIME_CONDITION(ASPCharacter, StaminaSeconds, COND_OwnerOnly);
	DOREPLIFETIME(ASPCharacter, bSprinting);
	DOREPLIFETIME_CONDITION(ASPCharacter, bSelfTreatmentInProgress, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ASPCharacter, TreatmentEndServerTime, COND_OwnerOnly);
}

void ASPCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(PickupRequestTimeoutHandle);
		GetWorldTimerManager().ClearTimer(SelfTreatmentTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void ASPCharacter::OnJumped_Implementation()
{
	Super::OnJumped_Implementation();
	if (HasAuthority())
	{
		EmitGameplayNoise(TEXT("Noise.Movement.Jump"), 0.65f, 800.0f);
	}
}

void ASPCharacter::Landed(const FHitResult& Hit)
{
	Super::Landed(Hit);
	if (HasAuthority())
	{
		const float FallSpeed = FMath::Abs(GetVelocity().Z);
		EmitGameplayNoise(
			TEXT("Noise.Movement.Landing"),
			FMath::Clamp(0.55f + FallSpeed / 2000.0f, 0.55f, 1.0f),
			FMath::Clamp(700.0f + FallSpeed, 700.0f, 1500.0f));
	}
}

USPInventoryComponent& ASPCharacter::GetInventory()
{
	check(Inventory);
	return *Inventory;
}

const USPInventoryComponent& ASPCharacter::GetInventory() const
{
	check(Inventory);
	return *Inventory;
}

bool ASPCharacter::IsSilenced() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	const float ServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : (World ? World->GetTimeSeconds() : 0.0f);
	return SilenceEndServerTime > ServerTime;
}

bool ASPCharacter::IsProtectedByScroll() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState =
		World ? World->GetGameState() : nullptr;
	const float ServerTime = GameState
		? GameState->GetServerWorldTimeSeconds()
		: (World ? World->GetTimeSeconds() : 0.0f);
	return ProtectionEndServerTime > ServerTime;
}

bool ASPCharacter::IsRevelationActive() const
{
	const UWorld* World = GetWorld();
	const AGameStateBase* GameState =
		World ? World->GetGameState() : nullptr;
	const float ServerTime = GameState
		? GameState->GetServerWorldTimeSeconds()
		: (World ? World->GetTimeSeconds() : 0.0f);
	return RevelationEndServerTime > ServerTime;
}

bool ASPCharacter::IsCarryingLargeCargo() const
{
	if (!Inventory)
	{
		return false;
	}

	const FSPInventorySlot& HandSlot =
		Inventory->GetInventoryState().HandSlot;
	return HandSlot.bOccupied
		&& HandSlot.Item.Kind == ESPItemKind::LargeCargo;
}

void ASPCharacter::RequestPickup(ASPScrollPickup* Pickup)
{
	if (!CanRequestInteraction() || !IsValid(Pickup))
	{
		if (IsLocallyControlled())
		{
			ShowNoTargetPickupFeedback();
		}
		return;
	}

	// Input/automation intent must originate on the locally controlled pawn.
	// Authority-side code must not manufacture a request for a remote pawn,
	// because its owning client would not have the matching pending RequestId.
	if (!IsLocallyControlled())
	{
		return;
	}
	if (IsPickupRequestPending())
	{
		UE_LOG(LogScrollPeddler, Verbose,
			TEXT("[SP_PICKUP_REQUEST_SKIPPED] Player=%s PendingRequestId=%u"),
			*GetNameSafe(this), PendingPickupRequestId);
		return;
	}

	const uint32 RequestId = AllocatePickupRequestId();
	BeginLocalPickupRequest(RequestId);
	UE_LOG(LogScrollPeddler, Log,
		TEXT("[SP_PICKUP_REQUEST] Player=%s Pickup=%s RequestId=%u"),
		*GetNameSafe(this), *GetNameSafe(Pickup), RequestId);
	ServerTryPickup(Pickup, RequestId);
}

ASPScrollPickup* ASPCharacter::FindPickupInView() const
{
	if (!GetWorld() || !CanRequestInteraction())
	{
		return nullptr;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	if (Controller)
	{
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	else
	{
		ViewLocation = GetPawnViewLocation();
		ViewRotation = GetActorRotation();
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SPInteractTrace), false, this);
	FHitResult Hit;
	const FVector TraceEnd = ViewLocation + ViewRotation.Vector() * MaxPickupDistance;
	if (!GetWorld()->LineTraceSingleByChannel(
		Hit, ViewLocation, TraceEnd, ECC_Visibility, QueryParams))
	{
		return nullptr;
	}

	ASPScrollPickup* Pickup = Cast<ASPScrollPickup>(Hit.GetActor());
	return IsValid(Pickup) && Pickup->IsAvailable() ? Pickup : nullptr;
}

ASPWorldItem* ASPCharacter::FindWorldItemInView() const
{
	if (!GetWorld() || !CanRequestInteraction())
	{
		return nullptr;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	if (Controller)
	{
		Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	}
	else
	{
		ViewLocation = GetPawnViewLocation();
		ViewRotation = GetActorRotation();
	}

	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(SPWorldItemInteractTrace),
		false,
		this);
	FHitResult Hit;
	const FVector TraceEnd =
		ViewLocation + ViewRotation.Vector() * MaxPickupDistance;
	if (!GetWorld()->LineTraceSingleByChannel(
		Hit,
		ViewLocation,
		TraceEnd,
		ECC_Visibility,
		QueryParams))
	{
		return nullptr;
	}

	ASPWorldItem* WorldItem = Cast<ASPWorldItem>(Hit.GetActor());
	return IsValid(WorldItem) && WorldItem->IsAvailable()
		? WorldItem
		: nullptr;
}

bool ASPCharacter::HasPickupTargetInView() const
{
	return FindWorldItemInView() != nullptr || FindPickupInView() != nullptr;
}

bool ASPCharacter::HasActivePickupFeedback() const
{
	return GetWorld() && PickupFeedbackExpiresAt > GetWorld()->GetTimeSeconds();
}

void ASPCharacter::RequestUseFirst()
{
	if (!CanRequestInteraction())
	{
		return;
	}

	const FGuid InstanceId = Inventory ? Inventory->GetFirstInstanceId() : FGuid();
	if (!InstanceId.IsValid())
	{
		return;
	}

	if (HasAuthority() || IsLocallyControlled())
	{
		FVector ViewLocation = GetPawnViewLocation();
		FRotator ViewRotation = GetActorRotation();
		if (Controller)
		{
			Controller->GetPlayerViewPoint(
				ViewLocation,
				ViewRotation);
		}
		ServerUseScroll(
			InstanceId,
			FGuid::NewGuid(),
			Inventory->GetInventoryRevision(),
			ViewRotation.Vector());
	}
}

void ASPCharacter::ServerTryPickup_Implementation(ASPScrollPickup* Pickup, const uint32 RequestId)
{
	const FSPInteractionRequest ReplayRequest = MakePickupInteractionRequest(Pickup, RequestId);
	FSPInteractionResult RecordedResult;
	switch (PickupReplayLedger.Find(ReplayRequest, RecordedResult))
	{
	case FSPInteractionReplayLedger::ELookup::ExactReplay:
		UE_LOG(LogScrollPeddler, Verbose,
			TEXT("[SP_PICKUP_REQUEST_REPLAYED] Player=%s RequestId=%u"),
			*GetNameSafe(this), RequestId);
		ClientNotifyPickupResult(RequestId, ToPickupResultCode(RecordedResult.Code));
		return;
	case FSPInteractionReplayLedger::ELookup::Conflict:
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("[SP_PICKUP_REQUEST_CONFLICT] Player=%s RequestId=%u"),
			*GetNameSafe(this), RequestId);
		ClientNotifyPickupResult(RequestId, ESPPickupResultCode::InvalidRequest);
		return;
	case FSPInteractionReplayLedger::ELookup::NotFound:
	default:
		break;
	}

	if (!HasValidOwningController())
	{
		RejectPickupRequest(RequestId, ESPPickupResultCode::InvalidRequest, TEXT("InvalidOwner"), Pickup);
		return;
	}
	ASPPlayerState* ScrollPlayerState = GetActiveScrollPlayerState();
	if (!ScrollPlayerState)
	{
		RejectPickupRequest(RequestId, ESPPickupResultCode::InvalidRequest, TEXT("InactivePlayerState"), Pickup);
		return;
	}
	if (!CanPerformFieldGameplayAction())
	{
		RejectPickupRequest(RequestId, ESPPickupResultCode::InvalidRequest, TEXT("RunPhase"), Pickup);
		return;
	}
	if (!IsValid(Pickup) || !Pickup->HasAuthority() || Pickup->GetWorld() != GetWorld())
	{
		RejectPickupRequest(RequestId, ESPPickupResultCode::InvalidRequest, TEXT("InvalidPickup"), Pickup);
		return;
	}
	if (FVector::DistSquared(GetActorLocation(), Pickup->GetActorLocation()) > FMath::Square(MaxPickupDistance))
	{
		RejectPickupRequest(RequestId, ESPPickupResultCode::OutOfRange, TEXT("Distance"), Pickup);
		return;
	}
	if (!Inventory || !Inventory->HasCapacity())
	{
		RejectPickupRequest(RequestId, ESPPickupResultCode::InventoryFull, TEXT("Capacity"), Pickup);
		return;
	}
	if (!Pickup->IsAvailable())
	{
		RejectPickupRequest(RequestId, ESPPickupResultCode::Unavailable, TEXT("Unavailable"), Pickup);
		return;
	}
	if (!HasLineOfSightToPickup(Pickup))
	{
		RejectPickupRequest(RequestId, ESPPickupResultCode::Obstructed, TEXT("LineOfSight"), Pickup);
		return;
	}
	if (!Pickup->TryReserve(this))
	{
		RejectPickupRequest(RequestId, ESPPickupResultCode::Contested, TEXT("Reservation"), Pickup);
		return;
	}

	const FSPScrollInstance Item = Pickup->GetScrollInstance();
	if (!Inventory->TryAddItem(Item))
	{
		Pickup->ReleaseReservation(this);
		RejectPickupRequest(RequestId, ESPPickupResultCode::InventoryFull, TEXT("InventoryCommit"), Pickup);
		return;
	}

	if (!ScrollPlayerState->RecordScrollPickedUp(Item))
	{
		FSPScrollInstance RolledBackItem;
		const bool bInventoryRolledBack = Inventory->RemoveItemByInstanceId(Item.InstanceId, RolledBackItem);
		Pickup->ReleaseReservation(this);
		UE_LOG(LogScrollPeddler, Error,
			TEXT("[SP_TECH_SPIKE_PICKUP_LEDGER_ROLLBACK] Player=%s InstanceId=%s InventoryRestored=%d"),
			*GetNameSafe(this), *Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower), bInventoryRolledBack ? 1 : 0);
		RejectPickupRequest(RequestId, ESPPickupResultCode::ServerError, TEXT("LedgerCommit"), Pickup);
		return;
	}

	if (!Pickup->CommitClaim(this))
	{
		FSPScrollInstance RolledBackItem;
		const bool bInventoryRolledBack = Inventory->RemoveItemByInstanceId(Item.InstanceId, RolledBackItem);
		const bool bLedgerRolledBack = ScrollPlayerState->RollbackScrollPickedUp(Item);
		Pickup->ReleaseReservation(this);
		UE_LOG(LogScrollPeddler, Error,
			TEXT("[SP_TECH_SPIKE_PICKUP_COMMIT_ROLLBACK] Player=%s InstanceId=%s InventoryRestored=%d LedgerRestored=%d"),
			*GetNameSafe(this), *Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower),
			bInventoryRolledBack ? 1 : 0, bLedgerRolledBack ? 1 : 0);
		RejectPickupRequest(RequestId, ESPPickupResultCode::ServerError, TEXT("PickupCommit"), Pickup);
		return;
	}
	ScrollPlayerState->ConfirmScrollPickedUp(Item);

	UE_LOG(LogScrollPeddler, Log, TEXT("[SP_TECH_SPIKE_PICKUP_COMMITTED] Player=%s Pickup=%s InstanceId=%s Count=%d"),
		*GetNameSafe(this), *GetNameSafe(Pickup), *Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower), Inventory->GetItemCount());
	PickupReplayLedger.Record(
		ReplayRequest,
		MakePickupInteractionResult(ReplayRequest, ESPPickupResultCode::Success));
	ClientNotifyPickupResult(RequestId, ESPPickupResultCode::Success);
}

void ASPCharacter::RequestPickupWorldItem(ASPWorldItem* WorldItem)
{
	if (!CanRequestInteraction() || !IsValid(WorldItem))
	{
		if (IsLocallyControlled())
		{
			ShowNoTargetPickupFeedback();
		}
		return;
	}
	if (!IsLocallyControlled() || IsPickupRequestPending())
	{
		return;
	}

	const uint32 RequestId = AllocatePickupRequestId();
	FSPInteractionRequest Request;
	Request.RequestId = RequestId;
	Request.Action = ESPInteractionAction::Pickup;
	Request.TargetInstanceId = WorldItem->GetItemInstance().InstanceId;
	Request.ExpectedRevision = WorldItem->GetLifecycleRevision();
	BeginLocalPickupRequest(RequestId);
	ServerTryPickupWorldItem(WorldItem, Request);
}

void ASPCharacter::ServerTryPickupWorldItem_Implementation(
	ASPWorldItem* WorldItem,
	const FSPInteractionRequest& Request)
{
	FSPInteractionResult Result;
	switch (PickupReplayLedger.Find(Request, Result))
	{
	case FSPInteractionReplayLedger::ELookup::ExactReplay:
		ClientNotifyPickupResult(
			static_cast<uint32>(Request.RequestId),
			ToPickupResultCode(Result.Code));
		return;
	case FSPInteractionReplayLedger::ELookup::Conflict:
		Result = MakeInteractionResult(
			Request,
			ESPInteractionResultCode::RequestConflict,
			Inventory ? Inventory->GetInventoryRevision() : INDEX_NONE);
		ClientNotifyPickupResult(
			static_cast<uint32>(Request.RequestId),
			ESPPickupResultCode::InvalidRequest);
		return;
	case FSPInteractionReplayLedger::ELookup::NotFound:
	default:
		break;
	}

	auto FinishRequest =
		[this, &Request](const ESPInteractionResultCode Code, const int32 Revision)
		{
			const FSPInteractionResult Finished =
				MakeInteractionResult(Request, Code, Revision);
			PickupReplayLedger.Record(Request, Finished);
			ClientNotifyPickupResult(
				static_cast<uint32>(Request.RequestId),
				ToPickupResultCode(Code));
		};

	if (!HasValidOwningController() || !CanRequestInteraction()
		|| !Inventory || !Request.IsValid()
		|| Request.Action != ESPInteractionAction::Pickup
		|| Request.RequestId > MAX_uint32
		|| !IsValid(WorldItem) || !WorldItem->HasAuthority()
		|| WorldItem->GetWorld() != GetWorld())
	{
		FinishRequest(
			ESPInteractionResultCode::InvalidRequest,
			Inventory ? Inventory->GetInventoryRevision() : INDEX_NONE);
		return;
	}

	ASPPlayerState* ScrollPlayerState = GetActiveScrollPlayerState();
	if (!ScrollPlayerState)
	{
		FinishRequest(
			ESPInteractionResultCode::InvalidState,
			Inventory->GetInventoryRevision());
		return;
	}
	if (!CanPerformFieldGameplayAction())
	{
		FinishRequest(
			ESPInteractionResultCode::InvalidState,
			Inventory->GetInventoryRevision());
		return;
	}

	FGuid ClaimToken;
	FSPItemInstance ItemSnapshot;
	int32 WorldRevision = INDEX_NONE;
	const ESPInteractionResultCode ClaimResult = WorldItem->TryClaimItem(
		this,
		Request,
		ClaimToken,
		ItemSnapshot,
		WorldRevision);
	if (ClaimResult != ESPInteractionResultCode::Success)
	{
		FinishRequest(ClaimResult, WorldRevision);
		return;
	}

	const FSPInventoryState InventoryBefore =
		Inventory->GetInventoryState();
	FSPItemInstance DisplacedItem;
	ESPInventoryMutationResult MutationResult =
		ESPInventoryMutationResult::InvalidItem;
	if (!Inventory->TryAddItem(
		ItemSnapshot,
		InventoryBefore.Revision,
		false,
		DisplacedItem,
		MutationResult))
	{
		WorldItem->RollbackClaim(this, ClaimToken, WorldRevision);
		FinishRequest(
			ToInteractionResultCode(MutationResult),
			Inventory->GetInventoryRevision());
		return;
	}

	FSPScrollInstance LegacyScroll;
	const bool bIsTrackedScroll =
		ItemSnapshot.TryToLegacyScroll(LegacyScroll);
	if (bIsTrackedScroll
		&& !ScrollPlayerState->RecordScrollPickedUp(LegacyScroll))
	{
		Inventory->AuthorityRestoreState(InventoryBefore);
		WorldItem->RollbackClaim(this, ClaimToken, WorldRevision);
		FinishRequest(
			ESPInteractionResultCode::ServerError,
			Inventory->GetInventoryRevision());
		return;
	}

	const ESPInteractionResultCode CommitResult =
		WorldItem->CommitClaim(this, ClaimToken, WorldRevision);
	if (CommitResult != ESPInteractionResultCode::Success)
	{
		Inventory->AuthorityRestoreState(InventoryBefore);
		if (bIsTrackedScroll)
		{
			ScrollPlayerState->RollbackScrollPickedUp(LegacyScroll);
		}
		WorldItem->RollbackClaim(
			this,
			ClaimToken,
			WorldItem->GetLifecycleRevision());
		FinishRequest(
			ESPInteractionResultCode::ServerError,
			Inventory->GetInventoryRevision());
		return;
	}

	if (bIsTrackedScroll)
	{
		ScrollPlayerState->ConfirmScrollPickedUp(LegacyScroll);
	}
	FinishRequest(
		ESPInteractionResultCode::Success,
		Inventory->GetInventoryRevision());
}

void ASPCharacter::ClientNotifyPickupResult_Implementation(
	const uint32 RequestId,
	const ESPPickupResultCode ResultCode)
{
	if (PendingPickupRequestId != RequestId)
	{
		UE_LOG(LogScrollPeddler, Verbose,
			TEXT("[SP_PICKUP_RESULT_STALE] Player=%s RequestId=%u PendingRequestId=%u Result=%s"),
			*GetNameSafe(this), RequestId, PendingPickupRequestId,
			*StaticEnum<ESPPickupResultCode>()->GetNameStringByValue(static_cast<int64>(ResultCode)));
		return;
	}

	GetWorldTimerManager().ClearTimer(PickupRequestTimeoutHandle);
	PendingPickupRequestId = 0;
	ShowPickupFeedback(ResultCode);
	UE_LOG(LogScrollPeddler, Log,
		TEXT("[SP_PICKUP_RESULT] Player=%s RequestId=%u Result=%s"),
		*GetNameSafe(this), RequestId,
		*StaticEnum<ESPPickupResultCode>()->GetNameStringByValue(static_cast<int64>(ResultCode)));
}

void ASPCharacter::ServerUseScroll_Implementation(
	const FGuid InstanceId,
	const FGuid RequestId,
	const int32 ExpectedInventoryRevision,
	const FVector_NetQuantizeNormal AimDirection)
{
	if (!HasValidOwningController() || !Inventory
		|| !InstanceId.IsValid() || !RequestId.IsValid()
		|| ExpectedInventoryRevision
			!= Inventory->GetInventoryRevision()
		|| AimDirection.IsNearlyZero()
		|| AimDirection.ContainsNaN())
	{
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("[SP_SCROLL_USE_REJECTED] Player=%s Reason=InvalidRequest ExpectedRevision=%d ActualRevision=%d"),
			*GetNameSafe(this),
			ExpectedInventoryRevision,
			Inventory ? Inventory->GetInventoryRevision() : INDEX_NONE);
		return;
	}

	ASPPlayerState* ScrollPlayerState = GetActiveScrollPlayerState();
	if (!ScrollPlayerState)
	{
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("[SP_SCROLL_USE_REJECTED] Player=%s Reason=InactivePlayerState"),
			*GetNameSafe(this));
		return;
	}
	if (!CanPerformFieldGameplayAction())
	{
		const ASPGameState* ScrollGameState =
			GetWorld() ? GetWorld()->GetGameState<ASPGameState>() : nullptr;
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("[SP_SCROLL_USE_REJECTED] Player=%s Reason=RunPhase Phase=%d"),
			*GetNameSafe(this),
			ScrollGameState
				? static_cast<int32>(ScrollGameState->GetRunPhase())
				: INDEX_NONE);
		return;
	}

	const FSPScrollInstance* FoundItem =
		Inventory->FindItemByInstanceId(InstanceId);
	if (!FoundItem)
	{
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("[SP_SCROLL_USE_REJECTED] Player=%s Reason=UnknownInstance InstanceId=%s"),
			*GetNameSafe(this),
			*InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
		return;
	}

	const FSPScrollInstance ItemSnapshot = *FoundItem;
	UAssetManager& AssetManager = UAssetManager::Get();
	USPScrollDefinition* ScrollDefinition =
		ResolvePrimaryAsset<USPScrollDefinition>(
			AssetManager,
			ItemSnapshot.BaseDefinitionId);
	USPScrollEngravingDefinition* EngravingDefinition =
		ResolvePrimaryAsset<USPScrollEngravingDefinition>(
			AssetManager,
			ItemSnapshot.EngravingDefinitionId);
	const TArray<FSPScrollFamilyTuning> FamilyTunings =
		SPBuildVerticalSliceScrollFamilyTunings();
	const FSPScrollFamilyTuning* FamilyTuning =
		FamilyTunings.FindByPredicate(
			[&ItemSnapshot](const FSPScrollFamilyTuning& Candidate)
			{
				return Candidate.BaseDefinitionId
					== ItemSnapshot.BaseDefinitionId;
			});
	if (!ScrollDefinition || !EngravingDefinition || !FamilyTuning
		|| !ScrollDefinition->AllowsEngraving(EngravingDefinition))
	{
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("[SP_SCROLL_USE_REJECTED] Player=%s Reason=AssetValidation Base=%s Engraving=%s"),
			*GetNameSafe(this),
			*ItemSnapshot.BaseDefinitionId.ToString(),
			*ItemSnapshot.EngravingDefinitionId.ToString());
		return;
	}

	FSPScrollUseRequest UseRequest;
	UseRequest.RequestId = RequestId;
	UseRequest.ScrollInstanceId = ItemSnapshot.InstanceId;
	UseRequest.AimDirection = AimDirection.GetSafeNormal();
	FSPAuthoritativeScrollUseState AuthorityState;
	AuthorityState.Scroll = ItemSnapshot;
	if (const ASPGameState* ScrollGameState =
		GetWorld() ? GetWorld()->GetGameState<ASPGameState>() : nullptr)
	{
		AuthorityState.RunSeed =
			static_cast<int32>(GetTypeHash(ScrollGameState->GetRunId()));
	}
	AuthorityState.bServerAuthority = true;
	const FSPScrollUseResult UseResult =
		FSPScrollUseResolver::Resolve(
			UseRequest,
			AuthorityState,
			*FamilyTuning,
			SPBuildScrollEngravingUseTuning(*EngravingDefinition));
	if (!UseResult.IsAccepted() || !UseResult.bShouldConsumeScroll)
	{
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("[SP_SCROLL_USE_REJECTED] Player=%s Reason=Resolver Code=%s"),
			*GetNameSafe(this),
			*StaticEnum<ESPScrollUseResultCode>()->GetNameStringByValue(
				static_cast<int64>(UseResult.Code)));
		return;
	}

	FSPScrollInstance ConsumedItem;
	if (!Inventory->RemoveItemByInstanceId(InstanceId, ConsumedItem))
	{
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("[SP_SCROLL_USE_REJECTED] Player=%s Reason=ConsumeRace InstanceId=%s"),
			*GetNameSafe(this),
			*InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
		return;
	}
	if (!ScrollPlayerState->RecordScrollConsumed(
		ConsumedItem,
		ScrollDefinition->DeliveryValue))
	{
		const bool bInventoryRestored =
			Inventory->TryAddItem(ConsumedItem);
		UE_LOG(LogScrollPeddler, Error,
			TEXT("[SP_SCROLL_USE_LEDGER_ROLLBACK] Player=%s InstanceId=%s InventoryRestored=%d"),
			*GetNameSafe(this),
			*ConsumedItem.InstanceId.ToString(
				EGuidFormats::DigitsWithHyphensLower),
			bInventoryRestored ? 1 : 0);
		return;
	}

	if (UseResult.ApplicationDelaySeconds > KINDA_SMALL_NUMBER)
	{
		const TWeakObjectPtr<ASPCharacter> WeakCharacter(this);
		FTimerDelegate DelayedApplication;
		DelayedApplication.BindLambda(
			[WeakCharacter, UseResult]()
			{
				if (WeakCharacter.IsValid()
					&& WeakCharacter->HasAuthority())
				{
					WeakCharacter->ApplyResolvedScrollUse(UseResult);
				}
			});
		FTimerHandle DelayedApplicationHandle;
		GetWorldTimerManager().SetTimer(
			DelayedApplicationHandle,
			MoveTemp(DelayedApplication),
			UseResult.ApplicationDelaySeconds,
			false);
	}
	else
	{
		ApplyResolvedScrollUse(UseResult);
	}

	UE_LOG(LogScrollPeddler, Log,
		TEXT("[SP_SCROLL_USE_COMMITTED] Player=%s InstanceId=%s Family=%s Malfunction=%s Delay=%.2f Effects=%d"),
		*GetNameSafe(this),
		*ConsumedItem.InstanceId.ToString(
			EGuidFormats::DigitsWithHyphensLower),
		*FamilyTuning->StableId.ToString(),
		*StaticEnum<ESPScrollMalfunctionOutcome>()->GetNameStringByValue(
			static_cast<int64>(UseResult.Malfunction)),
		UseResult.ApplicationDelaySeconds,
		UseResult.Effects.Num());
}

void ASPCharacter::ApplyResolvedScrollUse(
	const FSPScrollUseResult& UseResult)
{
	if (!HasAuthority() || !UseResult.IsAccepted())
	{
		return;
	}

	for (const FSPResolvedScrollEffect& Effect : UseResult.Effects)
	{
		ApplyResolvedScrollEffect(Effect);
	}
	ForceNetUpdate();
}

void ASPCharacter::ApplyResolvedScrollEffect(
	const FSPResolvedScrollEffect& Effect)
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const float ServerTime = GameState
		? GameState->GetServerWorldTimeSeconds()
		: GetWorld()->GetTimeSeconds();
	const float SafeDuration =
		FMath::Max(0.1f, Effect.DurationSeconds);

	switch (Effect.Kind)
	{
	case ESPResolvedScrollEffectKind::Damage:
		{
			const FVector EffectCenter =
				GetActorLocation()
				+ Effect.Direction.GetSafeNormal()
					* FMath::Max(100.0f, Effect.Radius);
			const float Radius = FMath::Max(100.0f, Effect.Radius);
			for (TActorIterator<ASPGrayboxThreat> Iterator(GetWorld());
				Iterator;
				++Iterator)
			{
				ASPGrayboxThreat* Threat = *Iterator;
				if (Threat
					&& FVector::DistSquared(
						Threat->GetActorLocation(),
						EffectCenter)
						<= FMath::Square(Radius))
				{
					Threat->AuthorityStagger(EffectCenter);
				}
			}
		}
		break;
	case ESPResolvedScrollEffectKind::Healing:
		for (TActorIterator<ASPCharacter> Iterator(GetWorld());
			Iterator;
			++Iterator)
		{
			ASPCharacter* Ally = *Iterator;
			const bool bSelfOnly =
				Effect.Target == ESPScrollEffectTarget::Self;
			if (!Ally
				|| (bSelfOnly && Ally != this)
				|| (!bSelfOnly
					&& FVector::DistSquared(
						Ally->GetActorLocation(),
						GetActorLocation())
						> FMath::Square(
							FMath::Max(100.0f, Effect.Radius))))
			{
				continue;
			}

			if (ASPPlayerState* AllyState =
				Ally->GetPlayerState<ASPPlayerState>())
			{
				switch (AllyState->GetPlayerCondition())
				{
				case ESPPlayerCondition::Down:
					AllyState->AuthorityTransitionCondition(
						ESPPlayerCondition::Injured);
					break;
				case ESPPlayerCondition::Injured:
					AllyState->AuthorityTransitionCondition(
						ESPPlayerCondition::Normal);
					break;
				default:
					break;
				}
			}
		}
		break;
	case ESPResolvedScrollEffectKind::Protection:
		ProtectionEndServerTime = FMath::Max(
			ProtectionEndServerTime,
			ServerTime + SafeDuration);
		break;
	case ESPResolvedScrollEffectKind::Movement:
		LaunchCharacter(
			Effect.Direction.GetSafeNormal()
				* FMath::Max(0.0f, Effect.Magnitude)
				+ FVector(0.0f, 0.0f, 120.0f),
			true,
			true);
		break;
	case ESPResolvedScrollEffectKind::Detection:
		RevelationEndServerTime = FMath::Max(
			RevelationEndServerTime,
			ServerTime + SafeDuration);
		break;
	case ESPResolvedScrollEffectKind::Noise:
		if (Effect.Magnitude < 0.0f)
		{
			SilenceEndServerTime = FMath::Max(
				SilenceEndServerTime,
				ServerTime + SafeDuration);
		}
		else
		{
			EmitGameplayNoise(
				TEXT("Noise.Scroll.Resolved"),
				FMath::Clamp(Effect.Magnitude, 0.0f, 2.0f),
				FMath::Max(100.0f, Effect.Radius));
		}
		break;
	default:
		break;
	}
}

void ASPCharacter::ServerSetSprinting_Implementation(const bool bRequested)
{
	if (!HasValidOwningController())
	{
		return;
	}

	bSprinting = bRequested && CanSprint();
	ApplyMovementTuning();
	ForceNetUpdate();
}

void ASPCharacter::ServerSwapHandWithBag_Implementation(
	const int32 BagIndex,
	const int32 ExpectedRevision,
	const int64 RequestId)
{
	FSPInteractionRequest Request;
	Request.RequestId = RequestId;
	Request.Action = ESPInteractionAction::Swap;
	Request.ExpectedRevision = ExpectedRevision;

	FSPInteractionResult Result;
	switch (InventoryReplayLedger.Find(Request, Result))
	{
	case FSPInteractionReplayLedger::ELookup::ExactReplay:
		ClientNotifyInventoryAction(Result);
		return;
	case FSPInteractionReplayLedger::ELookup::Conflict:
		Result.RequestId = RequestId;
		Result.Action = ESPInteractionAction::Swap;
		Result.Code = ESPInteractionResultCode::RequestConflict;
		Result.AuthoritativeRevision = Inventory
			? Inventory->GetInventoryRevision()
			: INDEX_NONE;
		ClientNotifyInventoryAction(Result);
		return;
	case FSPInteractionReplayLedger::ELookup::NotFound:
	default:
		break;
	}

	Result.RequestId = RequestId;
	Result.Action = ESPInteractionAction::Swap;
	Result.AuthoritativeRevision = Inventory
		? Inventory->GetInventoryRevision()
		: INDEX_NONE;

	if (!HasValidOwningController() || !Inventory || RequestId <= 0)
	{
		Result.Code = ESPInteractionResultCode::InvalidRequest;
	}
	else if (!CanRequestInteraction())
	{
		Result.Code = ESPInteractionResultCode::InvalidState;
	}
	else
	{
		ESPInventoryMutationResult MutationResult =
			ESPInventoryMutationResult::InvalidSlot;
		Inventory->SwapHandWithBag(BagIndex, ExpectedRevision, MutationResult);
		Result.Code = ToInteractionResultCode(MutationResult);
		Result.AuthoritativeRevision = Inventory->GetInventoryRevision();
	}

	InventoryReplayLedger.Record(Request, Result);
	ClientNotifyInventoryAction(Result);
}

void ASPCharacter::ServerDropHandItem_Implementation(
	const FGuid ExpectedInstanceId,
	const int32 ExpectedRevision,
	const int64 RequestId)
{
	FSPInteractionRequest Request;
	Request.RequestId = RequestId;
	Request.Action = ESPInteractionAction::Drop;
	Request.TargetInstanceId = ExpectedInstanceId;
	Request.ExpectedRevision = ExpectedRevision;

	FSPInteractionResult Result;
	switch (InventoryReplayLedger.Find(Request, Result))
	{
	case FSPInteractionReplayLedger::ELookup::ExactReplay:
		ClientNotifyInventoryAction(Result);
		return;
	case FSPInteractionReplayLedger::ELookup::Conflict:
		Result = MakeInteractionResult(
			Request,
			ESPInteractionResultCode::RequestConflict,
			Inventory ? Inventory->GetInventoryRevision() : INDEX_NONE);
		ClientNotifyInventoryAction(Result);
		return;
	case FSPInteractionReplayLedger::ELookup::NotFound:
	default:
		break;
	}

	Result = MakeInteractionResult(
		Request,
		ESPInteractionResultCode::InvalidRequest,
		Inventory ? Inventory->GetInventoryRevision() : INDEX_NONE);
	if (!HasValidOwningController() || !CanRequestInteraction()
		|| !Inventory || RequestId <= 0 || !ExpectedInstanceId.IsValid())
	{
		InventoryReplayLedger.Record(Request, Result);
		ClientNotifyInventoryAction(Result);
		return;
	}

	const FSPInventorySlot& HandSlot =
		Inventory->GetInventoryState().HandSlot;
	if (!HandSlot.bOccupied
		|| HandSlot.Item.InstanceId != ExpectedInstanceId)
	{
		Result.Code = ESPInteractionResultCode::NotOwner;
		InventoryReplayLedger.Record(Request, Result);
		ClientNotifyInventoryAction(Result);
		return;
	}

	FSPItemInstance DroppedItem;
	const FVector PreferredLocation =
		GetActorLocation()
		+ GetActorForwardVector() * 110.0f
		+ FVector(0.0f, 0.0f, 35.0f);
	if (DropHandItemInternal(
		PreferredLocation,
		ExpectedRevision,
		DroppedItem))
	{
		Result.Code = ESPInteractionResultCode::Success;
		Result.AuthoritativeRevision =
			Inventory->GetInventoryRevision();
	}
	else
	{
		Result.Code = ExpectedRevision
				!= Inventory->GetInventoryRevision()
			? ESPInteractionResultCode::StaleRevision
			: ESPInteractionResultCode::ServerError;
		Result.AuthoritativeRevision =
			Inventory->GetInventoryRevision();
	}

	InventoryReplayLedger.Record(Request, Result);
	ClientNotifyInventoryAction(Result);
}

void ASPCharacter::ClientNotifyInventoryAction_Implementation(
	const FSPInteractionResult& Result)
{
	const FString ActionName = StaticEnum<ESPInteractionAction>()->GetNameStringByValue(
		static_cast<int64>(Result.Action));
	const FString ResultName = StaticEnum<ESPInteractionResultCode>()->GetNameStringByValue(
		static_cast<int64>(Result.Code));
	if (Result.IsSuccess())
	{
		UE_LOG(LogScrollPeddler, Log,
			TEXT("[SP_INVENTORY_ACTION_RESULT] Player=%s RequestId=%lld Action=%s Result=%s Revision=%d"),
			*GetNameSafe(this), Result.RequestId, *ActionName, *ResultName,
			Result.AuthoritativeRevision);
	}
	else
	{
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("[SP_INVENTORY_ACTION_RESULT] Player=%s RequestId=%lld Action=%s Result=%s Revision=%d"),
			*GetNameSafe(this), Result.RequestId, *ActionName, *ResultName,
			Result.AuthoritativeRevision);
	}
}

void ASPCharacter::ServerSetSelfTreatment_Implementation(const bool bRequested)
{
	if (!HasValidOwningController())
	{
		return;
	}

	if (!bRequested)
	{
		CancelSelfTreatment();
		return;
	}

	ASPPlayerState* ScrollPlayerState = GetPlayerState<ASPPlayerState>();
	if (!ScrollPlayerState
		|| ScrollPlayerState->GetPlayerCondition() != ESPPlayerCondition::Injured
		|| ScrollPlayerState->IsRunTerminal()
		|| bSelfTreatmentInProgress)
	{
		return;
	}

	bSelfTreatmentInProgress = true;
	bInteractionDisabled = true;
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const double ServerTime = GameState
		? GameState->GetServerWorldTimeSeconds()
		: GetWorld()->GetTimeSeconds();
	TreatmentEndServerTime = ServerTime + SelfTreatmentSeconds;
	GetWorldTimerManager().SetTimer(
		SelfTreatmentTimerHandle,
		this,
		&ASPCharacter::CompleteSelfTreatment,
		SelfTreatmentSeconds,
		false);
	ForceNetUpdate();
	EmitGameplayNoise(TEXT("Noise.Player.Treatment"), 0.35f, 450.0f);
}

void ASPCharacter::ServerRequestAutoExtract_Implementation()
{
#if UE_BUILD_SHIPPING
	UE_LOG(LogScrollPeddler, Warning, TEXT("[SP_TECH_SPIKE_EXTRACTION_REJECTED] Player=%s Reason=ShippingBuild"), *GetNameSafe(this));
	return;
#else
	if (!HasValidOwningController())
	{
		UE_LOG(LogScrollPeddler, Warning, TEXT("[SP_TECH_SPIKE_EXTRACTION_REJECTED] Player=%s Reason=InvalidOwner"), *GetNameSafe(this));
		return;
	}
	if (!FParse::Param(FCommandLine::Get(), TEXT("SPAutoSpike")))
	{
		UE_LOG(LogScrollPeddler, Warning, TEXT("[SP_TECH_SPIKE_EXTRACTION_REJECTED] Player=%s Reason=AutomationDisabled"), *GetNameSafe(this));
		return;
	}
	if (!GetActiveScrollPlayerState())
	{
		UE_LOG(LogScrollPeddler, Warning, TEXT("[SP_TECH_SPIKE_EXTRACTION_REJECTED] Player=%s Reason=InactivePlayerState"), *GetNameSafe(this));
		return;
	}

	ASPGameMode* ScrollGameMode = GetWorld() ? Cast<ASPGameMode>(GetWorld()->GetAuthGameMode()) : nullptr;
	ASPExtractionZone* ExtractionZone = ScrollGameMode ? ScrollGameMode->GetExtractionZone() : nullptr;
	if (!ExtractionZone)
	{
		UE_LOG(LogScrollPeddler, Warning, TEXT("[SP_TECH_SPIKE_EXTRACTION_REJECTED] Player=%s Reason=MissingZone"), *GetNameSafe(this));
		return;
	}

	GetCharacterMovement()->StopMovementImmediately();
	const bool bMoved = SetActorLocation(
		ExtractionZone->GetExtractionPoint(), false, nullptr, ETeleportType::TeleportPhysics);
	if (bMoved && GetCapsuleComponent())
	{
		// Deliberately use the normal authority overlap notification. The automation
		// RPC never calls GameMode::TryExtractCharacter directly.
		GetCapsuleComponent()->UpdateOverlaps(nullptr, true);
	}
	UE_LOG(LogScrollPeddler, Log, TEXT("[SP_TECH_SPIKE_AUTO_EXTRACT_MOVED] Player=%s Moved=%d"), *GetNameSafe(this), bMoved ? 1 : 0);
#endif
}

void ASPCharacter::HandleExtractionCommitted()
{
	if (bInteractionDisabled)
	{
		return;
	}

	bInteractionDisabled = true;
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
		Movement->DisableMovement();
	}
	if (Controller && IsLocallyControlled())
	{
		Controller->SetIgnoreMoveInput(true);
	}

	UE_LOG(LogScrollPeddler, Log, TEXT("[SP_TECH_SPIKE_INTERACTION_DISABLED] Player=%s Reason=Extracted"), *GetNameSafe(this));
}

void ASPCharacter::MoveForward(float Value)
{
	if (Controller && !FMath::IsNearlyZero(Value))
	{
		const FRotator YawRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
		AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Value);
	}
}

void ASPCharacter::MoveRight(float Value)
{
	if (Controller && !FMath::IsNearlyZero(Value))
	{
		const FRotator YawRotation(0.0f, Controller->GetControlRotation().Yaw, 0.0f);
		AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Value);
	}
}

void ASPCharacter::Turn(float Value)
{
	AddControllerYawInput(Value);
}

void ASPCharacter::LookUp(float Value)
{
	AddControllerPitchInput(Value);
}

void ASPCharacter::HandleJumpPressed()
{
	const ASPPlayerState* ScrollPlayerState = GetPlayerState<ASPPlayerState>();
	if (!ScrollPlayerState
		|| (ScrollPlayerState->GetPlayerCondition() != ESPPlayerCondition::Down
			&& ScrollPlayerState->GetPlayerCondition() != ESPPlayerCondition::Missing))
	{
		Jump();
	}
}

void ASPCharacter::HandleJumpReleased()
{
	StopJumping();
}

void ASPCharacter::HandleSprintPressed()
{
	if (!CanSprint())
	{
		return;
	}

	bSprinting = true;
	ApplyMovementTuning();
	ServerSetSprinting(true);
}

void ASPCharacter::HandleSprintReleased()
{
	bSprinting = false;
	ApplyMovementTuning();
	ServerSetSprinting(false);
}

void ASPCharacter::HandleCrouchPressed()
{
	bSprinting = false;
	ServerSetSprinting(false);
	Crouch();
}

void ASPCharacter::HandleCrouchReleased()
{
	UnCrouch();
}

void ASPCharacter::HandleInteract()
{
	if (IsPickupRequestPending())
	{
		return;
	}

	if (ASPWorldItem* WorldItem = FindWorldItemInView())
	{
		RequestPickupWorldItem(WorldItem);
	}
	else if (ASPScrollPickup* Pickup = FindPickupInView())
	{
		RequestPickup(Pickup);
	}
	else
	{
		ShowNoTargetPickupFeedback();
	}
}

void ASPCharacter::HandleUseScroll()
{
	RequestUseFirst();
}

void ASPCharacter::HandleSwapBag0()
{
	RequestSwapBagSlot(0);
}

void ASPCharacter::HandleSwapBag1()
{
	RequestSwapBagSlot(1);
}

void ASPCharacter::HandleSwapBag2()
{
	RequestSwapBagSlot(2);
}

void ASPCharacter::HandleSwapBag3()
{
	RequestSwapBagSlot(3);
}

void ASPCharacter::HandleSelfTreatment()
{
	ServerSetSelfTreatment(!bSelfTreatmentInProgress);
}

void ASPCharacter::HandleDropHandItem()
{
	if (!Inventory || !CanRequestInteraction())
	{
		return;
	}

	const FSPInventorySlot& HandSlot =
		Inventory->GetInventoryState().HandSlot;
	if (!HandSlot.bOccupied)
	{
		return;
	}

	const int64 RequestId = NextInventoryRequestId++;
	if (NextInventoryRequestId <= 0)
	{
		NextInventoryRequestId = 1;
	}
	ServerDropHandItem(
		HandSlot.Item.InstanceId,
		Inventory->GetInventoryRevision(),
		RequestId);
}

void ASPCharacter::RequestSwapBagSlot(const int32 BagIndex)
{
	if (!Inventory || !CanRequestInteraction())
	{
		return;
	}

	const int64 RequestId = NextInventoryRequestId++;
	if (NextInventoryRequestId <= 0)
	{
		NextInventoryRequestId = 1;
	}
	ServerSwapHandWithBag(
		BagIndex,
		Inventory->GetInventoryRevision(),
		RequestId);
}

bool ASPCharacter::CanRequestInteraction() const
{
	if (bInteractionDisabled)
	{
		return false;
	}

	const ASPPlayerState* ScrollPlayerState = GetPlayerState<ASPPlayerState>();
	return IsValid(ScrollPlayerState) && !ScrollPlayerState->IsExtracted();
}

bool ASPCharacter::CanPerformFieldGameplayAction() const
{
	const ASPGameState* ScrollGameState =
		GetWorld() ? GetWorld()->GetGameState<ASPGameState>() : nullptr;
	const ASPPlayerState* ScrollPlayerState =
		GetPlayerState<ASPPlayerState>();
	return ScrollGameState
		&& ScrollPlayerState
		&& SPAllowsPlayerFieldGameplayAction(
			ScrollGameState->GetRunPhase(),
			ScrollPlayerState->GetParticipationState(),
			ScrollPlayerState->GetPlayerCondition());
}

bool ASPCharacter::HasValidOwningController() const
{
	return HasAuthority() && IsValid(Controller) && Controller->GetPawn() == this;
}

bool ASPCharacter::HasLineOfSightToPickup(const ASPScrollPickup* Pickup) const
{
	if (!GetWorld() || !IsValid(Pickup))
	{
		return false;
	}

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(SPPickupAuthorityTrace), false, this);
	FHitResult Hit;
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit, GetPawnViewLocation(), Pickup->GetActorLocation(), ECC_Visibility, QueryParams);
	return !bHit || Hit.GetActor() == Pickup;
}

bool ASPCharacter::CanSprint() const
{
	if (StaminaSeconds <= KINDA_SMALL_NUMBER || bIsCrouched
		|| IsCarryingLargeCargo())
	{
		return false;
	}

	const ASPPlayerState* ScrollPlayerState = GetPlayerState<ASPPlayerState>();
	return !ScrollPlayerState
		|| (ScrollPlayerState->GetPlayerCondition() != ESPPlayerCondition::Down
			&& ScrollPlayerState->GetPlayerCondition() != ESPPlayerCondition::Missing
			&& !ScrollPlayerState->IsRunTerminal());
}

void ASPCharacter::UpdateStamina(const float DeltaSeconds)
{
	const bool bMoving = GetVelocity().SizeSquared2D() > 25.0f;
	if (bSprinting && bMoving && CanSprint())
	{
		StaminaSeconds = FMath::Max(0.0f, StaminaSeconds - DeltaSeconds);
		if (StaminaSeconds <= KINDA_SMALL_NUMBER)
		{
			bSprinting = false;
			if (HasAuthority())
			{
				EmitGameplayNoise(TEXT("Noise.Player.Exhausted"), 0.8f, 1000.0f);
				ForceNetUpdate();
			}
		}
	}
	else
	{
		bSprinting = false;
		StaminaSeconds = FMath::Min(
			MaxStaminaSeconds,
			StaminaSeconds + StaminaRecoveryPerSecond * DeltaSeconds);
	}
}

void ASPCharacter::ApplyMovementTuning()
{
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	if (!Movement)
	{
		return;
	}

	float SpeedMultiplier = 1.0f;
	if (const ASPPlayerState* ScrollPlayerState = GetPlayerState<ASPPlayerState>())
	{
		if (ScrollPlayerState->GetPlayerCondition() == ESPPlayerCondition::Injured)
		{
			SpeedMultiplier = InjuredSpeedMultiplier;
		}
		else if (ScrollPlayerState->GetPlayerCondition() == ESPPlayerCondition::Down)
		{
			Movement->MaxWalkSpeed = 120.0f;
			Movement->MaxWalkSpeedCrouched = 120.0f;
			return;
		}
		else if (ScrollPlayerState->GetPlayerCondition() == ESPPlayerCondition::Missing)
		{
			Movement->DisableMovement();
			return;
		}
	}
	if (IsCarryingLargeCargo())
	{
		SpeedMultiplier *= LargeCargoSpeedMultiplier;
	}

	if (Movement->MovementMode == MOVE_None)
	{
		Movement->SetMovementMode(MOVE_Walking);
	}
	Movement->MaxWalkSpeed =
		(bSprinting && CanSprint() ? SprintSpeed : WalkSpeed) * SpeedMultiplier;
	Movement->MaxWalkSpeedCrouched = CrouchSpeed * SpeedMultiplier;
}

void ASPCharacter::UpdateMovementNoise()
{
	if (!GetWorld())
	{
		return;
	}

	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const double ServerTime = GameState
		? GameState->GetServerWorldTimeSeconds()
		: GetWorld()->GetTimeSeconds();

	const ASPPlayerState* ScrollPlayerState = GetPlayerState<ASPPlayerState>();
	if (ScrollPlayerState
		&& ScrollPlayerState->GetPlayerCondition() == ESPPlayerCondition::Injured
		&& ServerTime >= NextInjuredGroanServerTime)
	{
		EmitGameplayNoise(TEXT("Noise.Player.InjuredGroan"), 0.3f, 450.0f);
		NextInjuredGroanServerTime = ServerTime + 8.0;
	}

	if (ServerTime < NextMovementNoiseServerTime
		|| !GetCharacterMovement()
		|| !GetCharacterMovement()->IsMovingOnGround()
		|| GetVelocity().SizeSquared2D() <= 100.0f)
	{
		return;
	}

	float Loudness = 0.5f;
	float Radius = 650.0f;
	double Interval = 0.5;
	if (bIsCrouched)
	{
		Loudness = 0.25f;
		Radius = 350.0f;
		Interval = 0.75;
	}
	else if (bSprinting)
	{
		Loudness = 0.9f;
		Radius = 1200.0f;
		Interval = 0.3;
	}
	else if (IsCarryingLargeCargo())
	{
		Loudness = 0.75f;
		Radius = 950.0f;
		Interval = 0.4;
	}

	EmitGameplayNoise(TEXT("Noise.Movement.Footstep"), Loudness, Radius);
	NextMovementNoiseServerTime = ServerTime + Interval;
}

void ASPCharacter::EmitGameplayNoise(
	const FName TagName,
	float Loudness,
	const float Radius)
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	if (IsSilenced())
	{
		Loudness *= 0.2f;
	}

	if (USPNoiseSubsystem* NoiseSubsystem =
		GetWorld()->GetSubsystem<USPNoiseSubsystem>())
	{
		NoiseSubsystem->ReportNoise(
			GetActorLocation(),
			Loudness,
			Radius,
			FGameplayTag::RequestGameplayTag(TagName, false),
			this);
	}
}

bool ASPCharacter::AuthorityDropHandItem(
	const FVector& PreferredWorldLocation)
{
	if (!HasAuthority() || !Inventory || !CanRequestInteraction())
	{
		return false;
	}

	FSPItemInstance DroppedItem;
	return DropHandItemInternal(
		PreferredWorldLocation,
		Inventory->GetInventoryRevision(),
		DroppedItem);
}

bool ASPCharacter::AuthorityConsumeScrollProtection()
{
	if (!HasAuthority() || !IsProtectedByScroll())
	{
		return false;
	}

	ProtectionEndServerTime = 0.0f;
	ForceNetUpdate();
	EmitGameplayNoise(
		TEXT("Noise.Scroll.WardBreak"),
		0.55f,
		700.0f);
	return true;
}

bool ASPCharacter::DropHandItemInternal(
	const FVector& PreferredWorldLocation,
	const int32 ExpectedRevision,
	FSPItemInstance& OutDroppedItem)
{
	if (!HasAuthority() || !GetWorld() || !Inventory
		|| PreferredWorldLocation.ContainsNaN())
	{
		return false;
	}

	const FSPInventoryState InventoryBefore =
		Inventory->GetInventoryState();
	if (ExpectedRevision != InventoryBefore.Revision
		|| !InventoryBefore.HandSlot.bOccupied)
	{
		return false;
	}

	const FVector CandidateLocations[] =
	{
		PreferredWorldLocation,
		GetActorLocation() + GetActorRightVector() * 90.0f
			+ FVector(0.0f, 0.0f, 35.0f),
		GetActorLocation() - GetActorRightVector() * 90.0f
			+ FVector(0.0f, 0.0f, 35.0f)
	};
	FTransform DropTransform;
	bool bFoundPlacement = false;
	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(SPDropHandItem),
		false,
		this);
	QueryParams.AddIgnoredActor(this);
	const FCollisionShape DropShape =
		FCollisionShape::MakeBox(FVector(26.0f));
	for (const FVector& CandidateLocation : CandidateLocations)
	{
		const FTransform CandidateTransform(
			FRotator(0.0f, GetActorRotation().Yaw, 0.0f),
			CandidateLocation);
		const bool bBlocked = GetWorld()->OverlapBlockingTestByChannel(
			CandidateLocation,
			FQuat::Identity,
			ECC_Visibility,
			DropShape,
			QueryParams);
		if (SPValidateDropPlacement(
				GetActorLocation(),
				CandidateTransform,
				220.0f,
				!bBlocked) == ESPDropPlacementResult::Success)
		{
			DropTransform = CandidateTransform;
			bFoundPlacement = true;
			break;
		}
	}

	if (!bFoundPlacement)
	{
		return false;
	}

	ESPInventoryMutationResult MutationResult =
		ESPInventoryMutationResult::InvalidItem;
	if (!Inventory->RemoveItemByInstanceId(
		InventoryBefore.HandSlot.Item.InstanceId,
		ExpectedRevision,
		OutDroppedItem,
		MutationResult))
	{
		return false;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = nullptr;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASPWorldItem* WorldItem = GetWorld()->SpawnActor<ASPWorldItem>(
		ASPWorldItem::StaticClass(),
		DropTransform,
		SpawnParameters);
	if (!WorldItem || !WorldItem->InitializeItem(OutDroppedItem))
	{
		if (WorldItem)
		{
			WorldItem->Destroy();
		}
		Inventory->AuthorityRestoreState(InventoryBefore);
		return false;
	}

	FSPScrollInstance LegacyScroll;
	if (OutDroppedItem.TryToLegacyScroll(LegacyScroll))
	{
		if (ASPPlayerState* ScrollPlayerState =
			GetPlayerState<ASPPlayerState>())
		{
			ScrollPlayerState->RecordScrollDropped(LegacyScroll);
		}
	}

	EmitGameplayNoise(TEXT("Noise.Item.Drop"), 0.4f, 500.0f);
	return true;
}

void ASPCharacter::CancelSelfTreatment()
{
	if (!bSelfTreatmentInProgress)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(SelfTreatmentTimerHandle);
	bSelfTreatmentInProgress = false;
	bInteractionDisabled = false;
	TreatmentEndServerTime = 0.0;
	ForceNetUpdate();
}

void ASPCharacter::CompleteSelfTreatment()
{
	if (!HasAuthority() || !bSelfTreatmentInProgress)
	{
		return;
	}

	ASPPlayerState* ScrollPlayerState = GetPlayerState<ASPPlayerState>();
	if (!ScrollPlayerState
		|| !ScrollPlayerState->AuthorityTransitionCondition(
			ESPPlayerCondition::Normal))
	{
		CancelSelfTreatment();
		return;
	}

	CancelSelfTreatment();
	EmitGameplayNoise(TEXT("Noise.Player.TreatmentComplete"), 0.45f, 550.0f);
}

ASPPlayerState* ASPCharacter::GetActiveScrollPlayerState() const
{
	ASPPlayerState* ScrollPlayerState = GetPlayerState<ASPPlayerState>();
	return HasAuthority()
		&& IsValid(ScrollPlayerState)
		&& ScrollPlayerState->HasAuthority()
		&& ScrollPlayerState->GetParticipationState()
			== ESPParticipationState::Active
		&& !ScrollPlayerState->IsRunTerminal()
		? ScrollPlayerState
		: nullptr;
}

void ASPCharacter::RejectPickupRequest(
	const uint32 RequestId,
	const ESPPickupResultCode ResultCode,
	const TCHAR* Reason,
	const ASPScrollPickup* Pickup)
{
	const FSPInteractionRequest Request = MakePickupInteractionRequest(Pickup, RequestId);
	PickupReplayLedger.Record(Request, MakePickupInteractionResult(Request, ResultCode));
	UE_LOG(LogScrollPeddler, Warning,
		TEXT("[SP_TECH_SPIKE_PICKUP_REJECTED] Player=%s Pickup=%s RequestId=%u Result=%s Reason=%s"),
		*GetNameSafe(this), *GetNameSafe(Pickup), RequestId,
		*StaticEnum<ESPPickupResultCode>()->GetNameStringByValue(static_cast<int64>(ResultCode)), Reason);
	ClientNotifyPickupResult(RequestId, ResultCode);
}

void ASPCharacter::BeginLocalPickupRequest(const uint32 RequestId)
{
	PendingPickupRequestId = RequestId;
	PickupFeedbackExpiresAt = 0.0;
	bPickupFeedbackNoTarget = false;
	bPickupFeedbackTimedOut = false;

	GetWorldTimerManager().ClearTimer(PickupRequestTimeoutHandle);
	FTimerDelegate TimeoutDelegate = FTimerDelegate::CreateUObject(
		this, &ASPCharacter::HandlePickupRequestTimeout, RequestId);
	GetWorldTimerManager().SetTimer(
		PickupRequestTimeoutHandle,
		MoveTemp(TimeoutDelegate),
		PickupRequestTimeoutSeconds,
		false);
}

void ASPCharacter::HandlePickupRequestTimeout(const uint32 RequestId)
{
	if (PendingPickupRequestId != RequestId)
	{
		return;
	}

	PendingPickupRequestId = 0;
	ShowPickupFeedback(ESPPickupResultCode::ServerError, true);
	UE_LOG(LogScrollPeddler, Warning,
		TEXT("[SP_PICKUP_FEEDBACK_TIMEOUT] Player=%s RequestId=%u"),
		*GetNameSafe(this), RequestId);
}

void ASPCharacter::ShowNoTargetPickupFeedback()
{
	if (!IsLocallyControlled() || IsPickupRequestPending() || !GetWorld())
	{
		return;
	}

	LastPickupResult = ESPPickupResultCode::InvalidRequest;
	bPickupFeedbackNoTarget = true;
	bPickupFeedbackTimedOut = false;
	PickupFeedbackExpiresAt = GetWorld()->GetTimeSeconds() + NoTargetFeedbackSeconds;
}

void ASPCharacter::ShowPickupFeedback(
	const ESPPickupResultCode ResultCode,
	const bool bTimedOut)
{
	if (!GetWorld())
	{
		return;
	}

	LastPickupResult = ResultCode;
	bPickupFeedbackNoTarget = false;
	bPickupFeedbackTimedOut = bTimedOut;
	PickupFeedbackExpiresAt = GetWorld()->GetTimeSeconds() + PickupResultFeedbackSeconds;
}

uint32 ASPCharacter::AllocatePickupRequestId()
{
	const uint32 RequestId = NextPickupRequestId++;
	if (NextPickupRequestId == 0)
	{
		NextPickupRequestId = 1;
	}
	return RequestId == 0 ? AllocatePickupRequestId() : RequestId;
}

void ASPCharacter::OnRep_SilenceEndServerTime()
{
	UE_LOG(LogScrollPeddler, Verbose,
		TEXT("[SP_SCROLL_SILENCE_REPLICATED] Player=%s SilenceEnd=%.3f"),
		*GetNameSafe(this), SilenceEndServerTime);
}

void ASPCharacter::OnRep_ScrollEffectState()
{
	UE_LOG(LogScrollPeddler, Verbose,
		TEXT("[SP_SCROLL_EFFECT_REPLICATED] Player=%s ProtectionEnd=%.3f RevelationEnd=%.3f"),
		*GetNameSafe(this),
		ProtectionEndServerTime,
		RevelationEndServerTime);
}

void ASPCharacter::OnRep_MovementState()
{
	ApplyMovementTuning();
}

void ASPCharacter::OnRep_TreatmentState()
{
	bInteractionDisabled = bSelfTreatmentInProgress;
}
