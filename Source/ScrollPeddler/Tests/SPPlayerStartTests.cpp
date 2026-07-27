#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Engine/Engine.h"
#include "Engine/PlayerStartPIE.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/SPGameMode.h"
#include "Game/SPPlayerState.h"
#include "Game/SPPlayerStartPolicy.h"
#include "Game/SPPlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"

namespace
{
class FSPScopedPlayerStartReservationTestWorld
{
public:
	FSPScopedPlayerStartReservationTestWorld()
	{
		if (!GEngine)
		{
			return;
		}

		const FName WorldName = MakeUniqueObjectName(
			nullptr,
			UWorld::StaticClass(),
			NAME_None,
			EUniqueObjectNameOptions::GloballyUnique);
		FWorldContext& WorldContext =
			GEngine->CreateNewWorldContext(EWorldType::Game);
		World = UWorld::CreateWorld(
			EWorldType::Game,
			false,
			WorldName,
			GetTransientPackage());
		if (!World)
		{
			return;
		}

		World->AddToRoot();
		WorldContext.SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
	}

	~FSPScopedPlayerStartReservationTestWorld()
	{
		if (!World || !GEngine)
		{
			return;
		}

		if (World->AreActorsInitialized())
		{
			for (AActor* Actor : FActorRange(World))
			{
				if (Actor)
				{
					Actor->RouteEndPlay(
						EEndPlayReason::LevelTransition);
				}
			}
		}
		GEngine->ShutdownWorldNetDriver(World);
		World->DestroyWorld(true);
		GEngine->DestroyWorldContext(World);
		World->RemoveFromRoot();
	}

	UWorld* Get() const
	{
		return World;
	}

private:
	UWorld* World = nullptr;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPPlayerStartSlotPolicyTest,
	"ScrollPeddler.PlayerStart.SlotPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPPlayerStartSlotPolicyTest::RunTest(const FString& Parameters)
{
	for (int32 SlotIndex = 0;
		SlotIndex < SPPlayerStartPolicy::SlotCount;
		++SlotIndex)
	{
		const FName SlotTag =
			SPPlayerStartPolicy::MakeSlotTag(SlotIndex);
		TestEqual(
			*FString::Printf(
				TEXT("Slot %d has a reversible canonical tag"),
				SlotIndex),
			SPPlayerStartPolicy::GetSlotIndex(SlotTag),
			SlotIndex);
	}

	TestEqual(TEXT("Unrelated PlayerStart tag is rejected"),
		SPPlayerStartPolicy::GetSlotIndex(TEXT("PlayerStart")),
		INDEX_NONE);
	TestEqual(TEXT("Out-of-range fixed tag is rejected"),
		SPPlayerStartPolicy::GetSlotIndex(
			TEXT("SP_PlayerStart_4")),
		INDEX_NONE);
	TestTrue(TEXT("Negative slot cannot produce a tag"),
		SPPlayerStartPolicy::MakeSlotTag(-1).IsNone());
	TestTrue(TEXT("Out-of-range slot cannot produce a tag"),
		SPPlayerStartPolicy::MakeSlotTag(
			SPPlayerStartPolicy::SlotCount).IsNone());

	const TSet<int32> ConfiguredSlots = {3, 1, 0, 2};
	TSet<int32> ReservedSlots;
	TestEqual(TEXT("Lowest configured slot is selected first"),
		SPPlayerStartPolicy::ChooseLowestAvailableSlot(
			ConfiguredSlots,
			ReservedSlots),
		0);
	ReservedSlots.Add(0);
	ReservedSlots.Add(2);
	TestEqual(TEXT("Selection ignores set iteration order"),
		SPPlayerStartPolicy::ChooseLowestAvailableSlot(
			ConfiguredSlots,
			ReservedSlots),
		1);
	ReservedSlots.Add(1);
	ReservedSlots.Add(3);
	TestEqual(TEXT("Fully reserved policy reports no slot"),
		SPPlayerStartPolicy::ChooseLowestAvailableSlot(
			ConfiguredSlots,
			ReservedSlots),
		INDEX_NONE);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPPlayerStartReservationTest,
	"ScrollPeddler.PlayerStart.GameModeReservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPPlayerStartReservationTest::RunTest(
	const FString& Parameters)
{
	FSPScopedPlayerStartReservationTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("PlayerStart test world exists"), World);
	if (!World)
	{
		return false;
	}

	ASPGameMode* GameMode = World->SpawnActor<ASPGameMode>();
	TestNotNull(TEXT("GameMode spawns"), GameMode);
	if (!GameMode)
	{
		return false;
	}

	TArray<APlayerStart*> ConfiguredStarts;
	ConfiguredStarts.SetNumZeroed(SPPlayerStartPolicy::SlotCount);
	for (int32 SlotIndex = SPPlayerStartPolicy::SlotCount - 1;
		SlotIndex >= 0;
		--SlotIndex)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = FName(*FString::Printf(
			TEXT("ConfiguredStart_%d"),
			SlotIndex));
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APlayerStart* Start = World->SpawnActor<APlayerStart>(
			APlayerStart::StaticClass(),
			FTransform(FVector(
				static_cast<double>(SlotIndex) * 200.0,
				0.0,
				100.0)),
			SpawnParameters);
		TestNotNull(
			*FString::Printf(
				TEXT("Configured start %d spawns"),
				SlotIndex),
			Start);
		if (!Start)
		{
			return false;
		}
		Start->PlayerStartTag =
			SPPlayerStartPolicy::MakeSlotTag(SlotIndex);
		ConfiguredStarts[SlotIndex] = Start;
	}

