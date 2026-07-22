#include "Data/SPScrollDefinition.h"

#include "Data/SPScrollEngravingDefinition.h"
#include "Misc/DataValidation.h"

const FPrimaryAssetType USPScrollDefinition::PrimaryAssetType(TEXT("SPScroll"));

bool USPScrollDefinition::AllowsEngraving(const USPScrollEngravingDefinition* Engraving) const
{
	if (!Engraving)
	{
		return false;
	}

	const FSoftObjectPath EngravingPath(Engraving);
	return AllowedEngravings.ContainsByPredicate(
		[&EngravingPath](const TSoftObjectPtr<USPScrollEngravingDefinition>& Candidate)
		{
			return Candidate.ToSoftObjectPath() == EngravingPath;
		});
}

FPrimaryAssetId USPScrollDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

#if WITH_EDITOR
EDataValidationResult USPScrollDefinition::IsDataValid(FDataValidationContext& Context) const
{
	bool bIsValid = true;

	if (StableId.IsNone())
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "ScrollMissingStableId", "Scroll definition requires a StableId."));
		bIsValid = false;
	}
	if (DisplayName.IsEmpty())
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "ScrollMissingName", "Scroll definition requires a display name."));
		bIsValid = false;
	}
	if (AllowedEngravings.Num() < 2 || AllowedEngravings.Num() > 4)
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "ScrollInvalidEngravingCount", "A scroll family must allow between two and four engravings."));
		bIsValid = false;
	}
	if (AllowedEngravings.ContainsByPredicate(
		[](const TSoftObjectPtr<USPScrollEngravingDefinition>& Engraving)
		{
			return Engraving.IsNull();
		}))
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "ScrollNullEngraving", "Allowed engravings cannot contain an empty reference."));
		bIsValid = false;
	}
	if (PickupMesh.IsNull())
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "ScrollMissingPickupMesh", "Scroll definition requires a pickup mesh."));
		bIsValid = false;
	}
	else if (!PickupMesh.LoadSynchronous())
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "ScrollUnresolvedPickupMesh", "Scroll pickup mesh must reference a loadable static mesh."));
		bIsValid = false;
	}
	if (BaseDurationSeconds <= 0.0f || BaseRadius < 0.0f)
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "ScrollInvalidEffectValues", "Scroll effect values are outside the supported range."));
		bIsValid = false;
	}

	return bIsValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
