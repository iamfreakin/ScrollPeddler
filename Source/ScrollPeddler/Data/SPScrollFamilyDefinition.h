#pragma once

#include "CoreMinimal.h"
#include "Core/SPScrollUseTypes.h"
#include "Engine/DataAsset.h"
#include "SPScrollFamilyDefinition.generated.h"

class FDataValidationContext;
class USPScrollEngravingDefinition;

/**
 * Data asset for base-family tuning. It supplements the collectible scroll
 * identity asset and is linked through BaseDefinitionId rather than asset path.
 */
UCLASS(BlueprintType, Const)
class SCROLLPEDDLER_API USPScrollFamilyDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FPrimaryAssetType PrimaryAssetType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Scroll")
	FSPScrollFamilyTuning Tuning;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};

/** Copies the independently-authored engraving asset into resolver-safe data. */
SCROLLPEDDLER_API FSPScrollEngravingUseTuning SPBuildScrollEngravingUseTuning(
	const USPScrollEngravingDefinition& Definition);

/**
 * Code defaults used by the asset-free vertical-slice smoke map and tests.
 * Production content may replace values with USPScrollFamilyDefinition assets
 * while preserving BaseDefinitionId and StableId.
 */
SCROLLPEDDLER_API TArray<FSPScrollFamilyTuning>
SPBuildVerticalSliceScrollFamilyTunings();
