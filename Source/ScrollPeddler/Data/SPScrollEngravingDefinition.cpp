#include "Data/SPScrollEngravingDefinition.h"

#include "Misc/DataValidation.h"

const FPrimaryAssetType USPScrollEngravingDefinition::PrimaryAssetType(TEXT("SPScrollEngraving"));

FPrimaryAssetId USPScrollEngravingDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

#if WITH_EDITOR
EDataValidationResult USPScrollEngravingDefinition::IsDataValid(FDataValidationContext& Context) const
{
	bool bIsValid = true;

	if (StableId.IsNone())
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "EngravingMissingStableId", "Engraving requires a StableId."));
		bIsValid = false;
	}
	if (DisplayName.IsEmpty())
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "EngravingMissingName", "Engraving requires a display name."));
		bIsValid = false;
	}
	if (Advantage.IsEmpty() || Cost.IsEmpty())
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "EngravingMissingTradeoff", "Engraving requires both an advantage and a cost."));
		bIsValid = false;
	}
	if (DurationMultiplier <= 0.0f || RadiusMultiplier <= 0.0f)
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "EngravingInvalidMultiplier", "Engraving multipliers must be greater than zero."));
		bIsValid = false;
	}

	return bIsValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
