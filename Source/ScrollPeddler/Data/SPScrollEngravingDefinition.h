#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "SPScrollEngravingDefinition.generated.h"

class FDataValidationContext;

UCLASS(BlueprintType, Const)
class SCROLLPEDDLER_API USPScrollEngravingDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName StableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tradeoff", meta = (MultiLine = true))
	FText Advantage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tradeoff", meta = (MultiLine = true))
	FText Cost;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifiers", meta = (ClampMin = "0.1"))
	float DurationMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifiers", meta = (ClampMin = "0.1"))
	float RadiusMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifiers")
	float MisfireChanceDelta = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Modifiers")
	float AddedNoise = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tags")
	FGameplayTagContainer Tags;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
