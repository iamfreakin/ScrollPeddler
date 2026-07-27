#pragma once

#include "CoreMinimal.h"
#include "Core/SPRunTypes.h"
#include "Core/SPTypes.h"
#include "Engine/DataAsset.h"
#include "SPVerticalSliceDefinitions.generated.h"

class FDataValidationContext;

UENUM(BlueprintType)
enum class ESPContractKind : uint8
{
	ScrollDelivery,
	LargeCargoRecovery,
	PostRunProduction
};

UENUM(BlueprintType)
enum class ESPRoomRole : uint8
{
	EntryRecords,
	StandardStacks,
	Office,
	FloodedArchive,
	Bindery,
	Incinerator,
	EchoHall,
	SealedStorage
};

UENUM(BlueprintType)
enum class ESPThreatArchetype : uint8
{
	EchoHunter,
	PaperEater
};

UENUM(BlueprintType)
enum class ESPFearEventKind : uint8
{
	Whispers,
	PeripheralShadow,
	EquipmentAlarm,
	FalseReturnSigil,
	ScrollSneeze
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPContractScrollCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName BaseFamilyStableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FName> AllowedEngravingStableIds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ESPScrollQuality MinimumQuality = ESPScrollQuality::D;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float MaximumContamination = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1"))
	int32 Quantity = 1;

	bool IsConfigured() const;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPRecipeIngredient
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName ItemStableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1"))
	int32 Quantity = 1;

	bool IsValid() const { return !ItemStableId.IsNone() && Quantity > 0; }
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPThreatAttackPattern
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName StableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float TelegraphSeconds = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ESPPlayerCondition ResultCondition = ESPPlayerCondition::Injured;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bDropHandItem = true;

	bool IsValid() const { return !StableId.IsNone() && TelegraphSeconds >= 0.0f; }
};

UCLASS(BlueprintType, Const)
class SCROLLPEDDLER_API USPContractDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName StableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contract")
	ESPContractKind Kind = ESPContractKind::ScrollDelivery;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Contract", meta = (ClampMin = "1", ClampMax = "5"))
	int32 DangerTier = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward", meta = (ClampMin = "0"))
	int32 GoldReward = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reward", meta = (ClampMin = "0"))
	int32 GuildXpReward = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Delivery")
	FSPContractScrollCondition ScrollCondition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recovery")
	FName CargoStableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Production")
	FName SubmittedScrollStableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Production", meta = (ClampMin = "1.0"))
	float ProductionWindowSeconds = 90.0f;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

UCLASS(BlueprintType, Const)
class SCROLLPEDDLER_API USPCraftingRecipeDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName StableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	TArray<FSPRecipeIngredient> Ingredients;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe")
	FName OutputItemStableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe", meta = (ClampMin = "1"))
	int32 OutputQuantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Recipe", meta = (ClampMin = "8.0", ClampMax = "15.0"))
	float CraftDurationSeconds = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Governance")
	bool bRequiresCampaignVote = false;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

UCLASS(BlueprintType, Const)
class SCROLLPEDDLER_API USPDungeonSeedDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName StableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
	int32 Seed = 0;

	/** Vertical slice seeds contain each of the eight room roles exactly once. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
	TArray<ESPRoomRole> RoomOrder;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

UCLASS(BlueprintType, Const)
class SCROLLPEDDLER_API USPThreatDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName StableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Threat")
	ESPThreatArchetype Archetype = ESPThreatArchetype::EchoHunter;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Threat", meta = (ClampMin = "1"))
	int32 MaximumSimultaneous = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Senses", meta = (ClampMin = "0.0"))
	float HearingRadius = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Senses", meta = (ClampMin = "0.0"))
	float ConfirmationSightRadius = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Threat")
	bool bPermanentlyKillable = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Threat")
	TArray<FSPThreatAttackPattern> AttackPatterns;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

UCLASS(BlueprintType, Const)
class SCROLLPEDDLER_API USPFearEventDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName StableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	ESPFearEventKind Kind = ESPFearEventKind::Whispers;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Event")
	bool bEmitsAuthoritativeNoise = false;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
