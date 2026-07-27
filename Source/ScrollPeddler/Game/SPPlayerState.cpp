#include "Game/SPPlayerState.h"

#include "Data/SPScrollDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "Player/SPCharacter.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogSPPlayerState, Log, All);

namespace
{
bool ResolveDeliveryValue(const FSPScrollInstance& Item, int32& OutDeliveryValue)
{
	if (UAssetManager* AssetManager = UAssetManager::GetIfInitialized())
	{
		const USPScrollDefinition* Definition = Cast<USPScrollDefinition>(
			AssetManager->GetPrimaryAssetObject(Item.BaseDefinitionId));
		if (!Definition)
		{
			const FSoftObjectPath DefinitionPath = AssetManager->GetPrimaryAssetPath(Item.BaseDefinitionId);
			Definition = DefinitionPath.IsValid()
				? Cast<USPScrollDefinition>(DefinitionPath.TryLoad())
				: nullptr;
		}

		if (Definition && Definition->GetPrimaryAssetId() == Item.BaseDefinitionId)
		{
			OutDeliveryValue = FMath::Max(0, Definition->DeliveryValue);
			return true;
		}
	}

	return false;
}
}

bool FSPPlayerRunSnapshot::IsStructurallyValid() const
{
	if (PickedUpCount < 0 || ConsumedScrollCount < 0
		|| ExtractedScrollCount < 0 || CarriedDeliveryValue < 0
		|| GoldDelta < 0 || DownCount < 0
		|| !FMath::IsFinite(BleedoutDeadlineServerTime)
		|| BleedoutDeadlineServerTime < 0.0
		|| (PlayerCondition == ESPPlayerCondition::Down
			&& BleedoutDeadlineServerTime <= 0.0)
		|| (PlayerCondition != ESPPlayerCondition::Down
			&& BleedoutDeadlineServerTime > 0.0)
		|| ConsumedScrollCount > PickedUpCount
		|| PickedUpInstanceIds.Num() != PickedUpCount
		|| ConsumedInstanceIds.Num() != ConsumedScrollCount
		|| PickedUpDeliveryValues.Num() != PickedUpCount)
	{
		return false;
	}

	TSet<FGuid> SeenPickedUpIds;
	for (const FGuid& InstanceId : PickedUpInstanceIds)
	{
		if (!InstanceId.IsValid() || SeenPickedUpIds.Contains(InstanceId)
			|| !PickedUpDeliveryValues.Contains(InstanceId))
		{
			return false;
		}
		SeenPickedUpIds.Add(InstanceId);
	}

	TSet<FGuid> SeenConsumedIds;
	for (const FGuid& InstanceId : ConsumedInstanceIds)
	{
		if (!SeenPickedUpIds.Contains(InstanceId)
			|| SeenConsumedIds.Contains(InstanceId))
		{
			return false;
		}
		SeenConsumedIds.Add(InstanceId);
	}

	TSet<FGuid> SeenCarriedIds;
	for (const FGuid& InstanceId : CarriedInstanceIds)
	{
		if (!SeenPickedUpIds.Contains(InstanceId)
			|| SeenConsumedIds.Contains(InstanceId)
			|| SeenCarriedIds.Contains(InstanceId))
		{
			return false;
		}
		SeenCarriedIds.Add(InstanceId);
	}

	if (bExtracted)
	{
		return ParticipationState == ESPParticipationState::Extracted
			&& PlayerCondition != ESPPlayerCondition::Missing;
	}

	return PlayerCondition != ESPPlayerCondition::Missing
		&& ParticipationState != ESPParticipationState::Extracted
		&& ParticipationState != ESPParticipationState::Spectating;
}

bool FSPPlayerRunSnapshot::IsBleedoutExpired(
	const double CurrentServerTime) const
{
	return FMath::IsFinite(CurrentServerTime)
		&& CurrentServerTime >= 0.0
		&& PlayerCondition == ESPPlayerCondition::Down
		&& BleedoutDeadlineServerTime > 0.0
		&& CurrentServerTime >= BleedoutDeadlineServerTime;
}

