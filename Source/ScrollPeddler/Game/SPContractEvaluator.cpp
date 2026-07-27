#include "Game/SPContractEvaluator.h"

namespace
{
bool IsValidQuality(const ESPScrollQuality Quality)
{
	return StaticEnum<ESPScrollQuality>()->IsValidEnumValue(static_cast<int64>(Quality));
}

bool HasUniqueNonEmptyNames(const TArray<FName>& Names)
{
	if (Names.IsEmpty())
	{
		return false;
	}

	TSet<FName> UniqueNames;
	for (const FName Name : Names)
	{
		if (Name.IsNone() || UniqueNames.Contains(Name))
		{
			return false;
		}
		UniqueNames.Add(Name);
	}
	return true;
}

bool AreEquivalentItemEvidence(
	const FSPContractItemEvidence& First,
	const FSPContractItemEvidence& Second)
{
	return First.Item.InstanceId == Second.Item.InstanceId
		&& First.Item.DefinitionId == Second.Item.DefinitionId
		&& First.Item.Kind == Second.Item.Kind
		&& First.Item.Quantity == Second.Item.Quantity
		&& First.Item.ScrollRoll.EngravingDefinitionId
			== Second.Item.ScrollRoll.EngravingDefinitionId
		&& First.Item.ScrollRoll.Quality == Second.Item.ScrollRoll.Quality
		&& First.Item.ScrollRoll.Contamination
			== Second.Item.ScrollRoll.Contamination
		&& First.Item.ScrollRoll.Misfire == Second.Item.ScrollRoll.Misfire
		&& First.Item.MaterialQuality == Second.Item.MaterialQuality
		&& First.Item.MaterialContamination
			== Second.Item.MaterialContamination
		&& First.Item.EquipmentCondition == Second.Item.EquipmentCondition
		&& First.Item.MergedInstanceIds == Second.Item.MergedInstanceIds
		&& First.DefinitionStableId == Second.DefinitionStableId
		&& First.EngravingStableId == Second.EngravingStableId;
}

bool AreEquivalentReceipts(
	const FSPContractProductionReceipt& First,
	const FSPContractProductionReceipt& Second)
{
	return First.ReceiptId == Second.ReceiptId
		&& First.RunId == Second.RunId
		&& First.SubmittedItemInstanceId == Second.SubmittedItemInstanceId
		&& First.SubmittedScrollStableId == Second.SubmittedScrollStableId
		&& First.CompletedAtSeconds == Second.CompletedAtSeconds
		&& First.bCompleted == Second.bCompleted;
}

template <typename PredicateType>
void VisitExtractedUniqueItems(
	const FSPContractRunEvidence& Evidence,
	PredicateType&& Predicate)
{
	TMap<FGuid, const FSPContractItemEvidence*> UniqueItems;
	TSet<FGuid> ConflictedItemIds;
	TArray<FGuid> EncounterOrder;
	for (const FSPContractPlayerExtraction& Player : Evidence.Players)
	{
		if (!Player.bExtracted)
		{
			continue;
		}

		for (const FSPContractItemEvidence& ItemEvidence : Player.Items)
		{
			if (!ItemEvidence.IsValid())
			{
				continue;
			}

			const FGuid ItemId = ItemEvidence.Item.InstanceId;
			if (const FSPContractItemEvidence* const* Existing =
				UniqueItems.Find(ItemId))
			{
				if (!AreEquivalentItemEvidence(**Existing, ItemEvidence))
				{
					ConflictedItemIds.Add(ItemId);
				}
				continue;
			}

			UniqueItems.Add(ItemId, &ItemEvidence);
			EncounterOrder.Add(ItemId);
		}
	}

	for (const FGuid& ItemId : EncounterOrder)
	{
		if (!ConflictedItemIds.Contains(ItemId))
		{
			Predicate(*UniqueItems.FindChecked(ItemId));
		}
	}
}
}

bool FSPContractItemEvidence::IsValid() const
{
	return Item.IsValid()
		&& !DefinitionStableId.IsNone()
		&& (Item.Kind != ESPItemKind::Scroll || !EngravingStableId.IsNone());
}

bool FSPContractProductionReceipt::IsValid() const
{
	return ReceiptId.IsValid()
		&& RunId.IsValid()
		&& SubmittedItemInstanceId.IsValid()
		&& !SubmittedScrollStableId.IsNone()
		&& FMath::IsFinite(CompletedAtSeconds)
		&& CompletedAtSeconds >= 0.0
		&& bCompleted;
}

