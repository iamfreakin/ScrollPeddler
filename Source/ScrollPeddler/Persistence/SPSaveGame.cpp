#include "Persistence/SPSaveGame.h"

DEFINE_LOG_CATEGORY_STATIC(LogSPSaveGame, Log, All);

bool USPSaveGame::ApplyResultIfNew(const FSPSessionResult& Result)
{
	if (!SPIsSessionResultIntegrityValid(Result))
	{
		return false;
	}

	if (const FSPSessionResult* ExistingResult = FindCommittedResult(Result.SessionId))
	{
		const bool bExistingIntegrityValid =
			SPIsSessionResultIntegrityValid(*ExistingResult);
		const bool bIdempotentReplay = bExistingIntegrityValid
			&& SPIsIdempotentSessionResult(*ExistingResult, Result);

		if (!bIdempotentReplay)
		{
			UE_LOG(LogSPSaveGame, Warning,
				TEXT("SP_SPIKE_SAVE_CONFLICT session=%s existing_player=%s incoming_player=%s existing_hash=%s incoming_hash=%s existing_integrity=%d"),
				*Result.SessionId.ToString(EGuidFormats::DigitsWithHyphensLower),
				*ExistingResult->PlayerId,
				*Result.PlayerId,
				*ExistingResult->ResultHash,
				*Result.ResultHash,
				bExistingIntegrityValid ? 1 : 0);
		}

		return bIdempotentReplay;
	}

	CommittedResults.Add(Result);
	Gold = FMath::Max(0, Gold + Result.GoldDelta);
	++SessionsPlayed;
	return true;
}

bool USPSaveGame::HasCommittedSession(const FGuid& SessionId) const
{
	return SessionId.IsValid() && FindCommittedResult(SessionId) != nullptr;
}

const FSPSessionResult* USPSaveGame::FindCommittedResult(const FGuid& SessionId) const
{
	return CommittedResults.FindByPredicate(
		[&SessionId](const FSPSessionResult& Result)
		{
			return Result.SessionId == SessionId;
		});
}