ASPPlayerState::ASPPlayerState()
{
	bReplicates = true;
}

void ASPPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ASPPlayerState, PickedUpCount);
	DOREPLIFETIME(ASPPlayerState, ConsumedScrollCount);
	DOREPLIFETIME(ASPPlayerState, ExtractedScrollCount);
	DOREPLIFETIME(ASPPlayerState, CarriedDeliveryValue);
	DOREPLIFETIME(ASPPlayerState, GoldDelta);
	DOREPLIFETIME(ASPPlayerState, bExtracted);
	DOREPLIFETIME(ASPPlayerState, PlayerCondition);
	DOREPLIFETIME(ASPPlayerState, ParticipationState);
	DOREPLIFETIME(ASPPlayerState, DownCount);
	DOREPLIFETIME(ASPPlayerState, BleedoutEndServerTime);
}

bool ASPPlayerState::RecordScrollPickedUp(const FSPScrollInstance& Item)
{
	if (!HasAuthority() || bExtracted || !Item.IsValid())
	{
		UE_LOG(LogSPPlayerState, Warning,
			TEXT("SP_SPIKE_PICKUP_RECORD_REJECTED authority=%d extracted=%d valid=%d"),
			HasAuthority() ? 1 : 0, bExtracted ? 1 : 0, Item.IsValid() ? 1 : 0);
		return false;
	}

	if (PickedUpInstanceIds.Contains(Item.InstanceId))
	{
		if (ConsumedInstanceIds.Contains(Item.InstanceId)
			|| CarriedInstanceIds.Contains(Item.InstanceId))
		{
			UE_LOG(LogSPPlayerState, Warning,
				TEXT("SP_SPIKE_PICKUP_RECORD_REJECTED duplicate=1 instance=%s"),
				*Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
			return false;
		}

		const int32* ExistingDeliveryValue =
			PickedUpDeliveryValues.Find(Item.InstanceId);
		if (!ExistingDeliveryValue)
		{
			return false;
		}
		CarriedInstanceIds.Add(Item.InstanceId);
		RecentlyReacquiredInstanceIds.Add(Item.InstanceId);
		CarriedDeliveryValue += *ExistingDeliveryValue;
		ForceNetUpdate();
		UE_LOG(LogSPPlayerState, Display,
			TEXT("SP_SPIKE_PICKUP_REACQUIRED player=%s instance=%s carried_value=%d"),
			*GetPlayerName(),
			*Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower),
			CarriedDeliveryValue);
		return true;
	}

	int32 DeliveryValue = 0;
	if (!ResolveDeliveryValue(Item, DeliveryValue))
	{
		UE_LOG(LogSPPlayerState, Error,
			TEXT("SP_SPIKE_PICKUP_RECORD_REJECTED reason=definition_resolution base=%s"),
			*Item.BaseDefinitionId.ToString());
		return false;
	}

	PickedUpInstanceIds.Add(Item.InstanceId);
	CarriedInstanceIds.Add(Item.InstanceId);
	PickedUpDeliveryValues.Add(Item.InstanceId, DeliveryValue);
	++PickedUpCount;
	CarriedDeliveryValue += DeliveryValue;
	ForceNetUpdate();

	UE_LOG(LogSPPlayerState, Display,
		TEXT("SP_SPIKE_PICKUP_RECORDED player=%s instance=%s picked=%d carried_value=%d"),
		*GetPlayerName(), *Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower),
		PickedUpCount, CarriedDeliveryValue);
	return true;
}

