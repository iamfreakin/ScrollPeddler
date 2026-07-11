#include "Game/SPGameMode.h"

#include "Core/SPTypes.h"
#include "Data/SPScrollDefinition.h"
#include "Data/SPScrollEngravingDefinition.h"
#include "Engine/World.h"
#include "Game/SPGameState.h"
#include "Game/SPPlayerController.h"
#include "Game/SPPlayerState.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Player/SPCharacter.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SPExtractionZone.h"
#include "World/SPGrayboxBlock.h"
#include "World/SPGrayboxLighting.h"
#include "World/SPScrollPickup.h"

DEFINE_LOG_CATEGORY_STATIC(LogSPGameMode, Log, All);

namespace
{
constexpr int32 MinSpikePlayers = 1;
constexpr int32 MaxSpikePlayers = 4;

struct FGrayboxBlockSpec
{
	FVector Location;
	FVector Scale;
};
}

ASPGameMode::ASPGameMode()
{
	DefaultPawnClass = ASPCharacter::StaticClass();
	PlayerControllerClass = ASPPlayerController::StaticClass();
	PlayerStateClass = ASPPlayerState::StaticClass();
	GameStateClass = ASPGameState::StaticClass();
	bUseSeamlessTravel = false;

	static ConstructorHelpers::FObjectFinder<USPScrollDefinition> ScrollDefinition(
		TEXT("/Game/Data/Scrolls/DA_Scroll_VeilOfSilence.DA_Scroll_VeilOfSilence"));
	SpikeScrollDefinition = ScrollDefinition.Object;

	static ConstructorHelpers::FObjectFinder<USPScrollEngravingDefinition> AmplifiedDefinition(
		TEXT("/Game/Data/Engravings/DA_Engraving_Amplified.DA_Engraving_Amplified"));
	AmplifiedEngravingDefinition = AmplifiedDefinition.Object;

	static ConstructorHelpers::FObjectFinder<USPScrollEngravingDefinition> StableDefinition(
		TEXT("/Game/Data/Engravings/DA_Engraving_Stable.DA_Engraving_Stable"));
	StableEngravingDefinition = StableDefinition.Object;
}

void ASPGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	const FString ExpectedPlayersOption = UGameplayStatics::ParseOption(Options, TEXT("ExpectedPlayers"));
	ExpectedPlayers = ExpectedPlayersOption.IsEmpty()
		? 2
		: FMath::Clamp(FCString::Atoi(*ExpectedPlayersOption), MinSpikePlayers, MaxSpikePlayers);
	if (GameSession)
	{
		// Keep the authoritative roster fixed to the run contract. A third connection
		// must not be able to make the two-player settlement permanently unreachable.
		GameSession->MaxPlayers = ExpectedPlayers;
	}
	SessionId = FGuid::NewGuid();

	UE_LOG(LogSPGameMode, Display,
		TEXT("SP_SPIKE_SESSION_INIT session=%s expected_players=%d map=%s net_mode=%d"),
		*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), ExpectedPlayers,
		*MapName, static_cast<int32>(GetNetMode()));

	SpawnSpikeWorld();
}

void ASPGameMode::InitGameState()
{
	Super::InitGameState();
	if (ASPGameState* ScrollGameState = GetGameState<ASPGameState>())
	{
		ScrollGameState->AuthorityInitializeSession(SessionId, ExpectedPlayers);
		ScrollGameState->AuthoritySetPhase(ESPSessionPhase::PlayersJoining);
	}
}

void ASPGameMode::StartPlay()
{
	Super::StartPlay();
	RefreshSessionPhase();
}

void ASPGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	UE_LOG(LogSPGameMode, Display,
		TEXT("SP_SPIKE_PLAYER_JOIN controller=%s connected=%d expected=%d"),
		*GetNameSafe(NewPlayer), GetNumPlayers(), ExpectedPlayers);
	RefreshSessionPhase();
}

void ASPGameMode::Logout(AController* Exiting)
{
	UE_LOG(LogSPGameMode, Display,
		TEXT("SP_SPIKE_PLAYER_LEAVE controller=%s connected_before=%d expected=%d"),
		*GetNameSafe(Exiting), GetNumPlayers(), ExpectedPlayers);
	Super::Logout(Exiting);
	RefreshSessionPhase();
}

bool ASPGameMode::TryExtractCharacter(ASPCharacter* Character)
{
	if (!HasAuthority() || bSettlementStarted || !IsValid(Character) || !IsValid(ExtractionZone))
	{
		UE_LOG(LogSPGameMode, Warning,
			TEXT("SP_SPIKE_EXTRACTION_REJECTED reason=game_flow authority=%d settlement=%d character=%s zone=%s"),
			HasAuthority() ? 1 : 0, bSettlementStarted ? 1 : 0,
			*GetNameSafe(Character), *GetNameSafe(ExtractionZone));
		return false;
	}

	return ExtractionZone->TryExtract(Character);
}

