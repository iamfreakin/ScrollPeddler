#include "Hub/SPWorkshopSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"

namespace SPWorkshop
{
	FString BuildSelectionFingerprint(
		const USPContractDefinition& Contract,
		const FSPContractSelectionRequest& Request)
	{
		return FString::Printf(
			TEXT("select|%lld|%lld|%s|%s"),
			Request.ExpectedWorkshopRevision,
			Request.ExpectedCampaignRevision,
			*Contract.GetPrimaryAssetId().ToString(),
			*Contract.StableId.ToString());
	}

	FString BuildClearFingerprint(const FSPContractClearRequest& Request)
	{
		return FString::Printf(
			TEXT("clear|%lld|%lld|%s"),
			Request.ExpectedWorkshopRevision,
			Request.ExpectedCampaignRevision,
			*Request.ExpectedActiveContractId.ToString());
	}

	ESPWorkshopOperationResult CheckProcessedRequest(
		const TMap<FGuid, FString>& ProcessedRequests,
		const FGuid& TransactionId,
		const FString& Fingerprint)
	{
		if (const FString* ExistingFingerprint =
			ProcessedRequests.Find(TransactionId))
		{
			return *ExistingFingerprint == Fingerprint
				? ESPWorkshopOperationResult::AlreadyProcessed
				: ESPWorkshopOperationResult::TransactionConflict;
		}
		return ESPWorkshopOperationResult::Success;
	}

	bool IsContractPayloadValid(const USPContractDefinition& Contract)
	{
		if (!Contract.GetPrimaryAssetId().IsValid()
			|| Contract.StableId.IsNone()
			|| Contract.DisplayName.IsEmpty()
			|| Contract.DangerTier < 1
			|| Contract.DangerTier > 5
			|| Contract.GoldReward < 0
			|| Contract.GuildXpReward < 0)
		{
			return false;
		}

		switch (Contract.Kind)
		{
		case ESPContractKind::ScrollDelivery:
			return Contract.ScrollCondition.IsConfigured();
		case ESPContractKind::LargeCargoRecovery:
			return !Contract.CargoStableId.IsNone();
		case ESPContractKind::PostRunProduction:
			return !Contract.SubmittedScrollStableId.IsNone()
				&& Contract.ProductionWindowSeconds > 0.0f;
		default:
			return false;
		}
	}

	bool IsRecipePayloadValid(const USPCraftingRecipeDefinition& Recipe)
	{
		return Recipe.GetPrimaryAssetId().IsValid()
			&& !Recipe.StableId.IsNone()
			&& !Recipe.DisplayName.IsEmpty()
			&& !Recipe.Ingredients.IsEmpty()
			&& !Recipe.Ingredients.ContainsByPredicate(
				[](const FSPRecipeIngredient& Ingredient)
				{
					return !Ingredient.IsValid();
				})
			&& !Recipe.OutputItemStableId.IsNone()
			&& Recipe.OutputQuantity > 0
			&& Recipe.CraftDurationSeconds >= 8.0f
			&& Recipe.CraftDurationSeconds <= 15.0f;
	}

	bool HasOutputIdentityConflict(
		const TArray<FSPItemInstance>& Outputs,
		const TArray<FSPItemInstance>& ExistingItems)
	{
		for (const FSPItemInstance& Output : Outputs)
		{
			for (const FSPItemInstance& ExistingItem : ExistingItems)
			{
				if (Output.HasAnyIdentityInCommon(ExistingItem))
				{
					return true;
				}
			}
		}
		return false;
	}

	bool ValidateOutputs(
		const USPCraftingRecipeDefinition& Recipe,
		const USPCampaignSaveGame& Campaign,
		const TArray<FSPItemInstance>& Outputs)
	{
		if (Outputs.IsEmpty())
		{
			return false;
		}

		int64 TotalQuantity = 0;
		for (int32 FirstIndex = 0; FirstIndex < Outputs.Num(); ++FirstIndex)
		{
			const FSPItemInstance& Output = Outputs[FirstIndex];
			if (!Output.IsValid()
				|| Output.DefinitionId.PrimaryAssetName
					!= Recipe.OutputItemStableId)
			{
				return false;
			}
			TotalQuantity += Output.Quantity;

			for (int32 SecondIndex = FirstIndex + 1;
				SecondIndex < Outputs.Num();
				++SecondIndex)
			{
				if (Output.HasAnyIdentityInCommon(Outputs[SecondIndex]))
				{
					return false;
				}
			}
		}

		return TotalQuantity == Recipe.OutputQuantity
			&& !HasOutputIdentityConflict(Outputs, Campaign.GetStash())
			&& !HasOutputIdentityConflict(
				Outputs,
				Campaign.GetSettlementInbox());
	}

