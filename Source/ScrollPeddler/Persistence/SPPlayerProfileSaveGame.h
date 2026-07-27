#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "UObject/PrimaryAssetId.h"
#include "SPPlayerProfileSaveGame.generated.h"

UCLASS()
class SCROLLPEDDLER_API USPPlayerProfileSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static constexpr int32 CurrentSaveVersion = 1;

	bool InitializeNewProfile(const FString& InPlayerId);
	bool IsVersionCompatible() const;
	bool IsStructurallyValid() const;

	bool RecordDiscovery(const FPrimaryAssetId& DiscoveryId);
	bool IncrementStat(FName StatName, int64 Delta = 1);
	bool UnlockCosmetic(const FPrimaryAssetId& CosmeticId);
	bool EquipCosmetic(FName CosmeticSlot, const FPrimaryAssetId& CosmeticId);
	bool ClearEquippedCosmetic(FName CosmeticSlot);
	bool SetSettingFlag(FName SettingName, bool bEnabled);

	int32 GetSaveVersion() const { return SaveVersion; }
	const FString& GetPlayerId() const { return PlayerId; }
	int64 GetRevision() const { return Revision; }
	const TSet<FPrimaryAssetId>& GetDiscoveries() const { return Discoveries; }
	const TMap<FName, int64>& GetStats() const { return Stats; }
	const TSet<FPrimaryAssetId>& GetUnlockedCosmetics() const { return UnlockedCosmetics; }
	const TMap<FName, FPrimaryAssetId>& GetEquippedCosmetics() const { return EquippedCosmetics; }
	const TMap<FName, bool>& GetSettingsFlags() const { return SettingsFlags; }

private:
	bool CanAdvanceRevision() const;

	UPROPERTY(SaveGame)
	int32 SaveVersion = CurrentSaveVersion;

	UPROPERTY(SaveGame)
	FString PlayerId;

	UPROPERTY(SaveGame)
	int64 Revision = 0;

	UPROPERTY(SaveGame)
	TSet<FPrimaryAssetId> Discoveries;

	UPROPERTY(SaveGame)
	TMap<FName, int64> Stats;

	UPROPERTY(SaveGame)
	TSet<FPrimaryAssetId> UnlockedCosmetics;

	UPROPERTY(SaveGame)
	TMap<FName, FPrimaryAssetId> EquippedCosmetics;

	UPROPERTY(SaveGame)
	TMap<FName, bool> SettingsFlags;
};
