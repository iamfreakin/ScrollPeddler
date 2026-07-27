#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Data/SPVerticalSliceDefinitions.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPVerticalSliceDefinitionContractTest,
	"ScrollPeddler.Data.VerticalSliceDefinitionContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPVerticalSliceDefinitionContractTest::RunTest(const FString& Parameters)
{
	FSPContractScrollCondition Delivery;
	Delivery.BaseFamilyStableId = TEXT("Scroll.VeilOfSilence");
	Delivery.AllowedEngravingStableIds = { TEXT("Engraving.Stable") };
	Delivery.MinimumQuality = ESPScrollQuality::B;
	Delivery.MaximumContamination = 25.0f;
	Delivery.Quantity = 1;
	TestTrue(TEXT("A complete delivery condition is configured"), Delivery.IsConfigured());

	Delivery.AllowedEngravingStableIds.Add(NAME_None);
	TestFalse(TEXT("An empty allowed engraving is rejected"), Delivery.IsConfigured());

	const float BleedoutSequence[] = { 45.0f, 30.0f, 15.0f, 15.0f };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(BleedoutSequence); ++Index)
	{
		TestEqual(
			*FString::Printf(TEXT("Down %d uses the locked bleedout sequence"), Index + 1),
			SPGetBleedoutDurationSeconds(Index + 1),
			BleedoutSequence[Index]);
	}
	return true;
}

#endif
