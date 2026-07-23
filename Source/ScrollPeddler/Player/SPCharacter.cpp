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
		GetWorldTimerManager().ClearTimer(ScrollUseRequestTimeoutHandle);
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

	// 입력·자동화 의도는 로컬 제어 Pawn에서 시작해야 한다.
	// 권위 코드가 remote Pawn 요청을 대신 만들면 owning client에 대응하는
	// pending RequestId가 없으므로 허용하지 않는다.
	if (!IsLocallyControlled())
	{
		return;
	}
	if (IsPickupRequestPending() || IsScrollUseRequestPending())
	{
		UE_LOG(LogScrollPeddler, Verbose,
			TEXT("[SP_PICKUP_REQUEST_SKIPPED] Player=%s PendingPickupRequestId=%u PendingUseRequestId=%u"),
			*GetNameSafe(this), PendingPickupRequestId, PendingScrollUseRequestId);
		return;
	}

	const FGuid TargetInstanceId = Pickup->GetScrollInstance().InstanceId;
	const uint32 RequestId = AllocateInteractionRequestId();
	BeginLocalPickupRequest(RequestId);
	UE_LOG(LogScrollPeddler, Log,
		TEXT("[SP_PICKUP_REQUEST] Player=%s Pickup=%s InstanceId=%s RequestId=%u"),
		*GetNameSafe(this), *GetNameSafe(Pickup),
		*TargetInstanceId.ToString(EGuidFormats::DigitsWithHyphensLower), RequestId);
	ServerTryPickup(Pickup, TargetInstanceId, RequestId);
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

bool ASPCharacter::HasActiveScrollUseFeedback() const
{
	return GetWorld() && ScrollUseFeedbackExpiresAt > GetWorld()->GetTimeSeconds();
}

void ASPCharacter::RequestUseFirst()
{
	if (!CanRequestInteraction() || IsScrollUseRequestPending() || IsPickupRequestPending())
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
		const uint32 RequestId = AllocateInteractionRequestId();
		BeginLocalScrollUseRequest(RequestId);
		UE_LOG(LogScrollPeddler, Log,
			TEXT("[SP_SCROLL_USE_REQUEST] Player=%s InstanceId=%s RequestId=%u"),
			*GetNameSafe(this), *InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower), RequestId);
		ServerUseScroll(InstanceId, RequestId);
	}
}

#if !UE_BUILD_SHIPPING
bool ASPCharacter::DevelopmentRequestPickup(
	ASPScrollPickup* Pickup,
	const FGuid& TargetInstanceId,
	const uint32 RequestId)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("SPAdversarialSuite")) ||
		(!IsLocallyControlled() && GetNetMode() != NM_Client))
	{
		return false;
	}

	ServerTryPickup(Pickup, TargetInstanceId, RequestId);
	return true;
}

bool ASPCharacter::DevelopmentRequestUseScroll(const FGuid& InstanceId, const uint32 RequestId)
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("SPAdversarialSuite")) ||
		(!IsLocallyControlled() && GetNetMode() != NM_Client))
	{
		return false;
	}

	ServerUseScroll(InstanceId, RequestId);
	return true;
}
#endif

