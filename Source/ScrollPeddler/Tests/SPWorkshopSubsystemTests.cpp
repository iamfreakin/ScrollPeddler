#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Hub/SPWorkshopSubsystem.h"

namespace SPWorkshopSubsystemTests
{
	FSPItemInstance MakeMaterial(
		const FName StableId,
		const int32 Quantity = 1)
	{
		FSPItemInstance Item;
		Item.InstanceId = FGuid::NewGuid();
		Item.DefinitionId = FPrimaryAssetId(
			FPrimaryAssetType(TEXT("SPMaterial")),
			StableId);
		Item.Kind = ESPItemKind::Material;
		Item.Quantity = Quantity;
		Item.MaterialQuality = ESPMaterialQuality::B;
		Item.MaterialContamination = ESPContaminationTier::Clean;
		return Item;
	}

	USPContractDefinition* MakeRecoveryContract(
		const TCHAR* ObjectName,
		const FName StableId,
		const int32 DangerTier = 1)
	{
		USPContractDefinition* Contract =
			NewObject<USPContractDefinition>(
				GetTransientPackageAsObject(),
				FName(ObjectName));
		Contract->StableId = StableId;
		Contract->DisplayName = FText::FromName(StableId);
		Contract->Kind = ESPContractKind::LargeCargoRecovery;
		Contract->DangerTier = DangerTier;
		Contract->CargoStableId = TEXT("SealedLedger");
		return Contract;
	}

	USPCraftingRecipeDefinition* MakeMaterialRecipe(
		const FName IngredientStableId,
		const int32 IngredientQuantity,
		const FName OutputStableId,
		const int32 OutputQuantity)
	{
		USPCraftingRecipeDefinition* Recipe =
			NewObject<USPCraftingRecipeDefinition>(
				GetTransientPackageAsObject(),
				FName(*FString::Printf(
					TEXT("DA_Test_WorkshopRecipe_%s"),
					*FGuid::NewGuid().ToString(EGuidFormats::Digits))));
		Recipe->StableId = TEXT("TestWorkshopRecipe");
		Recipe->DisplayName = FText::FromString(TEXT("Test Workshop Recipe"));
		FSPRecipeIngredient Ingredient;
		Ingredient.ItemStableId = IngredientStableId;
		Ingredient.Quantity = IngredientQuantity;
		Recipe->Ingredients.Add(Ingredient);
		Recipe->OutputItemStableId = OutputStableId;
		Recipe->OutputQuantity = OutputQuantity;
		Recipe->CraftDurationSeconds = 10.0f;
		return Recipe;
	}

