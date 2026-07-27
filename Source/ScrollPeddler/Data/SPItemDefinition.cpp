#include "Data/SPItemDefinition.h"

#include "Misc/DataValidation.h"

const FPrimaryAssetType USPItemDefinition::PrimaryAssetType(TEXT("SPItem"));

FPrimaryAssetId USPItemDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, StableId.IsNone() ? GetFName() : StableId);
}

#if WITH_EDITOR
EDataValidationResult USPItemDefinition::IsDataValid(FDataValidationContext& Context) const
{
	bool bIsValid = true;
	if (StableId.IsNone())
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "ItemMissingStableId", "Item definition requires a StableId."));
		bIsValid = false;
	}
	if (DisplayName.IsEmpty())
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "ItemMissingDisplayName", "Item definition requires a display name."));
		bIsValid = false;
	}
	if (!StaticEnum<ESPItemKind>()->IsValidEnumValue(static_cast<int64>(Kind)))
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "ItemInvalidKind", "Item definition has an invalid item kind."));
		bIsValid = false;
	}

	const int32 ExpectedMaxStackSize = Kind == ESPItemKind::Material ? 10 : 1;
	if (MaxStackSize != ExpectedMaxStackSize)
	{
		Context.AddError(NSLOCTEXT(
			"ScrollPeddler",
			"ItemInvalidMaxStack",
			"Materials must stack to 10 and all other item kinds must be unique."));
		bIsValid = false;
	}
	if (bHandOnly != (Kind == ESPItemKind::LargeCargo))
	{
		Context.AddError(NSLOCTEXT(
			"ScrollPeddler",
			"ItemInvalidHandOnly",
			"Only large cargo is hand-only in the current inventory contract."));
		bIsValid = false;
	}

	return bIsValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}

void USPItemDefinition::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	RefreshInventoryRules();
}
#endif

void USPItemDefinition::RefreshInventoryRules()
{
	MaxStackSize = Kind == ESPItemKind::Material ? 10 : 1;
	bHandOnly = Kind == ESPItemKind::LargeCargo;
}
