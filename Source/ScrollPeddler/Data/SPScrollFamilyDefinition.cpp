#include "Data/SPScrollFamilyDefinition.h"

#include "Data/SPScrollEngravingDefinition.h"
#include "Misc/DataValidation.h"

const FPrimaryAssetType USPScrollFamilyDefinition::PrimaryAssetType(
	TEXT("SPScrollFamily"));

namespace
{
FSPScrollEffectSpec MakeEffect(
	const ESPResolvedScrollEffectKind Kind,
	const ESPScrollEffectTarget Target,
	const float Magnitude,
	const float Radius,
	const float DurationSeconds)
{
	FSPScrollEffectSpec Effect;
	Effect.Kind = Kind;
	Effect.Target = Target;
	Effect.Magnitude = Magnitude;
	Effect.Radius = Radius;
	Effect.DurationSeconds = DurationSeconds;
	return Effect;
}

FSPScrollFamilyTuning MakeFamily(
	const TCHAR* AssetName,
	const TCHAR* StableId,
	const ESPScrollBaseFamily Family,
	const FSPScrollEffectSpec& Effect)
{
	FSPScrollFamilyTuning Tuning;
	Tuning.BaseDefinitionId = FPrimaryAssetId(
		FPrimaryAssetType(TEXT("SPScroll")),
		AssetName);
	Tuning.StableId = StableId;
	Tuning.Family = Family;
	Tuning.BaseEffects.Add(Effect);
	return Tuning;
}
}

FPrimaryAssetId USPScrollFamilyDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(PrimaryAssetType, GetFName());
}

FSPScrollEngravingUseTuning SPBuildScrollEngravingUseTuning(
	const USPScrollEngravingDefinition& Definition)
{
	FSPScrollEngravingUseTuning Tuning;
	Tuning.EngravingDefinitionId = Definition.GetPrimaryAssetId();
	Tuning.StableId = Definition.StableId;
	Tuning.RadiusMultiplier = Definition.RadiusMultiplier;
	Tuning.DurationMultiplier = Definition.DurationMultiplier;
	// Existing engraving content authors these values as percentage points.
	Tuning.MalfunctionChanceDelta = FMath::Clamp(
		Definition.MisfireChanceDelta / 100.0f,
		-1.0f,
		1.0f);
	Tuning.AddedNoiseMagnitude = FMath::Max(
		0.0f,
		Definition.AddedNoise / 100.0f);
	return Tuning;
}

TArray<FSPScrollFamilyTuning> SPBuildVerticalSliceScrollFamilyTunings()
{
	TArray<FSPScrollFamilyTuning> Families;
	Families.Reserve(6);
	Families.Add(MakeFamily(
		TEXT("RuinousBrand"),
		TEXT("Scroll.RuinousBrand"),
		ESPScrollBaseFamily::Ruin,
		MakeEffect(
			ESPResolvedScrollEffectKind::Damage,
			ESPScrollEffectTarget::EnemiesInRadius,
			40.0f,
			250.0f,
			0.0f)));
	Families.Add(MakeFamily(
		TEXT("MendingCanticle"),
		TEXT("Scroll.MendingCanticle"),
		ESPScrollBaseFamily::Restoration,
		MakeEffect(
			ESPResolvedScrollEffectKind::Healing,
			ESPScrollEffectTarget::AlliesInRadius,
			30.0f,
			350.0f,
			0.0f)));
	Families.Add(MakeFamily(
		TEXT("AegisScript"),
		TEXT("Scroll.AegisScript"),
		ESPScrollBaseFamily::Ward,
		MakeEffect(
			ESPResolvedScrollEffectKind::Protection,
			ESPScrollEffectTarget::Self,
			50.0f,
			0.0f,
			8.0f)));
	Families.Add(MakeFamily(
		TEXT("Windstep"),
		TEXT("Scroll.Windstep"),
		ESPScrollBaseFamily::Traversal,
		MakeEffect(
			ESPResolvedScrollEffectKind::Movement,
			ESPScrollEffectTarget::Self,
			800.0f,
			0.0f,
			0.25f)));
	Families.Add(MakeFamily(
		TEXT("RevealingEye"),
		TEXT("Scroll.RevealingEye"),
		ESPScrollBaseFamily::Revelation,
		MakeEffect(
			ESPResolvedScrollEffectKind::Detection,
			ESPScrollEffectTarget::Area,
			1.0f,
			1200.0f,
			6.0f)));
	Families.Add(MakeFamily(
		TEXT("DA_Scroll_VeilOfSilence"),
		TEXT("Scroll.VeilOfSilence"),
		ESPScrollBaseFamily::Resonance,
		MakeEffect(
			ESPResolvedScrollEffectKind::Noise,
			ESPScrollEffectTarget::Area,
			-0.75f,
			650.0f,
			8.0f)));
	return Families;
}

#if WITH_EDITOR
EDataValidationResult USPScrollFamilyDefinition::IsDataValid(
	FDataValidationContext& Context) const
{
	bool bValid = !DisplayName.IsEmpty() && Tuning.IsValid();
	if (!bValid)
	{
		Context.AddError(NSLOCTEXT(
			"ScrollPeddler",
			"ScrollFamilyDefinitionInvalid",
			"Scroll family requires a display name, stable identities, valid effects, and finite malfunction tuning."));
	}
	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