bool ASPPlayerState::RecordScrollConsumed(const FSPScrollInstance& Item, const int32 DeliveryValue)
{
	const int32* RecordedDeliveryValue = PickedUpDeliveryValues.Find(Item.InstanceId);
	if (!HasAuthority() || bExtracted || !Item.IsValid()
		|| !PickedUpInstanceIds.Contains(Item.InstanceId)
		|| !CarriedInstanceIds.Contains(Item.InstanceId)
		|| !RecordedDeliveryValue
		|| *RecordedDeliveryValue != FMath::Max(0, DeliveryValue))
	{
		UE_LOG(LogSPPlayerState, Warning,
			TEXT("SP_SPIKE_CONSUME_RECORD_REJECTED authority=%d extracted=%d valid=%d known_pickup=%d value_match=%d"),
			HasAuthority() ? 1 : 0, bExtracted ? 1 : 0, Item.IsValid() ? 1 : 0,
			PickedUpInstanceIds.Contains(Item.InstanceId) ? 1 : 0,
			RecordedDeliveryValue && *RecordedDeliveryValue == FMath::Max(0, DeliveryValue) ? 1 : 0);
		return false;
	}

	if (ConsumedInstanceIds.Contains(Item.InstanceId))
	{
		UE_LOG(LogSPPlayerState, Warning,
			TEXT("SP_SPIKE_CONSUME_RECORD_REJECTED duplicate=1 instance=%s"),
			*Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
		return false;
	}

	ConsumedInstanceIds.Add(Item.InstanceId);
	CarriedInstanceIds.Remove(Item.InstanceId);
	RecentlyReacquiredInstanceIds.Remove(Item.InstanceId);
	++ConsumedScrollCount;
	CarriedDeliveryValue = FMath::Max(0, CarriedDeliveryValue - *RecordedDeliveryValue);
	ForceNetUpdate();

	UE_LOG(LogSPPlayerState, Display,
		TEXT("SP_SPIKE_SCROLL_CONSUMED player=%s instance=%s consumed=%d carried_value=%d"),
		*GetPlayerName(), *Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower),
		ConsumedScrollCount, CarriedDeliveryValue);
	return true;
}

bool ASPPlayerState::RollbackScrollPickedUp(const FSPScrollInstance& Item)
{
	const int32* RecordedDeliveryValue = PickedUpDeliveryValues.Find(Item.InstanceId);
	if (!HasAuthority() || bExtracted || !Item.IsValid()
		|| !PickedUpInstanceIds.Contains(Item.InstanceId)
		|| !CarriedInstanceIds.Contains(Item.InstanceId)
		|| ConsumedInstanceIds.Contains(Item.InstanceId)
		|| !RecordedDeliveryValue)
	{
		UE_LOG(LogSPPlayerState, Error,
			TEXT("SP_SPIKE_PICKUP_RECORD_ROLLBACK_REJECTED instance=%s"),
			*Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
		return false;
	}

	const int32 ValueToRemove = *RecordedDeliveryValue;
	CarriedInstanceIds.Remove(Item.InstanceId);
	if (RecentlyReacquiredInstanceIds.Remove(Item.InstanceId) == 0)
	{
		PickedUpInstanceIds.Remove(Item.InstanceId);
		PickedUpDeliveryValues.Remove(Item.InstanceId);
		PickedUpCount = FMath::Max(0, PickedUpCount - 1);
	}
	CarriedDeliveryValue = FMath::Max(0, CarriedDeliveryValue - ValueToRemove);
	ForceNetUpdate();

	UE_LOG(LogSPPlayerState, Warning,
		TEXT("SP_SPIKE_PICKUP_RECORD_ROLLED_BACK player=%s instance=%s picked=%d carried_value=%d"),
		*GetPlayerName(), *Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower),
		PickedUpCount, CarriedDeliveryValue);
	return true;
}

bool ASPPlayerState::ConfirmScrollPickedUp(const FSPScrollInstance& Item)
{
	if (!HasAuthority() || !Item.IsValid()
		|| !CarriedInstanceIds.Contains(Item.InstanceId))
	{
		return false;
	}

	RecentlyReacquiredInstanceIds.Remove(Item.InstanceId);
	return true;
}