void ASPCharacter::ServerTryPickup_Implementation(
	ASPScrollPickup* Pickup,
	const FGuid TargetInstanceId,
	const uint32 RequestId)
{
	if (HandleExistingInteractionRequest(
		RequestId, EInteractionRequestAction::Pickup, TargetInstanceId))
	{
		return;
	}
	if (!HasValidOwningController())
	{
		RejectPickupRequest(
			RequestId, TargetInstanceId, ESPPickupResultCode::InvalidRequest, TEXT("InvalidOwner"), Pickup);
		return;
	}
	ASPPlayerState* ScrollPlayerState = GetActiveScrollPlayerState();
	if (!ScrollPlayerState)
	{
		RejectPickupRequest(
			RequestId, TargetInstanceId, ESPPickupResultCode::InactivePlayer, TEXT("InactivePlayerState"), Pickup);
		return;
	}
	if (!IsValid(Pickup) || !Pickup->HasAuthority() || Pickup->GetWorld() != GetWorld())
	{
		RejectPickupRequest(
			RequestId, TargetInstanceId, ESPPickupResultCode::InvalidRequest, TEXT("InvalidPickup"), Pickup);
		return;
	}
	if (!TargetInstanceId.IsValid() || Pickup->GetScrollInstance().InstanceId != TargetInstanceId)
	{
		RejectPickupRequest(
			RequestId, TargetInstanceId, ESPPickupResultCode::InvalidRequest, TEXT("TargetMismatch"), Pickup);
		return;
	}
	if (!IsPickupWithinRange(GetActorLocation(), Pickup->GetActorLocation()))
	{
		RejectPickupRequest(
			RequestId, TargetInstanceId, ESPPickupResultCode::OutOfRange, TEXT("Distance"), Pickup);
		return;
	}
	if (!Inventory || !Inventory->HasCapacity())
	{
		RejectPickupRequest(
			RequestId, TargetInstanceId, ESPPickupResultCode::InventoryFull, TEXT("Capacity"), Pickup);
		return;
	}
	// 표시용 availability는 로컬 trace와 자동화 대상 선택에 사용한다.
	// 서버는 이미 claim됐거나 동시에 예약된 픽업을 TryReserve가
	// Contested로 분류할 수 있게 여기서 먼저 차단하지 않는다.
	if (!Pickup->GetScrollInstance().IsValid())
	{
		RejectPickupRequest(
			RequestId, TargetInstanceId, ESPPickupResultCode::Unavailable, TEXT("InvalidInstance"), Pickup);
		return;
	}
	if (!HasLineOfSightToPickup(Pickup))
	{
		RejectPickupRequest(
			RequestId, TargetInstanceId, ESPPickupResultCode::Obstructed, TEXT("LineOfSight"), Pickup);
		return;
	}
	if (!Pickup->TryReserve(this))
	{
		RejectPickupRequest(
			RequestId, TargetInstanceId, ESPPickupResultCode::Contested, TEXT("Reservation"), Pickup);
		return;
	}

	const FSPScrollInstance Item = Pickup->GetScrollInstance();
	if (!Inventory->TryAddItem(Item))
	{
		Pickup->ReleaseReservation(this);
		RejectPickupRequest(
			RequestId, TargetInstanceId, ESPPickupResultCode::InventoryFull, TEXT("InventoryCommit"), Pickup);
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
		RejectPickupRequest(
			RequestId, TargetInstanceId, ESPPickupResultCode::ServerError, TEXT("LedgerCommit"), Pickup);
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
		RejectPickupRequest(
			RequestId, TargetInstanceId, ESPPickupResultCode::ServerError, TEXT("PickupCommit"), Pickup);
		return;
	}

	UE_LOG(LogScrollPeddler, Log, TEXT("[SP_TECH_SPIKE_PICKUP_COMMITTED] Player=%s Pickup=%s InstanceId=%s Count=%d"),
		*GetNameSafe(this), *GetNameSafe(Pickup), *Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower), Inventory->GetItemCount());
	CompletePickupRequest(RequestId, TargetInstanceId, ESPPickupResultCode::Success);
}

