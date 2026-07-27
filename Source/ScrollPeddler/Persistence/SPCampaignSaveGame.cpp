#include "Persistence/SPCampaignSaveGame.h"

namespace SPCampaignSaveGame
{
	bool HasValidUniqueItemIds(
		const TArray<FSPItemInstance>& First,
		const TArray<FSPItemInstance>& Second)
	{
		TSet<FGuid> SeenIds;

		const auto AddItems = [&SeenIds](const TArray<FSPItemInstance>& Items)
		{
			for (const FSPItemInstance& Item : Items)
			{
				if (!Item.IsValid() || SeenIds.Contains(Item.InstanceId))
				{
					return false;
				}
				SeenIds.Add(Item.InstanceId);
				for (const FGuid& MergedInstanceId : Item.MergedInstanceIds)
				{
					if (SeenIds.Contains(MergedInstanceId))
					{
						return false;
					}
					SeenIds.Add(MergedInstanceId);
				}
			}
			return true;
		};

		return AddItems(First) && AddItems(Second);
	}

	bool HasOnlyValidIds(const TSet<FGuid>& Ids)
	{
		for (const FGuid& Id : Ids)
		{
			if (!Id.IsValid())
			{
				return false;
			}
		}
		return true;
	}

	bool HasUniqueValidIds(const TArray<FGuid>& Ids)
	{
		TSet<FGuid> UniqueIds;
		UniqueIds.Reserve(Ids.Num());
		for (const FGuid& Id : Ids)
		{
			if (!Id.IsValid() || UniqueIds.Contains(Id))
			{
				return false;
			}
			UniqueIds.Add(Id);
		}
		return true;
	}

	bool RemoveItems(
		TArray<FSPItemInstance>& Items,
		const TArray<FGuid>& ItemIdsToRemove)
	{
		for (const FGuid& ItemId : ItemIdsToRemove)
		{
			const int32 ItemIndex = Items.IndexOfByPredicate(
				[&ItemId](const FSPItemInstance& Item)
				{
					return Item.InstanceId == ItemId;
				});
			if (ItemIndex == INDEX_NONE)
			{
				return false;
			}
			Items.RemoveAt(ItemIndex);
		}
		return true;
	}

	bool IsTransactionShapeValid(const FSPCampaignTransaction& Transaction)
	{
		if (!Transaction.TransactionId.IsValid()
			|| Transaction.ExpectedRevision < 0
			|| Transaction.GuildRankOverride < 0
			|| Transaction.GuildRankOverride > 5)
		{
			return false;
		}

		if (!HasUniqueValidIds(Transaction.StashItemIdsToRemove)
			|| !HasUniqueValidIds(Transaction.SettlementItemIdsToRemove))
		{
			return false;
		}

		TSet<FGuid> RemovedIds;
		RemovedIds.Reserve(
			Transaction.StashItemIdsToRemove.Num()
			+ Transaction.SettlementItemIdsToRemove.Num());
		for (const FGuid& ItemId : Transaction.StashItemIdsToRemove)
		{
			RemovedIds.Add(ItemId);
		}
		for (const FGuid& ItemId : Transaction.SettlementItemIdsToRemove)
		{
			if (RemovedIds.Contains(ItemId))
			{
				return false;
			}
			RemovedIds.Add(ItemId);
		}

		return HasValidUniqueItemIds(
			Transaction.StashItemsToAdd,
			Transaction.SettlementItemsToAdd);
	}
}

bool USPCampaignSaveGame::InitializeNewCampaign(const FString& InOwnerId)
{
	FString NormalizedOwnerId = InOwnerId;
	NormalizedOwnerId.TrimStartAndEndInline();
	if (NormalizedOwnerId.IsEmpty())
	{
		return false;
	}

	SaveVersion = CurrentSaveVersion;
	CampaignId = FGuid::NewGuid();
	OwnerId = MoveTemp(NormalizedOwnerId);
	Revision = 0;
	Gold = 0;
	GuildXP = 0;
	GuildRank = 1;
	Stash.Reset();
	SettlementInbox.Reset();
	ProcessedTransactionIds.Reset();
	ProcessedRunIds.Reset();
	return IsStructurallyValid();
}

bool USPCampaignSaveGame::IsVersionCompatible() const
{
	return SaveVersion == CurrentSaveVersion;
}