	bool AccumulateIngredientRequirements(
		const USPCraftingRecipeDefinition& Recipe,
		TMap<FName, int32>& OutRequirements)
	{
		OutRequirements.Reset();
		for (const FSPRecipeIngredient& Ingredient : Recipe.Ingredients)
		{
			int32& RequiredQuantity =
				OutRequirements.FindOrAdd(Ingredient.ItemStableId);
			if (Ingredient.Quantity > MAX_int32 - RequiredQuantity)
			{
				return false;
			}
			RequiredQuantity += Ingredient.Quantity;
		}
		return true;
	}

	void ConsumeFromItems(
		const TArray<FSPItemInstance>& SourceItems,
		TMap<FName, int32>& RemainingRequirements,
		TArray<FGuid>& OutRemovedItemIds,
		TArray<FSPItemInstance>& OutReplacementItems)
	{
		for (const FSPItemInstance& Item : SourceItems)
		{
			if (Item.Kind != ESPItemKind::Material)
			{
				continue;
			}

			int32* RequiredQuantity =
				RemainingRequirements.Find(Item.DefinitionId.PrimaryAssetName);
			if (!RequiredQuantity || *RequiredQuantity <= 0)
			{
				continue;
			}

			const int32 ConsumedQuantity =
				FMath::Min(Item.Quantity, *RequiredQuantity);
			*RequiredQuantity -= ConsumedQuantity;
			OutRemovedItemIds.Add(Item.InstanceId);

			if (ConsumedQuantity < Item.Quantity)
			{
				FSPItemInstance Remainder = Item;
				Remainder.Quantity -= ConsumedQuantity;
				OutReplacementItems.Add(MoveTemp(Remainder));
			}
		}
	}

	bool HasRemainingIngredients(const TMap<FName, int32>& Requirements)
	{
		for (const TPair<FName, int32>& Requirement : Requirements)
		{
			if (Requirement.Value > 0)
			{
				return true;
			}
		}
		return false;
	}

	ESPWorkshopOperationResult MapCampaignApplyResult(
		const ESPCampaignApplyResult ApplyResult)
	{
		switch (ApplyResult)
		{
		case ESPCampaignApplyResult::Applied:
			return ESPWorkshopOperationResult::Success;
		case ESPCampaignApplyResult::AlreadyProcessed:
			return ESPWorkshopOperationResult::AlreadyProcessed;
		case ESPCampaignApplyResult::RevisionMismatch:
			return ESPWorkshopOperationResult::RevisionMismatch;
		case ESPCampaignApplyResult::InsufficientGold:
			return ESPWorkshopOperationResult::InsufficientGold;
		case ESPCampaignApplyResult::ArithmeticOverflow:
			return ESPWorkshopOperationResult::ArithmeticOverflow;
		default:
			return ESPWorkshopOperationResult::CampaignRejected;
		}
	}
}

ESPWorkshopOperationResult FSPWorkshopContractState::TrySelectContract(
	const USPContractDefinition& Contract,
	const FSPContractSelectionRequest& Request,
	const int64 CurrentCampaignRevision,
	const int32 CurrentGuildRank)
{
	if (!Request.TransactionId.IsValid()
		|| Request.ExpectedWorkshopRevision < 0
		|| Request.ExpectedCampaignRevision < 0)
	{
		return ESPWorkshopOperationResult::InvalidRequest;
	}

	const FString Fingerprint =
		SPWorkshop::BuildSelectionFingerprint(Contract, Request);
	const ESPWorkshopOperationResult ProcessedResult =
		SPWorkshop::CheckProcessedRequest(
			ProcessedRequestFingerprints,
			Request.TransactionId,
			Fingerprint);
	if (ProcessedResult != ESPWorkshopOperationResult::Success)
	{
		return ProcessedResult;
	}

	if (!SPWorkshop::IsContractPayloadValid(Contract))
	{
		return ESPWorkshopOperationResult::InvalidDefinition;
	}
	if (Request.ExpectedWorkshopRevision != Revision
		|| Request.ExpectedCampaignRevision != CurrentCampaignRevision)
	{
		return ESPWorkshopOperationResult::RevisionMismatch;
	}
	if (Revision == MAX_int64)
	{
		return ESPWorkshopOperationResult::ArithmeticOverflow;
	}
	if (HasActiveContract())
	{
		return ESPWorkshopOperationResult::ActiveContractExists;
	}
	if (CurrentGuildRank < 1
		|| CurrentGuildRank > 5
		|| Contract.DangerTier > FMath::Min(5, CurrentGuildRank + 1))
	{
		return ESPWorkshopOperationResult::DangerLocked;
	}

	ActiveContractId = Contract.GetPrimaryAssetId();
	ActiveContractStableId = Contract.StableId;
	ProcessedRequestFingerprints.Add(Request.TransactionId, Fingerprint);
	++Revision;
	return ESPWorkshopOperationResult::Success;
}

