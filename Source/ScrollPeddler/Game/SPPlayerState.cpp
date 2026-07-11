#include "Game/SPPlayerState.h"

#include "Data/SPScrollDefinition.h"
#include "Engine/AssetManager.h"
#include "Net/UnrealNetwork.h"
#include "Player/SPCharacter.h"

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
		UE_LOG(LogSPPlayerState, Warning,
			TEXT("SP_SPIKE_PICKUP_RECORD_REJECTED duplicate=1 instance=%s"),
			*Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
		return false;
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
		|| !PickedUpInstanceIds.Contains(Item.InstanceId) || !RecordedDeliveryValue
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
	if (!HasAuthority() || bExtracted || !Item.IsValid() ||
		!PickedUpInstanceIds.Contains(Item.InstanceId) || ConsumedInstanceIds.Contains(Item.InstanceId) ||
		!RecordedDeliveryValue)
	{
		UE_LOG(LogSPPlayerState, Error,
			TEXT("SP_SPIKE_PICKUP_RECORD_ROLLBACK_REJECTED instance=%s"),
			*Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower));
		return false;
	}

	const int32 ValueToRemove = *RecordedDeliveryValue;
	PickedUpInstanceIds.Remove(Item.InstanceId);
	PickedUpDeliveryValues.Remove(Item.InstanceId);
	PickedUpCount = FMath::Max(0, PickedUpCount - 1);
	CarriedDeliveryValue = FMath::Max(0, CarriedDeliveryValue - ValueToRemove);
	ForceNetUpdate();

	UE_LOG(LogSPPlayerState, Warning,
		TEXT("SP_SPIKE_PICKUP_RECORD_ROLLED_BACK player=%s instance=%s picked=%d carried_value=%d"),
		*GetPlayerName(), *Item.InstanceId.ToString(EGuidFormats::DigitsWithHyphensLower),
		PickedUpCount, CarriedDeliveryValue);
	return true;
}

void ASPPlayerState::MarkExtracted()
{
	if (!HasAuthority() || bExtracted)
	{
		return;
	}

	bExtracted = true;
	ExtractedScrollCount = FMath::Max(0, PickedUpCount - ConsumedScrollCount);
	GoldDelta = FMath::Max(0, CarriedDeliveryValue);
	ForceNetUpdate();
	OnRep_Extracted();

	UE_LOG(LogSPPlayerState, Display,
		TEXT("SP_SPIKE_PLAYER_EXTRACTED player=%s picked=%d consumed=%d extracted_scrolls=%d gold=%d"),
		*GetPlayerName(), PickedUpCount, ConsumedScrollCount, ExtractedScrollCount, GoldDelta);
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