	APlayerStart* UntaggedStart =
		World->SpawnActor<APlayerStart>();
	TestNotNull(TEXT("Untagged fallback start spawns"),
		UntaggedStart);

	FActorSpawnParameters PieSpawnParameters;
	PieSpawnParameters.Name = TEXT("DisallowedPIEStart");
	PieSpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APlayerStartPIE* PieStart =
		World->SpawnActor<APlayerStartPIE>(
			APlayerStartPIE::StaticClass(),
			FTransform(FVector(9999.0, 0.0, 100.0)),
			PieSpawnParameters);
	TestNotNull(TEXT("PIE fallback start spawns"), PieStart);
	if (!PieStart)
	{
		return false;
	}
	PieStart->PlayerStartTag =
		SPPlayerStartPolicy::MakeSlotTag(0);

	TArray<APlayerController*> Controllers;
	for (int32 Index = 0;
		Index < SPPlayerStartPolicy::SlotCount + 1;
		++Index)
	{
		FActorSpawnParameters ControllerSpawnParameters;
		ControllerSpawnParameters.Name =
			FName(*FString::Printf(
				TEXT("StartPolicyController_%d"),
				Index));
		ControllerSpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		APlayerController* Controller =
			World->SpawnActor<APlayerController>(
				APlayerController::StaticClass(),
				FTransform::Identity,
				ControllerSpawnParameters);
		TestNotNull(
			*FString::Printf(
				TEXT("Controller %d spawns"),
				Index),
			Controller);
		if (!Controller)
		{
			return false;
		}
		Controllers.Add(Controller);
	}

	for (int32 SlotIndex = 0;
		SlotIndex < SPPlayerStartPolicy::SlotCount;
		++SlotIndex)
	{
		AActor* ChosenStart =
			GameMode->ChoosePlayerStart(Controllers[SlotIndex]);
		TestTrue(
			*FString::Printf(
				TEXT("Controller %d reserves its deterministic slot"),
				SlotIndex),
			ChosenStart == ConfiguredStarts[SlotIndex]);
	}

	TestTrue(TEXT("Same controller restart reuses its reservation"),
		GameMode->FindPlayerStart(Controllers[1])
			== ConfiguredStarts[1]);
	TestTrue(TEXT("Tagged PIE camera start is still ignored"),
		GameMode->ChoosePlayerStart(Controllers[0])
			== ConfiguredStarts[0]);

	AddExpectedError(
		TEXT("SP_PLAYER_START_UNAVAILABLE"),
		EAutomationExpectedErrorFlags::Contains,
		1);
	TestNull(TEXT("Fifth controller cannot share a reserved slot"),
		GameMode->ChoosePlayerStart(Controllers[4]));

	GameMode->Logout(Controllers[0]);
	AActor* ReassignedStart = GameMode->FindPlayerStart(
		Controllers[4],
		TEXT("SP_PlayerStart_3"));
	TestTrue(TEXT("Logout releases slot zero for the next controller"),
		ReassignedStart == ConfiguredStarts[0]);
	TestTrue(TEXT("Incoming portal cannot bypass slot reservation"),
		ReassignedStart != ConfiguredStarts[3]);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSPRejectedPlayerSpawnCleanupTest,
	"ScrollPeddler.PlayerStart.RejectedSpawnCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSPRejectedPlayerSpawnCleanupTest::RunTest(
	const FString& Parameters)
{
	FSPScopedPlayerStartReservationTestWorld ScopedWorld;
	UWorld* World = ScopedWorld.Get();
	TestNotNull(TEXT("Rejected spawn test world exists"), World);
	if (!World)
	{
		return false;
	}

	ASPGameMode* GameMode = World->SpawnActor<ASPGameMode>();
	ASPPlayerController* Controller =
		World->SpawnActor<ASPPlayerController>();
	ASPPlayerState* PlayerState =
		World->SpawnActor<ASPPlayerState>();
	APawn* Pawn = World->SpawnActor<APawn>();
	TestNotNull(TEXT("GameMode spawns"), GameMode);
	TestNotNull(TEXT("Controller spawns"), Controller);
	TestNotNull(TEXT("PlayerState spawns"), PlayerState);
	TestNotNull(TEXT("Pawn spawns"), Pawn);
	if (!GameMode || !Controller || !PlayerState || !Pawn)
	{
		return false;
	}

	Controller->SetPlayerState(PlayerState);
	Controller->Possess(Pawn);
	GameMode->ControllerRosterKeys.Add(
		Controller,
		TEXT("RejectedPlayer"));
	GameMode->PlayerStartSlotReservations.Add(Controller, 0);
	GameMode->RejectPlayerSpawn(Controller);

	TestEqual(TEXT("Rejected participation becomes spectator"),
		PlayerState->GetParticipationState(),
		ESPParticipationState::Spectating);
	TestFalse(TEXT("Rejected controller releases its slot"),
		GameMode->PlayerStartSlotReservations.Contains(Controller));
	TestFalse(TEXT("Rejected controller leaves the admitted roster map"),
		GameMode->ControllerRosterKeys.Contains(Controller));
	TestNull(TEXT("Rejected controller no longer possesses gameplay Pawn"),
		Controller->GetPawn());
	TestFalse(TEXT("Rejected gameplay Pawn is destroyed"),
		IsValid(Pawn));
	return true;
}

#endif
