#include "Player/SPCharacter.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/SPScrollDefinition.h"
#include "Data/SPScrollEngravingDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "Game/SPGameMode.h"
#include "Game/SPPlayerState.h"
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
#include "World/SPScrollPickup.h"

namespace
{
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
	PrimaryActorTick.bCanEverTick = false;
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
	Movement->MaxWalkSpeed = 500.0f;

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

void ASPCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	check(PlayerInputComponent);

	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction(TEXT("Jump"), IE_Released, this, &ACharacter::StopJumping);
	PlayerInputComponent->BindAction(TEXT("Interact"), IE_Pressed, this, &ASPCharacter::HandleInteract);
	PlayerInputComponent->BindAction(TEXT("UseScroll"), IE_Pressed, this, &ASPCharacter::HandleUseScroll);
	PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &ASPCharacter::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &ASPCharacter::MoveRight);
	PlayerInputComponent->BindAxis(TEXT("Turn"), this, &ASPCharacter::Turn);
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &ASPCharacter::LookUp);
}

void ASPCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASPCharacter, SilenceEndServerTime);
}

void ASPCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetWorld())
	{
		GetWorldTimerManager().ClearTimer(PickupRequestTimeoutHandle);
	}
	Super::EndPlay(EndPlayReason);
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
		ServerUseScroll(InstanceId);
	}
}

void ASPCharacter::ServerTryPickup_Implementation(ASPScrollPickup* Pickup, const uint32 RequestId)
{
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

	UE_LOG(LogScrollPeddler, Log, TEXT("[SP_TECH_SPIKE_PICKUP_COMMITTED] Player=%s Pickup=%s InstanceId=%s Count=%d"),
		*GetNameSafe(this), *GetNameSafe(Pickup), *Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower), Inventory->GetItemCount());
	ClientNotifyPickupResult(RequestId, ESPPickupResultCode::Success);
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

void ASPCharacter::ServerUseScroll_Implementation(FGuid InstanceId)
{
	if (!HasValidOwningController() || !Inventory || !InstanceId.IsValid())
	{
		UE_LOG(LogScrollPeddler, Warning, TEXT("[SP_TECH_SPIKE_SCROLL_REJECTED] Player=%s Reason=InvalidRequest"), *GetNameSafe(this));
		return;
	}
	ASPPlayerState* ScrollPlayerState = GetActiveScrollPlayerState();
	if (!ScrollPlayerState)
	{
		UE_LOG(LogScrollPeddler, Warning, TEXT("[SP_TECH_SPIKE_SCROLL_REJECTED] Player=%s Reason=InactivePlayerState"), *GetNameSafe(this));
		return;
	}

	const FSPScrollInstance* FoundItem = Inventory->FindItemByInstanceId(InstanceId);
	if (!FoundItem)
	{
		UE_LOG(LogScrollPeddler, Warning, TEXT("[SP_TECH_SPIKE_SCROLL_REJECTED] Player=%s Reason=UnknownInstance InstanceId=%s"),
			*GetNameSafe(this), *InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
		return;
	}

	const FSPScrollInstance ItemSnapshot = *FoundItem;
	UAssetManager& AssetManager = UAssetManager::Get();
	USPScrollDefinition* ScrollDefinition = ResolvePrimaryAsset<USPScrollDefinition>(AssetManager, ItemSnapshot.BaseDefinitionId);
	USPScrollEngravingDefinition* EngravingDefinition = ResolvePrimaryAsset<USPScrollEngravingDefinition>(AssetManager, ItemSnapshot.EngravingDefinitionId);
	if (!ScrollDefinition || !EngravingDefinition || ScrollDefinition->EffectKind != ESPScrollEffectKind::Silence ||
		!ScrollDefinition->AllowsEngraving(EngravingDefinition))
	{
		UE_LOG(LogScrollPeddler, Warning, TEXT("[SP_TECH_SPIKE_SCROLL_REJECTED] Player=%s Reason=AssetValidation Base=%s Engraving=%s"),
			*GetNameSafe(this), *ItemSnapshot.BaseDefinitionId.ToString(), *ItemSnapshot.EngravingDefinitionId.ToString());
		return;
	}

	FSPScrollInstance ConsumedItem;
	if (!Inventory->RemoveItemByInstanceId(InstanceId, ConsumedItem))
	{
		UE_LOG(LogScrollPeddler, Warning, TEXT("[SP_TECH_SPIKE_SCROLL_REJECTED] Player=%s Reason=ConsumeRace InstanceId=%s"),
			*GetNameSafe(this), *InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
		return;
	}
	if (!ScrollPlayerState->RecordScrollConsumed(ConsumedItem, ScrollDefinition->DeliveryValue))
	{
		const bool bInventoryRestored = Inventory->TryAddItem(ConsumedItem);
		UE_LOG(LogScrollPeddler, Error,
			TEXT("[SP_TECH_SPIKE_SCROLL_LEDGER_ROLLBACK] Player=%s InstanceId=%s InventoryRestored=%d"),
			*GetNameSafe(this), *ConsumedItem.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower),
			bInventoryRestored ? 1 : 0);
		return;
	}

	const float Duration = ScrollDefinition->BaseDurationSeconds * EngravingDefinition->DurationMultiplier *
		SPGetQualityMultiplier(ConsumedItem.Quality);
	const AGameStateBase* GameState = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	const float ServerTime = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	SilenceEndServerTime = FMath::Max(SilenceEndServerTime, ServerTime + FMath::Max(0.1f, Duration));
	ForceNetUpdate();

	UE_LOG(LogScrollPeddler, Log, TEXT("[SP_TECH_SPIKE_SCROLL_CONSUMED] Player=%s InstanceId=%s SilenceEnd=%.3f Duration=%.3f"),
		*GetNameSafe(this), *ConsumedItem.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower), SilenceEndServerTime, Duration);
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

void ASPCharacter::HandleInteract()
{
	if (IsPickupRequestPending())
	{
		return;
	}

	if (ASPScrollPickup* Pickup = FindPickupInView())
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

bool ASPCharacter::CanRequestInteraction() const
{
	if (bInteractionDisabled)
	{
		return false;
	}

	const ASPPlayerState* ScrollPlayerState = GetPlayerState<ASPPlayerState>();
	return IsValid(ScrollPlayerState) && !ScrollPlayerState->IsExtracted();
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

ASPPlayerState* ASPCharacter::GetActiveScrollPlayerState() const
{
	ASPPlayerState* ScrollPlayerState = GetPlayerState<ASPPlayerState>();
	return HasAuthority() && IsValid(ScrollPlayerState) && ScrollPlayerState->HasAuthority() && !ScrollPlayerState->IsExtracted()
		? ScrollPlayerState
		: nullptr;
}

void ASPCharacter::RejectPickupRequest(
	const uint32 RequestId,
	const ESPPickupResultCode ResultCode,
	const TCHAR* Reason,
	const ASPScrollPickup* Pickup)
{
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
	UE_LOG(LogScrollPeddler, Verbose, TEXT("[SP_TECH_SPIKE_SILENCE_REPLICATED] Player=%s SilenceEnd=%.3f"),
		*GetNameSafe(this), SilenceEndServerTime);
}
