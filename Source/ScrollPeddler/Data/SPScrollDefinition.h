#pragma once

#include "CoreMinimal.h"
#include "Core/SPTypes.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "SPScrollDefinition.generated.h"

class USPScrollEngravingDefinition;
class FDataValidationContext;

UCLASS(BlueprintType, Const)
class SCROLLPEDDLER_API USPScrollDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName StableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity", meta = (MultiLine = true))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect")
	ESPScrollEffectKind EffectKind = ESPScrollEffectKind::Silence;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect", meta = (ClampMin = "0.1"))
	float BaseDurationSeconds = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Effect", meta = (ClampMin = "0.0"))
	float BaseRadius = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Economy", meta = (ClampMin = "0"))
	int32 DeliveryValue = 100;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Engravings")
	TArray<TSoftObjectPtr<USPScrollEngravingDefinition>> AllowedEngravings;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tags")
	FGameplayTagContainer Tags;

	bool AllowsEngraving(const USPScrollEngravingDefinition* Engraving) const;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