void ASPGameMode::HandlePlayerReachedExtraction(ASPCharacter* Character)
{
	if (!HasAuthority() || bSettlementStarted || !IsValid(Character))
	{
		return;
	}

	ASPPlayerState* ScrollPlayerState = Character->GetPlayerState<ASPPlayerState>();
	if (!ScrollPlayerState || ScrollPlayerState->IsExtracted())
	{
		return;
	}

	ScrollPlayerState->MarkExtracted();

	int32 ExtractedPlayers = 0;
	if (const ASPGameState* ScrollGameState = GetGameState<ASPGameState>())
	{
		for (const APlayerState* PlayerState : ScrollGameState->PlayerArray)
		{
			const ASPPlayerState* Candidate = Cast<ASPPlayerState>(PlayerState);
			ExtractedPlayers += Candidate && Candidate->IsExtracted() ? 1 : 0;
		}
	}

	if (ASPGameState* ScrollGameState = GetGameState<ASPGameState>())
	{
		ScrollGameState->AuthoritySetPhase(ESPSessionPhase::Extraction);
		ScrollGameState->AuthoritySetExtractedPlayerCount(ExtractedPlayers);
	}

	TryCommitSettlement();
}

void ASPGameMode::HandleSettlementAck(
	ASPPlayerController* PlayerController,
	const FGuid& AckSessionId,
	const FString& ResultHash,
	const bool bSaved)
{
	if (!HasAuthority() || !bSettlementStarted || !IsValid(PlayerController) || AckSessionId != SessionId)
	{
		UE_LOG(LogSPGameMode, Warning,
			TEXT("SP_SPIKE_SETTLEMENT_ACK_REJECTED reason=context controller=%s session=%s saved=%d"),
			*GetNameSafe(PlayerController),
			*AckSessionId.ToString(EGuidFormats::DigitsWithHyphensLower), bSaved ? 1 : 0);
		return;
	}

	const TWeakObjectPtr<ASPPlayerController> ControllerKey(PlayerController);
	const FString* ExpectedHash = PendingSettlementHashes.Find(ControllerKey);
	if (!bSaved || !ExpectedHash || !ExpectedHash->Equals(ResultHash, ESearchCase::CaseSensitive))
	{
		UE_LOG(LogSPGameMode, Error,
			TEXT("SP_SPIKE_SETTLEMENT_ACK_REJECTED reason=save_or_hash controller=%s saved=%d expected=%s actual=%s"),
			*GetNameSafe(PlayerController), bSaved ? 1 : 0,
			ExpectedHash ? **ExpectedHash : TEXT("<missing>"), *ResultHash);
		return;
	}

	SuccessfulSettlementAcks.Add(ControllerKey);
	UE_LOG(LogSPGameMode, Display,
		TEXT("SP_SPIKE_SETTLEMENT_ACK session=%s controller=%s acknowledged=%d expected=%d"),
		*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), *GetNameSafe(PlayerController),
		SuccessfulSettlementAcks.Num(), PendingSettlementHashes.Num());

	if (PendingSettlementHashes.Num() == ExpectedPlayers
		&& SuccessfulSettlementAcks.Num() == PendingSettlementHashes.Num())
	{
		if (ASPGameState* ScrollGameState = GetGameState<ASPGameState>())
		{
			ScrollGameState->AuthorityMarkSettlementCommitted();
		}
	}
}

void ASPGameMode::SpawnSpikeWorld()
{
	if (bSpikeWorldSpawned || !HasAuthority() || !GetWorld())
	{
		return;
	}

	bSpikeWorldSpawned = true;
	SpawnPlayerStarts();
	SpawnGrayboxLighting();
	SpawnGrayboxBlocks();
	SpawnSpikePickups();
	SpawnExtractionZone();

	UE_LOG(LogSPGameMode, Display,
		TEXT("SP_SPIKE_WORLD_SPAWNED starts=4 lights=1 blocks=8 pickups=2 extraction=%s"),
		*GetNameSafe(ExtractionZone));
}

void ASPGameMode::SpawnGrayboxLighting()
{
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = TEXT("SP_GrayboxLighting");
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	GetWorld()->SpawnActor<ASPGrayboxLighting>(
		ASPGrayboxLighting::StaticClass(), FTransform::Identity, SpawnParameters);
}

void ASPGameMode::SpawnPlayerStarts()
{
	static const FVector StartLocations[] =
	{
		FVector(-650.0f, -200.0f, 110.0f),
		FVector(-650.0f,  200.0f, 110.0f),
		FVector(-750.0f,    0.0f, 110.0f),
		FVector(-600.0f,    0.0f, 110.0f)
	};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(StartLocations); ++Index)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = FName(*FString::Printf(TEXT("SP_PlayerStart_%d"), Index));
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<APlayerStart>(
			APlayerStart::StaticClass(),
			FTransform(FRotator::ZeroRotator, StartLocations[Index]),
			SpawnParameters);
	}
}