bool ASPPlayerState::RecordScrollDropped(const FSPScrollInstance& Item)
{
	const int32* RecordedDeliveryValue =
		PickedUpDeliveryValues.Find(Item.InstanceId);
	if (!HasAuthority() || bExtracted || !Item.IsValid()
		|| !RecordedDeliveryValue
		|| !CarriedInstanceIds.Contains(Item.InstanceId)
		|| ConsumedInstanceIds.Contains(Item.InstanceId))
	{
		return false;
	}

	CarriedInstanceIds.Remove(Item.InstanceId);
	RecentlyReacquiredInstanceIds.Remove(Item.InstanceId);
	CarriedDeliveryValue = FMath::Max(
		0,
		CarriedDeliveryValue - *RecordedDeliveryValue);
	ForceNetUpdate();
	return true;
}

void ASPPlayerState::MarkExtracted()
{
	AuthorityMarkExtracted();
}

bool ASPPlayerState::AuthorityTransitionCondition(const ESPPlayerCondition InCondition)
{
	if (!HasAuthority())
	{
		return false;
	}

	if (PlayerCondition == InCondition)
	{
		return true;
	}

	if (IsRunTerminal() || !SPCanTransitionPlayerCondition(PlayerCondition, InCondition))
	{
		UE_LOG(LogSPPlayerState, Warning,
			TEXT("SP_RUN_CONDITION_REJECTED player=%s current=%d requested=%d terminal=%d"),
			*GetPlayerName(), static_cast<int32>(PlayerCondition),
			static_cast<int32>(InCondition), IsRunTerminal() ? 1 : 0);
		return false;
	}

	const ESPPlayerCondition PreviousCondition = PlayerCondition;
	if (PreviousCondition == ESPPlayerCondition::Down)
	{
		ClearBleedout();
	}

	PlayerCondition = InCondition;
	if (PlayerCondition == ESPPlayerCondition::Down)
	{
		++DownCount;
		ScheduleBleedout(SPGetBleedoutDurationSeconds(DownCount));
	}

	ForceNetUpdate();
	UE_LOG(LogSPPlayerState, Display,
		TEXT("SP_RUN_CONDITION player=%s previous=%d current=%d downs=%d bleedout_end=%.3f"),
		*GetPlayerName(), static_cast<int32>(PreviousCondition),
		static_cast<int32>(PlayerCondition), DownCount, BleedoutEndServerTime);
	return true;
}

bool ASPPlayerState::AuthorityTransitionParticipation(
	const ESPParticipationState InParticipationState)
{
	if (!HasAuthority())
	{
		return false;
	}

	if (ParticipationState == InParticipationState)
	{
		return true;
	}

	if (InParticipationState == ESPParticipationState::Extracted)
	{
		return AuthorityMarkExtracted();
	}

	if (IsRunTerminal()
		|| !SPCanTransitionParticipationState(ParticipationState, InParticipationState))
	{
		UE_LOG(LogSPPlayerState, Warning,
			TEXT("SP_RUN_PARTICIPATION_REJECTED player=%s current=%d requested=%d terminal=%d"),
			*GetPlayerName(), static_cast<int32>(ParticipationState),
			static_cast<int32>(InParticipationState), IsRunTerminal() ? 1 : 0);
		return false;
	}

	const ESPParticipationState PreviousState = ParticipationState;
	ParticipationState = InParticipationState;
	ForceNetUpdate();
	UE_LOG(LogSPPlayerState, Display,
		TEXT("SP_RUN_PARTICIPATION player=%s previous=%d current=%d"),
		*GetPlayerName(), static_cast<int32>(PreviousState),
		static_cast<int32>(ParticipationState));
	return true;
}

bool ASPPlayerState::AuthorityMarkExtracted()
{
	if (!HasAuthority())
	{
		return false;
	}

	if (bExtracted)
	{
		return true;
	}

	if (PlayerCondition == ESPPlayerCondition::Missing
		|| ParticipationState == ESPParticipationState::Spectating)
	{
		UE_LOG(LogSPPlayerState, Warning,
			TEXT("SP_RUN_EXTRACTION_REJECTED player=%s condition=%d participation=%d"),
			*GetPlayerName(), static_cast<int32>(PlayerCondition),
			static_cast<int32>(ParticipationState));
		return false;
	}

	ClearBleedout();
	ParticipationState = ESPParticipationState::Extracted;
	bExtracted = true;
	ExtractedScrollCount = CarriedInstanceIds.Num();
	GoldDelta = FMath::Max(0, CarriedDeliveryValue);
	ForceNetUpdate();
	OnRep_Extracted();

	UE_LOG(LogSPPlayerState, Display,
		TEXT("SP_SPIKE_PLAYER_EXTRACTED player=%s picked=%d consumed=%d extracted_scrolls=%d gold=%d"),
		*GetPlayerName(), PickedUpCount, ConsumedScrollCount, ExtractedScrollCount, GoldDelta);
	return true;
}

