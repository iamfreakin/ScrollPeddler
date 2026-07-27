#include "Persistence/SPPlayerProfileSaveGame.h"

bool USPPlayerProfileSaveGame::InitializeNewProfile(const FString& InPlayerId)
{
	FString NormalizedPlayerId = InPlayerId;
	NormalizedPlayerId.TrimStartAndEndInline();
	if (NormalizedPlayerId.IsEmpty())
	{
		return false;
	}

	SaveVersion = CurrentSaveVersion;
	PlayerId = MoveTemp(NormalizedPlayerId);
	Revision = 0;
	Discoveries.Reset();
	Stats.Reset();
	UnlockedCosmetics.Reset();
	EquippedCosmetics.Reset();
	SettingsFlags.Reset();
	return IsStructurallyValid();
}

bool USPPlayerProfileSaveGame::IsVersionCompatible() const
{
	return SaveVersion == CurrentSaveVersion;
}

bool USPPlayerProfileSaveGame::IsStructurallyValid() const
{
	if (!IsVersionCompatible() || PlayerId.IsEmpty() || Revision < 0)
	{
		return false;
	}

	for (const FPrimaryAssetId& DiscoveryId : Discoveries)
	{
		if (!DiscoveryId.IsValid())
		{
			return false;
		}
	}
	for (const TPair<FName, int64>& Stat : Stats)
	{
		if (Stat.Key.IsNone() || Stat.Value < 0)
		{
			return false;
		}
	}
	for (const FPrimaryAssetId& CosmeticId : UnlockedCosmetics)
	{
		if (!CosmeticId.IsValid())
		{
			return false;
		}
	}
	for (const TPair<FName, FPrimaryAssetId>& EquippedCosmetic : EquippedCosmetics)
	{
		if (EquippedCosmetic.Key.IsNone()
			|| !EquippedCosmetic.Value.IsValid()
			|| !UnlockedCosmetics.Contains(EquippedCosmetic.Value))
		{
			return false;
		}
	}
	for (const TPair<FName, bool>& SettingFlag : SettingsFlags)
	{
		if (SettingFlag.Key.IsNone())
		{
			return false;
		}
	}

	return true;
}

bool USPPlayerProfileSaveGame::CanAdvanceRevision() const
{
	return IsStructurallyValid() && Revision < MAX_int64;
}

bool USPPlayerProfileSaveGame::RecordDiscovery(const FPrimaryAssetId& DiscoveryId)
{
	if (!IsStructurallyValid() || !DiscoveryId.IsValid())
	{
		return false;
	}
	if (Discoveries.Contains(DiscoveryId))
	{
		return true;
	}
	if (!CanAdvanceRevision())
	{
		return false;
	}

	Discoveries.Add(DiscoveryId);
	++Revision;
	return true;
}

bool USPPlayerProfileSaveGame::IncrementStat(FName StatName, int64 Delta)
{
	if (StatName.IsNone() || Delta <= 0 || !CanAdvanceRevision())
	{
		return false;
	}

	const int64 CurrentValue = Stats.FindRef(StatName);
	if (Delta > MAX_int64 - CurrentValue)
	{
		return false;
	}

	Stats.Add(StatName, CurrentValue + Delta);
	++Revision;
	return true;
}

bool USPPlayerProfileSaveGame::UnlockCosmetic(const FPrimaryAssetId& CosmeticId)
{
	if (!IsStructurallyValid() || !CosmeticId.IsValid())
	{
		return false;
	}
	if (UnlockedCosmetics.Contains(CosmeticId))
	{
		return true;
	}
	if (!CanAdvanceRevision())
	{
		return false;
	}

	UnlockedCosmetics.Add(CosmeticId);
	++Revision;
	return true;
}

bool USPPlayerProfileSaveGame::EquipCosmetic(
	FName CosmeticSlot,
	const FPrimaryAssetId& CosmeticId)
{
	if (!IsStructurallyValid()
		|| CosmeticSlot.IsNone()
		|| !CosmeticId.IsValid()
		|| !UnlockedCosmetics.Contains(CosmeticId))
	{
		return false;
	}

	if (const FPrimaryAssetId* ExistingCosmetic = EquippedCosmetics.Find(CosmeticSlot))
	{
		if (*ExistingCosmetic == CosmeticId)
		{
			return true;
		}
	}
	if (!CanAdvanceRevision())
	{
		return false;
	}

	EquippedCosmetics.Add(CosmeticSlot, CosmeticId);
	++Revision;
	return true;
}

bool USPPlayerProfileSaveGame::ClearEquippedCosmetic(FName CosmeticSlot)
{
	if (!IsStructurallyValid() || CosmeticSlot.IsNone())
	{
		return false;
	}
	if (!EquippedCosmetics.Contains(CosmeticSlot))
	{
		return true;
	}
	if (!CanAdvanceRevision())
	{
		return false;
	}

	EquippedCosmetics.Remove(CosmeticSlot);
	++Revision;
	return true;
}

bool USPPlayerProfileSaveGame::SetSettingFlag(FName SettingName, bool bEnabled)
{
	if (!IsStructurallyValid() || SettingName.IsNone())
	{
		return false;
	}

	if (const bool* ExistingValue = SettingsFlags.Find(SettingName))
	{
		if (*ExistingValue == bEnabled)
		{
			return true;
		}
	}
	if (!CanAdvanceRevision())
	{
		return false;
	}

	SettingsFlags.Add(SettingName, bEnabled);
	++Revision;
	return true;
}