bool FSPContractEvaluator::IsDefinitionUsable(const USPContractDefinition& Definition)
{
	if (Definition.StableId.IsNone()
		|| Definition.DisplayName.IsEmpty()
		|| Definition.DangerTier < 1
		|| Definition.DangerTier > 5
		|| Definition.GoldReward < 0
		|| Definition.GuildXpReward < 0
		|| !StaticEnum<ESPContractKind>()->IsValidEnumValue(
			static_cast<int64>(Definition.Kind)))
	{
		return false;
	}

	switch (Definition.Kind)
	{
	case ESPContractKind::ScrollDelivery:
		return Definition.ScrollCondition.IsConfigured()
			&& !Definition.ScrollCondition.BaseFamilyStableId.IsNone()
			&& HasUniqueNonEmptyNames(
				Definition.ScrollCondition.AllowedEngravingStableIds)
			&& IsValidQuality(Definition.ScrollCondition.MinimumQuality)
			&& FMath::IsFinite(
				Definition.ScrollCondition.MaximumContamination);

	case ESPContractKind::LargeCargoRecovery:
		return !Definition.CargoStableId.IsNone();

	case ESPContractKind::PostRunProduction:
		return !Definition.SubmittedScrollStableId.IsNone()
			&& FMath::IsFinite(Definition.ProductionWindowSeconds)
			&& Definition.ProductionWindowSeconds > 0.0f;

	default:
		return false;
	}
}

FSPContractEvaluationResult FSPContractEvaluator::Evaluate(
	const USPContractDefinition& Definition,
	const FSPContractRunEvidence& Evidence)
{
	if (!IsDefinitionUsable(Definition))
	{
		return FSPContractEvaluationResult();
	}
	if (!IsRunEvidenceShapeValid(Evidence))
	{
		FSPContractEvaluationResult Result;
		Result.Outcome = ESPContractEvaluationOutcome::InvalidEvidence;
		return Result;
	}

	switch (Definition.Kind)
	{
	case ESPContractKind::ScrollDelivery:
		return EvaluateScrollDelivery(Definition, Evidence);
	case ESPContractKind::LargeCargoRecovery:
		return EvaluateLargeCargoRecovery(Definition, Evidence);
	case ESPContractKind::PostRunProduction:
		return EvaluatePostRunProduction(Definition, Evidence);
	default:
		return FSPContractEvaluationResult();
	}
}

bool FSPContractEvaluator::IsRunEvidenceShapeValid(
	const FSPContractRunEvidence& Evidence)
{
	if (!Evidence.RunId.IsValid())
	{
		return false;
	}

	TSet<FString> PlayerIds;
	for (const FSPContractPlayerExtraction& Player : Evidence.Players)
	{
		FString NormalizedPlayerId = Player.PlayerId;
		NormalizedPlayerId.TrimStartAndEndInline();
		if (NormalizedPlayerId.IsEmpty() || PlayerIds.Contains(NormalizedPlayerId))
		{
			return false;
		}
		PlayerIds.Add(MoveTemp(NormalizedPlayerId));
	}
	return true;
}

FSPContractEvaluationResult FSPContractEvaluator::EvaluateScrollDelivery(
	const USPContractDefinition& Definition,
	const FSPContractRunEvidence& Evidence)
{
	FSPContractEvaluationResult Result;
	Result.Outcome = ESPContractEvaluationOutcome::Failed;
	const FSPContractScrollCondition& Condition = Definition.ScrollCondition;

	VisitExtractedUniqueItems(
		Evidence,
		[&Condition, &Result](const FSPContractItemEvidence& ItemEvidence)
		{
			if (Result.MatchedQuantity >= Condition.Quantity
				|| ItemEvidence.Item.Kind != ESPItemKind::Scroll
				|| ItemEvidence.DefinitionStableId != Condition.BaseFamilyStableId
				|| !Condition.AllowedEngravingStableIds.Contains(
					ItemEvidence.EngravingStableId)
				|| static_cast<uint8>(ItemEvidence.Item.ScrollRoll.Quality)
					< static_cast<uint8>(Condition.MinimumQuality)
				|| ItemEvidence.Item.ScrollRoll.Contamination
					> Condition.MaximumContamination)
			{
				return;
			}

			++Result.MatchedQuantity;
			Result.CountedItemInstanceIds.Add(ItemEvidence.Item.InstanceId);
		});

	if (Result.MatchedQuantity >= Condition.Quantity)
	{
		Result.Outcome = ESPContractEvaluationOutcome::Succeeded;
		ApplyRewardOnSuccess(Definition, Result);
	}
	return Result;
}