void ASPCharacter::ClientNotifyPickupResult_Implementation(
	const uint32 RequestId,
	const ESPPickupResultCode ResultCode)
{
	LastCompletedPickupRequestId = RequestId;
	LastPickupResult = ResultCode;
	++PickupCompletionSerial;
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

void ASPCharacter::ServerUseScroll_Implementation(const FGuid InstanceId, const uint32 RequestId)
{
	if (HandleExistingInteractionRequest(
		RequestId, EInteractionRequestAction::UseScroll, InstanceId))
	{
		return;
	}
	if (!HasValidOwningController() || !Inventory || !InstanceId.IsValid())
	{
		RejectScrollUseRequest(
			RequestId, InstanceId, ESPScrollUseResultCode::InvalidRequest, TEXT("InvalidRequest"));
		return;
	}
	ASPPlayerState* ScrollPlayerState = GetActiveScrollPlayerState();
	if (!ScrollPlayerState)
	{
		RejectScrollUseRequest(
			RequestId, InstanceId, ESPScrollUseResultCode::InactivePlayer, TEXT("InactivePlayerState"));
		return;
	}

	const FSPScrollInstance* FoundItem = Inventory->FindItemByInstanceId(InstanceId);
	if (!FoundItem)
	{
		RejectScrollUseRequest(
			RequestId, InstanceId, ESPScrollUseResultCode::NotOwned, TEXT("UnknownInstance"));
		return;
	}

	const FSPScrollInstance ItemSnapshot = *FoundItem;
	UAssetManager& AssetManager = UAssetManager::Get();
	USPScrollDefinition* ScrollDefinition = ResolvePrimaryAsset<USPScrollDefinition>(AssetManager, ItemSnapshot.BaseDefinitionId);
	USPScrollEngravingDefinition* EngravingDefinition = ResolvePrimaryAsset<USPScrollEngravingDefinition>(AssetManager, ItemSnapshot.EngravingDefinitionId);
	if (!ScrollDefinition || !EngravingDefinition || ScrollDefinition->EffectKind != ESPScrollEffectKind::Silence ||
		!ScrollDefinition->AllowsEngraving(EngravingDefinition))
	{
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("[SP_TECH_SPIKE_SCROLL_ASSET_INVALID] Player=%s Base=%s Engraving=%s"),
			*GetNameSafe(this), *ItemSnapshot.BaseDefinitionId.ToString(),
			*ItemSnapshot.EngravingDefinitionId.ToString());
		RejectScrollUseRequest(
			RequestId, InstanceId, ESPScrollUseResultCode::InvalidDefinition, TEXT("AssetValidation"));
		return;
	}

	FSPScrollInstance ConsumedItem;
	if (!Inventory->RemoveItemByInstanceId(InstanceId, ConsumedItem))
	{
		RejectScrollUseRequest(
			RequestId, InstanceId, ESPScrollUseResultCode::ServerError, TEXT("ConsumeRace"));
		return;
	}
	if (!ScrollPlayerState->RecordScrollConsumed(ConsumedItem, ScrollDefinition->DeliveryValue))
	{
		const bool bInventoryRestored = Inventory->TryAddItem(ConsumedItem);
		UE_LOG(LogScrollPeddler, Error,
			TEXT("[SP_TECH_SPIKE_SCROLL_LEDGER_ROLLBACK] Player=%s InstanceId=%s InventoryRestored=%d"),
			*GetNameSafe(this), *ConsumedItem.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower),
			bInventoryRestored ? 1 : 0);
		RejectScrollUseRequest(
			RequestId, InstanceId, ESPScrollUseResultCode::ServerError, TEXT("LedgerCommit"));
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
	CompleteScrollUseRequest(RequestId, InstanceId, ESPScrollUseResultCode::Success);
}

void ASPCharacter::ClientNotifyScrollUseResult_Implementation(
	const uint32 RequestId,
	const ESPScrollUseResultCode ResultCode)
{
	LastCompletedUseRequestId = RequestId;
	LastScrollUseResult = ResultCode;
	++ScrollUseCompletionSerial;
	if (PendingScrollUseRequestId != RequestId)
	{
		UE_LOG(LogScrollPeddler, Verbose,
			TEXT("[SP_SCROLL_USE_RESULT_STALE] Player=%s RequestId=%u PendingRequestId=%u Result=%s"),
			*GetNameSafe(this), RequestId, PendingScrollUseRequestId,
			*StaticEnum<ESPScrollUseResultCode>()->GetNameStringByValue(static_cast<int64>(ResultCode)));
		return;
	}

	GetWorldTimerManager().ClearTimer(ScrollUseRequestTimeoutHandle);
	PendingScrollUseRequestId = 0;
	ShowScrollUseFeedback(ResultCode);
	UE_LOG(LogScrollPeddler, Log,
		TEXT("[SP_SCROLL_USE_RESULT] Player=%s RequestId=%u Result=%s"),
		*GetNameSafe(this), RequestId,
		*StaticEnum<ESPScrollUseResultCode>()->GetNameStringByValue(static_cast<int64>(ResultCode)));
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
	if (!FParse::Param(FCommandLine::Get(), TEXT("SPAutoSpike")) &&
		!FParse::Param(FCommandLine::Get(), TEXT("SPAdversarialSuite")))
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
		// 자동화 RPC도 GameMode::TryExtractCharacter를 직접 호출하지 않고
		// 일반 권위 overlap 알림 경로를 사용한다.
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
	if (IsPickupRequestPending() || IsScrollUseRequestPending())
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
	const FGuid& TargetInstanceId,
	const ESPPickupResultCode ResultCode,
	const TCHAR* Reason,
	const ASPScrollPickup* Pickup)
{
	UE_LOG(LogScrollPeddler, Warning,
		TEXT("[SP_TECH_SPIKE_PICKUP_REJECTED] Player=%s Pickup=%s InstanceId=%s RequestId=%u Result=%s Reason=%s"),
		*GetNameSafe(this), *GetNameSafe(Pickup),
		*TargetInstanceId.ToString(EGuidFormats::DigitsWithHyphensLower), RequestId,
		*StaticEnum<ESPPickupResultCode>()->GetNameStringByValue(static_cast<int64>(ResultCode)), Reason);
	CompletePickupRequest(RequestId, TargetInstanceId, ResultCode);
}

