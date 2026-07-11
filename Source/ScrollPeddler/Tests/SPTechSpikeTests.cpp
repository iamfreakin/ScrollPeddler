#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/SPTypes.h"
#include "Data/SPScrollDefinition.h"
#include "Data/SPScrollEngravingDefinition.h"
#include "Engine/AssetManager.h"
#include "Persistence/SPSaveGame.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPScrollAxesRemainIndependentTest,
	"ScrollPeddler.Data.ScrollAxesRemainIndependent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPScrollAxesRemainIndependentTest::RunTest(const FString& Parameters)
{
	FSPScrollInstance Instance;
	Instance.InstanceId = FGuid::NewGuid();
	Instance.BaseDefinitionId = FPrimaryAssetId(FPrimaryAssetType(TEXT("SPScroll")), TEXT("Family"));
	Instance.EngravingDefinitionId = FPrimaryAssetId(FPrimaryAssetType(TEXT("SPScrollEngraving")), TEXT("Stable"));
	Instance.Quality = ESPScrollQuality::S;
	Instance.Contamination = 37.0f;
	Instance.Misfire = ESPMisfireType::ExtraNoise;

	TestTrue(TEXT("A fully identified instance is valid"), Instance.IsValid());
	TestEqual(TEXT("Engraving stays independent from quality"), Instance.Quality, ESPScrollQuality::S);
	TestEqual(TEXT("Contamination stays independent from engraving"), Instance.Contamination, 37.0f);
	TestEqual(TEXT("Misfire stays on its own axis"), Instance.Misfire, ESPMisfireType::ExtraNoise);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPScrollDataAssetCompositionTest,
	"ScrollPeddler.Data.PrimaryAssetComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPScrollDataAssetCompositionTest::RunTest(const FString& Parameters)
{
	UAssetManager& AssetManager = UAssetManager::Get();
	const FPrimaryAssetId ExpectedScrollId(
		USPScrollDefinition::PrimaryAssetType, TEXT("DA_Scroll_VeilOfSilence"));
	const FPrimaryAssetId ExpectedAmplifiedId(
		USPScrollEngravingDefinition::PrimaryAssetType, TEXT("DA_Engraving_Amplified"));
	const FPrimaryAssetId ExpectedStableId(
		USPScrollEngravingDefinition::PrimaryAssetType, TEXT("DA_Engraving_Stable"));
	TestTrue(TEXT("AssetManager registers the base family"),
		AssetManager.GetPrimaryAssetPath(ExpectedScrollId).IsValid());
	TestTrue(TEXT("AssetManager registers the amplified engraving"),
		AssetManager.GetPrimaryAssetPath(ExpectedAmplifiedId).IsValid());
	TestTrue(TEXT("AssetManager registers the stable engraving"),
		AssetManager.GetPrimaryAssetPath(ExpectedStableId).IsValid());

	const USPScrollDefinition* Scroll = LoadObject<USPScrollDefinition>(
		nullptr,
		TEXT("/Game/Data/Scrolls/DA_Scroll_VeilOfSilence.DA_Scroll_VeilOfSilence"));
	const USPScrollEngravingDefinition* Amplified = LoadObject<USPScrollEngravingDefinition>(
		nullptr,
		TEXT("/Game/Data/Engravings/DA_Engraving_Amplified.DA_Engraving_Amplified"));
	const USPScrollEngravingDefinition* Stable = LoadObject<USPScrollEngravingDefinition>(
		nullptr,
		TEXT("/Game/Data/Engravings/DA_Engraving_Stable.DA_Engraving_Stable"));

	TestNotNull(TEXT("The base scroll family asset loads"), Scroll);
	TestNotNull(TEXT("The amplified engraving asset loads"), Amplified);
	TestNotNull(TEXT("The stable engraving asset loads"), Stable);
	if (!Scroll || !Amplified || !Stable)
	{
		return false;
	}

	TestEqual(TEXT("The spike family exposes exactly two engravings"), Scroll->AllowedEngravings.Num(), 2);
	TestTrue(TEXT("Amplified is allowed"), Scroll->AllowsEngraving(Amplified));
	TestTrue(TEXT("Stable is allowed"), Scroll->AllowsEngraving(Stable));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPSettlementIdempotencyTest,
	"ScrollPeddler.Persistence.SettlementIsIdempotent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPSettlementIdempotencyTest::RunTest(const FString& Parameters)
{
	USPSaveGame* Save = NewObject<USPSaveGame>();
	FSPSessionResult Result;
	Result.SessionId = FGuid::NewGuid();
	Result.PlayerId = TEXT("AutomationPlayer");
	Result.PartySize = 2;
	Result.bExtracted = true;
	Result.GoldDelta = 40;
	Result.CompletedAtUnixSeconds = FDateTime::UtcNow().ToUnixTimestamp();
	Result.ResultHash = SPBuildSessionResultHash(Result);

	TestTrue(TEXT("First settlement applies"), Save->ApplyResultIfNew(Result));
	TestTrue(TEXT("Exact replay is an idempotent success"), Save->ApplyResultIfNew(Result));
	TestTrue(TEXT("Session ledger contains the result"), Save->HasCommittedSession(Result.SessionId));
	TestEqual(TEXT("Gold is awarded once"), Save->GetGold(), 40);
	TestEqual(TEXT("Session count increments once"), Save->GetSessionsPlayed(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPSessionResultUnicodeHashTest,
	"ScrollPeddler.Persistence.ResultHashUsesCanonicalUtf8",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPSessionResultUnicodeHashTest::RunTest(const FString& Parameters)
{
	FSPSessionResult First;
	TestTrue(TEXT("Fixed session id parses"), FGuid::Parse(
		TEXT("01234567-89ab-cdef-1020-304050607080"), First.SessionId));
	First.PlayerId = TEXT("상인_가");
	First.BuildVersion = TEXT("0.1.0-tech-spike");
	First.PartySize = 2;
	First.bExtracted = true;
	First.PickedUpCount = 3;
	First.ConsumedScrollCount = 1;
	First.ExtractedScrollCount = 2;
	First.GoldDelta = 40;
	First.CompletedAtUnixSeconds = 1783782000;
	First.ResultHash = SPBuildSessionResultHash(First);
	TestEqual(TEXT("Known Unicode input hashes its canonical UTF-8 bytes"),
		First.ResultHash, FString(TEXT("baf57c21372b1122c1c6ed72ef4c5add")));

	FSPSessionResult Same = First;
	TestEqual(TEXT("Canonical UTF-8 hashing is deterministic"),
		SPBuildSessionResultHash(Same), First.ResultHash);
	TestTrue(TEXT("Unicode result passes integrity validation"),
		SPIsSessionResultIntegrityValid(First));
	TestTrue(TEXT("Exact Unicode identity is idempotent"),
		SPIsIdempotentSessionResult(First, Same));

	FSPSessionResult DifferentPlayer = First;
	DifferentPlayer.PlayerId = TEXT("상인_나");
	TestFalse(TEXT("PlayerId must match even if a hash is copied"),
		SPIsIdempotentSessionResult(First, DifferentPlayer));
	DifferentPlayer.ResultHash = SPBuildSessionResultHash(DifferentPlayer);
	TestNotEqual(TEXT("Distinct Unicode PlayerIds produce distinct UTF-8 checksums"),
		DifferentPlayer.ResultHash, First.ResultHash);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPSettlementConflictTest,
	"ScrollPeddler.Persistence.SessionIdConflictIsRejected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPSettlementConflictTest::RunTest(const FString& Parameters)
{
	AddExpectedErrorPlain(
		TEXT("SP_SPIKE_SAVE_CONFLICT"), EAutomationExpectedErrorFlags::Contains, 2);

	USPSaveGame* Save = NewObject<USPSaveGame>();
	FSPSessionResult Original;
	Original.SessionId = FGuid::NewGuid();
	Original.PlayerId = TEXT("상인_원본");
	Original.PartySize = 2;
	Original.bExtracted = true;
	Original.GoldDelta = 40;
	Original.CompletedAtUnixSeconds = FDateTime::UtcNow().ToUnixTimestamp();
	Original.ResultHash = SPBuildSessionResultHash(Original);

	TestTrue(TEXT("Original settlement applies"), Save->ApplyResultIfNew(Original));

	FSPSessionResult DifferentPlayer = Original;
	DifferentPlayer.PlayerId = TEXT("상인_충돌");
	DifferentPlayer.ResultHash = SPBuildSessionResultHash(DifferentPlayer);
	TestFalse(TEXT("Same SessionId with another PlayerId is rejected"),
		Save->ApplyResultIfNew(DifferentPlayer));

	FSPSessionResult DifferentResult = Original;
	DifferentResult.GoldDelta = 400;
	DifferentResult.ResultHash = SPBuildSessionResultHash(DifferentResult);
	TestFalse(TEXT("Same SessionId with another ResultHash is rejected"),
		Save->ApplyResultIfNew(DifferentResult));

	const FSPSessionResult* Stored = Save->FindCommittedResult(Original.SessionId);
	TestNotNull(TEXT("Original result remains committed"), Stored);
	if (Stored)
	{
		TestEqual(TEXT("Stored PlayerId is unchanged"), Stored->PlayerId, Original.PlayerId);
		TestEqual(TEXT("Stored ResultHash is unchanged"), Stored->ResultHash, Original.ResultHash);
	}
	TestEqual(TEXT("Conflicts do not award gold"), Save->GetGold(), 40);
	TestEqual(TEXT("Conflicts do not increment sessions"), Save->GetSessionsPlayed(), 1);
	return true;
}

#endif
