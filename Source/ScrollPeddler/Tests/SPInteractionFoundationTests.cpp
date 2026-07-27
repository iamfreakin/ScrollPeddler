#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/SPInteractionTypes.h"
#include "World/SPInteractable.h"
#include "World/SPScrollPickup.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPInteractionReplayLedgerTest,
	"ScrollPeddler.Interaction.ReplayLedger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPInteractionReplayLedgerTest::RunTest(const FString& Parameters)
{
	FSPInteractionReplayLedger Ledger(2);

	FSPInteractionRequest First;
	First.RequestId = 1;
	First.Action = ESPInteractionAction::Pickup;
	First.TargetInstanceId = FGuid::NewGuid();
	First.ExpectedRevision = 3;

	FSPInteractionResult FirstResult;
	FirstResult.RequestId = First.RequestId;
	FirstResult.Action = First.Action;
	FirstResult.Code = ESPInteractionResultCode::Success;
	FirstResult.AuthoritativeRevision = 4;

	TestTrue(TEXT("A valid terminal result records"), Ledger.Record(First, FirstResult));

	FSPInteractionResult ReplayResult;
	TestEqual(
		TEXT("An exact retry resolves as a replay"),
		Ledger.Find(First, ReplayResult),
		FSPInteractionReplayLedger::ELookup::ExactReplay);
	TestEqual(TEXT("The original authoritative revision is replayed"),
		ReplayResult.AuthoritativeRevision, 4);

	FSPInteractionRequest Conflict = First;
	Conflict.Action = ESPInteractionAction::Drop;
	TestEqual(
		TEXT("Reusing an id for different intent is rejected"),
		Ledger.Find(Conflict, ReplayResult),
		FSPInteractionReplayLedger::ELookup::Conflict);
	TestFalse(TEXT("A conflicting request cannot replace the record"),
		Ledger.Record(Conflict, FirstResult));

	FSPInteractionRequest Second = First;
	Second.RequestId = 2;
	FSPInteractionResult SecondResult = FirstResult;
	SecondResult.RequestId = 2;
	TestTrue(TEXT("Second request records"), Ledger.Record(Second, SecondResult));

	FSPInteractionRequest Third = First;
	Third.RequestId = 3;
	FSPInteractionResult ThirdResult = FirstResult;
	ThirdResult.RequestId = 3;
	TestTrue(TEXT("Third request records and trims the oldest"), Ledger.Record(Third, ThirdResult));
	TestEqual(TEXT("Capacity remains bounded"), Ledger.Num(), 2);
	TestEqual(
		TEXT("The oldest request is evicted"),
		Ledger.Find(First, ReplayResult),
		FSPInteractionReplayLedger::ELookup::NotFound);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPScrollPickupInteractionContractTest,
	"ScrollPeddler.Interaction.ScrollPickupContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPScrollPickupInteractionContractTest::RunTest(const FString& Parameters)
{
	const ASPScrollPickup* PickupCDO = GetDefault<ASPScrollPickup>();
	TestTrue(
		TEXT("Scroll pickup exposes the shared interactable interface"),
		PickupCDO->GetClass()->ImplementsInterface(USPInteractable::StaticClass()));

	FSPInteractionRequest InvalidRequest;
	TestEqual(
		TEXT("An empty request is rejected before mutation"),
		PickupCDO->ValidateInteraction(nullptr, InvalidRequest),
		ESPInteractionResultCode::InvalidRequest);
	return true;
}

#endif