FSPContractEvaluationResult FSPContractEvaluator::EvaluateLargeCargoRecovery(
	const USPContractDefinition& Definition,
	const FSPContractRunEvidence& Evidence)
{
	FSPContractEvaluationResult Result;
	Result.Outcome = ESPContractEvaluationOutcome::Failed;

	VisitExtractedUniqueItems(
		Evidence,
		[&Definition, &Result](const FSPContractItemEvidence& ItemEvidence)
		{
			if (Result.MatchedQuantity > 0
				|| ItemEvidence.Item.Kind != ESPItemKind::LargeCargo
				|| ItemEvidence.DefinitionStableId != Definition.CargoStableId)
			{
				return;
			}

			Result.MatchedQuantity = 1;
			Result.CountedItemInstanceIds.Add(ItemEvidence.Item.InstanceId);
		});

	if (Result.MatchedQuantity == 1)
	{
		Result.Outcome = ESPContractEvaluationOutcome::Succeeded;
		ApplyRewardOnSuccess(Definition, Result);
	}
	return Result;
}

FSPContractEvaluationResult FSPContractEvaluator::EvaluatePostRunProduction(
	const USPContractDefinition& Definition,
	const FSPContractRunEvidence& Evidence)
{
	FSPContractEvaluationResult Result;
	Result.Outcome = ESPContractEvaluationOutcome::Failed;
	if (!FMath::IsFinite(Evidence.ProductionWindowOpenedAtSeconds)
		|| Evidence.ProductionWindowOpenedAtSeconds < 0.0)
	{
		Result.Outcome = ESPContractEvaluationOutcome::InvalidEvidence;
		return Result;
	}

	TSet<FGuid> SeenSubmittedItemIds;
	TMap<FGuid, const FSPContractProductionReceipt*> UniqueReceipts;
	TSet<FGuid> ConflictedReceiptIds;
	TArray<FGuid> ReceiptOrder;
	for (const FSPContractProductionReceipt& Receipt : Evidence.ProductionReceipts)
	{
		if (!Receipt.ReceiptId.IsValid())
		{
			continue;
		}

		if (const FSPContractProductionReceipt* const* Existing =
			UniqueReceipts.Find(Receipt.ReceiptId))
		{
			if (!AreEquivalentReceipts(**Existing, Receipt))
			{
				ConflictedReceiptIds.Add(Receipt.ReceiptId);
			}
			continue;
		}
		UniqueReceipts.Add(Receipt.ReceiptId, &Receipt);
		ReceiptOrder.Add(Receipt.ReceiptId);
	}

	for (const FGuid& ReceiptId : ReceiptOrder)
	{
		if (ConflictedReceiptIds.Contains(ReceiptId))
		{
			continue;
		}

		const FSPContractProductionReceipt& Receipt =
			*UniqueReceipts.FindChecked(ReceiptId);
		if (!Receipt.IsValid()
			|| Receipt.RunId != Evidence.RunId
			|| Receipt.SubmittedScrollStableId
				!= Definition.SubmittedScrollStableId
			|| SeenSubmittedItemIds.Contains(Receipt.SubmittedItemInstanceId))
		{
			continue;
		}

		const double ElapsedSeconds =
			Receipt.CompletedAtSeconds - Evidence.ProductionWindowOpenedAtSeconds;
		if (!FMath::IsFinite(ElapsedSeconds)
			|| ElapsedSeconds < 0.0
			|| ElapsedSeconds
				> static_cast<double>(Definition.ProductionWindowSeconds))
		{
			continue;
		}

		SeenSubmittedItemIds.Add(Receipt.SubmittedItemInstanceId);
		Result.MatchedQuantity = 1;
		Result.CountedItemInstanceIds.Add(Receipt.SubmittedItemInstanceId);
		Result.CountedReceiptIds.Add(Receipt.ReceiptId);
		Result.Outcome = ESPContractEvaluationOutcome::Succeeded;
		ApplyRewardOnSuccess(Definition, Result);
		break;
	}
	return Result;
}

void FSPContractEvaluator::ApplyRewardOnSuccess(
	const USPContractDefinition& Definition,
	FSPContractEvaluationResult& Result)
{
	check(Result.Outcome == ESPContractEvaluationOutcome::Succeeded);
	Result.GoldReward = Definition.GoldReward;
	Result.GuildXpReward = Definition.GuildXpReward;
}