	const FSPItemInstance* FindItemByStableId(
		const TArray<FSPItemInstance>& Items,
		const FName StableId)
	{
		return Items.FindByPredicate(
			[StableId](const FSPItemInstance& Item)
			{
				return Item.DefinitionId.PrimaryAssetName == StableId;
			});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPWorkshopContractAuthorityTest,
	"ScrollPeddler.Hub.Workshop.ContractSelectionIsAuthoritativeAndIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPWorkshopContractAuthorityTest::RunTest(const FString& Parameters)
{
	USPCampaignSaveGame* Campaign = NewObject<USPCampaignSaveGame>();
	TestTrue(
		TEXT("Campaign initializes"),
		Campaign->InitializeNewCampaign(TEXT("WorkshopHost")));

	UGameInstance* TestGameInstance = NewObject<UGameInstance>(GEngine);
	TestNotNull(TEXT("Test game instance is available"), TestGameInstance);
	if (!TestGameInstance)
	{
		return false;
	}

	USPCampaignSubsystem* CampaignSubsystem =
		NewObject<USPCampaignSubsystem>(TestGameInstance);
	CampaignSubsystem->AdoptCampaignForTesting(Campaign, 0);

	USPWorkshopSubsystem* Workshop =
		NewObject<USPWorkshopSubsystem>(TestGameInstance);
	Workshop->ConfigureAuthorityForTesting(CampaignSubsystem, true);

	USPContractDefinition* FirstContract =
		SPWorkshopSubsystemTests::MakeRecoveryContract(
			TEXT("DA_Test_FirstContract"),
			TEXT("FirstContract"),
			2);
	USPContractDefinition* OtherContract =
		SPWorkshopSubsystemTests::MakeRecoveryContract(
			TEXT("DA_Test_OtherContract"),
			TEXT("OtherContract"),
			1);

	FSPContractSelectionRequest Selection;
	Selection.TransactionId = FGuid::NewGuid();
	Selection.ExpectedWorkshopRevision = 0;
	Selection.ExpectedCampaignRevision = 0;
	TestEqual(
		TEXT("Server selects one eligible contract"),
		Workshop->SelectContract(FirstContract, Selection),
		ESPWorkshopOperationResult::Success);
	TestTrue(
		TEXT("Selected contract becomes active"),
		Workshop->GetContractState().HasActiveContract());
	TestEqual(
		TEXT("Selection advances workshop revision"),
		Workshop->GetContractState().Revision,
		int64(1));

	TestEqual(
		TEXT("Exact selection retry is idempotent"),
		Workshop->SelectContract(FirstContract, Selection),
		ESPWorkshopOperationResult::AlreadyProcessed);
	TestEqual(
		TEXT("Transaction id cannot be reused for another contract"),
		Workshop->SelectContract(OtherContract, Selection),
		ESPWorkshopOperationResult::TransactionConflict);

	FSPContractSelectionRequest SecondSelection;
	SecondSelection.TransactionId = FGuid::NewGuid();
	SecondSelection.ExpectedWorkshopRevision = 1;
	SecondSelection.ExpectedCampaignRevision = 0;
	TestEqual(
		TEXT("A second active contract is rejected"),
		Workshop->SelectContract(OtherContract, SecondSelection),
		ESPWorkshopOperationResult::ActiveContractExists);

	FSPContractClearRequest Clear;
	Clear.TransactionId = FGuid::NewGuid();
	Clear.ExpectedWorkshopRevision = 1;
	Clear.ExpectedCampaignRevision = 0;
	Clear.ExpectedActiveContractId = FirstContract->GetPrimaryAssetId();
	Workshop->ConfigureAuthorityForTesting(CampaignSubsystem, false);
	TestEqual(
		TEXT("Client authority cannot clear the contract"),
		Workshop->ClearActiveContract(Clear),
		ESPWorkshopOperationResult::NotAuthority);
	TestTrue(
		TEXT("Rejected client mutation leaves the contract active"),
		Workshop->GetContractState().HasActiveContract());

	Workshop->ConfigureAuthorityForTesting(CampaignSubsystem, true);
	TestEqual(
		TEXT("Server clears the expected active contract"),
		Workshop->ClearActiveContract(Clear),
		ESPWorkshopOperationResult::Success);
	TestFalse(
		TEXT("Clear leaves no active contract"),
		Workshop->GetContractState().HasActiveContract());
	TestEqual(
		TEXT("Clear advances workshop revision"),
		Workshop->GetContractState().Revision,
		int64(2));
	TestEqual(
		TEXT("Clear retry is idempotent"),
		Workshop->ClearActiveContract(Clear),
		ESPWorkshopOperationResult::AlreadyProcessed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPWorkshopCraftAtomicityTest,
	"ScrollPeddler.Hub.Workshop.CraftConsumesAndProducesAtomically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPWorkshopCraftAtomicityTest::RunTest(const FString& Parameters)
{
	USPCampaignSaveGame* Campaign = NewObject<USPCampaignSaveGame>();
	TestTrue(TEXT("Campaign initializes"), Campaign->InitializeNewCampaign(TEXT("CraftHost")));

	const FName PaperId(TEXT("ArchivePaper"));
	const FName InkId(TEXT("QuietInk"));
	FSPCampaignTransaction Seed;
	Seed.TransactionId = FGuid::NewGuid();
	Seed.ExpectedRevision = 0;
	Seed.GoldDelta = 100;
	Seed.StashItemsToAdd.Add(
		SPWorkshopSubsystemTests::MakeMaterial(PaperId, 3));
	TestEqual(
		TEXT("Seed economy applies"),
		Campaign->ApplyTransaction(Seed),
		ESPCampaignApplyResult::Applied);

	USPCraftingRecipeDefinition* Recipe =
		SPWorkshopSubsystemTests::MakeMaterialRecipe(
			PaperId,
			2,
			InkId,
			2);
	FSPWorkshopCraftQuote Quote;
	Quote.TransactionId = FGuid::NewGuid();
	Quote.ExpectedCampaignRevision = Campaign->GetRevision();
	Quote.GoldCost = 25;
	Quote.IngredientSource = ESPWorkshopIngredientSource::StashOnly;
	Quote.RequestedDestination =
		ESPWorkshopOutputDestination::PreferStash;
	Quote.ServerGeneratedOutputs.Add(
		SPWorkshopSubsystemTests::MakeMaterial(InkId, 2));

	FSPWorkshopCraftPlan Plan;
	TestEqual(
		TEXT("Valid recipe creates an atomic campaign plan"),
		USPWorkshopSubsystem::BuildCraftPlan(
			*Campaign,
			*Recipe,
			Quote,
			Plan),
		ESPWorkshopOperationResult::Success);
	TestEqual(
		TEXT("Output uses available stash capacity"),
		Plan.ActualDestination,
		ESPWorkshopOutputDestination::PreferStash);
	TestEqual(
		TEXT("Gold cost is part of the same transaction"),
		Plan.CampaignTransaction.GoldDelta,
		-25);

	TestEqual(
		TEXT("Campaign accepts the complete craft"),
		Campaign->ApplyTransaction(Plan.CampaignTransaction),
		ESPCampaignApplyResult::Applied);
	TestEqual(TEXT("Gold is consumed once"), Campaign->GetGold(), 75);

	const FSPItemInstance* Paper =
		SPWorkshopSubsystemTests::FindItemByStableId(
			Campaign->GetStash(),
			PaperId);
	const FSPItemInstance* Ink =
		SPWorkshopSubsystemTests::FindItemByStableId(
			Campaign->GetStash(),
			InkId);
	TestNotNull(TEXT("Partial ingredient stack remains"), Paper);
	TestNotNull(TEXT("Craft output enters stash"), Ink);
	if (Paper)
	{
		TestEqual(TEXT("Exactly two paper are consumed"), Paper->Quantity, 1);
	}
	if (Ink)
	{
		TestEqual(TEXT("Recipe output quantity is preserved"), Ink->Quantity, 2);
	}

	TestEqual(
		TEXT("Craft retry is idempotent"),
		Campaign->ApplyTransaction(Plan.CampaignTransaction),
		ESPCampaignApplyResult::AlreadyProcessed);
	TestEqual(TEXT("Retry cannot consume Gold twice"), Campaign->GetGold(), 75);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPWorkshopFullStashFallbackTest,
	"ScrollPeddler.Hub.Workshop.FullStashFallsBackWithoutItemLoss",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPWorkshopFullStashFallbackTest::RunTest(const FString& Parameters)
{
	USPCampaignSaveGame* Campaign = NewObject<USPCampaignSaveGame>();
	TestTrue(TEXT("Campaign initializes"), Campaign->InitializeNewCampaign(TEXT("FullStashHost")));

	const FName PaperId(TEXT("FullStashPaper"));
	const FName OutputId(TEXT("FullStashOutput"));
	FSPCampaignTransaction Fill;
	Fill.TransactionId = FGuid::NewGuid();
	Fill.ExpectedRevision = 0;
	Fill.GoldDelta = 50;
	Fill.StashItemsToAdd.Add(
		SPWorkshopSubsystemTests::MakeMaterial(PaperId, 2));
	for (int32 Index = 1;
		Index < USPCampaignSaveGame::StashCapacity;
		++Index)
	{
		Fill.StashItemsToAdd.Add(
			SPWorkshopSubsystemTests::MakeMaterial(
				FName(*FString::Printf(TEXT("Filler_%02d"), Index))));
	}
	TestEqual(
		TEXT("Forty stash slots are filled"),
		Campaign->ApplyTransaction(Fill),
		ESPCampaignApplyResult::Applied);
	TestEqual(
		TEXT("Stash starts full"),
		Campaign->GetStash().Num(),
		USPCampaignSaveGame::StashCapacity);

	USPCraftingRecipeDefinition* Recipe =
		SPWorkshopSubsystemTests::MakeMaterialRecipe(
			PaperId,
			1,
			OutputId,
			1);
	FSPWorkshopCraftQuote Quote;
	Quote.TransactionId = FGuid::NewGuid();
	Quote.ExpectedCampaignRevision = Campaign->GetRevision();
	Quote.GoldCost = 10;
	Quote.IngredientSource = ESPWorkshopIngredientSource::StashOnly;
	Quote.RequestedDestination =
		ESPWorkshopOutputDestination::PreferStash;
	Quote.ServerGeneratedOutputs.Add(
		SPWorkshopSubsystemTests::MakeMaterial(OutputId));

	FSPWorkshopCraftPlan Plan;
	TestEqual(
		TEXT("Full stash still produces a valid plan"),
		USPWorkshopSubsystem::BuildCraftPlan(
			*Campaign,
			*Recipe,
			Quote,
			Plan),
		ESPWorkshopOperationResult::Success);
	TestEqual(
		TEXT("Output falls back to the unbounded inbox"),
		Plan.ActualDestination,
		ESPWorkshopOutputDestination::SettlementInbox);
	TestEqual(
		TEXT("Fallback transaction applies atomically"),
		Campaign->ApplyTransaction(Plan.CampaignTransaction),
		ESPCampaignApplyResult::Applied);

	TestEqual(
		TEXT("Ingredient remainder keeps the stash at forty"),
		Campaign->GetStash().Num(),
		USPCampaignSaveGame::StashCapacity);
	TestEqual(
		TEXT("Output is preserved in settlement inbox"),
		Campaign->GetSettlementInbox().Num(),
		1);
	TestNotNull(
		TEXT("Output identity is present in inbox"),
		SPWorkshopSubsystemTests::FindItemByStableId(
			Campaign->GetSettlementInbox(),
			OutputId));
	const FSPItemInstance* Paper =
		SPWorkshopSubsystemTests::FindItemByStableId(
			Campaign->GetStash(),
			PaperId);
	TestNotNull(TEXT("Unconsumed ingredient remains"), Paper);
	if (Paper)
	{
		TestEqual(TEXT("Only required quantity is consumed"), Paper->Quantity, 1);
	}
	TestEqual(TEXT("Gold is charged exactly once"), Campaign->GetGold(), 40);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPWorkshopCraftValidationTest,
	"ScrollPeddler.Hub.Workshop.InvalidCraftNeverCreatesMutation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPWorkshopCraftValidationTest::RunTest(const FString& Parameters)
{
	USPCampaignSaveGame* Campaign = NewObject<USPCampaignSaveGame>();
	TestTrue(TEXT("Campaign initializes"), Campaign->InitializeNewCampaign(TEXT("ValidationHost")));

	USPCraftingRecipeDefinition* Recipe =
		SPWorkshopSubsystemTests::MakeMaterialRecipe(
			TEXT("RequiredPaper"),
			1,
			TEXT("ExpectedOutput"),
			1);
	Recipe->bRequiresCampaignVote = true;

	FSPWorkshopCraftQuote Quote;
	Quote.TransactionId = FGuid::NewGuid();
	Quote.ExpectedCampaignRevision = 0;
	Quote.GoldCost = 0;
	Quote.IngredientSource = ESPWorkshopIngredientSource::StashOnly;
	Quote.ServerGeneratedOutputs.Add(
		SPWorkshopSubsystemTests::MakeMaterial(
			TEXT("ExpectedOutput")));

	FSPWorkshopCraftPlan Plan;
	TestEqual(
		TEXT("Vote-gated recipe rejects an unapproved quote"),
		USPWorkshopSubsystem::BuildCraftPlan(
			*Campaign,
			*Recipe,
			Quote,
			Plan),
		ESPWorkshopOperationResult::VoteRequired);
	TestFalse(
		TEXT("Rejected plan contains no campaign transaction id"),
		Plan.CampaignTransaction.TransactionId.IsValid());

	Quote.bCampaignVoteApproved = true;
	TestEqual(
		TEXT("Missing ingredients reject the craft"),
		USPWorkshopSubsystem::BuildCraftPlan(
			*Campaign,
			*Recipe,
			Quote,
			Plan),
		ESPWorkshopOperationResult::MissingIngredients);
	TestFalse(
		TEXT("Missing ingredients leave no partial mutation plan"),
		Plan.CampaignTransaction.TransactionId.IsValid());
	TestEqual(TEXT("Campaign revision stays unchanged"), Campaign->GetRevision(), int64(0));
	TestEqual(TEXT("Campaign Gold stays unchanged"), Campaign->GetGold(), 0);
	return true;
}

#endif
