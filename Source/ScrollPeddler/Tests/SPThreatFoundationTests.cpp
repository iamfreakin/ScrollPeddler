#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Components/StaticMeshComponent.h"
#include "Core/SPThreatDirectorTypes.h"
#include "Data/SPGrayboxThreatDefinition.h"
#include "World/SPGrayboxThreat.h"
#include "World/SPThreatRuntimeTypes.h"
#include "World/SPThreatTargetInterface.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPThreatDirectorBudgetTest,
	"ScrollPeddler.Threat.DirectorBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPThreatDirectorBudgetTest::RunTest(const FString& Parameters)
{
	FSPThreatDirectorConfig Config;
	TestTrue(TEXT("Default director config is valid"), Config.IsValid());

	FSPThreatDirectorState Initial;
	FSPThreatDirectorInputs Inputs;
	Inputs.CurrentServerTime = 10.0;
	Inputs.NoisePressure = 0.0f;
	Inputs.ActivePlayerCount = 1;
	const FSPThreatDirectorState Baseline =
		SPAdvanceThreatDirectorState(Initial, Config, Inputs);
	TestEqual(TEXT("Time produces baseline budget"),
		Baseline.AvailableBudget, 1.0f);
	TestEqual(TEXT("Advance records server time"),
		Baseline.LastServerTime, 10.0);

	Inputs.NoisePressure = 1.0f;
	const FSPThreatDirectorState Noisy =
		SPAdvanceThreatDirectorState(Initial, Config, Inputs);
	TestEqual(TEXT("Noise increases budget gain"),
		Noisy.AvailableBudget, 5.0f);

	Inputs.NoisePressure = 0.0f;
	Inputs.ActivePlayerCount = 4;
	const FSPThreatDirectorState FullParty =
		SPAdvanceThreatDirectorState(Initial, Config, Inputs);
	TestEqual(TEXT("Four players scale the baseline"),
		FullParty.AvailableBudget, 1.75f);

	Inputs.ActivePlayerCount = 0;
	const FSPThreatDirectorState EmptyRun =
		SPAdvanceThreatDirectorState(Initial, Config, Inputs);
	TestEqual(TEXT("No active players generate no threat"),
		EmptyRun.AvailableBudget, 0.0f);

	FSPThreatDirectorState NearCap;
	NearCap.AvailableBudget = 99.5f;
	Inputs.CurrentServerTime = 100.0;
	Inputs.NoisePressure = 1.0f;
	Inputs.ActivePlayerCount = 4;
	const FSPThreatDirectorState Capped =
		SPAdvanceThreatDirectorState(NearCap, Config, Inputs);
	TestEqual(TEXT("Budget is capped"), Capped.AvailableBudget, 100.0f);

	FSPThreatDirectorInputs BackwardInputs = Inputs;
	BackwardInputs.CurrentServerTime = 50.0;
	const FSPThreatDirectorState Backward =
		SPAdvanceThreatDirectorState(Capped, Config, BackwardInputs);
	TestEqual(TEXT("Backward time cannot change budget"),
		Backward.AvailableBudget, Capped.AvailableBudget);
	TestEqual(TEXT("Backward time cannot rewind state"),
		Backward.LastServerTime, Capped.LastServerTime);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPThreatDirectorFearCooldownTest,
	"ScrollPeddler.Threat.FearEventCooldown",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPThreatDirectorFearCooldownTest::RunTest(
	const FString& Parameters)
{
	FSPThreatDirectorConfig Config;
	FSPThreatDirectorState Ready;
	Ready.AvailableBudget = 30.0f;
	Ready.LastServerTime = 100.0;

	TestTrue(TEXT("Funded event is initially available"),
		SPCanTriggerFearEvent(Ready, Config, 100.0));
	const FSPThreatDirectorState Committed =
		SPCommitFearEvent(Ready, Config, 100.0);
	TestEqual(TEXT("Fear event spends its budget"),
		Committed.AvailableBudget, 20.0f);
	TestEqual(TEXT("Fear event starts a 30 second cooldown"),
		Committed.NextFearEventAllowedServerTime, 130.0);
	TestFalse(TEXT("Cooldown blocks an early repeat"),
		SPCanTriggerFearEvent(Committed, Config, 129.99));
	TestTrue(TEXT("Cooldown boundary permits another event"),
		SPCanTriggerFearEvent(Committed, Config, 130.0));

	const FSPThreatDirectorState EarlyReplay =
		SPCommitFearEvent(Committed, Config, 120.0);
	TestEqual(TEXT("Rejected replay does not spend budget"),
		EarlyReplay.AvailableBudget, Committed.AvailableBudget);
	TestEqual(TEXT("Rejected replay does not extend cooldown"),
		EarlyReplay.NextFearEventAllowedServerTime,
		Committed.NextFearEventAllowedServerTime);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPThreatArchetypeDefinitionTest,
	"ScrollPeddler.Threat.ArchetypeDefinitions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPThreatArchetypeDefinitionTest::RunTest(
	const FString& Parameters)
{
	USPGrayboxThreatDefinition* Echo =
		NewObject<USPGrayboxThreatDefinition>();
	Echo->StableId = TEXT("Threat.EchoHunter.Graybox");
	Echo->Archetype = ESPThreatArchetype::EchoHunter;
	TestTrue(TEXT("Default Echo Hunter profile is valid"),
		Echo->IsRuntimeDefinitionValid());
	TestEqual(TEXT("Echo attack has a fixed Down result"),
		Echo->AttackPattern.ResultCondition, ESPPlayerCondition::Down);
	TestTrue(TEXT("Echo attack explicitly requests a hand drop"),
		Echo->AttackPattern.bDropHandItem);
	TestTrue(TEXT("Echo attack is visibly telegraphed"),
		Echo->AttackPattern.TelegraphSeconds > 0.0f);

	USPGrayboxThreatDefinition* Paper =
		NewObject<USPGrayboxThreatDefinition>();
	Paper->StableId = TEXT("Threat.PaperEater.Graybox");
	Paper->Archetype = ESPThreatArchetype::PaperEater;
	TestTrue(TEXT("Default Paper Eater profile is valid"),
		Paper->IsRuntimeDefinitionValid());
	TestTrue(TEXT("Paper Eater requests contamination"),
		Paper->ContaminationDelta > 0.0f);
	TestTrue(TEXT("Paper Eater requests item damage"),
		Paper->bRequestItemDamage);

	Paper->ContaminationDelta = 0.0f;
	Paper->bRequestItemDamage = false;
	TestFalse(TEXT("Paper profile needs a real mutation"),
		Paper->IsRuntimeDefinitionValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPThreatSensingPolicyTest,
	"ScrollPeddler.Threat.SensingPolicies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPThreatSensingPolicyTest::RunTest(
	const FString& Parameters)
{
	TestTrue(TEXT("Large nearby noise is investigated"),
		SPShouldInvestigateNoise(
			FVector::ZeroVector,
			FVector(500.0, 0.0, 0.0),
			1.0f,
			1000.0f,
			0.65f,
			2500.0f,
			1.0f));
	TestFalse(TEXT("Quiet noise is ignored"),
		SPShouldInvestigateNoise(
			FVector::ZeroVector,
			FVector(100.0, 0.0, 0.0),
			0.5f,
			1000.0f,
			0.65f,
			2500.0f,
			1.0f));
	TestFalse(TEXT("Distant noise outside effective radius is ignored"),
		SPShouldInvestigateNoise(
			FVector::ZeroVector,
			FVector(1500.0, 0.0, 0.0),
			1.0f,
			1000.0f,
			0.65f,
			2500.0f,
			1.0f));

	const double NearbyWorldItem =
		SPCalculatePaperTargetPriority(
			ESPThreatItemTargetKind::WorldItem,
			1.0);
	const double DistantScroll =
		SPCalculatePaperTargetPriority(
			ESPThreatItemTargetKind::Scroll,
			1000000.0);
	TestTrue(TEXT("Scroll priority beats a closer ordinary item"),
		DistantScroll > NearbyWorldItem);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPGrayboxThreatActorContractTest,
	"ScrollPeddler.Threat.GrayboxActorContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPGrayboxThreatActorContractTest::RunTest(
	const FString& Parameters)
{
	const ASPGrayboxThreat* Threat = GetDefault<ASPGrayboxThreat>();
	const UStaticMeshComponent* ThreatMesh =
		Threat->FindComponentByClass<UStaticMeshComponent>();
	TestNotNull(TEXT("Graybox threat has a mesh"), ThreatMesh);
	TestTrue(TEXT("Threat actor replicates"), Threat->GetIsReplicated());
	TestTrue(TEXT("Threat movement replicates"),
		Threat->IsReplicatingMovement());
	TestFalse(TEXT("Threat has no permanent damage/death path"),
		Threat->CanBeDamaged());
	TestEqual(TEXT("Threat begins idle"),
		Threat->GetBehaviorState(), ESPThreatBehaviorState::Idle);
	TestNull(TEXT("Spawner must select a data profile"),
		Threat->GetThreatDefinition());
	TestNotNull(TEXT("Paper target interface is reflected"),
		USPThreatItemTarget::StaticClass());
	return true;
}

#endif