bool ASPPlayerState::AuthorityMarkMissing()
{
	if (!HasAuthority())
	{
		return false;
	}

	if (PlayerCondition == ESPPlayerCondition::Missing)
	{
		return true;
	}

	if (bExtracted || ParticipationState == ESPParticipationState::Extracted)
	{
		UE_LOG(LogSPPlayerState, Warning,
			TEXT("SP_RUN_MISSING_REJECTED player=%s reason=already_extracted"),
			*GetPlayerName());
		return false;
	}

	ClearBleedout();
	PlayerCondition = ESPPlayerCondition::Missing;
	ParticipationState = ESPParticipationState::Spectating;
	ForceNetUpdate();

	UE_LOG(LogSPPlayerState, Display,
		TEXT("SP_RUN_PLAYER_MISSING player=%s downs=%d"),
		*GetPlayerName(), DownCount);
	return true;
}

bool ASPPlayerState::AuthorityBuildReconnectSnapshot(
	FSPPlayerRunSnapshot& OutSnapshot) const
{
	if (!HasAuthority() || IsRunTerminal())
	{
		return false;
	}

	OutSnapshot.PickedUpCount = PickedUpCount;
	OutSnapshot.ConsumedScrollCount = ConsumedScrollCount;
	OutSnapshot.ExtractedScrollCount = ExtractedScrollCount;
	OutSnapshot.CarriedDeliveryValue = CarriedDeliveryValue;
	OutSnapshot.GoldDelta = GoldDelta;
	OutSnapshot.bExtracted = bExtracted;
	OutSnapshot.PlayerCondition = PlayerCondition;
	OutSnapshot.ParticipationState = ESPParticipationState::Disconnected;
	OutSnapshot.DownCount = DownCount;
	OutSnapshot.BleedoutDeadlineServerTime =
		PlayerCondition == ESPPlayerCondition::Down
			? BleedoutEndServerTime
			: 0.0;
	OutSnapshot.PickedUpInstanceIds = PickedUpInstanceIds.Array();
	OutSnapshot.ConsumedInstanceIds = ConsumedInstanceIds.Array();
	OutSnapshot.CarriedInstanceIds = CarriedInstanceIds.Array();
	OutSnapshot.PickedUpDeliveryValues = PickedUpDeliveryValues;
	return OutSnapshot.IsStructurallyValid();
}

