#include "Game/SPGameMode.h"

#include "Core/SPTypes.h"
#include "Game/SPContractEvaluator.h"
#include "Data/SPScrollDefinition.h"
#include "Data/SPScrollEngravingDefinition.h"
#include "Data/SPGrayboxThreatDefinition.h"
#include "Data/SPItemDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/SPGameState.h"
#include "Game/SPPartyState.h"
#include "Game/SPPlayerController.h"
#include "Game/SPPlayerState.h"
#include "GameFramework/GameSession.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Online/SPCampaignSubsystem.h"
#include "Online/SPOnlineSessionSubsystem.h"
#include "Player/SPCharacter.h"
#include "Player/SPInventoryComponent.h"
#include "UI/SPHUD.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SPExtractionZone.h"
#include "World/SPDungeonLayoutActor.h"
#include "World/SPDungeonLayoutTypes.h"
#include "World/SPGrayboxBlock.h"
#include "World/SPGrayboxLighting.h"
#include "World/SPGrayboxThreat.h"
#include "World/SPScrollPickup.h"
#include "World/SPThreatDirector.h"
#include "World/SPWorldItem.h"

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

template <typename TDefinition>
const TDefinition* ResolvePrimaryAssetDefinition(
	UAssetManager& AssetManager,
	const FPrimaryAssetId& AssetId)
{
	if (!AssetId.IsValid())
	{
		return nullptr;
	}

	if (const TDefinition* Loaded =
		Cast<TDefinition>(
			AssetManager.GetPrimaryAssetObject(AssetId)))
	{
		return Loaded;
	}

	const FSoftObjectPath AssetPath =
		AssetManager.GetPrimaryAssetPath(AssetId);
	return AssetPath.IsValid()
		? Cast<TDefinition>(AssetPath.TryLoad())
		: nullptr;
}
}

ASPGameMode::ASPGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.25f;
	DefaultPawnClass = ASPCharacter::StaticClass();
	PlayerControllerClass = ASPPlayerController::StaticClass();
	PlayerStateClass = ASPPlayerState::StaticClass();
	GameStateClass = ASPGameState::StaticClass();
	HUDClass = ASPHUD::StaticClass();
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
	const FString CampaignSlotOption =
		UGameplayStatics::ParseOption(Options, TEXT("CampaignSlot"));
	CampaignSlotIndex = CampaignSlotOption.IsEmpty()
		? 0
		: FMath::Clamp(
			FCString::Atoi(*CampaignSlotOption),
			0,
			USPCampaignSubsystem::CampaignSlotCount - 1);
	const FString DungeonSeedOption =
		UGameplayStatics::ParseOption(Options, TEXT("DungeonSeed"));
	DungeonSeed = DungeonSeedOption.IsEmpty()
		? 1729
		: FCString::Atoi(*DungeonSeedOption);
	if (DungeonSeed == 0)
	{
		DungeonSeed = 1729;
	}
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

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = TEXT("SP_PartyState");
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PartyState = GetWorld()->SpawnActor<ASPPartyState>(
		ASPPartyState::StaticClass(),
		FTransform::Identity,
		SpawnParameters);
}

void ASPGameMode::StartPlay()
{
	Super::StartPlay();
	RefreshSessionPhase();
}

void ASPGameMode::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	const double ServerTime = GetWorld()->GetTimeSeconds();
	if (ServerTime < NextRunRefreshServerTime)
	{
		return;
	}

	NextRunRefreshServerTime = ServerTime + 0.25;
	ApplyPendingReconnectInventories();
	ExpireDisconnectedPlayers(ServerTime);
	RefreshRunRosterAndResolution();
	LockOnlineLobbyForExpedition();
}

void ASPGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	const FString RosterKey = ResolveRosterKey(NewPlayer);
	ControllerRosterKeys.Add(NewPlayer, RosterKey);
	RegisterPartyMember(NewPlayer, RosterKey);
	if (IsPartyMemberKicked(RosterKey))
	{
		if (ASPPlayerState* ScrollPlayerState =
			NewPlayer->GetPlayerState<ASPPlayerState>())
		{
			ScrollPlayerState->AuthorityTransitionParticipation(
				ESPParticipationState::Spectating);
		}
		NewPlayer->StartSpectatingOnly();
		ControllerRosterKeys.Remove(NewPlayer);
		if (GameSession)
		{
			GameSession->KickPlayer(
				NewPlayer,
				NSLOCTEXT(
					"ScrollPeddler",
					"KickedIdentityReconnectReason",
					"A removed party member cannot reconnect."));
		}
		UE_LOG(LogSPGameMode, Warning,
			TEXT("SP_PARTY_KICKED_IDENTITY_REJECTED controller=%s key=%s"),
			*GetNameSafe(NewPlayer), *RosterKey);
		RefreshRunRosterAndResolution();
		return;
	}

	ASPGameState* ScrollGameState = GetGameState<ASPGameState>();
	const bool bRunInProgress = ScrollGameState
		&& ScrollGameState->GetRunPhase() != ESPRunPhase::Preparing;
	bool bAcceptedIntoRun = false;
	if (bRunInProgress)
	{
		bAcceptedIntoRun = IsRunReconnectAllowed(RosterKey)
			&& TryRestoreDisconnectedPlayer(NewPlayer, RosterKey);
		if (!bAcceptedIntoRun)
		{
			if (ASPPlayerState* ScrollPlayerState =
				NewPlayer->GetPlayerState<ASPPlayerState>())
			{
				ScrollPlayerState->AuthorityTransitionParticipation(
					ESPParticipationState::Spectating);
			}
			NewPlayer->StartSpectatingOnly();
			UE_LOG(LogSPGameMode, Warning,
				TEXT("SP_RUN_LATE_JOIN_REJECTED controller=%s key=%s"),
				*GetNameSafe(NewPlayer), *RosterKey);
		}
	}
	else if (RunRosterKeys.Num() < ExpectedPlayers
		&& !RunRosterKeys.Contains(RosterKey))
	{
		RunRosterKeys.Add(RosterKey);
		bAcceptedIntoRun = true;
	}
	else
	{
		bAcceptedIntoRun = RunRosterKeys.Contains(RosterKey);
	}

	UE_LOG(LogSPGameMode, Display,
		TEXT("SP_SPIKE_PLAYER_JOIN controller=%s key=%s accepted=%d connected=%d expected=%d"),
		*GetNameSafe(NewPlayer), *RosterKey, bAcceptedIntoRun ? 1 : 0,
		GetNumPlayers(), ExpectedPlayers);
	RefreshSessionPhase();
	RefreshRunRosterAndResolution();
}