ESPWorkshopOperationResult FSPWorkshopContractState::TryClearContract(
	const FSPContractClearRequest& Request,
	const int64 CurrentCampaignRevision)
{
	if (!Request.TransactionId.IsValid()
		|| Request.ExpectedWorkshopRevision < 0
		|| Request.ExpectedCampaignRevision < 0
		|| !Request.ExpectedActiveContractId.IsValid())
	{
		return ESPWorkshopOperationResult::InvalidRequest;
	}

	const FString Fingerprint = SPWorkshop::BuildClearFingerprint(Request);
	const ESPWorkshopOperationResult ProcessedResult =
		SPWorkshop::CheckProcessedRequest(
			ProcessedRequestFingerprints,
			Request.TransactionId,
			Fingerprint);
	if (ProcessedResult != ESPWorkshopOperationResult::Success)
	{
		return ProcessedResult;
	}

	if (Request.ExpectedWorkshopRevision != Revision
		|| Request.ExpectedCampaignRevision != CurrentCampaignRevision)
	{
		return ESPWorkshopOperationResult::RevisionMismatch;
	}
	if (Revision == MAX_int64)
	{
		return ESPWorkshopOperationResult::ArithmeticOverflow;
	}
	if (!HasActiveContract())
	{
		return ESPWorkshopOperationResult::NoActiveContract;
	}
	if (Request.ExpectedActiveContractId != ActiveContractId)
	{
		return ESPWorkshopOperationResult::ContractMismatch;
	}

	ActiveContractId = FPrimaryAssetId();
	ActiveContractStableId = NAME_None;
	ProcessedRequestFingerprints.Add(Request.TransactionId, Fingerprint);
	++Revision;
	return ESPWorkshopOperationResult::Success;
}

bool USPWorkshopSubsystem::IsContractDefinitionUsable(
	const USPContractDefinition* Contract)
{
	return Contract && SPWorkshop::IsContractPayloadValid(*Contract);
}

bool USPWorkshopSubsystem::IsRecipeDefinitionUsable(
	const USPCraftingRecipeDefinition* Recipe)
{
	return Recipe && SPWorkshop::IsRecipePayloadValid(*Recipe);
}