void ASPCharacter::CompletePickupRequest(
	const uint32 RequestId,
	const FGuid& TargetInstanceId,
	const ESPPickupResultCode ResultCode)
{
	if (!TryRecordInteractionRequest(
		RequestId,
		EInteractionRequestAction::Pickup,
		TargetInstanceId,
		static_cast<uint8>(ResultCode)))
	{
		UE_LOG(LogScrollPeddler, Error,
			TEXT("[SP_INTERACTION_REQUEST_RECORD_FAILED] Player=%s Action=Pickup RequestId=%u TargetId=%s"),
			*GetNameSafe(this), RequestId,
			*TargetInstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
		ClientNotifyPickupResult(RequestId, ESPPickupResultCode::InvalidRequest);
		return;
	}

	ClientNotifyPickupResult(RequestId, ResultCode);
}

void ASPCharacter::RejectScrollUseRequest(
	const uint32 RequestId,
	const FGuid& InstanceId,
	const ESPScrollUseResultCode ResultCode,
	const TCHAR* Reason)
{
	UE_LOG(LogScrollPeddler, Warning,
		TEXT("[SP_TECH_SPIKE_SCROLL_REJECTED] Player=%s InstanceId=%s RequestId=%u Result=%s Reason=%s"),
		*GetNameSafe(this), *InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower), RequestId,
		*StaticEnum<ESPScrollUseResultCode>()->GetNameStringByValue(static_cast<int64>(ResultCode)), Reason);
	CompleteScrollUseRequest(RequestId, InstanceId, ResultCode);
}

