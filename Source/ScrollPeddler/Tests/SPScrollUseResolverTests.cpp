#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Data/SPScrollFamilyDefinition.h"
#include "Game/SPScrollUseResolver.h"

namespace
{
const FGuid TestRequestId(0xA10B20C3, 0xD40E50F6, 0x11223344, 0x55667788);
const FGuid TestScrollId(0x12345678, 0x90ABCDEF, 0x10203040, 0x50607080);

FSPScrollEngravingUseTuning MakeEngraving(
	const float MagnitudeMultiplier = 1.0f,
	const float ChanceDelta = 0.0f,
	const float AddedNoise = 0.0f)
{
	FSPScrollEngravingUseTuning Engraving;
	Engraving.EngravingDefinitionId = FPrimaryAssetId(
		FPrimaryAssetType(TEXT("SPScrollEngraving")),
		TEXT("TestEngraving"));
	Engraving.StableId = TEXT("Engraving.Test");
	Engraving.MagnitudeMultiplier = MagnitudeMultiplier;
	Engraving.MalfunctionChanceDelta = ChanceDelta;
	Engraving.AddedNoiseMagnitude = AddedNoise;
	return Engraving;
}

struct FSPTestScrollUse
{
	FSPScrollUseRequest Request;
	FSPAuthoritativeScrollUseState AuthorityState;
};

FSPTestScrollUse MakeRequest(
	const FSPScrollFamilyTuning& Family,
	const FSPScrollEngravingUseTuning& Engraving)
{
	FSPTestScrollUse Use;
	Use.Request.RequestId = TestRequestId;
	Use.Request.ScrollInstanceId = TestScrollId;
	Use.Request.AimDirection = FVector(1.0, 0.25, 0.0);
	Use.AuthorityState.Scroll.InstanceId = TestScrollId;
	Use.AuthorityState.Scroll.BaseDefinitionId = Family.BaseDefinitionId;
	Use.AuthorityState.Scroll.EngravingDefinitionId =
		Engraving.EngravingDefinitionId;
	Use.AuthorityState.Scroll.Quality = ESPScrollQuality::B;
	Use.AuthorityState.Scroll.Contamination = 0.0f;
	Use.AuthorityState.Scroll.Misfire = ESPMisfireType::ExtraNoise;
	Use.AuthorityState.RunSeed = 739391;
	Use.AuthorityState.bServerAuthority = true;
	return Use;
}

const FSPResolvedScrollEffect* FindEffect(
	const FSPScrollUseResult& Result,
	const ESPResolvedScrollEffectKind Kind,
	const bool bMalfunctionOnly = false)
{
	return Result.Effects.FindByPredicate(
		[Kind, bMalfunctionOnly](const FSPResolvedScrollEffect& Effect)
		{
			return Effect.Kind == Kind
				&& (!bMalfunctionOnly || Effect.bGeneratedByMalfunction);
		});
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPVerticalSliceScrollFamiliesTest,
	"ScrollPeddler.ScrollUse.VerticalSliceFamilies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPVerticalSliceScrollFamiliesTest::RunTest(const FString& Parameters)
{
	const TArray<FSPScrollFamilyTuning> Families =
		SPBuildVerticalSliceScrollFamilyTunings();
	TestEqual(TEXT("Vertical slice defines six base families"), Families.Num(), 6);

	TSet<FName> StableIds;
	TSet<FPrimaryAssetId> DefinitionIds;
	TSet<ESPScrollBaseFamily> FamilyKinds;
	TSet<ESPResolvedScrollEffectKind> EffectKinds;
	const FSPScrollEngravingUseTuning Engraving = MakeEngraving();
	for (const FSPScrollFamilyTuning& Family : Families)
	{
		TestTrue(*FString::Printf(
			TEXT("%s has valid data-driven tuning"),
			*Family.StableId.ToString()), Family.IsValid());
		StableIds.Add(Family.StableId);
		DefinitionIds.Add(Family.BaseDefinitionId);
		FamilyKinds.Add(Family.Family);

		const FSPTestScrollUse Use = MakeRequest(Family, Engraving);
		const FSPScrollUseResult Result = FSPScrollUseResolver::Resolve(
			Use.Request,
			Use.AuthorityState,
			Family,
			Engraving);
		TestTrue(*FString::Printf(
			TEXT("%s resolves on authority"),
			*Family.StableId.ToString()), Result.IsAccepted());
		TestTrue(TEXT("An accepted family consumes exactly one scroll intent"),
			Result.bShouldConsumeScroll);
		if (!Family.BaseEffects.IsEmpty())
		{
			const ESPResolvedScrollEffectKind ExpectedKind =
				Family.BaseEffects[0].Kind;
			TestNotNull(TEXT("Resolved result contains its authored effect"),
				FindEffect(Result, ExpectedKind));
			EffectKinds.Add(ExpectedKind);
		}
	}

	TestEqual(TEXT("Every base family has a unique StableId"), StableIds.Num(), 6);
	TestEqual(TEXT("Every base family links a unique definition"), DefinitionIds.Num(), 6);
	TestEqual(TEXT("All six gameplay family categories are represented"), FamilyKinds.Num(), 6);
	TestEqual(TEXT("Damage, healing, protection, movement, detection and noise are represented"),
		EffectKinds.Num(), 6);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPScrollUseIndependentAxesTest,
	"ScrollPeddler.ScrollUse.IndependentAxes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPScrollUseIndependentAxesTest::RunTest(const FString& Parameters)
{
	FSPScrollFamilyTuning Family =
		SPBuildVerticalSliceScrollFamilyTunings()[0];
	Family.BaseMalfunctionChance = 0.0f;
	Family.ContaminationChanceAtMaximum = 1.0f;
	const FSPScrollEngravingUseTuning Engraving =
		MakeEngraving(2.0f);

	FSPTestScrollUse CleanUse = MakeRequest(Family, Engraving);
	CleanUse.AuthorityState.Scroll.Quality = ESPScrollQuality::D;
	CleanUse.AuthorityState.Scroll.Misfire = ESPMisfireType::ExtraNoise;
	const FSPScrollUseResult CleanResult =
		FSPScrollUseResolver::Resolve(
			CleanUse.Request,
			CleanUse.AuthorityState,
			Family,
			Engraving);
	TestEqual(TEXT("Clean scroll has no malfunction chance"),
		CleanResult.MalfunctionChance, 0.0f);
	TestEqual(TEXT("Clean scroll does not malfunction"),
		CleanResult.Malfunction, ESPScrollMalfunctionOutcome::None);
	const FSPResolvedScrollEffect* CrudeDamage =
		FindEffect(CleanResult, ESPResolvedScrollEffectKind::Damage);
	TestNotNull(TEXT("Crude engraved scroll still deals damage"), CrudeDamage);
	if (CrudeDamage)
	{
		TestEqual(TEXT("Quality and engraving multiply magnitude independently"),
			CrudeDamage->Magnitude, 56.0f);
	}

	FSPTestScrollUse ContaminatedUse = CleanUse;
	ContaminatedUse.AuthorityState.Scroll.Contamination = 100.0f;
	const FSPScrollUseResult ContaminatedResult =
		FSPScrollUseResolver::Resolve(
			ContaminatedUse.Request,
			ContaminatedUse.AuthorityState,
			Family,
			Engraving);
	TestEqual(TEXT("Maximum contamination independently reaches configured chance"),
		ContaminatedResult.MalfunctionChance, 1.0f);
	TestEqual(TEXT("Configured malfunction axis selects extra noise"),
		ContaminatedResult.Malfunction,
		ESPScrollMalfunctionOutcome::ExtraNoise);
	const FSPResolvedScrollEffect* ContaminatedDamage =
		FindEffect(ContaminatedResult, ESPResolvedScrollEffectKind::Damage);
	TestNotNull(TEXT("Extra-noise malfunction retains the base effect"),
		ContaminatedDamage);
	if (CrudeDamage && ContaminatedDamage)
	{
		TestEqual(TEXT("Contamination does not replace quality or engraving magnitude"),
			ContaminatedDamage->Magnitude, CrudeDamage->Magnitude);
	}
	TestNotNull(TEXT("Malfunction emits a separately marked noise command"),
		FindEffect(
			ContaminatedResult,
			ESPResolvedScrollEffectKind::Noise,
			true));

	FSPTestScrollUse MasterworkUse = CleanUse;
	MasterworkUse.AuthorityState.Scroll.Quality = ESPScrollQuality::S;
	const FSPScrollUseResult MasterworkResult =
		FSPScrollUseResolver::Resolve(
			MasterworkUse.Request,
			MasterworkUse.AuthorityState,
			Family,
			Engraving);
	const FSPResolvedScrollEffect* MasterworkDamage =
		FindEffect(MasterworkResult, ESPResolvedScrollEffectKind::Damage);
	TestNotNull(TEXT("Masterwork resolves damage"), MasterworkDamage);
	if (MasterworkDamage)
	{
		TestEqual(TEXT("S quality changes only its own multiplier"),
			MasterworkDamage->Magnitude, 116.0f);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPScrollUseDeterminismAndAuthorityTest,
	"ScrollPeddler.ScrollUse.DeterminismAndAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPScrollUseDeterminismAndAuthorityTest::RunTest(const FString& Parameters)
{
	FSPScrollFamilyTuning Family =
		SPBuildVerticalSliceScrollFamilyTunings()[3];
	Family.BaseMalfunctionChance = 1.0f;
	Family.ContaminationChanceAtMaximum = 0.0f;
	Family.DirectionShiftDegrees = 90.0f;
	const FSPScrollEngravingUseTuning Engraving = MakeEngraving();
	FSPTestScrollUse Use = MakeRequest(Family, Engraving);
	Use.AuthorityState.Scroll.Misfire = ESPMisfireType::DirectionShift;

	const FSPScrollUseResult First =
		FSPScrollUseResolver::Resolve(
			Use.Request,
			Use.AuthorityState,
			Family,
			Engraving);
	const FSPScrollUseResult Replay =
		FSPScrollUseResolver::Resolve(
			Use.Request,
			Use.AuthorityState,
			Family,
			Engraving);
	FSPScrollUseRequest AlternateClientRequest = Use.Request;
	AlternateClientRequest.RequestId = FGuid(
		0xCAFEBABE,
		0x01020304,
		0x55667788,
		0x99AABBCC);
	const FSPScrollUseResult AlternateRequestResult =
		FSPScrollUseResolver::Resolve(
			AlternateClientRequest,
			Use.AuthorityState,
			Family,
			Engraving);
	TestTrue(TEXT("Authority request is accepted"), First.IsAccepted());
	TestEqual(TEXT("Replay has the exact deterministic roll"),
		Replay.MalfunctionRoll, First.MalfunctionRoll);
	TestEqual(TEXT("Replay has the exact malfunction"),
		Replay.Malfunction, First.Malfunction);
	TestEqual(TEXT("Replay has the same effect count"),
		Replay.Effects.Num(), First.Effects.Num());
	TestEqual(TEXT("Client-selected request IDs cannot alter the malfunction roll"),
		AlternateRequestResult.MalfunctionRoll, First.MalfunctionRoll);
	TestEqual(TEXT("Client-selected request IDs cannot alter the malfunction"),
		AlternateRequestResult.Malfunction, First.Malfunction);
	if (!First.Effects.IsEmpty() && !Replay.Effects.IsEmpty())
	{
		TestTrue(TEXT("Replay has the exact shifted direction"),
			Replay.Effects[0].Direction.Equals(
				First.Effects[0].Direction,
				KINDA_SMALL_NUMBER));
		TestFalse(TEXT("Direction-shift malfunction changes the submitted aim"),
			First.Effects[0].Direction.Equals(
				Use.Request.AimDirection.GetSafeNormal(),
				KINDA_SMALL_NUMBER));
		if (!AlternateRequestResult.Effects.IsEmpty())
		{
			TestTrue(TEXT("Client-selected request IDs cannot alter direction shift"),
				AlternateRequestResult.Effects[0].Direction.Equals(
					First.Effects[0].Direction,
					KINDA_SMALL_NUMBER));
		}
	}

	Use.AuthorityState.bServerAuthority = false;
	const FSPScrollUseResult ClientResult =
		FSPScrollUseResolver::Resolve(
			Use.Request,
			Use.AuthorityState,
			Family,
			Engraving);
	TestEqual(TEXT("Client-only resolution is rejected"),
		ClientResult.Code, ESPScrollUseResultCode::NotAuthority);
	TestFalse(TEXT("Rejected client intent never consumes a scroll"),
		ClientResult.bShouldConsumeScroll);
	TestEqual(TEXT("Rejected client intent emits no gameplay commands"),
		ClientResult.Effects.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPScrollUseMalfunctionModesTest,
	"ScrollPeddler.ScrollUse.MalfunctionModes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPScrollUseMalfunctionModesTest::RunTest(const FString& Parameters)
{
	FSPScrollFamilyTuning Family =
		SPBuildVerticalSliceScrollFamilyTunings()[1];
	Family.BaseMalfunctionChance = 1.0f;
	Family.ContaminationChanceAtMaximum = 0.0f;
	Family.MalfunctionDelaySeconds = 2.25f;
	const FSPScrollEngravingUseTuning Engraving = MakeEngraving();
	FSPTestScrollUse Use = MakeRequest(Family, Engraving);

	Use.AuthorityState.Scroll.Misfire = ESPMisfireType::None;
	FSPScrollUseResult Result =
		FSPScrollUseResolver::Resolve(
			Use.Request,
			Use.AuthorityState,
			Family,
			Engraving);
	TestEqual(TEXT("No authored malfunction type fizzles when the roll fails"),
		Result.Malfunction, ESPScrollMalfunctionOutcome::Fizzle);
	TestEqual(TEXT("Fizzle emits no effect"), Result.Effects.Num(), 0);
	TestTrue(TEXT("Fizzle still consumes the used scroll"), Result.bShouldConsumeScroll);

	Use.AuthorityState.Scroll.Misfire = ESPMisfireType::Delay;
	Result = FSPScrollUseResolver::Resolve(
		Use.Request,
		Use.AuthorityState,
		Family,
		Engraving);
	TestEqual(TEXT("Delay type is preserved"),
		Result.Malfunction, ESPScrollMalfunctionOutcome::Delay);
	TestEqual(TEXT("Delay uses family tuning"),
		Result.ApplicationDelaySeconds, 2.25f);
	TestTrue(TEXT("Delayed effect remains available to schedule"),
		!Result.Effects.IsEmpty());

	Use.AuthorityState.Scroll.Misfire = ESPMisfireType::ExtraNoise;
	Result = FSPScrollUseResolver::Resolve(
		Use.Request,
		Use.AuthorityState,
		Family,
		Engraving);
	TestNotNull(TEXT("Extra noise is explicitly marked"),
		FindEffect(Result, ESPResolvedScrollEffectKind::Noise, true));
	return true;
}

#endif
