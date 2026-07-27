#pragma once

#include "CoreMinimal.h"
#include "Data/SPVerticalSliceDefinitions.h"
#include "Engine/DataAsset.h"
#include "SPGrayboxThreatDefinition.generated.h"

/**
 * Runtime tuning profile for the graybox threat actor. Both archetypes use
 * stagger -> retreat instead of a permanent-death state.
 */
UCLASS(BlueprintType, Const)
class SCROLLPEDDLER_API USPGrayboxThreatDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	USPGrayboxThreatDefinition();

	static const FPrimaryAssetType PrimaryAssetType;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName StableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Threat")
	ESPThreatArchetype Archetype = ESPThreatArchetype::EchoHunter;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "1.0"))
	float MoveSpeed = 260.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "1.0"))
	float RetreatSpeed = 420.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defense", meta = (ClampMin = "0.0"))
	float StaggerSeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Defense", meta = (ClampMin = "0.0"))
	float RetreatSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Echo Hunter", meta = (ClampMin = "0.0"))
	float MinimumInvestigatedLoudness = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Echo Hunter", meta = (ClampMin = "1.0"))
	float MaximumHearingRadius = 2500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Echo Hunter", meta = (ClampMin = "0.01"))
	float HearingRadiusScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Echo Hunter", meta = (ClampMin = "1.0"))
	float InvestigationAcceptanceRadius = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Echo Hunter", meta = (ClampMin = "1.0"))
	float ConfirmationSightRadius = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Echo Hunter", meta = (ClampMin = "1.0", ClampMax = "89.0"))
	float ConfirmationHalfAngleDegrees = 60.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Echo Hunter", meta = (ClampMin = "1.0"))
	float AttackRange = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Echo Hunter", meta = (ClampMin = "0.0"))
	float AttackRecoverySeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Echo Hunter")
	FSPThreatAttackPattern AttackPattern;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paper Eater", meta = (ClampMin = "1.0"))
	float ItemSenseRadius = 1800.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paper Eater", meta = (ClampMin = "1.0"))
	float ItemInteractionRange = 110.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paper Eater", meta = (ClampMin = "0.0"))
	float ItemScanIntervalSeconds = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paper Eater", meta = (ClampMin = "0.0"))
	float CorruptionRecoverySeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paper Eater", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float ContaminationDelta = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Paper Eater")
	bool bRequestItemDamage = true;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

	bool IsRuntimeDefinitionValid() const;
};
