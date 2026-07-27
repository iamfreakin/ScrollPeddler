#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Game/SPContractEvaluator.h"

namespace
{
FSPItemInstance MakeItem(
	const ESPItemKind Kind,
	const TCHAR* DefinitionAssetName)
{
	FSPItemInstance Item;
	Item.InstanceId = FGuid::NewGuid();
	Item.DefinitionId = FPrimaryAssetId(
		Kind == ESPItemKind::Scroll
			? FPrimaryAssetType(TEXT("SPScroll"))
			: FPrimaryAssetType(TEXT("SPItem")),
		DefinitionAssetName);
	Item.Kind = Kind;
	Item.Quantity = 1;
	if (Kind == ESPItemKind::Scroll)
	{
		Item.ScrollRoll.EngravingDefinitionId = FPrimaryAssetId(
			FPrimaryAssetType(TEXT("SPScrollEngraving")),
			TEXT("Stable"));
	}
	return Item;
}

FSPContractItemEvidence MakeScrollEvidence(
	const TCHAR* FamilyStableId,
	const TCHAR* EngravingStableId,
	const ESPScrollQuality Quality,
	const float Contamination)
{
	FSPContractItemEvidence Evidence;
	Evidence.Item = MakeItem(ESPItemKind::Scroll, TEXT("TestScroll"));
	Evidence.Item.ScrollRoll.Quality = Quality;
	Evidence.Item.ScrollRoll.Contamination = Contamination;
	Evidence.DefinitionStableId = FamilyStableId;
	Evidence.EngravingStableId = EngravingStableId;
	return Evidence;
}

USPContractDefinition* MakeContract(
	const ESPContractKind Kind,
	const TCHAR* StableId,
	const int32 GoldReward,
	const int32 GuildXpReward)
{
	USPContractDefinition* Contract = NewObject<USPContractDefinition>();
	Contract->StableId = StableId;
	Contract->DisplayName = FText::FromString(StableId);
	Contract->Kind = Kind;
	Contract->GoldReward = GoldReward;
	Contract->GuildXpReward = GuildXpReward;
	return Contract;
}

FSPContractRunEvidence MakeRunEvidence()
{
	FSPContractRunEvidence Evidence;
	Evidence.RunId = FGuid::NewGuid();
	return Evidence;
}

FSPContractPlayerExtraction MakePlayer(
	const TCHAR* PlayerId,
	const bool bExtracted)
{
	FSPContractPlayerExtraction Player;
	Player.PlayerId = PlayerId;
	Player.bExtracted = bExtracted;
	return Player;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPScrollDeliveryEvaluationTest,
	"ScrollPeddler.Contract.ScrollDeliveryEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPScrollDeliveryEvaluationTest::RunTest(const FString& Parameters)
{
	USPContractDefinition* Contract = MakeContract(
		ESPContractKind::ScrollDelivery,
		TEXT("Contract.ScrollDelivery"),
		120,
		35);
	Contract->ScrollCondition.BaseFamilyStableId = TEXT("Scroll.VeilOfSilence");
	Contract->ScrollCondition.AllowedEngravingStableIds =
		{ TEXT("Engraving.Stable"), TEXT("Engraving.Amplified") };
	Contract->ScrollCondition.MinimumQuality = ESPScrollQuality::B;
	Contract->ScrollCondition.MaximumContamination = 30.0f;
	Contract->ScrollCondition.Quantity = 2;

	FSPContractRunEvidence Evidence = MakeRunEvidence();
	FSPContractPlayerExtraction ExtractedPlayer = MakePlayer(TEXT("Player_A"), true);
	const FSPContractItemEvidence FirstMatch = MakeScrollEvidence(
		TEXT("Scroll.VeilOfSilence"),
		TEXT("Engraving.Stable"),
		ESPScrollQuality::B,
		30.0f);
	const FSPContractItemEvidence SecondMatch = MakeScrollEvidence(
		TEXT("Scroll.VeilOfSilence"),
		TEXT("Engraving.Amplified"),
		ESPScrollQuality::S,
		0.0f);
	ExtractedPlayer.Items =
	{
		FirstMatch,
		FirstMatch,
		SecondMatch,
		MakeScrollEvidence(
			TEXT("Scroll.WrongFamily"),
			TEXT("Engraving.Stable"),
			ESPScrollQuality::S,
			0.0f),
		MakeScrollEvidence(
			TEXT("Scroll.VeilOfSilence"),
			TEXT("Engraving.Wrong"),
			ESPScrollQuality::S,
			0.0f),
		MakeScrollEvidence(
			TEXT("Scroll.VeilOfSilence"),
			TEXT("Engraving.Stable"),
			ESPScrollQuality::C,
			0.0f),
		MakeScrollEvidence(
			TEXT("Scroll.VeilOfSilence"),
			TEXT("Engraving.Stable"),
			ESPScrollQuality::S,
			30.01f)
	};

	FSPContractPlayerExtraction MissingPlayer = MakePlayer(TEXT("Player_B"), false);
	MissingPlayer.Items.Add(MakeScrollEvidence(
		TEXT("Scroll.VeilOfSilence"),
		TEXT("Engraving.Stable"),
		ESPScrollQuality::S,
		0.0f));
	Evidence.Players = { ExtractedPlayer, MissingPlayer };

	FSPContractEvaluationResult Result =
		FSPContractEvaluator::Evaluate(*Contract, Evidence);
	TestTrue(TEXT("Two distinct qualifying extracted scrolls succeed"), Result.IsSuccess());
	TestEqual(TEXT("Duplicate item evidence counts once"), Result.MatchedQuantity, 2);
	TestEqual(TEXT("Exactly two item identities are credited"),
		Result.CountedItemInstanceIds.Num(), 2);
	TestEqual(TEXT("Successful delivery grants configured gold"), Result.GoldReward, 120);
	TestEqual(TEXT("Successful delivery grants configured guild XP"), Result.GuildXpReward, 35);

	Contract->ScrollCondition.Quantity = 3;
	Result = FSPContractEvaluator::Evaluate(*Contract, Evidence);
	TestEqual(TEXT("Missing and rejected scrolls cannot satisfy quantity"),
		Result.Outcome, ESPContractEvaluationOutcome::Failed);
	TestEqual(TEXT("Failed delivery reports only two valid matches"), Result.MatchedQuantity, 2);
	TestEqual(TEXT("Failed delivery never grants gold"), Result.GoldReward, 0);
	TestEqual(TEXT("Failed delivery never grants guild XP"), Result.GuildXpReward, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPLargeCargoRecoveryEvaluationTest,
	"ScrollPeddler.Contract.LargeCargoRecoveryEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPLargeCargoRecoveryEvaluationTest::RunTest(const FString& Parameters)
{
	USPContractDefinition* Contract = MakeContract(
		ESPContractKind::LargeCargoRecovery,
		TEXT("Contract.ArchiveRelic"),
		200,
		50);
	Contract->CargoStableId = TEXT("Cargo.ArchiveRelic");

	FSPContractItemEvidence Cargo;
	Cargo.Item = MakeItem(ESPItemKind::LargeCargo, TEXT("ArchiveRelic"));
	Cargo.DefinitionStableId = TEXT("Cargo.ArchiveRelic");

	FSPContractRunEvidence Evidence = MakeRunEvidence();
	FSPContractPlayerExtraction MissingPlayer = MakePlayer(TEXT("Player_A"), false);
	MissingPlayer.Items.Add(Cargo);
	Evidence.Players.Add(MissingPlayer);

	FSPContractEvaluationResult Result =
		FSPContractEvaluator::Evaluate(*Contract, Evidence);
	TestEqual(TEXT("Cargo carried by a missing player does not recover"),
		Result.Outcome, ESPContractEvaluationOutcome::Failed);
	TestEqual(TEXT("Failed recovery grants no gold"), Result.GoldReward, 0);

	FSPContractPlayerExtraction ExtractedPlayer = MakePlayer(TEXT("Player_B"), true);
	ExtractedPlayer.Items = { Cargo, Cargo };
	FSPContractItemEvidence WrongCargo = Cargo;
	WrongCargo.Item.InstanceId = FGuid::NewGuid();
	WrongCargo.DefinitionStableId = TEXT("Cargo.WrongRelic");
	ExtractedPlayer.Items.Add(WrongCargo);
	Evidence.Players.Add(ExtractedPlayer);

	Result = FSPContractEvaluator::Evaluate(*Contract, Evidence);
	TestTrue(TEXT("The exact StableId recovered by an extracted player succeeds"),
		Result.IsSuccess());
	TestEqual(TEXT("Duplicate cargo evidence counts once"), Result.MatchedQuantity, 1);
	TestEqual(TEXT("Exactly one cargo identity is credited"),
		Result.CountedItemInstanceIds.Num(), 1);
	TestEqual(TEXT("Successful recovery grants configured gold"), Result.GoldReward, 200);
	TestEqual(TEXT("Successful recovery grants configured guild XP"), Result.GuildXpReward, 50);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPPostRunProductionEvaluationTest,
	"ScrollPeddler.Contract.PostRunProductionEvaluation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPPostRunProductionEvaluationTest::RunTest(const FString& Parameters)
{
	USPContractDefinition* Contract = MakeContract(
		ESPContractKind::PostRunProduction,
		TEXT("Contract.Production"),
		150,
		40);
	Contract->SubmittedScrollStableId = TEXT("Scroll.VeilOfSilence");
	Contract->ProductionWindowSeconds = 90.0f;

	FSPContractRunEvidence Evidence = MakeRunEvidence();
	Evidence.ProductionWindowOpenedAtSeconds = 1000.0;
	const FGuid SubmittedItemId = FGuid::NewGuid();

	FSPContractProductionReceipt LateReceipt;
	LateReceipt.ReceiptId = FGuid::NewGuid();
	LateReceipt.RunId = Evidence.RunId;
	LateReceipt.SubmittedItemInstanceId = FGuid::NewGuid();
	LateReceipt.SubmittedScrollStableId = Contract->SubmittedScrollStableId;
	LateReceipt.CompletedAtSeconds = 1090.01;
	LateReceipt.bCompleted = true;

	FSPContractProductionReceipt WrongRunReceipt = LateReceipt;
	WrongRunReceipt.ReceiptId = FGuid::NewGuid();
	WrongRunReceipt.RunId = FGuid::NewGuid();
	WrongRunReceipt.CompletedAtSeconds = 1050.0;

	FSPContractProductionReceipt ExactBoundaryReceipt;
	ExactBoundaryReceipt.ReceiptId = FGuid::NewGuid();
	ExactBoundaryReceipt.RunId = Evidence.RunId;
	ExactBoundaryReceipt.SubmittedItemInstanceId = SubmittedItemId;
	ExactBoundaryReceipt.SubmittedScrollStableId =
		Contract->SubmittedScrollStableId;
	ExactBoundaryReceipt.CompletedAtSeconds = 1090.0;
	ExactBoundaryReceipt.bCompleted = true;

	FSPContractProductionReceipt DuplicateItemReceipt =
		ExactBoundaryReceipt;
	DuplicateItemReceipt.ReceiptId = FGuid::NewGuid();
	Evidence.ProductionReceipts =
	{
		LateReceipt,
		WrongRunReceipt,
		ExactBoundaryReceipt,
		ExactBoundaryReceipt,
		DuplicateItemReceipt
	};

	FSPContractEvaluationResult Result =
		FSPContractEvaluator::Evaluate(*Contract, Evidence);
	TestTrue(TEXT("Completion exactly at the production deadline succeeds"),
		Result.IsSuccess());
	TestEqual(TEXT("Duplicated receipt and item count once"), Result.MatchedQuantity, 1);
	TestEqual(TEXT("Only one receipt identity is credited"),
		Result.CountedReceiptIds.Num(), 1);
	TestEqual(TEXT("Only one submitted item identity is credited"),
		Result.CountedItemInstanceIds.Num(), 1);
	TestEqual(TEXT("Successful production grants configured gold"), Result.GoldReward, 150);
	TestEqual(TEXT("Successful production grants configured guild XP"), Result.GuildXpReward, 40);

	Evidence.ProductionReceipts = { LateReceipt, WrongRunReceipt };
	Result = FSPContractEvaluator::Evaluate(*Contract, Evidence);
	TestEqual(TEXT("Late and foreign-run receipts fail production"),
		Result.Outcome, ESPContractEvaluationOutcome::Failed);
	TestEqual(TEXT("Failed production grants no reward"), Result.GoldReward, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPContractInvalidInputEvaluationTest,
	"ScrollPeddler.Contract.InvalidInputHasNoReward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPContractInvalidInputEvaluationTest::RunTest(const FString& Parameters)
{
	USPContractDefinition* Contract = MakeContract(
		ESPContractKind::LargeCargoRecovery,
		TEXT("Contract.Invalid"),
		100,
		20);
	Contract->CargoStableId = TEXT("Cargo.ArchiveRelic");
	FSPContractRunEvidence Evidence;

	FSPContractEvaluationResult Result =
		FSPContractEvaluator::Evaluate(*Contract, Evidence);
	TestEqual(TEXT("Invalid RunId rejects evidence"),
		Result.Outcome, ESPContractEvaluationOutcome::InvalidEvidence);
	TestEqual(TEXT("Invalid evidence grants no gold"), Result.GoldReward, 0);
	TestEqual(TEXT("Invalid evidence grants no guild XP"), Result.GuildXpReward, 0);

	Evidence = MakeRunEvidence();
	Contract->GoldReward = -1;
	Result = FSPContractEvaluator::Evaluate(*Contract, Evidence);
	TestEqual(TEXT("Negative reward rejects the definition"),
		Result.Outcome, ESPContractEvaluationOutcome::InvalidDefinition);
	TestEqual(TEXT("Invalid definition grants no reward"), Result.GoldReward, 0);
	return true;
}

#endif
