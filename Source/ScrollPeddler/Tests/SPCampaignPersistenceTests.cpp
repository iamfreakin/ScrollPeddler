#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/SPItemTypes.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Online/SPCampaignSubsystem.h"
#include "Persistence/SPCampaignSaveGame.h"
#include "Persistence/SPPlayerProfileSaveGame.h"

namespace SPCampaignPersistenceTests
{
	FSPItemInstance MakeMaterial(const FString& Name)
	{
		FSPItemInstance Item;
		Item.InstanceId = FGuid::NewGuid();
		Item.DefinitionId = FPrimaryAssetId(
			FPrimaryAssetType(TEXT("SPMaterial")),
			FName(*Name));
		Item.Kind = ESPItemKind::Material;
		Item.Quantity = 1;
		Item.MaterialQuality = ESPMaterialQuality::B;
		Item.MaterialContamination = ESPContaminationTier::Clean;
		return Item;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPCampaignTransactionIdempotencyTest,
	"ScrollPeddler.Persistence.Campaign.TransactionAndRunAreIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPCampaignTransactionIdempotencyTest::RunTest(const FString& Parameters)
{
	USPCampaignSaveGame* Campaign = NewObject<USPCampaignSaveGame>();
	TestTrue(
		TEXT("A campaign initializes with an explicit owner"),
		Campaign->InitializeNewCampaign(TEXT("Host_One")));

	FSPCampaignTransaction Transaction;
	Transaction.TransactionId = FGuid::NewGuid();
	Transaction.RunId = FGuid::NewGuid();
	Transaction.ExpectedRevision = 0;
	Transaction.GoldDelta = 80;
	Transaction.GuildXPDelta = 30;
	Transaction.GuildRankOverride = 2;

	TestEqual(
		TEXT("The first transaction applies"),
		Campaign->ApplyTransaction(Transaction),
		ESPCampaignApplyResult::Applied);
	TestEqual(TEXT("Gold applies once"), Campaign->GetGold(), 80);
	TestEqual(TEXT("Guild XP applies once"), Campaign->GetGuildXP(), 30);
	TestEqual(TEXT("Rank override applies"), Campaign->GetGuildRank(), 2);
	TestEqual(TEXT("Revision advances once"), Campaign->GetRevision(), int64(1));
	TestTrue(
		TEXT("Transaction id enters the ledger"),
		Campaign->HasProcessedTransaction(Transaction.TransactionId));
	TestTrue(
		TEXT("Run id enters the ledger"),
		Campaign->HasProcessedRun(Transaction.RunId));

	TestEqual(
		TEXT("An exact transaction retry is an idempotent success"),
		Campaign->ApplyTransaction(Transaction),
		ESPCampaignApplyResult::AlreadyProcessed);
	TestEqual(TEXT("A retry does not award gold"), Campaign->GetGold(), 80);
	TestEqual(TEXT("A retry does not advance revision"), Campaign->GetRevision(), int64(1));

	FSPCampaignTransaction DuplicateRun;
	DuplicateRun.TransactionId = FGuid::NewGuid();
	DuplicateRun.RunId = Transaction.RunId;
	DuplicateRun.ExpectedRevision = Campaign->GetRevision();
	DuplicateRun.GoldDelta = 800;
	TestEqual(
		TEXT("A new transaction cannot settle an already processed run"),
		Campaign->ApplyTransaction(DuplicateRun),
		ESPCampaignApplyResult::AlreadyProcessed);
	TestEqual(TEXT("Duplicate run does not award gold"), Campaign->GetGold(), 80);
	TestFalse(
		TEXT("A rejected duplicate-run transaction does not enter the transaction ledger"),
		Campaign->HasProcessedTransaction(DuplicateRun.TransactionId));

	TArray<uint8> SaveBytes;
	TestTrue(
		TEXT("Campaign serializes through the SaveGame memory pipeline"),
		UGameplayStatics::SaveGameToMemory(Campaign, SaveBytes));
	USPCampaignSaveGame* Reloaded = Cast<USPCampaignSaveGame>(
		UGameplayStatics::LoadGameFromMemory(SaveBytes));
	TestNotNull(TEXT("Serialized campaign reloads with its concrete type"), Reloaded);
	if (Reloaded)
	{
		TestTrue(TEXT("Reloaded campaign remains structurally valid"), Reloaded->IsStructurallyValid());
		TestEqual(TEXT("Reload preserves campaign id"), Reloaded->GetCampaignId(), Campaign->GetCampaignId());
		TestEqual(TEXT("Reload preserves revision"), Reloaded->GetRevision(), Campaign->GetRevision());
		TestTrue(
			TEXT("Reload preserves processed run ids"),
			Reloaded->HasProcessedRun(Transaction.RunId));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPCampaignStorageCapacityTest,
	"ScrollPeddler.Persistence.Campaign.StashCapacityAndUnboundedInbox",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPCampaignStorageCapacityTest::RunTest(const FString& Parameters)
{
	USPCampaignSaveGame* Campaign = NewObject<USPCampaignSaveGame>();
	TestTrue(TEXT("Campaign initializes"), Campaign->InitializeNewCampaign(TEXT("CapacityHost")));

	FSPCampaignTransaction FillStash;
	FillStash.TransactionId = FGuid::NewGuid();
	FillStash.ExpectedRevision = 0;
	for (int32 Index = 0; Index < USPCampaignSaveGame::StashCapacity; ++Index)
	{
		FillStash.StashItemsToAdd.Add(
			SPCampaignPersistenceTests::MakeMaterial(
				FString::Printf(TEXT("Stash_%02d"), Index)));
	}
	TestEqual(
		TEXT("Exactly forty stash slots are accepted"),
		Campaign->ApplyTransaction(FillStash),
		ESPCampaignApplyResult::Applied);
	TestEqual(
		TEXT("Stash contains forty item stacks"),
		Campaign->GetStash().Num(),
		USPCampaignSaveGame::StashCapacity);

	FSPCampaignTransaction FillInbox;
	FillInbox.TransactionId = FGuid::NewGuid();
	FillInbox.ExpectedRevision = Campaign->GetRevision();
	for (int32 Index = 0; Index < USPCampaignSaveGame::StashCapacity + 5; ++Index)
	{
		FillInbox.SettlementItemsToAdd.Add(
			SPCampaignPersistenceTests::MakeMaterial(
				FString::Printf(TEXT("Inbox_%02d"), Index)));
	}
	TestEqual(
		TEXT("Settlement inbox is not constrained by stash capacity"),
		Campaign->ApplyTransaction(FillInbox),
		ESPCampaignApplyResult::Applied);
	TestEqual(
		TEXT("All settlement entries remain pending"),
		Campaign->GetSettlementInbox().Num(),
		USPCampaignSaveGame::StashCapacity + 5);

	const int64 RevisionBeforeOverflow = Campaign->GetRevision();
	const int32 TransactionCountBeforeOverflow =
		Campaign->GetProcessedTransactionIds().Num();
	FSPCampaignTransaction Overflow;
	Overflow.TransactionId = FGuid::NewGuid();
	Overflow.ExpectedRevision = RevisionBeforeOverflow;
	Overflow.StashItemsToAdd.Add(
		SPCampaignPersistenceTests::MakeMaterial(TEXT("Overflow")));
	TestEqual(
		TEXT("A forty-first stash stack is rejected"),
		Campaign->ApplyTransaction(Overflow),
		ESPCampaignApplyResult::StashCapacityExceeded);
	TestEqual(
		TEXT("Capacity failure leaves revision unchanged"),
		Campaign->GetRevision(),
		RevisionBeforeOverflow);
	TestEqual(
		TEXT("Capacity failure does not consume transaction id"),
		Campaign->GetProcessedTransactionIds().Num(),
		TransactionCountBeforeOverflow);
	TestFalse(
		TEXT("Capacity failure leaves no partial stash mutation"),
		Campaign->HasProcessedTransaction(Overflow.TransactionId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPCampaignSlotIsolationTest,
	"ScrollPeddler.Persistence.Campaign.ThreeSlotsAreIsolated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPCampaignSlotIsolationTest::RunTest(const FString& Parameters)
{
	TSet<FString> SlotNames;
	for (int32 SlotIndex = 0;
		SlotIndex < USPCampaignSubsystem::CampaignSlotCount;
		++SlotIndex)
	{
		const FString SlotName = USPCampaignSubsystem::GetCampaignSlotName(SlotIndex);
		TestFalse(
			*FString::Printf(TEXT("Campaign slot %d has a name"), SlotIndex),
			SlotName.IsEmpty());
		TestFalse(
			*FString::Printf(TEXT("Campaign slot %d is unique"), SlotIndex),
			SlotNames.Contains(SlotName));
		TestNotEqual(
			TEXT("Campaign slots never reuse the legacy local profile slot"),
			SlotName,
			FString(TEXT("ScrollPeddler_LocalProfile")));
		SlotNames.Add(SlotName);
	}

	TestEqual(
		TEXT("Exactly three campaign slots are exposed"),
		SlotNames.Num(),
		USPCampaignSubsystem::CampaignSlotCount);
	TestTrue(
		TEXT("Negative slot index is rejected"),
		USPCampaignSubsystem::GetCampaignSlotName(-1).IsEmpty());
	TestTrue(
		TEXT("Fourth slot index is rejected"),
		USPCampaignSubsystem::GetCampaignSlotName(3).IsEmpty());
	TestFalse(
		TEXT("Player profile has an independent name"),
		SlotNames.Contains(USPCampaignSubsystem::GetPlayerProfileSlotName()));
	TestNotEqual(
		TEXT("Player profile does not reuse the legacy local profile"),
		USPCampaignSubsystem::GetPlayerProfileSlotName(),
		FString(TEXT("ScrollPeddler_LocalProfile")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPCampaignPersistenceRollbackTest,
	"ScrollPeddler.Persistence.Campaign.WriteFailureRollsBackMemory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPCampaignPersistenceRollbackTest::RunTest(const FString& Parameters)
{
	AddExpectedErrorPlain(
		TEXT("SP_CAMPAIGN_COMMIT_ROLLED_BACK"),
		EAutomationExpectedErrorFlags::Contains,
		1);

	USPCampaignSaveGame* InitialCampaign = NewObject<USPCampaignSaveGame>();
	TestTrue(
		TEXT("Initial campaign is valid"),
		InitialCampaign->InitializeNewCampaign(TEXT("RollbackHost")));

	UGameInstance* TestGameInstance = NewObject<UGameInstance>(GEngine);
	USPCampaignSubsystem* Subsystem =
		NewObject<USPCampaignSubsystem>(TestGameInstance);
	Subsystem->AdoptCampaignForTesting(InitialCampaign, 0);
	Subsystem->FailNextCampaignPersistenceForTesting();

	FSPCampaignTransaction Transaction;
	Transaction.TransactionId = FGuid::NewGuid();
	Transaction.ExpectedRevision = 0;
	Transaction.GoldDelta = 25;

	ESPCampaignApplyResult ApplyResult = ESPCampaignApplyResult::InvalidCampaign;
	TestEqual(
		TEXT("Injected write failure is reported"),
		Subsystem->CommitCampaignTransaction(Transaction, ApplyResult),
		ESPCampaignPersistenceResult::WriteFailed);
	TestEqual(
		TEXT("The transaction reached the in-memory apply phase"),
		ApplyResult,
		ESPCampaignApplyResult::Applied);

	const USPCampaignSaveGame* RestoredCampaign = Subsystem->GetCurrentCampaign();
	TestNotNull(TEXT("Rollback restores a usable campaign object"), RestoredCampaign);
	if (RestoredCampaign)
	{
		TestTrue(TEXT("Restored campaign is structurally valid"), RestoredCampaign->IsStructurallyValid());
		TestEqual(TEXT("Gold rolls back"), RestoredCampaign->GetGold(), 0);
		TestEqual(TEXT("Revision rolls back"), RestoredCampaign->GetRevision(), int64(0));
		TestFalse(
			TEXT("Failed transaction id is not retained"),
			RestoredCampaign->HasProcessedTransaction(Transaction.TransactionId));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPPlayerProfileShapeTest,
	"ScrollPeddler.Persistence.Profile.LocalDataRemainsNonEconomic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPPlayerProfileShapeTest::RunTest(const FString& Parameters)
{
	USPPlayerProfileSaveGame* Profile = NewObject<USPPlayerProfileSaveGame>();
	TestTrue(
		TEXT("Profile initializes with a player identity"),
		Profile->InitializeNewProfile(TEXT("Guest_One")));

	const FPrimaryAssetId Discovery(
		FPrimaryAssetType(TEXT("SPMonster")),
		TEXT("PaperEater"));
	const FPrimaryAssetId Cosmetic(
		FPrimaryAssetType(TEXT("SPCosmetic")),
		TEXT("ApronBlue"));
	TestTrue(TEXT("Discovery is recorded"), Profile->RecordDiscovery(Discovery));
	TestTrue(
		TEXT("Lifetime stat increments"),
		Profile->IncrementStat(FName(TEXT("RunsJoined")), 2));
	TestTrue(TEXT("Cosmetic unlocks"), Profile->UnlockCosmetic(Cosmetic));
	TestTrue(
		TEXT("Unlocked cosmetic equips"),
		Profile->EquipCosmetic(FName(TEXT("Apron")), Cosmetic));
	TestTrue(
		TEXT("Boolean setting persists"),
		Profile->SetSettingFlag(FName(TEXT("CameraShake")), false));
	TestTrue(TEXT("Profile remains structurally valid"), Profile->IsStructurallyValid());
	TestEqual(TEXT("Profile contains one discovery"), Profile->GetDiscoveries().Num(), 1);
	TestEqual(
		TEXT("Profile stat is cumulative"),
		Profile->GetStats().FindRef(FName(TEXT("RunsJoined"))),
		int64(2));
	TestEqual(TEXT("Profile contains no campaign economy field by contract"), Profile->GetRevision(), int64(5));
	return true;
}

#endif
