#pragma once

#include "CoreMinimal.h"
#include "Core/SPItemTypes.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "SPItemDefinition.generated.h"

class FDataValidationContext;
class UStaticMesh;

/** Data-driven definition for generic materials, equipment, and large cargo. */
UCLASS(BlueprintType, Const)
class SCROLLPEDDLER_API USPItemDefinition : public UPrimaryDataAsset
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	ESPItemKind Kind = ESPItemKind::Material;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	int32 MaxStackSize = 10;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	bool bHandOnly = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (AssetBundles = "Pickup"))
	TSoftObjectPtr<UStaticMesh> PickupMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Tags")
	FGameplayTagContainer Tags;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void RefreshInventoryRules();
};