bool USPCampaignSaveGame::IsStructurallyValid() const
{
	return IsVersionCompatible()
		&& CampaignId.IsValid()
		&& !OwnerId.IsEmpty()
		&& Revision >= 0
		&& Gold >= 0
		&& GuildXP >= 0
		&& GuildRank >= 1
		&& GuildRank <= 5
		&& Stash.Num() <= StashCapacity
		&& SPCampaignSaveGame::HasValidUniqueItemIds(Stash, SettlementInbox)
		&& SPCampaignSaveGame::HasOnlyValidIds(ProcessedTransactionIds)
		&& SPCampaignSaveGame::HasOnlyValidIds(ProcessedRunIds);
}

ESPCampaignApplyResult USPCampaignSaveGame::ApplyTransaction(
	const FSPCampaignTransaction& Transaction)
{
	if (!IsStructurallyValid())
	{
		return ESPCampaignApplyResult::InvalidCampaign;
	}

	if (!Transaction.TransactionId.IsValid())
	{
		return ESPCampaignApplyResult::InvalidTransaction;
	}

	if (ProcessedTransactionIds.Contains(Transaction.TransactionId)
		|| (Transaction.RunId.IsValid() && ProcessedRunIds.Contains(Transaction.RunId)))
	{
		return ESPCampaignApplyResult::AlreadyProcessed;
	}

	if (!SPCampaignSaveGame::IsTransactionShapeValid(Transaction))
	{
		return ESPCampaignApplyResult::InvalidTransaction;
	}

	if (Transaction.ExpectedRevision != Revision)
	{
		return ESPCampaignApplyResult::RevisionMismatch;
	}

	const int64 NextGold = static_cast<int64>(Gold) + Transaction.GoldDelta;
	if (NextGold < 0)
	{
		return ESPCampaignApplyResult::InsufficientGold;
	}
	if (NextGold > MAX_int32)
	{
		return ESPCampaignApplyResult::ArithmeticOverflow;
	}

	const int64 NextGuildXP = static_cast<int64>(GuildXP) + Transaction.GuildXPDelta;
	if (NextGuildXP < 0 || NextGuildXP > MAX_int32 || Revision == MAX_int64)
	{
		return ESPCampaignApplyResult::ArithmeticOverflow;
	}

	TArray<FSPItemInstance> NextStash = Stash;
	TArray<FSPItemInstance> NextSettlementInbox = SettlementInbox;
	if (!SPCampaignSaveGame::RemoveItems(NextStash, Transaction.StashItemIdsToRemove)
		|| !SPCampaignSaveGame::RemoveItems(
			NextSettlementInbox,
			Transaction.SettlementItemIdsToRemove))
	{
		return ESPCampaignApplyResult::ItemConflict;
	}

	NextStash.Append(Transaction.StashItemsToAdd);
	NextSettlementInbox.Append(Transaction.SettlementItemsToAdd);
	if (NextStash.Num() > StashCapacity)
	{
		return ESPCampaignApplyResult::StashCapacityExceeded;
	}
	if (!SPCampaignSaveGame::HasValidUniqueItemIds(NextStash, NextSettlementInbox))
	{
		return ESPCampaignApplyResult::ItemConflict;
	}

	Gold = static_cast<int32>(NextGold);
	GuildXP = static_cast<int32>(NextGuildXP);
	if (Transaction.GuildRankOverride > 0)
	{
		GuildRank = Transaction.GuildRankOverride;
	}
	Stash = MoveTemp(NextStash);
	SettlementInbox = MoveTemp(NextSettlementInbox);
	ProcessedTransactionIds.Add(Transaction.TransactionId);
	if (Transaction.RunId.IsValid())
	{
		ProcessedRunIds.Add(Transaction.RunId);
	}
	++Revision;
	return ESPCampaignApplyResult::Applied;
}

bool USPCampaignSaveGame::HasProcessedTransaction(const FGuid& TransactionId) const
{
	return TransactionId.IsValid() && ProcessedTransactionIds.Contains(TransactionId);
}

bool USPCampaignSaveGame::HasProcessedRun(const FGuid& RunId) const
{
	return RunId.IsValid() && ProcessedRunIds.Contains(RunId);
}
