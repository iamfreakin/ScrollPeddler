#include "Data/SPGrayboxThreatDefinition.h"

const FPrimaryAssetType USPGrayboxThreatDefinition::PrimaryAssetType(
	TEXT("SPGrayboxThreat"));

USPGrayboxThreatDefinition::USPGrayboxThreatDefinition()
{
	AttackPattern.StableId = TEXT("EchoHunter_TelegraphedCharge");
	AttackPattern.TelegraphSeconds = 0.8f;
	AttackPattern.ResultCondition = ESPPlayerCondition::Down;
	AttackPattern.bDropHandItem = true;
}

FPrimaryAssetId USPGrayboxThreatDefinition::GetPrimaryAssetId() const
{
	const FName AssetName = StableId.IsNone() ? GetFName() : StableId;
	return FPrimaryAssetId(PrimaryAssetType, AssetName);
}

bool USPGrayboxThreatDefinition::IsRuntimeDefinitionValid() const
{
	const bool bCommonValid = !StableId.IsNone()
		&& FMath::IsFinite(MoveSpeed) && MoveSpeed > 0.0f
		&& FMath::IsFinite(RetreatSpeed) && RetreatSpeed > 0.0f
		&& FMath::IsFinite(StaggerSeconds) && StaggerSeconds >= 0.0f
		&& FMath::IsFinite(RetreatSeconds) && RetreatSeconds >= 0.0f;
	if (!bCommonValid)
	{
		return false;
	}

	if (Archetype == ESPThreatArchetype::EchoHunter)
	{
		return FMath::IsFinite(MinimumInvestigatedLoudness)
			&& MinimumInvestigatedLoudness >= 0.0f
			&& FMath::IsFinite(MaximumHearingRadius)
			&& MaximumHearingRadius > 0.0f
			&& FMath::IsFinite(HearingRadiusScale)
			&& HearingRadiusScale > 0.0f
			&& FMath::IsFinite(InvestigationAcceptanceRadius)
			&& InvestigationAcceptanceRadius > 0.0f
			&& FMath::IsFinite(ConfirmationSightRadius)
			&& ConfirmationSightRadius > 0.0f
			&& FMath::IsFinite(ConfirmationHalfAngleDegrees)
			&& ConfirmationHalfAngleDegrees > 0.0f
			&& ConfirmationHalfAngleDegrees < 90.0f
			&& FMath::IsFinite(AttackRange)
			&& AttackRange > 0.0f
			&& FMath::IsFinite(AttackRecoverySeconds)
			&& AttackRecoverySeconds >= 0.0f
			&& AttackPattern.IsValid()
			&& (AttackPattern.ResultCondition == ESPPlayerCondition::Injured
				|| AttackPattern.ResultCondition == ESPPlayerCondition::Down);
	}

	return FMath::IsFinite(ItemSenseRadius)
		&& ItemSenseRadius > 0.0f
		&& FMath::IsFinite(ItemInteractionRange)
		&& ItemInteractionRange > 0.0f
		&& ItemInteractionRange <= ItemSenseRadius
		&& FMath::IsFinite(ItemScanIntervalSeconds)
		&& ItemScanIntervalSeconds >= 0.0f
		&& FMath::IsFinite(CorruptionRecoverySeconds)
		&& CorruptionRecoverySeconds >= 0.0f
		&& FMath::IsFinite(ContaminationDelta)
		&& ContaminationDelta >= 0.0f
		&& (ContaminationDelta > 0.0f || bRequestItemDamage);
}