void ASPCharacter::CompleteScrollUseRequest(
	const uint32 RequestId,
	const FGuid& InstanceId,
	const ESPScrollUseResultCode ResultCode)
{
	if (!TryRecordInteractionRequest(
		RequestId,
		EInteractionRequestAction::UseScroll,
		InstanceId,
		static_cast<uint8>(ResultCode)))
	{
		UE_LOG(LogScrollPeddler, Error,
			TEXT("[SP_INTERACTION_REQUEST_RECORD_FAILED] Player=%s Action=UseScroll RequestId=%u TargetId=%s"),
			*GetNameSafe(this), RequestId, *InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
		ClientNotifyScrollUseResult(RequestId, ESPScrollUseResultCode::InvalidRequest);
		return;
	}

	ClientNotifyScrollUseResult(RequestId, ResultCode);
}

bool ASPCharacter::HandleExistingInteractionRequest(
	const uint32 RequestId,
	const EInteractionRequestAction Action,
	const FGuid& TargetId)
{
	const TCHAR* ActionName = Action == EInteractionRequestAction::Pickup ? TEXT("Pickup") : TEXT("UseScroll");
	const EInteractionRequestDisposition Disposition = ClassifyInteractionRequest(
		ServerInteractionRequestLedger, RequestId, Action, TargetId);
	if (Disposition == EInteractionRequestDisposition::InvalidRequestId)
	{
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("[SP_INTERACTION_REQUEST_REJECTED] Player=%s Action=%s RequestId=0 TargetId=%s Reason=ZeroRequestId"),
			*GetNameSafe(this), ActionName, *TargetId.ToString(EGuidFormats::DigitsWithHyphensLower));
		NotifyInteractionResult(RequestId, Action, static_cast<uint8>(
			Action == EInteractionRequestAction::Pickup
				? static_cast<uint8>(ESPPickupResultCode::InvalidRequest)
				: static_cast<uint8>(ESPScrollUseResultCode::InvalidRequest)));
		return true;
	}

	const FInteractionRequestRecord* ExistingRecord = ServerInteractionRequestLedger.Find(RequestId);
	if (Disposition == EInteractionRequestDisposition::ExactReplay)
	{
		UE_LOG(LogScrollPeddler, Log,
			TEXT("[SP_INTERACTION_REQUEST_REPLAY] Player=%s Action=%s RequestId=%u TargetId=%s Result=%u"),
			*GetNameSafe(this), ActionName, RequestId,
			*TargetId.ToString(EGuidFormats::DigitsWithHyphensLower), ExistingRecord->ResultCode);
		NotifyInteractionResult(RequestId, Action, ExistingRecord->ResultCode);
		return true;
	}

	if (Disposition == EInteractionRequestDisposition::Conflict)
	{
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("[SP_INTERACTION_REQUEST_CONFLICT] Player=%s Action=%s RequestId=%u TargetId=%s OriginalAction=%s OriginalTargetId=%s"),
			*GetNameSafe(this), ActionName, RequestId,
			*TargetId.ToString(EGuidFormats::DigitsWithHyphensLower),
			ExistingRecord->Action == EInteractionRequestAction::Pickup ? TEXT("Pickup") : TEXT("UseScroll"),
			*ExistingRecord->TargetId.ToString(EGuidFormats::DigitsWithHyphensLower));
		NotifyInteractionResult(
			RequestId,
			Action,
			Action == EInteractionRequestAction::Pickup
				? static_cast<uint8>(ESPPickupResultCode::InvalidRequest)
				: static_cast<uint8>(ESPScrollUseResultCode::InvalidRequest));
		return true;
	}

	if (Disposition == EInteractionRequestDisposition::LedgerFull)
	{
		UE_LOG(LogScrollPeddler, Warning,
			TEXT("[SP_INTERACTION_REQUEST_REJECTED] Player=%s Action=%s RequestId=%u TargetId=%s Reason=LedgerFull Limit=%d"),
			*GetNameSafe(this), ActionName, RequestId,
			*TargetId.ToString(EGuidFormats::DigitsWithHyphensLower),
			MaxInteractionRequestLedgerEntries);
		NotifyInteractionResult(
			RequestId,
			Action,
			Action == EInteractionRequestAction::Pickup
				? static_cast<uint8>(ESPPickupResultCode::InvalidRequest)
				: static_cast<uint8>(ESPScrollUseResultCode::InvalidRequest));
		return true;
	}

	return false;
}

bool ASPCharacter::TryRecordInteractionRequest(
	const uint32 RequestId,
	const EInteractionRequestAction Action,
	const FGuid& TargetId,
	const uint8 ResultCode)
{
	return TryRecordInteractionRequestInLedger(
		ServerInteractionRequestLedger, RequestId, Action, TargetId, ResultCode);
}

ASPCharacter::EInteractionRequestDisposition ASPCharacter::ClassifyInteractionRequest(
	const TMap<uint32, FInteractionRequestRecord>& Ledger,
	const uint32 RequestId,
	const EInteractionRequestAction Action,
	const FGuid& TargetId)
{
	if (RequestId == 0)
	{
		return EInteractionRequestDisposition::InvalidRequestId;
	}

	if (const FInteractionRequestRecord* ExistingRecord = Ledger.Find(RequestId))
	{
		return ExistingRecord->Action == Action && ExistingRecord->TargetId == TargetId
			? EInteractionRequestDisposition::ExactReplay
			: EInteractionRequestDisposition::Conflict;
	}

	return Ledger.Num() >= MaxInteractionRequestLedgerEntries
		? EInteractionRequestDisposition::LedgerFull
		: EInteractionRequestDisposition::NewRequest;
}

