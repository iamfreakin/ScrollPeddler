#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "World/SPWorldItem.h"

#include <limits>

namespace
{
FGuid MakeGuid(const TCHAR* Value)
{
	FGuid Guid;
	verify(FGuid::Parse(Value, Guid));
	return Guid;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPWorldItemClaimCommitTest,
	"ScrollPeddler.WorldItem.ClaimCommitIsTokenBoundAndIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPWorldItemClaimCommitTest::RunTest(const FString& Parameters)
{
	FSPWorldItemClaimState State;
	UObject* FirstClaimant = NewObject<UBoxComponent>();
	UObject* SecondClaimant = NewObject<UBoxComponent>();
	const FGuid FirstToken = MakeGuid(TEXT("10000000-0000-0000-0000-000000000001"));
	const FGuid SecondToken = MakeGuid(TEXT("20000000-0000-0000-0000-000000000002"));

	TestTrue(TEXT("Fresh claim state initializes once"), State.Initialize());
	TestFalse(TEXT("Claim state cannot initialize twice"), State.Initialize());
	TestEqual(TEXT("Initialization exposes the item"), State.Lifecycle, ESPWorldItemLifecycleState::Available);
	TestEqual(TEXT("Initialization starts revision one"), State.Revision, 1);

	FGuid ActiveToken;
	TestEqual(TEXT("First claimant reserves the item"),
		State.TryClaim(FirstClaimant, FirstToken, 1, ActiveToken),
		ESPInteractionResultCode::Success);
	TestEqual(TEXT("The server-issued token is returned"), ActiveToken, FirstToken);
	TestEqual(TEXT("Claim advances revision once"), State.Revision, 2);

	FGuid ReplayToken;
	TestEqual(TEXT("Same claimant replay is an idempotent success"),
		State.TryClaim(FirstClaimant, SecondToken, 1, ReplayToken),
		ESPInteractionResultCode::Success);
	TestEqual(TEXT("Replay returns the original token"), ReplayToken, FirstToken);
	TestEqual(TEXT("Replay does not advance revision"), State.Revision, 2);

	FGuid ContestedToken;
	TestEqual(TEXT("A concurrent claimant loses the reservation race"),
		State.TryClaim(SecondClaimant, SecondToken, 2, ContestedToken),
		ESPInteractionResultCode::Contested);
	TestEqual(TEXT("Contested claim cannot advance revision"), State.Revision, 2);
	TestEqual(TEXT("Another claimant cannot commit the active token"),
		State.Commit(SecondClaimant, FirstToken, 2),
		ESPInteractionResultCode::NotOwner);
	TestEqual(TEXT("The owner cannot commit a different token"),
		State.Commit(FirstClaimant, SecondToken, 2),
		ESPInteractionResultCode::RequestConflict);

	TestEqual(TEXT("The exact claimant and token commit"),
		State.Commit(FirstClaimant, FirstToken, 2),
		ESPInteractionResultCode::Success);
	TestEqual(TEXT("Commit makes the item terminal"), State.Lifecycle, ESPWorldItemLifecycleState::Committed);
	TestEqual(TEXT("Commit advances revision once"), State.Revision, 3);
	TestEqual(TEXT("Exact commit replay remains successful"),
		State.Commit(FirstClaimant, FirstToken, 2),
		ESPInteractionResultCode::Success);
	TestEqual(TEXT("Commit replay does not advance revision"), State.Revision, 3);
	TestEqual(TEXT("Committed items cannot roll back"),
		State.Rollback(FirstClaimant, FirstToken, 3),
		ESPInteractionResultCode::InvalidState);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPWorldItemRollbackIsolationTest,
	"ScrollPeddler.WorldItem.RollbackCannotReleaseNewerClaim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPWorldItemRollbackIsolationTest::RunTest(const FString& Parameters)
{
	FSPWorldItemClaimState State;
	UObject* FirstClaimant = NewObject<UBoxComponent>();
	UObject* SecondClaimant = NewObject<UBoxComponent>();
	const FGuid FirstToken = MakeGuid(TEXT("30000000-0000-0000-0000-000000000003"));
	const FGuid SecondToken = MakeGuid(TEXT("40000000-0000-0000-0000-000000000004"));
	TestTrue(TEXT("State initializes"), State.Initialize());

	FGuid ActiveToken;
	TestEqual(TEXT("First claim succeeds"),
		State.TryClaim(FirstClaimant, FirstToken, 1, ActiveToken),
		ESPInteractionResultCode::Success);
	TestEqual(TEXT("Stale rollback cannot release a live claim"),
		State.Rollback(FirstClaimant, FirstToken, 1),
		ESPInteractionResultCode::StaleRevision);
	TestTrue(TEXT("Stale rollback preserves owner"), State.IsClaimedBy(FirstClaimant));
	TestEqual(TEXT("Current rollback succeeds"),
		State.Rollback(FirstClaimant, FirstToken, 2),
		ESPInteractionResultCode::Success);
	TestEqual(TEXT("Rollback makes the item available"), State.Lifecycle, ESPWorldItemLifecycleState::Available);
	TestEqual(TEXT("Rollback advances revision"), State.Revision, 3);
	TestEqual(TEXT("Exact rollback replay is idempotent"),
		State.Rollback(FirstClaimant, FirstToken, 2),
		ESPInteractionResultCode::Success);
	TestEqual(TEXT("Rollback replay does not advance revision"), State.Revision, 3);

	TestEqual(TEXT("A rolled-back token cannot be reused for another claim"),
		State.TryClaim(SecondClaimant, FirstToken, 3, ActiveToken),
		ESPInteractionResultCode::RequestConflict);
	TestEqual(TEXT("Rejected token reuse leaves the item available"),
		State.Lifecycle, ESPWorldItemLifecycleState::Available);
	TestEqual(TEXT("Second claimant can reserve the released item"),
		State.TryClaim(SecondClaimant, SecondToken, 3, ActiveToken),
		ESPInteractionResultCode::Success);
	TestTrue(TEXT("Second claimant now owns the reservation"), State.IsClaimedBy(SecondClaimant));
	TestEqual(TEXT("Old rollback cannot release the new claimant"),
		State.Rollback(FirstClaimant, FirstToken, 4),
		ESPInteractionResultCode::NotOwner);
	TestTrue(TEXT("Newer claim survives the old rollback"), State.IsClaimedBy(SecondClaimant));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPDropPlacementValidationTest,
	"ScrollPeddler.WorldItem.DropPlacementValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPDropPlacementValidationTest::RunTest(const FString& Parameters)
{
	const FVector Source(100.0f, 200.0f, 50.0f);
	const FTransform ValidTransform(FRotator(0.0f, 45.0f, 0.0f), Source + FVector(150.0f, 0.0f, 0.0f));
	TestEqual(TEXT("A finite, close, collision-free transform is accepted"),
		SPValidateDropPlacement(Source, ValidTransform, 200.0f, true),
		ESPDropPlacementResult::Success);
	TestEqual(TEXT("Distance boundary is inclusive"),
		SPValidateDropPlacement(
			Source,
			FTransform(FRotator::ZeroRotator, Source + FVector(200.0f, 0.0f, 0.0f)),
			200.0f,
			true),
		ESPDropPlacementResult::Success);
	TestEqual(TEXT("Out-of-range drop is rejected"),
		SPValidateDropPlacement(
			Source,
			FTransform(FRotator::ZeroRotator, Source + FVector(201.0f, 0.0f, 0.0f)),
			200.0f,
			true),
		ESPDropPlacementResult::OutOfRange);
	TestEqual(TEXT("Authoritative collision denial is preserved"),
		SPValidateDropPlacement(Source, ValidTransform, 200.0f, false),
		ESPDropPlacementResult::Blocked);
	TestEqual(TEXT("Non-positive limits are invalid"),
		SPValidateDropPlacement(Source, ValidTransform, 0.0f, true),
		ESPDropPlacementResult::InvalidDistanceLimit);

	FTransform ZeroScaleTransform = ValidTransform;
	ZeroScaleTransform.SetScale3D(FVector(1.0f, 0.0f, 1.0f));
	TestEqual(TEXT("Degenerate scale is invalid"),
		SPValidateDropPlacement(Source, ZeroScaleTransform, 200.0f, true),
		ESPDropPlacementResult::InvalidTransform);

	FVector InvalidSource = Source;
	InvalidSource.X = std::numeric_limits<double>::quiet_NaN();
	TestEqual(TEXT("Non-finite source is invalid"),
		SPValidateDropPlacement(InvalidSource, ValidTransform, 200.0f, true),
		ESPDropPlacementResult::InvalidSource);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPWorldItemComponentContractTest,
	"ScrollPeddler.WorldItem.ComponentContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPWorldItemComponentContractTest::RunTest(const FString& Parameters)
{
	const ASPWorldItem* WorldItemCDO = GetDefault<ASPWorldItem>();
	const UBoxComponent* InteractionBounds = WorldItemCDO->GetInteractionBounds();
	const UStaticMeshComponent* PickupVisual = WorldItemCDO->GetPickupVisual();
	TestNotNull(TEXT("World item has query bounds"), InteractionBounds);
	TestNotNull(TEXT("World item has replaceable visual"), PickupVisual);
	if (!InteractionBounds || !PickupVisual)
	{
		return false;
	}

	TestTrue(TEXT("World item actor replicates"), WorldItemCDO->GetIsReplicated());
	TestTrue(TEXT("World item transform replicates"), WorldItemCDO->IsReplicatingMovement());
	TestEqual(TEXT("Bounds are query-only"),
		InteractionBounds->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestFalse(TEXT("World items never simulate physics"), InteractionBounds->IsSimulatingPhysics());
	TestEqual(TEXT("Visibility traces reach the interaction bounds"),
		InteractionBounds->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
	TestEqual(TEXT("Pawn contact remains overlap-only"),
		InteractionBounds->GetCollisionResponseToChannel(ECC_Pawn), ECR_Overlap);
	TestEqual(TEXT("Presentation mesh never owns collision"),
		PickupVisual->GetCollisionEnabled(), ECollisionEnabled::NoCollision);
	TestTrue(TEXT("Presentation mesh is attached under interaction bounds"),
		PickupVisual->GetAttachParent() == InteractionBounds);
	return true;
}

#endif