void ASPGameMode::Logout(AController* Exiting)
{
	const FString* ExistingRosterKey = ControllerRosterKeys.Find(Exiting);
	const FString RosterKey = ExistingRosterKey
		? *ExistingRosterKey
		: ResolveRosterKey(Exiting);
	ASPGameState* ScrollGameState = GetGameState<ASPGameState>();
	ASPPlayerState* ScrollPlayerState =
		Exiting ? Exiting->GetPlayerState<ASPPlayerState>() : nullptr;
	const ASPCharacter* ScrollCharacter =
		Exiting ? Cast<ASPCharacter>(Exiting->GetPawn()) : nullptr;
	const bool bRunInProgress = ScrollGameState
		&& ScrollGameState->GetRunPhase() != ESPRunPhase::Preparing
		&& ScrollGameState->GetRunPhase() != ESPRunPhase::Settlement;

	if (bRunInProgress && RunRosterKeys.Contains(RosterKey)
		&& !RunOutcomes.Contains(RosterKey) && ScrollPlayerState)
	{
		FSPDisconnectedRunRecord Record;
		if (ScrollPlayerState->AuthorityBuildReconnectSnapshot(
				Record.PlayerSnapshot))
		{
			if (ScrollCharacter)
			{
				Record.InventorySnapshot =
					ScrollCharacter->GetInventory().GetInventoryState();
			}
			Record.ExpiresAtServerTime =
				(GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0)
				+ ReconnectGraceSeconds;
			DisconnectedRunPlayers.Add(RosterKey, MoveTemp(Record));
			ScrollPlayerState->AuthorityTransitionParticipation(
				ESPParticipationState::Disconnected);
			UE_LOG(LogSPGameMode, Display,
				TEXT("SP_RUN_PLAYER_DISCONNECTED key=%s grace=%.0f"),
				*RosterKey, ReconnectGraceSeconds);
		}
	}

	if (PartyState && PartyState->GetGovernanceState().FindMember(RosterKey))
	{
		FSPPartyActionRequest PartyRequest;
		PartyRequest.RequestId = FGuid::NewGuid();
		PartyRequest.ExpectedRevision =
			PartyState->GetGovernanceState().Revision;
		PartyState->AuthoritySetMemberConnected(
			RosterKey,
			false,
			PartyRequest);
	}

	UE_LOG(LogSPGameMode, Display,
		TEXT("SP_SPIKE_PLAYER_LEAVE controller=%s key=%s connected_before=%d expected=%d"),
		*GetNameSafe(Exiting), *RosterKey, GetNumPlayers(), ExpectedPlayers);
	ControllerRosterKeys.Remove(Exiting);
	Super::Logout(Exiting);
	RefreshSessionPhase();
	RefreshRunRosterAndResolution();
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
	const FString* ExistingRosterKey =
		ControllerRosterKeys.Find(Character->GetController());
	const FString RosterKey = ExistingRosterKey
		? *ExistingRosterKey
		: ResolveRosterKey(Character->GetController());
	CaptureRunOutcome(RosterKey, ScrollPlayerState, Character, true);

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

	RefreshRunRosterAndResolution();
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
	if (!ExpectedHash || !ExpectedHash->Equals(ResultHash, ESearchCase::CaseSensitive))
	{
		UE_LOG(LogSPGameMode, Error,
			TEXT("SP_SPIKE_SETTLEMENT_ACK_REJECTED reason=save_or_hash controller=%s saved=%d expected=%s actual=%s"),
			*GetNameSafe(PlayerController), bSaved ? 1 : 0,
			ExpectedHash ? **ExpectedHash : TEXT("<missing>"), *ResultHash);
		return;
	}

	SuccessfulSettlementAcks.Add(ControllerKey);
	if (bSaved)
	{
		UE_LOG(LogSPGameMode, Display,
			TEXT("SP_SPIKE_SETTLEMENT_ACK session=%s controller=%s acknowledged=%d expected=%d"),
			*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*GetNameSafe(PlayerController),
			SuccessfulSettlementAcks.Num(), PendingSettlementHashes.Num());
	}
	else
	{
		UE_LOG(LogSPGameMode, Warning,
			TEXT("SP_SPIKE_SETTLEMENT_ACK session=%s controller=%s acknowledged=%d expected=%d local_save=failed"),
			*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*GetNameSafe(PlayerController),
			SuccessfulSettlementAcks.Num(), PendingSettlementHashes.Num());
	}

	if (SuccessfulSettlementAcks.Num() == PendingSettlementHashes.Num())
	{
		CompleteSettlementAfterAckWindow();
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
	SpawnDungeonLayout();
	RuntimeContractDefinition =
		NewObject<USPContractDefinition>(this);
	RuntimeContractDefinition->StableId =
		TEXT("contract.silence_scroll_delivery");
	RuntimeContractDefinition->DisplayName =
		FText::FromString(TEXT("Silence Scroll Delivery"));
	RuntimeContractDefinition->Kind =
		ESPContractKind::ScrollDelivery;
	RuntimeContractDefinition->DangerTier = 1;
	RuntimeContractDefinition->GoldReward = 300;
	RuntimeContractDefinition->GuildXpReward = 50;
	RuntimeContractDefinition->ScrollCondition.BaseFamilyStableId =
		SpikeScrollDefinition
		? SpikeScrollDefinition->StableId
		: TEXT("DA_Scroll_VeilOfSilence");
	RuntimeContractDefinition->ScrollCondition.AllowedEngravingStableIds =
	{
		AmplifiedEngravingDefinition
			? AmplifiedEngravingDefinition->StableId
			: FName(TEXT("DA_Engraving_Amplified")),
		StableEngravingDefinition
			? StableEngravingDefinition->StableId
			: FName(TEXT("DA_Engraving_Stable"))
	};
	RuntimeContractDefinition->ScrollCondition.MinimumQuality =
		ESPScrollQuality::B;
	RuntimeContractDefinition->ScrollCondition.MaximumContamination =
		30.0f;
	RuntimeContractDefinition->ScrollCondition.Quantity = 1;
	SpawnSpikePickups();
	SpawnExtractionZone();
	SpawnThreats();

	UE_LOG(LogSPGameMode, Display,
		TEXT("SP_SPIKE_WORLD_SPAWNED starts=4 lights=1 rooms=8 pickups=4 threats=2 extraction=%s"),
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

void ASPGameMode::SpawnDungeonLayout()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	RuntimeDungeonSeedDefinition =
		NewObject<USPDungeonSeedDefinition>(this);
	RuntimeDungeonSeedDefinition->StableId =
		TEXT("dungeon.tech_spike.vertical_slice");
	RuntimeDungeonSeedDefinition->Seed = DungeonSeed;
	RuntimeDungeonSeedDefinition->RoomOrder =
	{
		ESPRoomRole::EntryRecords,
		ESPRoomRole::StandardStacks,
		ESPRoomRole::Office,
		ESPRoomRole::FloodedArchive,
		ESPRoomRole::Bindery,
		ESPRoomRole::Incinerator,
		ESPRoomRole::EchoHall,
		ESPRoomRole::SealedStorage
	};

	FActorSpawnParameters LayoutSpawnParameters;
	LayoutSpawnParameters.Name = TEXT("SP_DungeonLayout");
	LayoutSpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	DungeonLayoutActor =
		GetWorld()->SpawnActor<ASPDungeonLayoutActor>(
			ASPDungeonLayoutActor::StaticClass(),
			FTransform::Identity,
			LayoutSpawnParameters);
	FSPDungeonLayoutValidationReport Report;
	if (!DungeonLayoutActor
		|| !DungeonLayoutActor->AuthorityGenerateLayout(
			RuntimeDungeonSeedDefinition,
			Report))
	{
		UE_LOG(LogSPGameMode, Error,
			TEXT("SP_DUNGEON_GENERATION_FAILED seed=%d result=%d message=%s"),
			DungeonSeed,
			static_cast<int32>(Report.Result),
			*Report.Message.ToString());
		SpawnGrayboxBlocks();
		return;
	}

	const FSPDungeonLayout& Layout =
		DungeonLayoutActor->GetLayout();
	for (const FSPDungeonRoomLayout& Room : Layout.Rooms)
	{
		FActorSpawnParameters FloorSpawnParameters;
		FloorSpawnParameters.Name = FName(*FString::Printf(
			TEXT("SP_RoomFloor_%d"),
			Room.RoomIndex));
		FloorSpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<ASPGrayboxBlock>(
			ASPGrayboxBlock::StaticClass(),
			FTransform(
				FRotator::ZeroRotator,
				Room.WorldLocation + FVector(0.0f, 0.0f, -25.0f),
				FVector(18.0f, 18.0f, 0.5f)),
			FloorSpawnParameters);

		FActorSpawnParameters MarkerSpawnParameters;
		MarkerSpawnParameters.Name = FName(*FString::Printf(
			TEXT("SP_RoomMarker_%d_%s"),
			Room.RoomIndex,
			*StaticEnum<ESPRoomRole>()->GetNameStringByValue(
				static_cast<int64>(Room.Role))));
		MarkerSpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<ASPGrayboxBlock>(
			ASPGrayboxBlock::StaticClass(),
			FTransform(
				FRotator::ZeroRotator,
				Room.WorldLocation + FVector(0.0f, 0.0f, 75.0f),
				FVector(0.35f, 0.35f, 1.5f)),
			MarkerSpawnParameters);

		const FSPDungeonRoomLayout* Parent =
			Layout.FindRoom(Room.ParentRoomIndex);
		if (!Parent)
		{
			continue;
		}

		const FVector Delta =
			Room.WorldLocation - Parent->WorldLocation;
		const FVector CorridorScale =
			FMath::Abs(Delta.X) > FMath::Abs(Delta.Y)
			? FVector(
				FMath::Max(3.0f, FMath::Abs(Delta.X) / 100.0f),
				3.0f,
				0.35f)
			: FVector(
				3.0f,
				FMath::Max(3.0f, FMath::Abs(Delta.Y) / 100.0f),
				0.35f);
		FActorSpawnParameters CorridorSpawnParameters;
		CorridorSpawnParameters.Name = FName(*FString::Printf(
			TEXT("SP_Corridor_%d_%d"),
			Parent->RoomIndex,
			Room.RoomIndex));
		CorridorSpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<ASPGrayboxBlock>(
			ASPGrayboxBlock::StaticClass(),
			FTransform(
				FRotator::ZeroRotator,
				(Parent->WorldLocation + Room.WorldLocation) * 0.5f
					+ FVector(0.0f, 0.0f, -25.0f),
				CorridorScale),
			CorridorSpawnParameters);
	}

	UE_LOG(LogSPGameMode, Display,
		TEXT("SP_DUNGEON_GENERATED seed=%d checksum=%lld rooms=%d objective=%d"),
		Layout.Seed,
		Layout.LayoutChecksum,
		Layout.Rooms.Num(),
		Layout.ObjectiveRoomIndex);
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
		FVector(-500.0f, -90.0f, 80.0f),
		FVector(-500.0f, -30.0f, 80.0f),
		FVector(-500.0f,  30.0f, 80.0f),
		FVector(-500.0f,  90.0f, 80.0f)
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
		const uint32 StablePickupIndex =
			static_cast<uint32>(Index + 1);
		ScrollInstance.InstanceId = FGuid(
			0x53500001,
			StablePickupIndex,
			0x00000000,
			StablePickupIndex);
		ScrollInstance.BaseDefinitionId = BaseDefinitionId;
		ScrollInstance.EngravingDefinitionId =
			EngravingIds[Index % UE_ARRAY_COUNT(EngravingIds)];
		ScrollInstance.Quality = ESPScrollQuality::B;
		ScrollInstance.Contamination = 0.0f;
		ScrollInstance.Misfire = ESPMisfireType::None;
		Pickup->InitializeScroll(ScrollInstance);

		UE_LOG(LogSPGameMode, Display,
			TEXT("SP_SPIKE_PICKUP_SPAWNED index=%d instance=%s base=%s engraving=%s"),
			Index, *ScrollInstance.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*BaseDefinitionId.ToString(),
			*ScrollInstance.EngravingDefinitionId.ToString());
	}

	struct FGenericItemSpawnSpec
	{
		FName StableId;
		ESPItemKind Kind;
		int32 Quantity;
		FVector Location;
	};
	const FGenericItemSpawnSpec GenericItems[] =
	{
		{
			TEXT("material.paper_scrap"),
			ESPItemKind::Material,
			3,
			FVector(-200.0f, -300.0f, 70.0f)
		},
		{
			TEXT("equipment.archive_lantern"),
			ESPItemKind::Equipment,
			1,
			FVector(-50.0f, 300.0f, 70.0f)
		},
		{
			TEXT("cargo.bound_archive_crate"),
			ESPItemKind::LargeCargo,
			1,
			FVector(300.0f, 250.0f, 80.0f)
		}
	};
	for (int32 Index = 0;
		Index < UE_ARRAY_COUNT(GenericItems);
		++Index)
	{
		const FGenericItemSpawnSpec& Spec = GenericItems[Index];
		FSPItemInstance Item;
		Item.InstanceId = FGuid::NewGuid();
		Item.DefinitionId = FPrimaryAssetId(
			USPItemDefinition::PrimaryAssetType,
			Spec.StableId);
		Item.Kind = Spec.Kind;
		Item.Quantity = Spec.Quantity;

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = FName(*FString::Printf(
			TEXT("SP_WorldItem_%d_%s"),
			Index,
			*Spec.StableId.ToString()));
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASPWorldItem* WorldItem =
			GetWorld()->SpawnActor<ASPWorldItem>(
				ASPWorldItem::StaticClass(),
				FTransform(
					FRotator::ZeroRotator,
					Spec.Location),
				SpawnParameters);
		if (!WorldItem || !WorldItem->InitializeItem(Item))
		{
			UE_LOG(LogSPGameMode, Error,
				TEXT("SP_WORLD_ITEM_SPAWN_FAILED id=%s"),
				*Spec.StableId.ToString());
		}
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

void ASPGameMode::SpawnThreats()
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	struct FThreatSpawnSpec
	{
		ESPThreatArchetype Archetype;
		FName StableId;
		FName AttackStableId;
		FVector Location;
	};
	const FThreatSpawnSpec Specs[] =
	{
		{
			ESPThreatArchetype::EchoHunter,
			TEXT("threat.echo_hunter.graybox"),
			TEXT("attack.echo_hunter.swipe"),
			FVector(450.0f, 650.0f, 80.0f)
		},
		{
			ESPThreatArchetype::PaperEater,
			TEXT("threat.paper_eater.graybox"),
			TEXT("attack.paper_eater.corrupt"),
			FVector(250.0f, -650.0f, 80.0f)
		}
	};

	for (const FThreatSpawnSpec& Spec : Specs)
	{
		USPGrayboxThreatDefinition* Definition =
			NewObject<USPGrayboxThreatDefinition>(this);
		Definition->StableId = Spec.StableId;
		Definition->Archetype = Spec.Archetype;
		Definition->AttackPattern.StableId = Spec.AttackStableId;
		Definition->AttackPattern.ResultCondition =
			ESPPlayerCondition::Injured;
		Definition->AttackPattern.bDropHandItem = true;
		RuntimeThreatDefinitions.Add(Definition);

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Name = FName(*FString::Printf(
			TEXT("SP_Threat_%s"),
			*Spec.StableId.ToString()));
		SpawnParameters.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ASPGrayboxThreat* Threat =
			GetWorld()->SpawnActor<ASPGrayboxThreat>(
				ASPGrayboxThreat::StaticClass(),
				FTransform(FRotator::ZeroRotator, Spec.Location),
				SpawnParameters);
		if (!Threat
			|| !Threat->AuthoritySetThreatDefinition(Definition))
		{
			UE_LOG(LogSPGameMode, Error,
				TEXT("SP_THREAT_SPAWN_FAILED id=%s"),
				*Spec.StableId.ToString());
			continue;
		}

		Threat->OnAttackIntent.AddDynamic(
			this,
			&ASPGameMode::HandleThreatAttackIntent);
	}

	FActorSpawnParameters DirectorSpawnParameters;
	DirectorSpawnParameters.Name = TEXT("SP_ThreatDirector");
	DirectorSpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ThreatDirector = GetWorld()->SpawnActor<ASPThreatDirector>(
		ASPThreatDirector::StaticClass(),
		FTransform::Identity,
		DirectorSpawnParameters);
	if (ThreatDirector)
	{
		ThreatDirector->OnThreatSpawnRequested().AddUObject(
			this,
			&ASPGameMode::HandleThreatSpawnRequested);
	}
}

void ASPGameMode::HandleThreatAttackIntent(
	const FSPThreatAttackIntent& Intent)
{
	if (!HasAuthority() || !Intent.IsValid()
		|| ProcessedThreatAttackIntents.Contains(Intent.IntentId))
	{
		return;
	}

	ProcessedThreatAttackIntents.Add(Intent.IntentId);
	if (!Intent.bRequestHandItemDrop)
	{
		return;
	}

	ASPCharacter* TargetCharacter =
		Cast<ASPCharacter>(Intent.TargetActor);
	if (!TargetCharacter)
	{
		return;
	}

	const bool bDropped = TargetCharacter->AuthorityDropHandItem(
		Intent.ImpactLocation + FVector(0.0f, 0.0f, 35.0f));
	UE_LOG(LogSPGameMode, Display,
		TEXT("SP_THREAT_ATTACK_RESOLVED intent=%s target=%s condition=%d hand_drop=%d"),
		*Intent.IntentId.ToString(EGuidFormats::DigitsWithHyphensLower),
		*GetNameSafe(TargetCharacter),
		static_cast<int32>(Intent.ResultCondition),
		bDropped ? 1 : 0);
}

void ASPGameMode::HandleThreatSpawnRequested(
	const ESPThreatArchetype Archetype)
{
	if (!HasAuthority() || !GetWorld())
	{
		return;
	}

	int32 ExistingCount = 0;
	for (TActorIterator<ASPGrayboxThreat> Iterator(GetWorld());
		Iterator;
		++Iterator)
	{
		const USPGrayboxThreatDefinition* Definition =
			Iterator->GetThreatDefinition();
		ExistingCount += Definition
			&& Definition->Archetype == Archetype
			? 1
			: 0;
	}
	if (ExistingCount >= 2)
	{
		return;
	}

	USPGrayboxThreatDefinition* RuntimeDefinition = nullptr;
	for (USPGrayboxThreatDefinition* Candidate
		: RuntimeThreatDefinitions)
	{
		if (Candidate && Candidate->Archetype == Archetype)
		{
			RuntimeDefinition = Candidate;
			break;
		}
	}
	if (!RuntimeDefinition)
	{
		return;
	}

	const float Side = Archetype == ESPThreatArchetype::EchoHunter
		? 1.0f
		: -1.0f;
	const FVector SpawnLocation(
		700.0f - ExistingCount * 150.0f,
		Side * (700.0f - ExistingCount * 100.0f),
		80.0f);
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ASPGrayboxThreat* Threat =
		GetWorld()->SpawnActor<ASPGrayboxThreat>(
			ASPGrayboxThreat::StaticClass(),
			FTransform(FRotator::ZeroRotator, SpawnLocation),
			SpawnParameters);
	if (Threat && Threat->AuthoritySetThreatDefinition(RuntimeDefinition))
	{
		Threat->OnAttackIntent.AddDynamic(
			this,
			&ASPGameMode::HandleThreatAttackIntent);
	}
}

void ASPGameMode::RegisterPartyMember(
	APlayerController* NewPlayer,
	const FString& RosterKey)
{
	if (!HasAuthority() || !PartyState || !NewPlayer
		|| RosterKey.IsEmpty())
	{
		return;
	}

	const ASPPlayerState* ScrollPlayerState =
		NewPlayer->GetPlayerState<ASPPlayerState>();
	const FString DisplayName = ScrollPlayerState
		? ScrollPlayerState->GetPlayerName()
		: RosterKey;
	if (PartyState->GetGovernanceState().Revision == 0)
	{
		PartyState->AuthorityInitializeParty(RosterKey, DisplayName);
	}
	else
	{
		FSPPartyActionRequest Request;
		Request.RequestId = FGuid::NewGuid();
		Request.ExpectedRevision =
			PartyState->GetGovernanceState().Revision;
		if (PartyState->GetGovernanceState().FindMember(RosterKey))
		{
			PartyState->AuthoritySetMemberConnected(
				RosterKey,
				true,
				Request);
		}
		else
		{
			PartyState->AuthorityRegisterMember(
				RosterKey,
				DisplayName,
				Request);
		}
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("SPAutoSpike")))
	{
		FSPPartyActionRequest ReadyRequest;
		ReadyRequest.RequestId = FGuid::NewGuid();
		ReadyRequest.ExpectedRevision =
			PartyState->GetGovernanceState().Revision;
		PartyState->AuthoritySetReady(
			RosterKey,
			true,
			ReadyRequest);
	}
}

void ASPGameMode::UpdatePartyRunPhase(const ESPRunPhase RunPhase)
{
	if (!HasAuthority() || !PartyState
		|| PartyState->GetGovernanceState().RunPhase == RunPhase)
	{
		return;
	}

	FSPPartyActionRequest Request;
	Request.RequestId = FGuid::NewGuid();
	Request.ExpectedRevision =
		PartyState->GetGovernanceState().Revision;
	PartyState->AuthoritySetRunPhase(RunPhase, Request);
}

bool ASPGameMode::IsPartyMemberKicked(
	const FString& RosterKey) const
{
	const FSPPartyMemberState* Member = PartyState
		? PartyState->GetGovernanceState().FindMember(RosterKey)
		: nullptr;
	return Member && Member->bKicked;
}

bool ASPGameMode::IsRunReconnectAllowed(
	const FString& RosterKey) const
{
	return !RosterKey.IsEmpty()
		&& RunRosterKeys.Contains(RosterKey)
		&& !RunOutcomes.Contains(RosterKey)
		&& DisconnectedRunPlayers.Contains(RosterKey)
		&& !IsPartyMemberKicked(RosterKey);
}

void ASPGameMode::ApplyKickedMemberRunPolicy(
	const FString& RosterKey,
	const ESPRunPhase RunPhase,
	ASPPlayerController* TargetController)
{
	if (RosterKey.IsEmpty())
	{
		return;
	}

	const bool bFieldPhase =
		RunPhase == ESPRunPhase::Expedition
		|| RunPhase == ESPRunPhase::Collapse
		|| RunPhase == ESPRunPhase::Resolution;
	if (bFieldPhase)
	{
		if (!RunOutcomes.Contains(RosterKey))
		{
			ASPPlayerState* TargetPlayerState = TargetController
				? TargetController->GetPlayerState<ASPPlayerState>()
				: nullptr;
			ASPCharacter* TargetCharacter = TargetController
				? Cast<ASPCharacter>(TargetController->GetPawn())
				: nullptr;
			if (TargetPlayerState)
			{
				TargetPlayerState->AuthorityMarkMissing();
				CaptureRunOutcome(
					RosterKey,
					TargetPlayerState,
					TargetCharacter,
					false);
			}
			else
			{
				FSPPlayerRunSnapshot DisconnectedSnapshot;
				const FSPDisconnectedRunRecord* Record =
					DisconnectedRunPlayers.Find(RosterKey);
				const FSPPlayerRunSnapshot* Snapshot = nullptr;
				if (Record)
				{
					DisconnectedSnapshot = Record->PlayerSnapshot;
					Snapshot = &DisconnectedSnapshot;
				}
				CaptureMissingOutcome(RosterKey, Snapshot);
			}
		}
	}
	else
	{
		// Hub removal frees this fixed roster slot for a different identity.
		RunRosterKeys.Remove(RosterKey);
		RunOutcomes.Remove(RosterKey);
	}

	DisconnectedRunPlayers.Remove(RosterKey);
	PendingReconnectInventories.Remove(RosterKey);
	if (TargetController)
	{
		ControllerRosterKeys.Remove(TargetController);
		PendingSettlementHashes.Remove(TargetController);
		SuccessfulSettlementAcks.Remove(TargetController);
	}

	UE_LOG(LogSPGameMode, Display,
		TEXT("SP_PARTY_KICK_APPLIED key=%s phase=%d terminal=%d roster=%d outcomes=%d"),
		*RosterKey,
		static_cast<int32>(RunPhase),
		bFieldPhase ? 1 : 0,
		RunRosterKeys.Num(),
		RunOutcomes.Num());
}

ASPPlayerController* ASPGameMode::FindControllerByPlayerId(
	const int32 PlayerId) const
{
	for (FConstPlayerControllerIterator Iterator =
			GetWorld()->GetPlayerControllerIterator();
		Iterator;
		++Iterator)
	{
		ASPPlayerController* PlayerController =
			Cast<ASPPlayerController>(Iterator->Get());
		const ASPPlayerState* PlayerState = PlayerController
			? PlayerController->GetPlayerState<ASPPlayerState>()
			: nullptr;
		if (PlayerState && PlayerState->GetPlayerId() == PlayerId)
		{
			return PlayerController;
		}
	}
	return nullptr;
}

ESPPartyActionResult ASPGameMode::HandlePartyReadyRequest(
	ASPPlayerController* Requester,
	const bool bReady,
	const FSPPartyActionRequest& Request)
{
	const FString* RequesterId =
		ControllerRosterKeys.Find(Requester);
	if (!HasAuthority() || !PartyState || !RequesterId)
	{
		return ESPPartyActionResult::InvalidRequest;
	}

	const ESPPartyActionResult Result =
		PartyState->AuthoritySetReady(*RequesterId, bReady, Request);
	if (Result == ESPPartyActionResult::Success
		|| Result == ESPPartyActionResult::AlreadyProcessed)
	{
		RefreshSessionPhase();
	}
	return Result;
}

ESPPartyActionResult ASPGameMode::HandlePartyChatRequest(
	ASPPlayerController* Requester,
	const FString& Message,
	const FSPPartyActionRequest& Request)
{
	const FString* RequesterId =
		ControllerRosterKeys.Find(Requester);
	return HasAuthority() && PartyState && RequesterId
		? PartyState->AuthoritySubmitChat(
			*RequesterId,
			Message,
			Request)
		: ESPPartyActionResult::InvalidRequest;
}

ESPPartyActionResult ASPGameMode::HandleHostKickRequest(
	ASPPlayerController* Requester,
	const int32 TargetPlayerId,
	const FSPPartyActionRequest& Request)
{
	const FString* RequesterId =
		ControllerRosterKeys.Find(Requester);
	ASPPlayerController* TargetController =
		FindControllerByPlayerId(TargetPlayerId);
	const FString* TargetId =
		ControllerRosterKeys.Find(TargetController);
	if (!HasAuthority() || !PartyState || !RequesterId
		|| !TargetController || !TargetId)
	{
		return ESPPartyActionResult::InvalidRequest;
	}

	const ESPPartyActionResult Result =
		PartyState->AuthorityHostKickImmediately(
			*RequesterId,
			*TargetId,
			Request);
	if ((Result == ESPPartyActionResult::Success
			|| Result == ESPPartyActionResult::AlreadyProcessed))
	{
		const FString TargetRosterKey = *TargetId;
		const ESPRunPhase RunPhase =
			PartyState->GetGovernanceState().RunPhase;
		ApplyKickedMemberRunPolicy(
			TargetRosterKey,
			RunPhase,
			TargetController);
		if (GameSession)
		{
			GameSession->KickPlayer(
				TargetController,
				NSLOCTEXT(
					"ScrollPeddler",
					"HostKickReason",
					"Removed by the host."));
		}
		RefreshSessionPhase();
		RefreshRunRosterAndResolution();
	}
	return Result;
}

ESPPartyActionResult ASPGameMode::HandleStartKickVoteRequest(
	ASPPlayerController* Requester,
	const int32 TargetPlayerId,
	const FGuid& VoteId,
	const FSPPartyActionRequest& Request)
{
	const FString* RequesterId =
		ControllerRosterKeys.Find(Requester);
	const ASPPlayerController* TargetController =
		FindControllerByPlayerId(TargetPlayerId);
	const FString* TargetId =
		ControllerRosterKeys.Find(TargetController);
	if (!HasAuthority() || !PartyState || !RequesterId || !TargetId)
	{
		return ESPPartyActionResult::InvalidRequest;
	}

	const ESPPartyActionResult Result =
		PartyState->AuthorityStartKickVote(
			*RequesterId,
			*TargetId,
			VoteId,
			Request);
	ApplyPassedKickVote(VoteId);
	return Result;
}

ESPPartyActionResult ASPGameMode::HandleCastPartyVoteRequest(
	ASPPlayerController* Requester,
	const FGuid& VoteId,
	const ESPPartyVoteChoice Choice,
	const FSPPartyActionRequest& Request)
{
	const FString* RequesterId =
		ControllerRosterKeys.Find(Requester);
	if (!HasAuthority() || !PartyState || !RequesterId)
	{
		return ESPPartyActionResult::InvalidRequest;
	}

	const ESPPartyActionResult Result =
		PartyState->AuthorityCastVote(
			*RequesterId,
			VoteId,
			Choice,
			Request);
	ApplyPassedKickVote(VoteId);
	return Result;
}

void ASPGameMode::ApplyPassedKickVote(const FGuid& VoteId)
{
	if (!PartyState || AppliedKickVoteIds.Contains(VoteId))
	{
		return;
	}

	const FSPPartyVoteState* Vote =
		PartyState->GetGovernanceState().FindVote(VoteId);
	if (!Vote || Vote->Kind != ESPPartyVoteKind::KickMember
		|| Vote->Status != ESPPartyVoteStatus::Passed)
	{
		return;
	}

	ASPPlayerController* TargetController = nullptr;
	for (const TPair<TWeakObjectPtr<AController>, FString>& Entry
		: ControllerRosterKeys)
	{
		ASPPlayerController* Candidate =
			Cast<ASPPlayerController>(Entry.Key.Get());
		if (Candidate && Entry.Value == Vote->TargetMemberId)
		{
			TargetController = Candidate;
			break;
		}
	}

	const FString TargetRosterKey = Vote->TargetMemberId;
	AppliedKickVoteIds.Add(VoteId);
	ApplyKickedMemberRunPolicy(
		TargetRosterKey,
		PartyState->GetGovernanceState().RunPhase,
		TargetController);
	if (GameSession && TargetController)
	{
		GameSession->KickPlayer(
			TargetController,
				NSLOCTEXT(
					"ScrollPeddler",
					"VoteKickReason",
					"Removed by party vote."));
	}
	RefreshRunRosterAndResolution();
}

void ASPGameMode::RefreshSessionPhase()
{
	ASPGameState* ScrollGameState = GetGameState<ASPGameState>();
	if (!ScrollGameState || bSettlementStarted || ScrollGameState->GetSessionPhase() == ESPSessionPhase::Extraction)
	{
		return;
	}

	const bool bRosterReady = RunRosterKeys.Num() == ExpectedPlayers
		&& GetNumPlayers() >= ExpectedPlayers
		&& (!PartyState || PartyState->AreAllPresentMembersReady());
	ScrollGameState->AuthoritySetPhase(bRosterReady
		? ESPSessionPhase::InExpedition
		: ESPSessionPhase::PlayersJoining);

	if (!bRosterReady
		|| ScrollGameState->GetRunPhase() != ESPRunPhase::Expedition)
	{
		return;
	}

	UpdatePartyRunPhase(ESPRunPhase::Expedition);
	LockOnlineLobbyForExpedition();

	for (const TPair<TWeakObjectPtr<AController>, FString>& Entry
		: ControllerRosterKeys)
	{
		AController* Controller = Entry.Key.Get();
		if (!Controller || !RunRosterKeys.Contains(Entry.Value))
		{
			continue;
		}

		if (ASPPlayerState* ScrollPlayerState =
			Controller->GetPlayerState<ASPPlayerState>())
		{
			ScrollPlayerState->AuthorityTransitionParticipation(
				ESPParticipationState::Active);
		}
	}
}

void ASPGameMode::LockOnlineLobbyForExpedition()
{
	if (!HasAuthority() || bOnlineLobbyLocked)
	{
		return;
	}

	const ASPGameState* ScrollGameState = GetGameState<ASPGameState>();
	if (!ScrollGameState
		|| ScrollGameState->GetRunPhase() == ESPRunPhase::Preparing)
	{
		return;
	}

	USPOnlineSessionSubsystem* OnlineSessions =
		GetGameInstance()
			? GetGameInstance()->GetSubsystem<USPOnlineSessionSubsystem>()
			: nullptr;
	if (!OnlineSessions || !OnlineSessions->IsLobbyHost())
	{
		return;
	}

	if (OnlineSessions->GetLobbyState() == ESPOnlineLobbyState::Expedition)
	{
		bOnlineLobbyLocked = true;
		return;
	}

	if (OnlineSessions->GetActiveOperation()
		!= ESPOnlineSessionOperation::None)
	{
		return;
	}

	const bool bSubmitted = OnlineSessions->UpdateLobby(
		ESPOnlineLobbyState::Expedition,
		false,
		false,
		0);
	if (bSubmitted)
	{
		UE_LOG(LogSPGameMode, Display,
			TEXT("SP_ONLINE_EXPEDITION_LOCK submitted=1"));
	}
	else
	{
		UE_LOG(LogSPGameMode, Warning,
			TEXT("SP_ONLINE_EXPEDITION_LOCK submitted=0"));
	}
}

void ASPGameMode::RefreshRunRosterAndResolution()
{
	ASPGameState* ScrollGameState = GetGameState<ASPGameState>();
	if (!HasAuthority() || !ScrollGameState
		|| ScrollGameState->GetRunPhase() == ESPRunPhase::Preparing
		|| bSettlementStarted)
	{
		return;
	}

	for (const TPair<TWeakObjectPtr<AController>, FString>& Entry
		: ControllerRosterKeys)
	{
		AController* Controller = Entry.Key.Get();
		if (!Controller || !RunRosterKeys.Contains(Entry.Value)
			|| RunOutcomes.Contains(Entry.Value))
		{
			continue;
		}

		ASPPlayerState* ScrollPlayerState =
			Controller->GetPlayerState<ASPPlayerState>();
		ASPCharacter* ScrollCharacter =
			Cast<ASPCharacter>(Controller->GetPawn());
		if (!ScrollPlayerState)
		{
			continue;
		}

		if (ScrollPlayerState->IsExtracted())
		{
			CaptureRunOutcome(
				Entry.Value,
				ScrollPlayerState,
				ScrollCharacter,
				true);
		}
		else if (ScrollPlayerState->GetPlayerCondition()
			== ESPPlayerCondition::Missing)
		{
			CaptureMissingOutcome(Entry.Value);
		}
	}

	if (ScrollGameState->GetRunPhase() == ESPRunPhase::Resolution)
	{
		UpdatePartyRunPhase(ESPRunPhase::Resolution);
		ForceResolveOutstandingPlayers();
	}

	int32 ActivePlayers = 0;
	int32 DisconnectedPlayers = 0;
	int32 ExtractedPlayers = 0;
	int32 MissingPlayers = 0;
	for (const FString& RosterKey : RunRosterKeys)
	{
		if (const FSPRunOutcomeRecord* Outcome = RunOutcomes.Find(RosterKey))
		{
			Outcome->bExtracted ? ++ExtractedPlayers : ++MissingPlayers;
			continue;
		}

		if (DisconnectedRunPlayers.Contains(RosterKey))
		{
			++DisconnectedPlayers;
			continue;
		}

		bool bHasActiveController = false;
		for (const TPair<TWeakObjectPtr<AController>, FString>& ControllerEntry
			: ControllerRosterKeys)
		{
			if (ControllerEntry.Key.IsValid()
				&& ControllerEntry.Value == RosterKey)
			{
				bHasActiveController = true;
				break;
			}
		}
		ActivePlayers += bHasActiveController ? 1 : 0;
	}

	ScrollGameState->AuthoritySetRunRosterCounts(
		ActivePlayers,
		DisconnectedPlayers,
		ExtractedPlayers,
		MissingPlayers);
	ScrollGameState->AuthoritySetExtractedPlayerCount(ExtractedPlayers);

	if (RunRosterKeys.Num() == ExpectedPlayers
		&& RunOutcomes.Num() == ExpectedPlayers)
	{
		ScrollGameState->AuthoritySetRunPhase(ESPRunPhase::Resolution);
		TryCommitSettlement();
	}
}

void ASPGameMode::ExpireDisconnectedPlayers(const double ServerTime)
{
	TArray<FString> ExpiredKeys;
	for (const TPair<FString, FSPDisconnectedRunRecord>& Entry
		: DisconnectedRunPlayers)
	{
		if (ServerTime >= Entry.Value.ExpiresAtServerTime
			|| Entry.Value.PlayerSnapshot.IsBleedoutExpired(ServerTime))
		{
			ExpiredKeys.Add(Entry.Key);
		}
	}

	for (const FString& RosterKey : ExpiredKeys)
	{
		const FSPDisconnectedRunRecord* Record =
			DisconnectedRunPlayers.Find(RosterKey);
		const bool bBleedoutExpired = Record
			&& Record->PlayerSnapshot.IsBleedoutExpired(ServerTime);
		CaptureMissingOutcome(
			RosterKey,
			Record ? &Record->PlayerSnapshot : nullptr);
		DisconnectedRunPlayers.Remove(RosterKey);
		PendingReconnectInventories.Remove(RosterKey);
		UE_LOG(LogSPGameMode, Warning,
			TEXT("SP_RUN_RECONNECT_EXPIRED key=%s grace=%.0f bleedout=%d"),
			*RosterKey,
			ReconnectGraceSeconds,
			bBleedoutExpired ? 1 : 0);
	}
}

void ASPGameMode::ApplyPendingReconnectInventories()
{
	if (PendingReconnectInventories.IsEmpty())
	{
		return;
	}

	TArray<FString> RestoredKeys;
	for (const TPair<TWeakObjectPtr<AController>, FString>& Entry
		: ControllerRosterKeys)
	{
		const FSPInventoryState* PendingState =
			PendingReconnectInventories.Find(Entry.Value);
		ASPCharacter* ScrollCharacter = Entry.Key.IsValid()
			? Cast<ASPCharacter>(Entry.Key->GetPawn())
			: nullptr;
		if (!PendingState || !ScrollCharacter)
		{
			continue;
		}

		if (ScrollCharacter->GetInventory().AuthorityRestoreState(*PendingState))
		{
			RestoredKeys.Add(Entry.Value);
		}
	}

	for (const FString& RosterKey : RestoredKeys)
	{
		PendingReconnectInventories.Remove(RosterKey);
	}
}

void ASPGameMode::ForceResolveOutstandingPlayers()
{
	for (const TPair<TWeakObjectPtr<AController>, FString>& Entry
		: ControllerRosterKeys)
	{
		AController* Controller = Entry.Key.Get();
		if (!Controller || !RunRosterKeys.Contains(Entry.Value)
			|| RunOutcomes.Contains(Entry.Value))
		{
			continue;
		}

		if (ASPPlayerState* ScrollPlayerState =
			Controller->GetPlayerState<ASPPlayerState>())
		{
			ScrollPlayerState->AuthorityMarkMissing();
			CaptureMissingOutcome(Entry.Value);
		}
	}

	TArray<FString> DisconnectedKeys;
	DisconnectedRunPlayers.GetKeys(DisconnectedKeys);
	for (const FString& RosterKey : DisconnectedKeys)
	{
		const FSPDisconnectedRunRecord* Record =
			DisconnectedRunPlayers.Find(RosterKey);
		CaptureMissingOutcome(
			RosterKey,
			Record ? &Record->PlayerSnapshot : nullptr);
		DisconnectedRunPlayers.Remove(RosterKey);
		PendingReconnectInventories.Remove(RosterKey);
	}
}

void ASPGameMode::CaptureRunOutcome(
	const FString& RosterKey,
	const ASPPlayerState* PlayerState,
	const ASPCharacter* Character,
	const bool bExtracted)
{
	if (RosterKey.IsEmpty() || !RunRosterKeys.Contains(RosterKey)
		|| RunOutcomes.Contains(RosterKey) || !PlayerState)
	{
		return;
	}

	FSPRunOutcomeRecord Outcome;
	Outcome.PlayerId = BuildLocalPlayerId(PlayerState);
	Outcome.bExtracted = bExtracted;
	Outcome.PickedUpCount = PlayerState->GetPickedUpCount();
	Outcome.ConsumedScrollCount = PlayerState->GetConsumedScrollCount();
	Outcome.ExtractedScrollCount =
		bExtracted ? PlayerState->GetExtractedScrollCount() : 0;
	Outcome.GoldDelta = bExtracted ? PlayerState->GetGoldDelta() : 0;

	if (bExtracted && Character)
	{
		const FSPInventoryState& InventoryState =
			Character->GetInventory().GetInventoryState();
		auto AddOccupiedSlot =
			[&Outcome](const FSPInventorySlot& Slot)
			{
				if (Slot.bOccupied)
				{
					Outcome.ExtractedItems.Add(Slot.Item);
				}
			};
		AddOccupiedSlot(InventoryState.HandSlot);
		for (const FSPInventorySlot& BagSlot : InventoryState.BagSlots)
		{
			AddOccupiedSlot(BagSlot);
		}
	}

	RunOutcomes.Add(RosterKey, MoveTemp(Outcome));
	DisconnectedRunPlayers.Remove(RosterKey);
	PendingReconnectInventories.Remove(RosterKey);
}

void ASPGameMode::CaptureMissingOutcome(
	const FString& RosterKey,
	const FSPPlayerRunSnapshot* Snapshot)
{
	if (RosterKey.IsEmpty() || !RunRosterKeys.Contains(RosterKey)
		|| RunOutcomes.Contains(RosterKey))
	{
		return;
	}

	FSPRunOutcomeRecord Outcome;
	Outcome.PlayerId = RosterKey;
	Outcome.bExtracted = false;
	if (Snapshot)
	{
		Outcome.PickedUpCount = Snapshot->PickedUpCount;
		Outcome.ConsumedScrollCount = Snapshot->ConsumedScrollCount;
	}
	RunOutcomes.Add(RosterKey, MoveTemp(Outcome));
}

FString ASPGameMode::ResolveRosterKey(const AController* Controller) const
{
	const APlayerState* PlayerState = Controller ? Controller->PlayerState : nullptr;
	if (PlayerState && PlayerState->GetUniqueId().IsValid())
	{
		return FString::Printf(
			TEXT("Online:%s"),
			*PlayerState->GetUniqueId().ToString());
	}

	if (PlayerState)
	{
		return FString::Printf(
			TEXT("Local:%d:%s"),
			PlayerState->GetPlayerId(),
			*PlayerState->GetPlayerName());
	}

	return FString::Printf(TEXT("Controller:%s"), *GetNameSafe(Controller));
}

FString ASPGameMode::ResolveHostCampaignOwnerId() const
{
	for (const TPair<TWeakObjectPtr<AController>, FString>& Entry
		: ControllerRosterKeys)
	{
		const APlayerController* PlayerController =
			Cast<APlayerController>(Entry.Key.Get());
		if (PlayerController && PlayerController->IsLocalController())
		{
			return Entry.Value;
		}
	}

	return TEXT("ListenHost");
}

bool ASPGameMode::TryRestoreDisconnectedPlayer(
	APlayerController* NewPlayer,
	const FString& RosterKey)
{
	FSPDisconnectedRunRecord* Record =
		DisconnectedRunPlayers.Find(RosterKey);
	if (!NewPlayer || !Record || !GetWorld()
		|| !IsRunReconnectAllowed(RosterKey))
	{
		return false;
	}

	ASPPlayerState* ScrollPlayerState =
		NewPlayer->GetPlayerState<ASPPlayerState>();
	const double ServerTime = GetWorld()->GetTimeSeconds();
	const bool bReconnectExpired =
		ServerTime >= Record->ExpiresAtServerTime;
	const bool bBleedoutExpired =
		Record->PlayerSnapshot.IsBleedoutExpired(ServerTime);
	const bool bSnapshotValid =
		Record->PlayerSnapshot.IsStructurallyValid()
		&& Record->InventorySnapshot.IsStructurallyValid();
	if (!ScrollPlayerState || bReconnectExpired
		|| bBleedoutExpired || !bSnapshotValid)
	{
		if (ScrollPlayerState)
		{
			ScrollPlayerState->AuthorityMarkMissing();
		}
		const FSPPlayerRunSnapshot ExpiredSnapshot =
			Record->PlayerSnapshot;
		CaptureMissingOutcome(RosterKey, &ExpiredSnapshot);
		DisconnectedRunPlayers.Remove(RosterKey);
		PendingReconnectInventories.Remove(RosterKey);
		UE_LOG(LogSPGameMode, Warning,
			TEXT("SP_RUN_RECONNECT_REJECTED key=%s grace_expired=%d bleedout_expired=%d snapshot_valid=%d"),
			*RosterKey,
			bReconnectExpired ? 1 : 0,
			bBleedoutExpired ? 1 : 0,
			bSnapshotValid ? 1 : 0);
		return false;
	}

	if (!ScrollPlayerState->AuthorityRestoreReconnectSnapshot(
			Record->PlayerSnapshot))
	{
		if (ScrollPlayerState->GetPlayerCondition()
			== ESPPlayerCondition::Missing)
		{
			const FSPPlayerRunSnapshot ExpiredSnapshot =
				Record->PlayerSnapshot;
			CaptureMissingOutcome(RosterKey, &ExpiredSnapshot);
			DisconnectedRunPlayers.Remove(RosterKey);
			PendingReconnectInventories.Remove(RosterKey);
		}
		return false;
	}

	if (ASPCharacter* ScrollCharacter =
		Cast<ASPCharacter>(NewPlayer->GetPawn()))
	{
		if (!ScrollCharacter->GetInventory().AuthorityRestoreState(
			Record->InventorySnapshot))
		{
			return false;
		}
	}
	else
	{
		PendingReconnectInventories.Add(
			RosterKey,
			Record->InventorySnapshot);
	}

	DisconnectedRunPlayers.Remove(RosterKey);
	UE_LOG(LogSPGameMode, Display,
		TEXT("SP_RUN_PLAYER_RECONNECTED key=%s"), *RosterKey);
	return true;
}

bool ASPGameMode::EnsureHostCampaignReady()
{
	UGameInstance* GameInstance = GetGameInstance();
	USPCampaignSubsystem* CampaignSubsystem = GameInstance
		? GameInstance->GetSubsystem<USPCampaignSubsystem>()
		: nullptr;
	if (!CampaignSubsystem)
	{
		return false;
	}

	if (CampaignSubsystem->GetCurrentCampaign()
		&& CampaignSubsystem->GetCurrentCampaignSlotIndex()
			== CampaignSlotIndex)
	{
		return true;
	}

	const FString OwnerId = ResolveHostCampaignOwnerId();
	ESPCampaignPersistenceResult Result =
		CampaignSubsystem->LoadCampaignSlot(CampaignSlotIndex, OwnerId);
	if (Result == ESPCampaignPersistenceResult::SlotEmpty)
	{
		Result = CampaignSubsystem->CreateCampaignSlot(
			CampaignSlotIndex,
			OwnerId);
	}

	if (Result != ESPCampaignPersistenceResult::Success)
	{
		UE_LOG(LogSPGameMode, Error,
			TEXT("SP_CAMPAIGN_READY_FAILED slot=%d owner=%s result=%d"),
			CampaignSlotIndex,
			*OwnerId,
			static_cast<int32>(Result));
		return false;
	}

	return true;
}

bool ASPGameMode::CommitHostCampaignSettlement()
{
	if (bHostCampaignCommitted)
	{
		return true;
	}

	if (!EnsureHostCampaignReady())
	{
		return false;
	}

	USPCampaignSubsystem* CampaignSubsystem =
		GetGameInstance()->GetSubsystem<USPCampaignSubsystem>();
	const USPCampaignSaveGame* Campaign =
		CampaignSubsystem ? CampaignSubsystem->GetCurrentCampaign() : nullptr;
	if (!Campaign)
	{
		return false;
	}

	int64 GoldDelta = 0;
	int32 ExtractedPlayers = 0;
	FSPContractRunEvidence ContractEvidence;
	ContractEvidence.RunId = SessionId;
	FSPCampaignTransaction Transaction;
	Transaction.TransactionId = SessionId;
	Transaction.RunId = SessionId;
	Transaction.ExpectedRevision = Campaign->GetRevision();
	UAssetManager& AssetManager = UAssetManager::Get();
	for (const TPair<FString, FSPRunOutcomeRecord>& Entry : RunOutcomes)
	{
		FSPContractPlayerExtraction PlayerEvidence;
		PlayerEvidence.PlayerId = Entry.Value.PlayerId;
		PlayerEvidence.bExtracted = Entry.Value.bExtracted;
		for (const FSPItemInstance& Item
			: Entry.Value.ExtractedItems)
		{
			FSPContractItemEvidence ItemEvidence;
			ItemEvidence.Item = Item;
			ItemEvidence.DefinitionStableId =
				Item.DefinitionId.PrimaryAssetName;
			if (Item.Kind == ESPItemKind::Scroll)
			{
				if (const USPScrollDefinition* Definition =
					ResolvePrimaryAssetDefinition<USPScrollDefinition>(
						AssetManager,
						Item.DefinitionId);
					Definition && !Definition->StableId.IsNone())
				{
					ItemEvidence.DefinitionStableId =
						Definition->StableId;
				}
				ItemEvidence.EngravingStableId =
					Item.ScrollRoll.EngravingDefinitionId.PrimaryAssetName;
				if (const USPScrollEngravingDefinition* Engraving =
					ResolvePrimaryAssetDefinition<
						USPScrollEngravingDefinition>(
							AssetManager,
							Item.ScrollRoll.EngravingDefinitionId);
					Engraving && !Engraving->StableId.IsNone())
				{
					ItemEvidence.EngravingStableId =
						Engraving->StableId;
				}
			}
			else if (const USPItemDefinition* Definition =
				ResolvePrimaryAssetDefinition<USPItemDefinition>(
					AssetManager,
					Item.DefinitionId);
				Definition && !Definition->StableId.IsNone())
			{
				ItemEvidence.DefinitionStableId =
					Definition->StableId;
			}
			PlayerEvidence.Items.Add(MoveTemp(ItemEvidence));
		}
		ContractEvidence.Players.Add(MoveTemp(PlayerEvidence));

		if (!Entry.Value.bExtracted)
		{
			continue;
		}

		++ExtractedPlayers;
		GoldDelta += Entry.Value.GoldDelta;
		Transaction.SettlementItemsToAdd.Append(
			Entry.Value.ExtractedItems);
	}

	FSPContractEvaluationResult ContractResult;
	if (RuntimeContractDefinition)
	{
		ContractResult = FSPContractEvaluator::Evaluate(
			*RuntimeContractDefinition,
			ContractEvidence);
		if (ContractResult.IsSuccess())
		{
			GoldDelta += ContractResult.GoldReward;
		}
	}

	if (GoldDelta > MAX_int32)
	{
		UE_LOG(LogSPGameMode, Error,
			TEXT("SP_CAMPAIGN_SETTLEMENT_REJECTED run=%s reason=gold_overflow"),
			*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower));
		return false;
	}

	Transaction.GoldDelta = static_cast<int32>(GoldDelta);
	Transaction.GuildXPDelta = ExtractedPlayers * 10
		+ (ContractResult.IsSuccess()
			? ContractResult.GuildXpReward
			: 0);
	ESPCampaignApplyResult ApplyResult =
		ESPCampaignApplyResult::InvalidTransaction;
	const ESPCampaignPersistenceResult PersistenceResult =
		CampaignSubsystem->CommitCampaignTransaction(
			Transaction,
			ApplyResult);
	if (PersistenceResult != ESPCampaignPersistenceResult::Success
		|| (ApplyResult != ESPCampaignApplyResult::Applied
			&& ApplyResult != ESPCampaignApplyResult::AlreadyProcessed))
	{
		UE_LOG(LogSPGameMode, Error,
			TEXT("SP_CAMPAIGN_SETTLEMENT_FAILED run=%s persistence=%d apply=%d"),
			*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower),
			static_cast<int32>(PersistenceResult),
			static_cast<int32>(ApplyResult));
		return false;
	}

	bHostCampaignCommitted = true;
	UE_LOG(LogSPGameMode, Display,
		TEXT("SP_CAMPAIGN_SETTLEMENT_COMMITTED run=%s extracted=%d items=%d gold=%d xp=%d contract=%d"),
		*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower),
		ExtractedPlayers,
		Transaction.SettlementItemsToAdd.Num(),
		Transaction.GoldDelta,
		Transaction.GuildXPDelta,
		ContractResult.IsSuccess() ? 1 : 0);
	return true;
}