void ASPGameMode::SpawnGrayboxBlocks()
{
	static const FGrayboxBlockSpec Blocks[] =
	{
		{FVector(0.0f, 0.0f, -25.0f), FVector(20.0f, 20.0f, 0.5f)},
		{FVector(0.0f, -1000.0f, 250.0f), FVector(20.0f, 0.5f, 5.0f)},
		{FVector(0.0f, 1000.0f, 250.0f), FVector(20.0f, 0.5f, 5.0f)},
		{FVector(-1000.0f, 0.0f, 250.0f), FVector(0.5f, 20.0f, 5.0f)},
		{FVector(1000.0f, 0.0f, 250.0f), FVector(0.5f, 20.0f, 5.0f)},
		{FVector(-50.0f, -450.0f, 100.0f), FVector(2.0f, 1.0f, 2.0f)},
		{FVector(-50.0f, 450.0f, 100.0f), FVector(2.0f, 1.0f, 2.0f)},
		{FVector(250.0f, 0.0f, 75.0f), FVector(1.0f, 3.0f, 1.5f)}
	};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Blocks); ++Index)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = FName(*FString::Printf(TEXT("SP_GrayboxBlock_%d"), Index));
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<ASPGrayboxBlock>(
			ASPGrayboxBlock::StaticClass(),
			FTransform(FRotator::ZeroRotator, Blocks[Index].Location, Blocks[Index].Scale),
			SpawnParameters);
	}
}

void ASPGameMode::SpawnSpikePickups()
{
	const FPrimaryAssetId BaseDefinitionId = SpikeScrollDefinition
		? SpikeScrollDefinition->GetPrimaryAssetId()
		: FPrimaryAssetId(USPScrollDefinition::PrimaryAssetType, TEXT("DA_Scroll_VeilOfSilence"));
	const FPrimaryAssetId EngravingIds[] =
	{
		AmplifiedEngravingDefinition
			? AmplifiedEngravingDefinition->GetPrimaryAssetId()
			: FPrimaryAssetId(USPScrollEngravingDefinition::PrimaryAssetType, TEXT("DA_Engraving_Amplified")),
		StableEngravingDefinition
			? StableEngravingDefinition->GetPrimaryAssetId()
			: FPrimaryAssetId(USPScrollEngravingDefinition::PrimaryAssetType, TEXT("DA_Engraving_Stable"))
	};
	static const FVector PickupLocations[] =
	{
		FVector(-450.0f, -75.0f, 80.0f),
		FVector(-450.0f,  75.0f, 80.0f)
	};

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(PickupLocations); ++Index)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = FName(*FString::Printf(TEXT("SP_ScrollPickup_%d"), Index));
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASPScrollPickup* Pickup = GetWorld()->SpawnActor<ASPScrollPickup>(
			ASPScrollPickup::StaticClass(),
			FTransform(FRotator::ZeroRotator, PickupLocations[Index]),
			SpawnParameters);
		if (!Pickup)
		{
			continue;
		}

		FSPScrollInstance ScrollInstance;
		ScrollInstance.InstanceId = Index == 0
			? FGuid(0x53500001, 0x00000001, 0x00000000, 0x00000001)
			: FGuid(0x53500001, 0x00000002, 0x00000000, 0x00000002);
		ScrollInstance.BaseDefinitionId = BaseDefinitionId;
		ScrollInstance.EngravingDefinitionId = EngravingIds[Index];
		ScrollInstance.Quality = ESPScrollQuality::B;
		ScrollInstance.Contamination = 0.0f;
		ScrollInstance.Misfire = ESPMisfireType::None;
		Pickup->InitializeScroll(ScrollInstance);

		UE_LOG(LogSPGameMode, Display,
			TEXT("SP_SPIKE_PICKUP_SPAWNED index=%d instance=%s base=%s engraving=%s"),
			Index, *ScrollInstance.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*BaseDefinitionId.ToString(), *EngravingIds[Index].ToString());
	}
}

void ASPGameMode::SpawnExtractionZone()
{
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = TEXT("SP_ExtractionZone");
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ExtractionZone = GetWorld()->SpawnActor<ASPExtractionZone>(
		ASPExtractionZone::StaticClass(),
		FTransform(FRotator::ZeroRotator, FVector(650.0f, 0.0f, 110.0f)),
		SpawnParameters);
}

