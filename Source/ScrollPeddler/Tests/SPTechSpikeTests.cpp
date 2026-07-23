#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/SPTypes.h"
#include "Camera/CameraComponent.h"
#include "Data/SPScrollDefinition.h"
#include "Data/SPScrollEngravingDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/StaticMesh.h"
#include "Game/SPGameMode.h"
#include "Game/SPPlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Persistence/SPSaveGame.h"
#include "Player/SPCharacter.h"
#include "Player/SPInventoryComponent.h"
#include "UI/SPHUD.h"
#include "World/SPScrollPickup.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPScrollAxesRemainIndependentTest,
	"ScrollPeddler.Data.ScrollAxesRemainIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPScrollAxesRemainIndependentTest::RunTest(const FString& Parameters)
{
	FSPScrollInstance Instance;
	Instance.InstanceId = FGuid::NewGuid();
	Instance.BaseDefinitionId = FPrimaryAssetId(FPrimaryAssetType(TEXT("SPScroll")), TEXT("Family"));
	Instance.EngravingDefinitionId = FPrimaryAssetId(FPrimaryAssetType(TEXT("SPScrollEngraving")), TEXT("Stable"));
	Instance.Quality = ESPScrollQuality::S;
	Instance.Contamination = 37.0f;
	Instance.Misfire = ESPMisfireType::ExtraNoise;

	TestTrue(TEXT("A fully identified instance is valid"), Instance.IsValid());
	TestEqual(TEXT("Engraving stays independent from quality"), Instance.Quality, ESPScrollQuality::S);
	TestEqual(TEXT("Contamination stays independent from engraving"), Instance.Contamination, 37.0f);
	TestEqual(TEXT("Misfire stays on its own axis"), Instance.Misfire, ESPMisfireType::ExtraNoise);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPScrollDataAssetCompositionTest,
	"ScrollPeddler.Data.PrimaryAssetComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPScrollDataAssetCompositionTest::RunTest(const FString& Parameters)
{
	UAssetManager& AssetManager = UAssetManager::Get();
	const FPrimaryAssetId ExpectedScrollId(
		USPScrollDefinition::PrimaryAssetType, TEXT("DA_Scroll_VeilOfSilence"));
	const FPrimaryAssetId ExpectedAmplifiedId(
		USPScrollEngravingDefinition::PrimaryAssetType, TEXT("DA_Engraving_Amplified"));
	const FPrimaryAssetId ExpectedStableId(
		USPScrollEngravingDefinition::PrimaryAssetType, TEXT("DA_Engraving_Stable"));
	TestTrue(TEXT("AssetManager registers the base family"),
		AssetManager.GetPrimaryAssetPath(ExpectedScrollId).IsValid());
	TestTrue(TEXT("AssetManager registers the amplified engraving"),
		AssetManager.GetPrimaryAssetPath(ExpectedAmplifiedId).IsValid());
	TestTrue(TEXT("AssetManager registers the stable engraving"),
		AssetManager.GetPrimaryAssetPath(ExpectedStableId).IsValid());

	const USPScrollDefinition* Scroll = LoadObject<USPScrollDefinition>(
		nullptr,
		TEXT("/Game/Data/Scrolls/DA_Scroll_VeilOfSilence.DA_Scroll_VeilOfSilence"));
	const USPScrollEngravingDefinition* Amplified = LoadObject<USPScrollEngravingDefinition>(
		nullptr,
		TEXT("/Game/Data/Engravings/DA_Engraving_Amplified.DA_Engraving_Amplified"));
	const USPScrollEngravingDefinition* Stable = LoadObject<USPScrollEngravingDefinition>(
		nullptr,
		TEXT("/Game/Data/Engravings/DA_Engraving_Stable.DA_Engraving_Stable"));

	TestNotNull(TEXT("The base scroll family asset loads"), Scroll);
	TestNotNull(TEXT("The amplified engraving asset loads"), Amplified);
	TestNotNull(TEXT("The stable engraving asset loads"), Stable);
	if (!Scroll || !Amplified || !Stable)
	{
		return false;
	}

	TestEqual(TEXT("The spike family exposes exactly two engravings"), Scroll->AllowedEngravings.Num(), 2);
	TestTrue(TEXT("Amplified is allowed"), Scroll->AllowsEngraving(Amplified));
	TestTrue(TEXT("Stable is allowed"), Scroll->AllowsEngraving(Stable));
	TestFalse(TEXT("The family defines pickup presentation art"), Scroll->PickupMesh.IsNull());
	TestEqual(TEXT("The family uses the shared test-blockout presentation"),
		Scroll->PickupMesh.ToSoftObjectPath().ToString(),
		FString(TEXT("/Game/Art/TestAssets/Props/Scrolls/ScrollPickup/SM_ScrollPickup_TestBlockout.SM_ScrollPickup_TestBlockout")));
	TestNotNull(TEXT("The pickup presentation mesh is loadable"), Scroll->PickupMesh.LoadSynchronous());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPScrollPickupComponentContractTest,
	"ScrollPeddler.World.ScrollPickupComponentContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPScrollPickupComponentContractTest::RunTest(const FString& Parameters)
{
	const ASPScrollPickup* PickupCDO = GetDefault<ASPScrollPickup>();
	const UBoxComponent* InteractionBounds = Cast<UBoxComponent>(PickupCDO->GetRootComponent());
	const UStaticMeshComponent* PickupVisual = PickupCDO->FindComponentByClass<UStaticMeshComponent>();

	TestNotNull(TEXT("Interaction bounds are the pickup root"), InteractionBounds);
	TestNotNull(TEXT("Pickup visual is a separate static mesh component"), PickupVisual);
	if (!InteractionBounds || !PickupVisual)
	{
		return false;
	}

	TestEqual(TEXT("Interaction bounds use query-only collision"),
		InteractionBounds->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestEqual(TEXT("Interaction bounds are world-dynamic"),
		InteractionBounds->GetCollisionObjectType(), ECC_WorldDynamic);
	TestEqual(TEXT("Visibility traces hit the interaction bounds"),
		InteractionBounds->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
	TestEqual(TEXT("Pawn collision preserves the overlap policy"),
		InteractionBounds->GetCollisionResponseToChannel(ECC_Pawn), ECR_Overlap);
	TestFalse(TEXT("Interaction bounds do not generate overlap events"),
		InteractionBounds->GetGenerateOverlapEvents());
	TestTrue(TEXT("Interaction bounds match the scroll pickup envelope"),
		InteractionBounds->GetUnscaledBoxExtent().Equals(FVector(26.0f, 10.0f, 10.0f)));
	TestEqual(TEXT("Pickup art never participates in collision"),
		PickupVisual->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Pickup art is attached below the interaction bounds"),
		PickupVisual->GetAttachParent() == InteractionBounds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPFirstPersonPresentationContractTest,
	"ScrollPeddler.Player.FirstPersonPresentationContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPFirstPersonPresentationContractTest::RunTest(const FString& Parameters)
{
	const ASPCharacter* CharacterCDO = GetDefault<ASPCharacter>();
	const UCameraComponent* Camera = CharacterCDO->GetFirstPersonCamera();
	const USkeletalMeshComponent* Hands = CharacterCDO->GetFirstPersonHands();
	const UStaticMeshComponent* RemoteBody = CharacterCDO->GetRemoteBodyMesh();
	const UCharacterMovementComponent* Movement = CharacterCDO->GetCharacterMovement();

	TestNotNull(TEXT("First-person camera exists"), Camera);
	TestNotNull(TEXT("First-person hands slot exists"), Hands);
	TestNotNull(TEXT("Remote body presentation exists"), RemoteBody);
	TestNotNull(TEXT("Character movement exists"), Movement);
	TestNull(TEXT("Third-person spring arm is removed"),
		CharacterCDO->FindComponentByClass<USpringArmComponent>());
	if (!Camera || !Hands || !RemoteBody || !Movement)
	{
		return false;
	}

	TestEqual(TEXT("Eye height is 64 cm"), CharacterCDO->BaseEyeHeight, 64.0f);
	TestTrue(TEXT("Character yaw follows the controller"), CharacterCDO->bUseControllerRotationYaw);
	TestFalse(TEXT("Movement does not rotate toward velocity"), Movement->bOrientRotationToMovement);
	TestTrue(TEXT("Camera is attached directly to the capsule"),
		Camera->GetAttachParent() == CharacterCDO->GetCapsuleComponent());
	TestEqual(TEXT("Camera field of view is 90 degrees"), Camera->FieldOfView, 90.0f);
	TestTrue(TEXT("Camera consumes pawn control rotation"), Camera->bUsePawnControlRotation);

	TestTrue(TEXT("Hands attach below the camera"), Hands->GetAttachParent() == Camera);
	TestNull(TEXT("Hands intentionally start without an art mesh"), Hands->GetSkeletalMeshAsset());
	TestTrue(TEXT("Hands render only for their owning player"), Hands->bOnlyOwnerSee);
	TestFalse(TEXT("Hands do not hide from their owner"), Hands->bOwnerNoSee);
	TestEqual(TEXT("Hands never participate in collision"),
		Hands->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("Hands do not cast shadows"), Hands->CastShadow);
	TestFalse(TEXT("Hands component does not replicate"), Hands->GetIsReplicated());

	TestTrue(TEXT("Remote body hides from its owner"), RemoteBody->bOwnerNoSee);
	TestFalse(TEXT("Remote body is not owner-only"), RemoteBody->bOnlyOwnerSee);
	TestEqual(TEXT("Remote body never participates in collision"),
		RemoteBody->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestFalse(TEXT("Remote body component does not replicate"), RemoteBody->GetIsReplicated());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPInteractionRequestLedgerTest,
	"ScrollPeddler.Network.InteractionRequestLedger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPInteractionRequestLedgerTest::RunTest(const FString& Parameters)
{
	using EAction = ASPCharacter::EInteractionRequestAction;
	using EDisposition = ASPCharacter::EInteractionRequestDisposition;
	using FRecord = ASPCharacter::FInteractionRequestRecord;

	const FGuid TargetA = FGuid::NewGuid();
	const FGuid TargetB = FGuid::NewGuid();
	TMap<uint32, FRecord> Ledger;

	TestEqual(TEXT("A nonzero unknown id is a new request"),
		ASPCharacter::ClassifyInteractionRequest(Ledger, 1, EAction::Pickup, TargetA),
		EDisposition::NewRequest);
	TestEqual(TEXT("Zero is always invalid"),
		ASPCharacter::ClassifyInteractionRequest(Ledger, 0, EAction::Pickup, TargetA),
		EDisposition::InvalidRequestId);

	TestTrue(TEXT("The first result is recorded"),
		ASPCharacter::TryRecordInteractionRequestInLedger(
			Ledger,
			1,
			EAction::Pickup,
			TargetA,
			static_cast<uint8>(ESPPickupResultCode::OutOfRange)));
	TestEqual(TEXT("An identical action and target replays"),
		ASPCharacter::ClassifyInteractionRequest(Ledger, 1, EAction::Pickup, TargetA),
		EDisposition::ExactReplay);
	TestEqual(TEXT("The replay retains the first result"),
		Ledger.FindChecked(1).ResultCode,
		static_cast<uint8>(ESPPickupResultCode::OutOfRange));
	TestEqual(TEXT("A different target conflicts"),
		ASPCharacter::ClassifyInteractionRequest(Ledger, 1, EAction::Pickup, TargetB),
		EDisposition::Conflict);
	TestEqual(TEXT("A different action conflicts"),
		ASPCharacter::ClassifyInteractionRequest(Ledger, 1, EAction::UseScroll, TargetA),
		EDisposition::Conflict);
	TestFalse(TEXT("A replay cannot overwrite its first result"),
		ASPCharacter::TryRecordInteractionRequestInLedger(
			Ledger,
			1,
			EAction::Pickup,
			TargetA,
			static_cast<uint8>(ESPPickupResultCode::Success)));
	TestEqual(TEXT("The original result remains unchanged"),
		Ledger.FindChecked(1).ResultCode,
		static_cast<uint8>(ESPPickupResultCode::OutOfRange));

	TMap<uint32, FRecord> FullLedger;
	bool bFilledLedger = true;
	for (uint32 RequestId = 1;
		RequestId <= static_cast<uint32>(ASPCharacter::MaxInteractionRequestLedgerEntries);
		++RequestId)
	{
		bFilledLedger &= ASPCharacter::TryRecordInteractionRequestInLedger(
			FullLedger,
			RequestId,
			EAction::Pickup,
			TargetA,
			static_cast<uint8>(ESPPickupResultCode::Success));
	}
	TestTrue(TEXT("Exactly 1024 results fit in the ledger"), bFilledLedger);
	TestEqual(TEXT("The ledger reaches its fixed session limit"),
		FullLedger.Num(), ASPCharacter::MaxInteractionRequestLedgerEntries);
	TestEqual(TEXT("A new id is rejected after the limit"),
		ASPCharacter::ClassifyInteractionRequest(
			FullLedger,
			ASPCharacter::MaxInteractionRequestLedgerEntries + 1,
			EAction::Pickup,
			TargetA),
		EDisposition::LedgerFull);
	TestEqual(TEXT("An exact replay still resolves when the ledger is full"),
		ASPCharacter::ClassifyInteractionRequest(FullLedger, 1, EAction::Pickup, TargetA),
		EDisposition::ExactReplay);
	TestEqual(TEXT("A conflicting replay still resolves when the ledger is full"),
		ASPCharacter::ClassifyInteractionRequest(FullLedger, 1, EAction::Pickup, TargetB),
		EDisposition::Conflict);
	TestFalse(TEXT("The full ledger does not mutate"),
		ASPCharacter::TryRecordInteractionRequestInLedger(
			FullLedger,
			ASPCharacter::MaxInteractionRequestLedgerEntries + 1,
			EAction::Pickup,
			TargetA,
			static_cast<uint8>(ESPPickupResultCode::Success)));
	TestEqual(TEXT("Rejected ids do not increase the ledger"),
		FullLedger.Num(), ASPCharacter::MaxInteractionRequestLedgerEntries);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPInteractionRequestIdTest,
	"ScrollPeddler.Network.InteractionRequestIdIsSharedAndNonZero",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPInteractionRequestIdTest::RunTest(const FString& Parameters)
{
	uint32 NextRequestId = 1;
	TestEqual(TEXT("The first interaction receives id 1"),
		ASPCharacter::AllocateInteractionRequestIdFromCounter(NextRequestId), 1u);
	TestEqual(TEXT("The next interaction shares the same sequence"),
		ASPCharacter::AllocateInteractionRequestIdFromCounter(NextRequestId), 2u);

	NextRequestId = MAX_uint32;
	TestEqual(TEXT("The maximum id remains usable"),
		ASPCharacter::AllocateInteractionRequestIdFromCounter(NextRequestId), MAX_uint32);
	TestEqual(TEXT("The counter skips zero after wrapping"),
		ASPCharacter::AllocateInteractionRequestIdFromCounter(NextRequestId), 1u);

	NextRequestId = 0;
	TestEqual(TEXT("A corrupted zero counter is repaired"),
		ASPCharacter::AllocateInteractionRequestIdFromCounter(NextRequestId), 1u);
	TestEqual(TEXT("The repaired sequence continues normally"), NextRequestId, 2u);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPPickupDistanceBoundaryTest,
	"ScrollPeddler.Network.PickupDistanceBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPPickupDistanceBoundaryTest::RunTest(const FString& Parameters)
{
	const FVector Origin = FVector::ZeroVector;
	TestTrue(TEXT("A pickup at exactly 350 cm is in range"),
		ASPCharacter::IsPickupWithinRange(Origin, FVector(350.0f, 0.0f, 0.0f)));
	TestTrue(TEXT("A pickup just inside 350 cm is in range"),
		ASPCharacter::IsPickupWithinRange(Origin, FVector(349.99f, 0.0f, 0.0f)));
	TestFalse(TEXT("A pickup just beyond 350 cm is out of range"),
		ASPCharacter::IsPickupWithinRange(Origin, FVector(350.01f, 0.0f, 0.0f)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPInventoryCapacityBoundaryTest,
	"ScrollPeddler.Inventory.CapacityBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPInventoryCapacityBoundaryTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("A fourth item can enter a four-slot inventory"),
		USPInventoryComponent::HasCapacityForCount(3, 4));
	TestFalse(TEXT("A fifth item cannot enter a four-slot inventory"),
		USPInventoryComponent::HasCapacityForCount(4, 4));
	TestFalse(TEXT("An already overfilled inventory remains closed"),
		USPInventoryComponent::HasCapacityForCount(5, 4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPPlayerStatePickupRollbackTest,
	"ScrollPeddler.PlayerState.PickupRollbackAndDuplicateSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPPlayerStatePickupRollbackTest::RunTest(const FString& Parameters)
{
	AddExpectedErrorPlain(
		TEXT("SP_SPIKE_PICKUP_RECORD_REJECTED duplicate=1"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	AddExpectedErrorPlain(
		TEXT("SP_SPIKE_PICKUP_RECORD_ROLLBACK_REJECTED"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	AddExpectedErrorPlain(
		TEXT("SP_SPIKE_PICKUP_RECORD_ROLLED_BACK"),
		EAutomationExpectedErrorFlags::Contains,
		1);

	const USPScrollDefinition* Scroll = LoadObject<USPScrollDefinition>(
		nullptr,
		TEXT("/Game/Data/Scrolls/DA_Scroll_VeilOfSilence.DA_Scroll_VeilOfSilence"));
	TestNotNull(TEXT("The accounting fixture definition loads"), Scroll);
	if (!Scroll)
	{
		return false;
	}

	FSPScrollInstance Item;
	Item.InstanceId = FGuid::NewGuid();
	Item.BaseDefinitionId = Scroll->GetPrimaryAssetId();
	Item.EngravingDefinitionId = FPrimaryAssetId(
		USPScrollEngravingDefinition::PrimaryAssetType,
		TEXT("DA_Engraving_Stable"));

	ASPPlayerState* PlayerState = NewObject<ASPPlayerState>();
	TestNotNull(TEXT("A transient PlayerState can host authority accounting"), PlayerState);
	TestTrue(TEXT("The transient PlayerState uses the authority role"), PlayerState->HasAuthority());
	if (!PlayerState || !PlayerState->HasAuthority())
	{
		return false;
	}

	TestTrue(TEXT("The first pickup is recorded"), PlayerState->RecordScrollPickedUp(Item));
	TestEqual(TEXT("Pickup count increments once"), PlayerState->GetPickedUpCount(), 1);
	TestEqual(TEXT("Carried value matches the definition"),
		PlayerState->GetCarriedDeliveryValue(), FMath::Max(0, Scroll->DeliveryValue));

	TestFalse(TEXT("A duplicate pickup is rejected"), PlayerState->RecordScrollPickedUp(Item));
	TestEqual(TEXT("Duplicate pickup does not increment the count"), PlayerState->GetPickedUpCount(), 1);
	TestEqual(TEXT("Duplicate pickup does not add delivery value"),
		PlayerState->GetCarriedDeliveryValue(), FMath::Max(0, Scroll->DeliveryValue));

	TestTrue(TEXT("The authoritative pickup record rolls back"), PlayerState->RollbackScrollPickedUp(Item));
	TestEqual(TEXT("Rollback restores the pickup count"), PlayerState->GetPickedUpCount(), 0);
	TestEqual(TEXT("Rollback restores carried value"), PlayerState->GetCarriedDeliveryValue(), 0);
	TestFalse(TEXT("A repeated rollback is rejected"), PlayerState->RollbackScrollPickedUp(Item));
	TestEqual(TEXT("Repeated rollback leaves accounting unchanged"), PlayerState->GetPickedUpCount(), 0);

	TestTrue(TEXT("The same stable id can be recorded after rollback"),
		PlayerState->RecordScrollPickedUp(Item));
	TestEqual(TEXT("Re-recording after rollback applies exactly once"), PlayerState->GetPickedUpCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPPlayerStateConsumeInactiveTest,
	"ScrollPeddler.PlayerState.ConsumeDuplicateAndInactiveSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPPlayerStateConsumeInactiveTest::RunTest(const FString& Parameters)
{
	AddExpectedErrorPlain(
		TEXT("SP_SPIKE_CONSUME_RECORD_REJECTED duplicate=1"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	AddExpectedErrorPlain(
		TEXT("SP_SPIKE_PICKUP_RECORD_REJECTED authority=1 extracted=1 valid=1"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	AddExpectedErrorPlain(
		TEXT("SP_SPIKE_CONSUME_RECORD_REJECTED authority=1 extracted=1 valid=1"),
		EAutomationExpectedErrorFlags::Contains,
		1);

	const USPScrollDefinition* Scroll = LoadObject<USPScrollDefinition>(
		nullptr,
		TEXT("/Game/Data/Scrolls/DA_Scroll_VeilOfSilence.DA_Scroll_VeilOfSilence"));
	TestNotNull(TEXT("The consume fixture definition loads"), Scroll);
	if (!Scroll)
	{
		return false;
	}

	FSPScrollInstance Item;
	Item.InstanceId = FGuid::NewGuid();
	Item.BaseDefinitionId = Scroll->GetPrimaryAssetId();
	Item.EngravingDefinitionId = FPrimaryAssetId(
		USPScrollEngravingDefinition::PrimaryAssetType,
		TEXT("DA_Engraving_Stable"));

	ASPPlayerState* PlayerState = NewObject<ASPPlayerState>();
	TestNotNull(TEXT("A transient PlayerState can host consume accounting"), PlayerState);
	TestTrue(TEXT("The consume fixture uses the authority role"), PlayerState->HasAuthority());
	if (!PlayerState || !PlayerState->HasAuthority())
	{
		return false;
	}

	const int32 DeliveryValue = FMath::Max(0, Scroll->DeliveryValue);
	TestTrue(TEXT("The consume fixture pickup is recorded"), PlayerState->RecordScrollPickedUp(Item));
	TestTrue(TEXT("The first consume is recorded"),
		PlayerState->RecordScrollConsumed(Item, DeliveryValue));
	TestEqual(TEXT("Consume count increments once"), PlayerState->GetConsumedScrollCount(), 1);
	TestEqual(TEXT("Consume removes carried delivery value"), PlayerState->GetCarriedDeliveryValue(), 0);

	TestFalse(TEXT("A duplicate consume is rejected"),
		PlayerState->RecordScrollConsumed(Item, DeliveryValue));
	TestEqual(TEXT("Duplicate consume does not increment the count"),
		PlayerState->GetConsumedScrollCount(), 1);
	TestEqual(TEXT("Duplicate consume does not subtract value twice"),
		PlayerState->GetCarriedDeliveryValue(), 0);

	PlayerState->MarkExtracted();
	TestTrue(TEXT("Extraction makes the player inactive"), PlayerState->IsExtracted());
	TestEqual(TEXT("Consumed inventory produces no extracted scrolls"),
		PlayerState->GetExtractedScrollCount(), 0);
	TestEqual(TEXT("Consumed inventory produces no extraction gold"), PlayerState->GetGoldDelta(), 0);

	FSPScrollInstance LateItem = Item;
	LateItem.InstanceId = FGuid::NewGuid();
	TestFalse(TEXT("An inactive player cannot record another pickup"),
		PlayerState->RecordScrollPickedUp(LateItem));
	TestFalse(TEXT("An inactive player cannot record another consume"),
		PlayerState->RecordScrollConsumed(Item, DeliveryValue));
	TestEqual(TEXT("Inactive requests leave pickup accounting unchanged"),
		PlayerState->GetPickedUpCount(), 1);
	TestEqual(TEXT("Inactive requests leave consume accounting unchanged"),
		PlayerState->GetConsumedScrollCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPPickupHUDContractTest,
	"ScrollPeddler.UI.PickupFeedbackContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPPickupHUDContractTest::RunTest(const FString& Parameters)
{
	const ASPGameMode* GameModeCDO = GetDefault<ASPGameMode>();
	TestTrue(TEXT("Game mode installs the pickup HUD"), GameModeCDO->HUDClass == ASPHUD::StaticClass());

	const ESPPickupResultCode ResultCodes[] =
	{
		ESPPickupResultCode::Success,
		ESPPickupResultCode::InvalidRequest,
		ESPPickupResultCode::InactivePlayer,
		ESPPickupResultCode::OutOfRange,
		ESPPickupResultCode::InventoryFull,
		ESPPickupResultCode::Unavailable,
		ESPPickupResultCode::Obstructed,
		ESPPickupResultCode::Contested,
		ESPPickupResultCode::ServerError
	};
	for (const ESPPickupResultCode ResultCode : ResultCodes)
	{
		TestFalse(
			*FString::Printf(TEXT("Result %s has visible HUD copy"),
				*StaticEnum<ESPPickupResultCode>()->GetNameStringByValue(static_cast<int64>(ResultCode))),
			ASPHUD::GetPickupResultMessage(ResultCode).IsEmpty());
	}

	TestEqual(TEXT("Success copy is stable"),
		ASPHUD::GetPickupResultMessage(ESPPickupResultCode::Success), FString(TEXT("PICKED UP")));
	TestEqual(TEXT("Obstruction copy is stable"),
		ASPHUD::GetPickupResultMessage(ESPPickupResultCode::Obstructed), FString(TEXT("BLOCKED")));
	TestEqual(TEXT("Contested pickup copy is stable"),
		ASPHUD::GetPickupResultMessage(ESPPickupResultCode::Contested), FString(TEXT("ALREADY CLAIMED")));
	TestTrue(TEXT("Success uses a distinct green result color"),
		ASPHUD::GetPickupResultColor(ESPPickupResultCode::Success).G >
		ASPHUD::GetPickupResultColor(ESPPickupResultCode::Success).R);
	TestTrue(TEXT("Rejections use a distinct red result color"),
		ASPHUD::GetPickupResultColor(ESPPickupResultCode::ServerError).R >
		ASPHUD::GetPickupResultColor(ESPPickupResultCode::ServerError).G);

	const ESPScrollUseResultCode UseResultCodes[] =
	{
		ESPScrollUseResultCode::Success,
		ESPScrollUseResultCode::InvalidRequest,
		ESPScrollUseResultCode::InactivePlayer,
		ESPScrollUseResultCode::NotOwned,
		ESPScrollUseResultCode::InvalidDefinition,
		ESPScrollUseResultCode::ServerError
	};
	for (const ESPScrollUseResultCode ResultCode : UseResultCodes)
	{
		TestFalse(
			*FString::Printf(TEXT("Scroll-use result %s has visible HUD copy"),
				*StaticEnum<ESPScrollUseResultCode>()->GetNameStringByValue(static_cast<int64>(ResultCode))),
			ASPHUD::GetScrollUseResultMessage(ResultCode).IsEmpty());
	}
	TestEqual(TEXT("Scroll-use success copy is stable"),
		ASPHUD::GetScrollUseResultMessage(ESPScrollUseResultCode::Success), FString(TEXT("SCROLL USED")));
	TestTrue(TEXT("Scroll-use success uses the green result color"),
		ASPHUD::GetScrollUseResultColor(ESPScrollUseResultCode::Success).G >
		ASPHUD::GetScrollUseResultColor(ESPScrollUseResultCode::Success).R);
	TestTrue(TEXT("Scroll-use rejection uses the red result color"),
		ASPHUD::GetScrollUseResultColor(ESPScrollUseResultCode::NotOwned).R >
		ASPHUD::GetScrollUseResultColor(ESPScrollUseResultCode::NotOwned).G);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPSettlementIdempotencyTest,
	"ScrollPeddler.Persistence.SettlementIsIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPSettlementIdempotencyTest::RunTest(const FString& Parameters)
{
	USPSaveGame* Save = NewObject<USPSaveGame>();
	FSPSessionResult Result;
	Result.SessionId = FGuid::NewGuid();
	Result.PlayerId = TEXT("AutomationPlayer");
	Result.PartySize = 2;
	Result.bExtracted = true;
	Result.GoldDelta = 40;
	Result.CompletedAtUnixSeconds = FDateTime::UtcNow().ToUnixTimestamp();
	Result.ResultHash = SPBuildSessionResultHash(Result);

	TestTrue(TEXT("First settlement applies"), Save->ApplyResultIfNew(Result));
	TestTrue(TEXT("Exact replay is an idempotent success"), Save->ApplyResultIfNew(Result));
	TestTrue(TEXT("Session ledger contains the result"), Save->HasCommittedSession(Result.SessionId));
	TestEqual(TEXT("Gold is awarded once"), Save->GetGold(), 40);
	TestEqual(TEXT("Session count increments once"), Save->GetSessionsPlayed(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPSessionResultUnicodeHashTest,
	"ScrollPeddler.Persistence.ResultHashUsesCanonicalUtf8",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPSessionResultUnicodeHashTest::RunTest(const FString& Parameters)
{
	FSPSessionResult First;
	TestTrue(TEXT("Fixed session id parses"), FGuid::Parse(
		TEXT("01234567-89ab-cdef-1020-304050607080"), First.SessionId));
	First.PlayerId = TEXT("상인_가");
	First.BuildVersion = TEXT("0.1.0-tech-spike");
	First.PartySize = 2;
	First.bExtracted = true;
	First.PickedUpCount = 3;
	First.ConsumedScrollCount = 1;
	First.ExtractedScrollCount = 2;
	First.GoldDelta = 40;
	First.CompletedAtUnixSeconds = 1783782000;
	First.ResultHash = SPBuildSessionResultHash(First);
	TestEqual(TEXT("Known Unicode input hashes its canonical UTF-8 bytes"),
		First.ResultHash, FString(TEXT("baf57c21372b1122c1c6ed72ef4c5add")));

	FSPSessionResult Same = First;
	TestEqual(TEXT("Canonical UTF-8 hashing is deterministic"),
		SPBuildSessionResultHash(Same), First.ResultHash);
	TestTrue(TEXT("Unicode result passes integrity validation"),
		SPIsSessionResultIntegrityValid(First));
	TestTrue(TEXT("Exact Unicode identity is idempotent"),
		SPIsIdempotentSessionResult(First, Same));

	FSPSessionResult DifferentPlayer = First;
	DifferentPlayer.PlayerId = TEXT("상인_나");
	TestFalse(TEXT("PlayerId must match even if a hash is copied"),
		SPIsIdempotentSessionResult(First, DifferentPlayer));
	DifferentPlayer.ResultHash = SPBuildSessionResultHash(DifferentPlayer);
	TestNotEqual(TEXT("Distinct Unicode PlayerIds produce distinct UTF-8 checksums"),
		DifferentPlayer.ResultHash, First.ResultHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPSettlementConflictTest,
	"ScrollPeddler.Persistence.SessionIdConflictIsRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPSettlementConflictTest::RunTest(const FString& Parameters)
{
	AddExpectedErrorPlain(
		TEXT("SP_SPIKE_SAVE_CONFLICT"), EAutomationExpectedErrorFlags::Contains, 2);

	USPSaveGame* Save = NewObject<USPSaveGame>();
	FSPSessionResult Original;
	Original.SessionId = FGuid::NewGuid();
	Original.PlayerId = TEXT("상인_원본");
	Original.PartySize = 2;
	Original.bExtracted = true;
	Original.GoldDelta = 40;
	Original.CompletedAtUnixSeconds = FDateTime::UtcNow().ToUnixTimestamp();
	Original.ResultHash = SPBuildSessionResultHash(Original);

	TestTrue(TEXT("Original settlement applies"), Save->ApplyResultIfNew(Original));

	FSPSessionResult DifferentPlayer = Original;
	DifferentPlayer.PlayerId = TEXT("상인_충돌");
	DifferentPlayer.ResultHash = SPBuildSessionResultHash(DifferentPlayer);
	TestFalse(TEXT("Same SessionId with another PlayerId is rejected"),
		Save->ApplyResultIfNew(DifferentPlayer));

	FSPSessionResult DifferentResult = Original;
	DifferentResult.GoldDelta = 400;
	DifferentResult.ResultHash = SPBuildSessionResultHash(DifferentResult);
	TestFalse(TEXT("Same SessionId with another ResultHash is rejected"),
		Save->ApplyResultIfNew(DifferentResult));

	const FSPSessionResult* Stored = Save->FindCommittedResult(Original.SessionId);
	TestNotNull(TEXT("Original result remains committed"), Stored);
	if (Stored)
	{
		TestEqual(TEXT("Stored PlayerId is unchanged"), Stored->PlayerId, Original.PlayerId);
		TestEqual(TEXT("Stored ResultHash is unchanged"), Stored->ResultHash, Original.ResultHash);
	}
	TestEqual(TEXT("Conflicts do not award gold"), Save->GetGold(), 40);
	TestEqual(TEXT("Conflicts do not increment sessions"), Save->GetSessionsPlayed(), 1);
	return true;
}

#endif
