#include "Data/SPVerticalSliceDefinitions.h"

#include "Misc/DataValidation.h"

const FPrimaryAssetType USPContractDefinition::PrimaryAssetType(TEXT("SPContract"));
const FPrimaryAssetType USPCraftingRecipeDefinition::PrimaryAssetType(TEXT("SPCraftingRecipe"));
const FPrimaryAssetType USPDungeonSeedDefinition::PrimaryAssetType(TEXT("SPDungeonSeed"));
const FPrimaryAssetType USPThreatDefinition::PrimaryAssetType(TEXT("SPThreat"));
const FPrimaryAssetType USPFearEventDefinition::PrimaryAssetType(TEXT("SPFearEvent"));

bool FSPContractScrollCondition::IsConfigured() const
{
	return !BaseFamilyStableId.IsNone()
		&& Quantity > 0
		&& MaximumContamination >= 0.0f
		&& MaximumContamination <= 100.0f
		&& !AllowedEngravingStableIds.Contains(NAME_None);
}

FPrimaryAssetId USPContractDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

FPrimaryAssetId USPCraftingRecipeDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

FPrimaryAssetId USPDungeonSeedDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

FPrimaryAssetId USPThreatDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

FPrimaryAssetId USPFearEventDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

#if WITH_EDITOR
namespace
{
	bool ValidateIdentity(
		const FName StableId,
		const FText& DisplayName,
		FDataValidationContext& Context,
		const TCHAR* Label)
	{
		bool bValid = true;
		if (StableId.IsNone())
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("ScrollPeddler", "DefinitionMissingStableId", "{0} requires a StableId."),
				FText::FromString(FString(Label))));
			bValid = false;
		}
		if (DisplayName.IsEmpty())
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("ScrollPeddler", "DefinitionMissingDisplayName", "{0} requires a display name."),
				FText::FromString(FString(Label))));
			bValid = false;
		}
		return bValid;
	}
}

EDataValidationResult USPContractDefinition::IsDataValid(FDataValidationContext& Context) const
{
	bool bValid = ValidateIdentity(StableId, DisplayName, Context, TEXT("Contract definition"));
	bValid &= DangerTier >= 1 && DangerTier <= 5 && GoldReward >= 0 && GuildXpReward >= 0;

	switch (Kind)
	{
	case ESPContractKind::ScrollDelivery:
		if (!ScrollCondition.IsConfigured())
		{
			Context.AddError(NSLOCTEXT("ScrollPeddler", "ContractDeliveryInvalid", "Scroll delivery requires a valid scroll condition."));
			bValid = false;
		}
		break;
	case ESPContractKind::LargeCargoRecovery:
		if (CargoStableId.IsNone())
		{
			Context.AddError(NSLOCTEXT("ScrollPeddler", "ContractCargoInvalid", "Cargo recovery requires a cargo StableId."));
			bValid = false;
		}
		break;
	case ESPContractKind::PostRunProduction:
		if (SubmittedScrollStableId.IsNone() || ProductionWindowSeconds <= 0.0f)
		{
			Context.AddError(NSLOCTEXT("ScrollPeddler", "ContractProductionInvalid", "Production requires an output StableId and a positive window."));
			bValid = false;
		}
		break;
	}

	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}

EDataValidationResult USPCraftingRecipeDefinition::IsDataValid(FDataValidationContext& Context) const
{
	bool bValid = ValidateIdentity(StableId, DisplayName, Context, TEXT("Crafting recipe"));
	if (Ingredients.IsEmpty()
		|| Ingredients.ContainsByPredicate([](const FSPRecipeIngredient& Ingredient) { return !Ingredient.IsValid(); })
		|| OutputItemStableId.IsNone()
		|| OutputQuantity <= 0
		|| CraftDurationSeconds < 8.0f
		|| CraftDurationSeconds > 15.0f)
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "RecipeInvalid", "Recipe ingredients, output, or duration are invalid."));
		bValid = false;
	}
	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}

EDataValidationResult USPDungeonSeedDefinition::IsDataValid(FDataValidationContext& Context) const
{
	bool bValid = !StableId.IsNone() && Seed != 0 && RoomOrder.Num() == 8;
	for (uint8 RoleValue = 0; RoleValue < 8; ++RoleValue)
	{
		const ESPRoomRole Role = static_cast<ESPRoomRole>(RoleValue);
		int32 RoleCount = 0;
		for (const ESPRoomRole Candidate : RoomOrder)
		{
			RoleCount += Candidate == Role ? 1 : 0;
		}
		if (RoleCount != 1)
		{
			bValid = false;
			break;
		}
	}
	if (!bValid)
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "DungeonSeedInvalid", "A dungeon seed requires a StableId, non-zero seed, and every room role exactly once."));
	}
	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}

EDataValidationResult USPThreatDefinition::IsDataValid(FDataValidationContext& Context) const
{
	bool bValid = ValidateIdentity(StableId, DisplayName, Context, TEXT("Threat definition"));
	if (MaximumSimultaneous <= 0
		|| HearingRadius <= 0.0f
		|| ConfirmationSightRadius < 0.0f
		|| AttackPatterns.IsEmpty()
		|| AttackPatterns.ContainsByPredicate([](const FSPThreatAttackPattern& Pattern) { return !Pattern.IsValid(); }))
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "ThreatInvalid", "Threat senses, cap, or attack patterns are invalid."));
		bValid = false;
	}
	if (bPermanentlyKillable)
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "ThreatMustRetreat", "Vertical-slice threats use stagger and retreat instead of permanent death."));
		bValid = false;
	}
	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}

EDataValidationResult USPFearEventDefinition::IsDataValid(FDataValidationContext& Context) const
{
	bool bValid = !StableId.IsNone() && CooldownSeconds >= 0.0f;
	if (Kind == ESPFearEventKind::ScrollSneeze && !bEmitsAuthoritativeNoise)
	{
		Context.AddError(NSLOCTEXT("ScrollPeddler", "ScrollSneezeMustMakeNoise", "Scroll Sneeze must emit authoritative noise."));
		bValid = false;
	}
	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
