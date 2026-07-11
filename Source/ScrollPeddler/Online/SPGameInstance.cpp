#include "Online/SPGameInstance.h"

#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Kismet/GameplayStatics.h"
#include "Persistence/SPSaveGame.h"

DEFINE_LOG_CATEGORY_STATIC(LogSPGameInstance, Log, All);

const FString USPGameInstance::SaveSlotName(TEXT("ScrollPeddler_LocalProfile"));

FString USPGameInstance::GetSaveSlotName()
{
	FString Override;
	if (FParse::Value(FCommandLine::Get(), TEXT("SPProfileSlot="), Override))
	{
		Override.TrimStartAndEndInline();
		if (!Override.IsEmpty())
		{
			return Override.Left(128);
		}
	}

	return SaveSlotName;
}

USPSaveGame* USPGameInstance::LoadOrCreateLocalSave() const
{
	const FString SlotName = GetSaveSlotName();
	if (UGameplayStatics::DoesSaveGameExist(SlotName, SaveUserIndex))
	{
		if (USPSaveGame* Loaded = Cast<USPSaveGame>(
			UGameplayStatics::LoadGameFromSlot(SlotName, SaveUserIndex)))
		{
			return Loaded;
		}

		UE_LOG(LogSPGameInstance, Error,
			TEXT("SP_SPIKE_SAVE_LOAD_FAILED slot=%s user=%d"), *SlotName, SaveUserIndex);
		return nullptr;
	}

	return Cast<USPSaveGame>(UGameplayStatics::CreateSaveGameObject(USPSaveGame::StaticClass()));
}

bool USPGameInstance::CommitSessionResult(const FSPSessionResult& Result)
{
	const FString SlotName = GetSaveSlotName();
	if (!SPIsSessionResultIntegrityValid(Result))
	{
		UE_LOG(LogSPGameInstance, Error,
			TEXT("SP_SPIKE_SAVE_REJECTED session=%s player=%s reason=invalid_result"),
			*Result.SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), *Result.PlayerId);
		return false;
	}

	USPSaveGame* Save = LoadOrCreateLocalSave();
	if (!Save)
	{
		return false;
	}

	if (const FSPSessionResult* ExistingResult = Save->FindCommittedResult(Result.SessionId))
	{
		const bool bExistingIntegrityValid =
			SPIsSessionResultIntegrityValid(*ExistingResult);
		if (bExistingIntegrityValid
			&& SPIsIdempotentSessionResult(*ExistingResult, Result))
		{
			LastVerifiedSave = Save;
			UE_LOG(LogSPGameInstance, Display,
				TEXT("SP_SPIKE_SAVE_IDEMPOTENT session=%s player=%s sessions=%d gold=%d"),
				*Result.SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), *Result.PlayerId,
				Save->GetSessionsPlayed(), Save->GetGold());
			return true;
		}

		UE_LOG(LogSPGameInstance, Error,
			TEXT("SP_SPIKE_SAVE_CONFLICT session=%s existing_player=%s incoming_player=%s existing_hash=%s incoming_hash=%s existing_integrity=%d"),
			*Result.SessionId.ToString(EGuidFormats::DigitsWithHyphensLower),
			*ExistingResult->PlayerId,
			*Result.PlayerId,
			*ExistingResult->ResultHash,
			*Result.ResultHash,
			bExistingIntegrityValid ? 1 : 0);
		return false;
	}

	if (!Save->ApplyResultIfNew(Result))
	{
		UE_LOG(LogSPGameInstance, Error,
			TEXT("SP_SPIKE_SAVE_REJECTED session=%s player=%s reason=apply_failed"),
			*Result.SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), *Result.PlayerId);
		return false;
	}

	if (!UGameplayStatics::SaveGameToSlot(Save, SlotName, SaveUserIndex))
	{
		UE_LOG(LogSPGameInstance, Error,
			TEXT("SP_SPIKE_SAVE_WRITE_FAILED session=%s slot=%s"),
			*Result.SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), *SlotName);
		return false;
	}

	UE_LOG(LogSPGameInstance, Display,
		TEXT("SP_SPIKE_SAVE_COMMITTED session=%s player=%s sessions=%d gold=%d"),
		*Result.SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), *Result.PlayerId,
		Save->GetSessionsPlayed(), Save->GetGold());

	USPSaveGame* Reloaded = Cast<USPSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SlotName, SaveUserIndex));
	const FSPSessionResult* ReloadedResult = Reloaded
		? Reloaded->FindCommittedResult(Result.SessionId)
		: nullptr;
	const bool bVerified = ReloadedResult
		&& SPIsSessionResultIntegrityValid(*ReloadedResult)
		&& SPIsIdempotentSessionResult(*ReloadedResult, Result)
		&& Reloaded->GetGold() == Save->GetGold()
		&& Reloaded->GetSessionsPlayed() == Save->GetSessionsPlayed();

	if (!bVerified)
	{
		UE_LOG(LogSPGameInstance, Error,
			TEXT("SP_SPIKE_SAVE_VERIFY_FAILED session=%s slot=%s"),
			*Result.SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), *SlotName);
		return false;
	}

	LastVerifiedSave = Reloaded;
	UE_LOG(LogSPGameInstance, Display,
		TEXT("SP_SPIKE_SAVE_VERIFIED session=%s player=%s sessions=%d gold=%d"),
		*Result.SessionId.ToString(EGuidFormats::DigitsWithHyphensLower), *Result.PlayerId,
		Reloaded->GetSessionsPlayed(), Reloaded->GetGold());
	return true;
}