bool ASPCharacter::TryRecordInteractionRequestInLedger(
	TMap<uint32, FInteractionRequestRecord>& Ledger,
	const uint32 RequestId,
	const EInteractionRequestAction Action,
	const FGuid& TargetId,
	const uint8 ResultCode)
{
	if (ClassifyInteractionRequest(Ledger, RequestId, Action, TargetId) !=
		EInteractionRequestDisposition::NewRequest)
	{
		return false;
	}

	FInteractionRequestRecord& NewRecord = Ledger.Add(RequestId);
	NewRecord.Action = Action;
	NewRecord.TargetId = TargetId;
	NewRecord.ResultCode = ResultCode;
	return true;
}

void ASPCharacter::NotifyInteractionResult(
	const uint32 RequestId,
	const EInteractionRequestAction Action,
	const uint8 ResultCode)
{
	if (Action == EInteractionRequestAction::Pickup)
	{
		ClientNotifyPickupResult(RequestId, static_cast<ESPPickupResultCode>(ResultCode));
	}
	else
	{
		ClientNotifyScrollUseResult(RequestId, static_cast<ESPScrollUseResultCode>(ResultCode));
	}
}

void ASPCharacter::BeginLocalPickupRequest(const uint32 RequestId)
{
	PendingPickupRequestId = RequestId;
	PickupFeedbackExpiresAt = 0.0;
	ScrollUseFeedbackExpiresAt = 0.0;
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

void ASPCharacter::BeginLocalScrollUseRequest(const uint32 RequestId)
{
	PendingScrollUseRequestId = RequestId;
	ScrollUseFeedbackExpiresAt = 0.0;
	PickupFeedbackExpiresAt = 0.0;
	bScrollUseFeedbackTimedOut = false;

	GetWorldTimerManager().ClearTimer(ScrollUseRequestTimeoutHandle);
	FTimerDelegate TimeoutDelegate = FTimerDelegate::CreateUObject(
		this, &ASPCharacter::HandleScrollUseRequestTimeout, RequestId);
	GetWorldTimerManager().SetTimer(
		ScrollUseRequestTimeoutHandle,
		MoveTemp(TimeoutDelegate),
		ScrollUseRequestTimeoutSeconds,
		false);
}

void ASPCharacter::HandleScrollUseRequestTimeout(const uint32 RequestId)
{
	if (PendingScrollUseRequestId != RequestId)
	{
		return;
	}

	PendingScrollUseRequestId = 0;
	ShowScrollUseFeedback(ESPScrollUseResultCode::ServerError, true);
	UE_LOG(LogScrollPeddler, Warning,
		TEXT("[SP_SCROLL_USE_FEEDBACK_TIMEOUT] Player=%s RequestId=%u"),
		*GetNameSafe(this), RequestId);
}

void ASPCharacter::ShowScrollUseFeedback(
	const ESPScrollUseResultCode ResultCode,
	const bool bTimedOut)
{
	if (!GetWorld())
	{
		return;
	}

	LastScrollUseResult = ResultCode;
	bScrollUseFeedbackTimedOut = bTimedOut;
	ScrollUseFeedbackExpiresAt = GetWorld()->GetTimeSeconds() + ScrollUseResultFeedbackSeconds;
}

uint32 ASPCharacter::AllocateInteractionRequestId()
{
	return AllocateInteractionRequestIdFromCounter(NextInteractionRequestId);
}

uint32 ASPCharacter::AllocateInteractionRequestIdFromCounter(uint32& NextRequestId)
{
	if (NextRequestId == 0)
	{
		NextRequestId = 1;
	}

	const uint32 RequestId = NextRequestId++;
	if (NextRequestId == 0)
	{
		NextRequestId = 1;
	}
	return RequestId;
}

bool ASPCharacter::IsPickupWithinRange(
	const FVector& CharacterLocation,
	const FVector& PickupLocation)
{
	return FVector::DistSquared(CharacterLocation, PickupLocation) <= FMath::Square(MaxPickupDistance);
}

void ASPCharacter::OnRep_SilenceEndServerTime()
{
	UE_LOG(LogScrollPeddler, Verbose, TEXT("[SP_TECH_SPIKE_SILENCE_REPLICATED] Player=%s SilenceEnd=%.3f"),
		*GetNameSafe(this), SilenceEndServerTime);
}