void ASPGameMode::TryCommitSettlement()
{
	if (!HasAuthority() || bSettlementStarted
		|| RunRosterKeys.Num() != ExpectedPlayers
		|| RunOutcomes.Num() != ExpectedPlayers
		|| !CommitHostCampaignSettlement())
	{
		return;
	}

	ASPGameState* ScrollGameState = GetGameState<ASPGameState>();
	if (!ScrollGameState)
	{
		return;
	}

	const int64 CompletionTime = FDateTime::UtcNow().ToUnixTimestamp();
	TArray<TPair<TWeakObjectPtr<ASPPlayerController>, FSPSessionResult>>
		ResultsToSend;
	for (const TPair<TWeakObjectPtr<AController>, FString>& Entry
		: ControllerRosterKeys)
	{
		ASPPlayerController* ScrollPlayerController =
			Cast<ASPPlayerController>(Entry.Key.Get());
		const FSPRunOutcomeRecord* Outcome =
			RunOutcomes.Find(Entry.Value);
		if (!ScrollPlayerController || !Outcome)
		{
			continue;
		}

		FSPSessionResult Result;
		Result.SessionId = SessionId;
		Result.PlayerId = Outcome->PlayerId;
		Result.PartySize = ExpectedPlayers;
		Result.bExtracted = Outcome->bExtracted;
		Result.PickedUpCount = Outcome->PickedUpCount;
		Result.ConsumedScrollCount = Outcome->ConsumedScrollCount;
		Result.ExtractedScrollCount = Outcome->ExtractedScrollCount;
		Result.GoldDelta = Outcome->GoldDelta;
		Result.CompletedAtUnixSeconds = CompletionTime;
		Result.ResultHash = SPBuildSessionResultHash(Result);
		ResultsToSend.Emplace(ScrollPlayerController, MoveTemp(Result));
	}

	bSettlementStarted = true;
	PendingSettlementHashes.Reset();
	SuccessfulSettlementAcks.Reset();
	for (const TPair<TWeakObjectPtr<ASPPlayerController>, FSPSessionResult>& Entry
		: ResultsToSend)
	{
		PendingSettlementHashes.Add(Entry.Key, Entry.Value.ResultHash);
	}

	ScrollGameState->AuthoritySetPhase(ESPSessionPhase::SettlementPending);
	UpdatePartyRunPhase(ESPRunPhase::Settlement);
	UE_LOG(LogSPGameMode, Display,
		TEXT("SP_SPIKE_SETTLEMENT_PENDING session=%s party_size=%d connected_results=%d host_campaign=1"),
		*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower),
		ExpectedPlayers,
		ResultsToSend.Num());

	for (const TPair<TWeakObjectPtr<ASPPlayerController>, FSPSessionResult>& Entry
		: ResultsToSend)
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
			*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*Result.PlayerId,
			Result.PickedUpCount,
			Result.ConsumedScrollCount,
			Result.ExtractedScrollCount,
			Result.GoldDelta,
			*Result.ResultHash);
	}

	if (PendingSettlementHashes.IsEmpty())
	{
		CompleteSettlementAfterAckWindow();
	}
	else
	{
		GetWorldTimerManager().SetTimer(
			SettlementAckTimerHandle,
			this,
			&ASPGameMode::CompleteSettlementAfterAckWindow,
			SettlementAckWindowSeconds,
			false);
	}
}

void ASPGameMode::CompleteSettlementAfterAckWindow()
{
	if (!HasAuthority() || !bSettlementStarted || !bHostCampaignCommitted)
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(SettlementAckTimerHandle);
	const int32 MissingAcks = FMath::Max(
		0,
		PendingSettlementHashes.Num()
			- SuccessfulSettlementAcks.Num());
	if (MissingAcks > 0)
	{
		UE_LOG(LogSPGameMode, Warning,
			TEXT("SP_SPIKE_SETTLEMENT_ACK_WINDOW_EXPIRED session=%s missing=%d host_campaign=committed"),
			*SessionId.ToString(EGuidFormats::DigitsWithHyphensLower),
			MissingAcks);
	}

	if (ASPGameState* ScrollGameState = GetGameState<ASPGameState>())
	{
		ScrollGameState->AuthorityMarkSettlementCommitted();
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