bool ASPPlayerState::AuthorityRestoreReconnectSnapshot(
	const FSPPlayerRunSnapshot& Snapshot)
{
	if (!HasAuthority() || !Snapshot.IsStructurallyValid()
		|| Snapshot.bExtracted
		|| Snapshot.ParticipationState != ESPParticipationState::Disconnected)
	{
		return false;
	}

	ClearBleedout();
	PickedUpCount = Snapshot.PickedUpCount;
	ConsumedScrollCount = Snapshot.ConsumedScrollCount;
	ExtractedScrollCount = Snapshot.ExtractedScrollCount;
	CarriedDeliveryValue = Snapshot.CarriedDeliveryValue;
	GoldDelta = Snapshot.GoldDelta;
	bExtracted = false;
	PlayerCondition = Snapshot.PlayerCondition;
	ParticipationState = ESPParticipationState::Active;
	DownCount = Snapshot.DownCount;
	PickedUpInstanceIds.Reset();
	for (const FGuid& InstanceId : Snapshot.PickedUpInstanceIds)
	{
		PickedUpInstanceIds.Add(InstanceId);
	}
	ConsumedInstanceIds.Reset();
	for (const FGuid& InstanceId : Snapshot.ConsumedInstanceIds)
	{
		ConsumedInstanceIds.Add(InstanceId);
	}
	CarriedInstanceIds.Reset();
	for (const FGuid& InstanceId : Snapshot.CarriedInstanceIds)
	{
		CarriedInstanceIds.Add(InstanceId);
	}
	RecentlyReacquiredInstanceIds.Reset();
	PickedUpDeliveryValues = Snapshot.PickedUpDeliveryValues;

	if (PlayerCondition == ESPPlayerCondition::Down)
	{
		if (Snapshot.IsBleedoutExpired(
				GetAuthoritativeServerTimeSeconds()))
		{
			AuthorityMarkMissing();
			return false;
		}
		ScheduleBleedoutUntil(
			Snapshot.BleedoutDeadlineServerTime);
	}

	ForceNetUpdate();
	UE_LOG(LogSPPlayerState, Display,
		TEXT("SP_RUN_RECONNECT_RESTORED player=%s condition=%d downs=%d picked=%d consumed=%d"),
		*GetPlayerName(), static_cast<int32>(PlayerCondition),
		DownCount, PickedUpCount, ConsumedScrollCount);
	return true;
}

float ASPPlayerState::GetBleedoutDurationForCurrentDown() const
{
	return DownCount > 0 ? SPGetBleedoutDurationSeconds(DownCount) : 0.0f;
}

double ASPPlayerState::GetRemainingBleedoutSeconds() const
{
	if (PlayerCondition != ESPPlayerCondition::Down || BleedoutEndServerTime <= 0.0)
	{
		return 0.0;
	}

	return FMath::Max(0.0, BleedoutEndServerTime - GetAuthoritativeServerTimeSeconds());
}

bool ASPPlayerState::IsRunTerminal() const
{
	return bExtracted
		|| PlayerCondition == ESPPlayerCondition::Missing
		|| ParticipationState == ESPParticipationState::Spectating;
}

void ASPPlayerState::ScheduleBleedout(const float DurationSeconds)
{
	ScheduleBleedoutUntil(
		GetAuthoritativeServerTimeSeconds()
			+ FMath::Max(0.0f, DurationSeconds));
}

void ASPPlayerState::ScheduleBleedoutUntil(
	const double DeadlineServerTime)
{
	ClearBleedout();
	const double CurrentServerTime =
		GetAuthoritativeServerTimeSeconds();
	BleedoutEndServerTime = FMath::Max(
		CurrentServerTime,
		DeadlineServerTime);
	const double RemainingSeconds =
		BleedoutEndServerTime - CurrentServerTime;

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			BleedoutTimerHandle,
			this,
			&ASPPlayerState::HandleBleedoutExpired,
			FMath::Max(
				static_cast<double>(KINDA_SMALL_NUMBER),
				RemainingSeconds),
			false);
	}
}

void ASPPlayerState::ClearBleedout()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BleedoutTimerHandle);
	}

	BleedoutEndServerTime = 0.0;
}

void ASPPlayerState::HandleBleedoutExpired()
{
	if (!HasAuthority() || PlayerCondition != ESPPlayerCondition::Down)
	{
		return;
	}

	AuthorityMarkMissing();
}

double ASPPlayerState::GetAuthoritativeServerTimeSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}

	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}

	return static_cast<double>(World->GetTimeSeconds());
}

void ASPPlayerState::OnRep_Extracted()
{
	if (bExtracted)
	{
		if (ASPCharacter* ScrollCharacter = GetPawn<ASPCharacter>())
		{
			ScrollCharacter->HandleExtractionCommitted();
		}
	}
}

void ASPPlayerState::OnRep_RunState()
{
	UE_LOG(LogSPPlayerState, Verbose,
		TEXT("SP_RUN_STATE_REPLICATED player=%s condition=%d participation=%d downs=%d bleedout_end=%.3f"),
		*GetPlayerName(), static_cast<int32>(PlayerCondition),
		static_cast<int32>(ParticipationState), DownCount, BleedoutEndServerTime);
}