void ASPGameMode::RefreshSessionPhase()
{
	ASPGameState* ScrollGameState = GetGameState<ASPGameState>();
	if (!ScrollGameState || bSettlementStarted || ScrollGameState->GetSessionPhase() == ESPSessionPhase::Extraction)
	{
		return;
	}

	ScrollGameState->AuthoritySetPhase(
		GetNumPlayers() >= ExpectedPlayers
			? ESPSessionPhase::InExpedition
			: ESPSessionPhase::PlayersJoining);
}

void ASPGameMode::TryCommitSettlement()
{
	if (!HasAuthority() || bSettlementStarted || GetNumPlayers() != ExpectedPlayers)
	{
		return;
	}

	ASPGameState* ScrollGameState = GetGameState<ASPGameState>();
	if (!ScrollGameState)
	{
		return;
	}

	int32 ExtractedPlayers = 0;
	for (const APlayerState* PlayerState : ScrollGameState->PlayerArray)
	{
		const ASPPlayerState* ScrollPlayerState = Cast<ASPPlayerState>(PlayerState);
		ExtractedPlayers += ScrollPlayerState && ScrollPlayerState->IsExtracted() ? 1 : 0;
	}

	if (ExtractedPlayers != ExpectedPlayers)
	{
		return;
	}

	const int64 CompletionTime = FDateTime::UtcNow().ToUnixTimestamp();
	TArray<TPair<TWeakObjectPtr<ASPPlayerController>, FSPSessionResult>> ResultsToSend;
	for (FConstPlayerControllerIterator Iterator = GetWorld()->GetPlayerControllerIterator(); Iterator; ++Iterator)
	{
		ASPPlayerController* ScrollPlayerController = Cast<ASPPlayerController>(Iterator->Get());
		ASPPlayerState* ScrollPlayerState = ScrollPlayerController
			? ScrollPlayerController->GetPlayerState<ASPPlayerState>()
			: nullptr;
		if (!ScrollPlayerController || !ScrollPlayerState)
		{
			continue;
		}

		FSPSessionResult Result;
		Result.SessionId = SessionId;
		Result.PlayerId = BuildLocalPlayerId(ScrollPlayerState);
		Result.PartySize = ExpectedPlayers;
		Result.bExtracted = ScrollPlayerState->IsExtracted();
		Result.PickedUpCount = ScrollPlayerState->GetPickedUpCount();
		Result.ConsumedScrollCount = ScrollPlayerState->GetConsumedScrollCount();
		Result.ExtractedScrollCount = ScrollPlayerState->GetExtractedScrollCount();
		Result.GoldDelta = ScrollPlayerState->GetGoldDelta();
		Result.CompletedAtUnixSeconds = CompletionTime;
		Result.ResultHash = SPBuildSessionResultHash(Result);
		ResultsToSend.Emplace(ScrollPlayerController, MoveTemp(Result));
	}

	if (ResultsToSend.Num() != ExpectedPlayers)
	{
		UE_LOG(LogSPGameMode, Error,
			TEXT("SP_SPIKE_SETTLEMENT_REJECTED reason=incomplete_result_roster actual=%d expected=%d"),
			ResultsToSend.Num(), ExpectedPlayers);
		return;
	}

	bSettlementStarted = true;
	PendingSettlementHashes.Reset();
	SuccessfulSettlementAcks.Reset();
	for (const TPair<TWeakObjectPtr<ASPPlayerController>, FSPSessionResult>& Entry : ResultsToSend)
	{
		PendingSettlementHashes.Add(Entry.Key, Entry.Value.ResultHash);
	}
	ScrollGameState->AuthoritySetPhase(ESPSessionPhase::SettlementPending);
	UE_LOG(LogSPGameMode, Display,
		TEXT("SP_SPIKE_SETTLEMENT_PENDING session=%s party_size=%d"),
		*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), ExpectedPlayers);

	for (const TPair<TWeakObjectPtr<ASPPlayerController>, FSPSessionResult>& Entry : ResultsToSend)
	{
		ASPPlayerController* ScrollPlayerController = Entry.Key.Get();
		if (!ScrollPlayerController)
		{
			continue;
		}
		const FSPSessionResult& Result = Entry.Value;

		ScrollPlayerController->ClientCommitSessionResult(Result);
		UE_LOG(LogSPGameMode, Display,
			TEXT("SP_SPIKE_RESULT_SENT session=%s player=%s picked=%d consumed=%d extracted=%d gold=%d hash=%s"),
			*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), *Result.PlayerId,
			Result.PickedUpCount, Result.ConsumedScrollCount,
			Result.ExtractedScrollCount, Result.GoldDelta, *Result.ResultHash);
	}
}

FString ASPGameMode::BuildLocalPlayerId(const ASPPlayerState* PlayerState) const
{
	if (!PlayerState)
	{
		return TEXT("LocalPlayer-Unknown");
	}

	return FString::Printf(TEXT("LocalPlayer-%d-%s"), PlayerState->GetPlayerId(), *PlayerState->GetPlayerName());
}