ESPWorkshopOperationResult USPWorkshopSubsystem::BuildCraftPlan(
	const USPCampaignSaveGame& Campaign,
	const USPCraftingRecipeDefinition& Recipe,
	const FSPWorkshopCraftQuote& Quote,
	FSPWorkshopCraftPlan& OutPlan)
{
	OutPlan = FSPWorkshopCraftPlan();

	if (!Campaign.IsStructurallyValid())
	{
		return ESPWorkshopOperationResult::NoCampaignLoaded;
	}
	if (!Quote.TransactionId.IsValid()
		|| Quote.ExpectedCampaignRevision < 0
		|| Quote.GoldCost < 0)
	{
		return ESPWorkshopOperationResult::InvalidRequest;
	}
	if (Campaign.HasProcessedTransaction(Quote.TransactionId))
	{
		return ESPWorkshopOperationResult::AlreadyProcessed;
	}
	if (!IsRecipeDefinitionUsable(&Recipe))
	{
		return ESPWorkshopOperationResult::InvalidDefinition;
	}
	if (Quote.ExpectedCampaignRevision != Campaign.GetRevision())
	{
		return ESPWorkshopOperationResult::RevisionMismatch;
	}
	if (Recipe.bRequiresCampaignVote && !Quote.bCampaignVoteApproved)
	{
		return ESPWorkshopOperationResult::VoteRequired;
	}
	if (Campaign.GetGold() < Quote.GoldCost)
	{
		return ESPWorkshopOperationResult::InsufficientGold;
	}
	if (!SPWorkshop::ValidateOutputs(
		Recipe,
		Campaign,
		Quote.ServerGeneratedOutputs))
	{
		return ESPWorkshopOperationResult::InvalidOutput;
	}

	TMap<FName, int32> RemainingRequirements;
	if (!SPWorkshop::AccumulateIngredientRequirements(
		Recipe,
		RemainingRequirements))
	{
		return ESPWorkshopOperationResult::ArithmeticOverflow;
	}

	FSPCampaignTransaction Transaction;
	Transaction.TransactionId = Quote.TransactionId;
	Transaction.ExpectedRevision = Quote.ExpectedCampaignRevision;
	Transaction.GoldDelta = -Quote.GoldCost;

	switch (Quote.IngredientSource)
	{
	case ESPWorkshopIngredientSource::StashOnly:
		SPWorkshop::ConsumeFromItems(
			Campaign.GetStash(),
			RemainingRequirements,
			Transaction.StashItemIdsToRemove,
			Transaction.StashItemsToAdd);
		break;
	case ESPWorkshopIngredientSource::SettlementInboxOnly:
		SPWorkshop::ConsumeFromItems(
			Campaign.GetSettlementInbox(),
			RemainingRequirements,
			Transaction.SettlementItemIdsToRemove,
			Transaction.SettlementItemsToAdd);
		break;
	case ESPWorkshopIngredientSource::SettlementInboxThenStash:
		SPWorkshop::ConsumeFromItems(
			Campaign.GetSettlementInbox(),
			RemainingRequirements,
			Transaction.SettlementItemIdsToRemove,
			Transaction.SettlementItemsToAdd);
		SPWorkshop::ConsumeFromItems(
			Campaign.GetStash(),
			RemainingRequirements,
			Transaction.StashItemIdsToRemove,
			Transaction.StashItemsToAdd);
		break;
	default:
		return ESPWorkshopOperationResult::InvalidRequest;
	}

	if (SPWorkshop::HasRemainingIngredients(RemainingRequirements))
	{
		return ESPWorkshopOperationResult::MissingIngredients;
	}

	const int32 StashCountAfterIngredients =
		Campaign.GetStash().Num()
		- Transaction.StashItemIdsToRemove.Num()
		+ Transaction.StashItemsToAdd.Num();
	const bool bOutputsFitInStash =
		StashCountAfterIngredients >= 0
		&& Quote.ServerGeneratedOutputs.Num()
			<= USPCampaignSaveGame::StashCapacity - StashCountAfterIngredients;

	if (Quote.RequestedDestination
			== ESPWorkshopOutputDestination::PreferStash
		&& bOutputsFitInStash)
	{
		Transaction.StashItemsToAdd.Append(Quote.ServerGeneratedOutputs);
		OutPlan.ActualDestination =
			ESPWorkshopOutputDestination::PreferStash;
	}
	else
	{
		Transaction.SettlementItemsToAdd.Append(
			Quote.ServerGeneratedOutputs);
		OutPlan.ActualDestination =
			ESPWorkshopOutputDestination::SettlementInbox;
	}

	OutPlan.CampaignTransaction = MoveTemp(Transaction);
	return ESPWorkshopOperationResult::Success;
}

ESPWorkshopOperationResult USPWorkshopSubsystem::SelectContract(
	const USPContractDefinition* Contract,
	const FSPContractSelectionRequest& Request)
{
	if (!HasServerAuthority())
	{
		return ESPWorkshopOperationResult::NotAuthority;
	}

	USPCampaignSubsystem* CampaignSubsystem = ResolveCampaignSubsystem();
	const USPCampaignSaveGame* Campaign = CampaignSubsystem
		? CampaignSubsystem->GetCurrentCampaign()
		: nullptr;
	if (!Campaign)
	{
		return ESPWorkshopOperationResult::NoCampaignLoaded;
	}
	if (!Contract)
	{
		return ESPWorkshopOperationResult::InvalidDefinition;
	}

	return ContractState.TrySelectContract(
		*Contract,
		Request,
		Campaign->GetRevision(),
		Campaign->GetGuildRank());
}

ESPWorkshopOperationResult USPWorkshopSubsystem::ClearActiveContract(
	const FSPContractClearRequest& Request)
{
	if (!HasServerAuthority())
	{
		return ESPWorkshopOperationResult::NotAuthority;
	}

	USPCampaignSubsystem* CampaignSubsystem = ResolveCampaignSubsystem();
	const USPCampaignSaveGame* Campaign = CampaignSubsystem
		? CampaignSubsystem->GetCurrentCampaign()
		: nullptr;
	if (!Campaign)
	{
		return ESPWorkshopOperationResult::NoCampaignLoaded;
	}

	return ContractState.TryClearContract(
		Request,
		Campaign->GetRevision());
}

ESPWorkshopOperationResult USPWorkshopSubsystem::CraftRecipe(
	const USPCraftingRecipeDefinition* Recipe,
	const FSPWorkshopCraftQuote& Quote,
	ESPCampaignApplyResult& OutApplyResult,
	ESPCampaignPersistenceResult& OutPersistenceResult,
	ESPWorkshopOutputDestination& OutActualDestination)
{
	OutApplyResult = ESPCampaignApplyResult::InvalidCampaign;
	OutPersistenceResult =
		ESPCampaignPersistenceResult::NoCampaignLoaded;
	OutActualDestination =
		ESPWorkshopOutputDestination::SettlementInbox;

	if (!HasServerAuthority())
	{
		return ESPWorkshopOperationResult::NotAuthority;
	}

	USPCampaignSubsystem* CampaignSubsystem = ResolveCampaignSubsystem();
	const USPCampaignSaveGame* Campaign = CampaignSubsystem
		? CampaignSubsystem->GetCurrentCampaign()
		: nullptr;
	if (!Campaign)
	{
		return ESPWorkshopOperationResult::NoCampaignLoaded;
	}
	if (!Recipe)
	{
		return ESPWorkshopOperationResult::InvalidDefinition;
	}

	FSPWorkshopCraftPlan Plan;
	const ESPWorkshopOperationResult PlanResult =
		BuildCraftPlan(*Campaign, *Recipe, Quote, Plan);
	if (PlanResult != ESPWorkshopOperationResult::Success)
	{
		if (PlanResult == ESPWorkshopOperationResult::AlreadyProcessed)
		{
			OutApplyResult = ESPCampaignApplyResult::AlreadyProcessed;
			OutPersistenceResult = ESPCampaignPersistenceResult::Success;
		}
		return PlanResult;
	}

	OutActualDestination = Plan.ActualDestination;
	OutPersistenceResult =
		CampaignSubsystem->CommitCampaignTransaction(
			Plan.CampaignTransaction,
			OutApplyResult);
	if (OutPersistenceResult != ESPCampaignPersistenceResult::Success)
	{
		return OutPersistenceResult
				== ESPCampaignPersistenceResult::ApplyRejected
			? SPWorkshop::MapCampaignApplyResult(OutApplyResult)
			: ESPWorkshopOperationResult::PersistenceFailed;
	}
	return SPWorkshop::MapCampaignApplyResult(OutApplyResult);
}

bool USPWorkshopSubsystem::HasServerAuthority() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (AuthorityForTesting.IsSet())
	{
		return AuthorityForTesting.GetValue();
	}
#endif

	const UGameInstance* GameInstance = GetGameInstance();
	const UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	return World && World->GetNetMode() != NM_Client;
}

USPCampaignSubsystem* USPWorkshopSubsystem::ResolveCampaignSubsystem() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (CampaignSubsystemForTesting.IsValid())
	{
		return CampaignSubsystemForTesting.Get();
	}
#endif

	UGameInstance* GameInstance = GetGameInstance();
	return GameInstance
		? GameInstance->GetSubsystem<USPCampaignSubsystem>()
		: nullptr;
}

#if WITH_DEV_AUTOMATION_TESTS
void USPWorkshopSubsystem::ConfigureAuthorityForTesting(
	USPCampaignSubsystem* InCampaignSubsystem,
	const bool bInHasAuthority)
{
	CampaignSubsystemForTesting = InCampaignSubsystem;
	AuthorityForTesting = bInHasAuthority;
}
#endif
