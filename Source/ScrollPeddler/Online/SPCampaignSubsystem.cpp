#include "Online/SPCampaignSubsystem.h"

#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Persistence/SPPlayerProfileSaveGame.h"

DEFINE_LOG_CATEGORY_STATIC(LogSPCampaignPersistence, Log, All);

namespace SPCampaignPersistence
{
	const FString CampaignSlotPrefix(TEXT("ScrollPeddler_HostCampaign_"));
	const FString PlayerProfileSlot(TEXT("ScrollPeddler_PlayerProfile"));

	FString GetSaveDirectory()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"));
	}

	FString GetFilePathForSlot(const FString& SlotName)
	{
		return FPaths::Combine(GetSaveDirectory(), SlotName + TEXT(".sav"));
	}

	ESPCampaignPersistenceResult SerializeSave(
		USaveGame* SaveGame,
		TArray<uint8>& OutBytes)
	{
		OutBytes.Reset();
		return SaveGame && UGameplayStatics::SaveGameToMemory(SaveGame, OutBytes)
			? ESPCampaignPersistenceResult::Success
			: ESPCampaignPersistenceResult::SerializeFailed;
	}

	template <typename TSaveGame>
	TSaveGame* LoadTypedSaveFromBytes(const TArray<uint8>& Bytes)
	{
		return Cast<TSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	}

	template <typename TSaveGame, typename TValidator>
	ESPCampaignPersistenceResult PersistAtomically(
		TSaveGame* SaveGame,
		const FString& FinalPath,
		TValidator&& Validator,
		TSaveGame*& OutVerifiedSave)
	{
		OutVerifiedSave = nullptr;

		TArray<uint8> SaveBytes;
		const ESPCampaignPersistenceResult SerializeResult =
			SerializeSave(SaveGame, SaveBytes);
		if (SerializeResult != ESPCampaignPersistenceResult::Success)
		{
			return SerializeResult;
		}

		TSaveGame* MemoryVerified =
			LoadTypedSaveFromBytes<TSaveGame>(SaveBytes);
		if (!MemoryVerified || !Validator(MemoryVerified))
		{
			return ESPCampaignPersistenceResult::VerificationFailed;
		}

		IFileManager& FileManager = IFileManager::Get();
		const FString SaveDirectory = FPaths::GetPath(FinalPath);
		if (!FileManager.MakeDirectory(*SaveDirectory, true))
		{
			return ESPCampaignPersistenceResult::WriteFailed;
		}

		const FString TempPath = FinalPath + TEXT(".tmp");
		if (FileManager.FileExists(*TempPath)
			&& !FileManager.Delete(*TempPath, false, true, true))
		{
			return ESPCampaignPersistenceResult::WriteFailed;
		}

		if (!FFileHelper::SaveArrayToFile(SaveBytes, *TempPath))
		{
			FileManager.Delete(*TempPath, false, true, true);
			return ESPCampaignPersistenceResult::WriteFailed;
		}

		TArray<uint8> TempBytes;
		TSaveGame* TempVerified = nullptr;
		if (!FFileHelper::LoadFileToArray(TempBytes, *TempPath)
			|| TempBytes != SaveBytes)
		{
			FileManager.Delete(*TempPath, false, true, true);
			return ESPCampaignPersistenceResult::VerificationFailed;
		}
		TempVerified = LoadTypedSaveFromBytes<TSaveGame>(TempBytes);
		if (!TempVerified || !Validator(TempVerified))
		{
			FileManager.Delete(*TempPath, false, true, true);
			return ESPCampaignPersistenceResult::VerificationFailed;
		}

		// Replace only after the temporary file has passed byte and object checks.
		// No .bak is created or retained by design.
		if (!FileManager.Move(*FinalPath, *TempPath, true, true, false, true))
		{
			FileManager.Delete(*TempPath, false, true, true);
			return ESPCampaignPersistenceResult::WriteFailed;
		}

		TArray<uint8> FinalBytes;
		if (!FFileHelper::LoadFileToArray(FinalBytes, *FinalPath)
			|| FinalBytes != SaveBytes)
		{
			return ESPCampaignPersistenceResult::VerificationFailed;
		}

		TSaveGame* FinalVerified =
			LoadTypedSaveFromBytes<TSaveGame>(FinalBytes);
		if (!FinalVerified || !Validator(FinalVerified))
		{
			return ESPCampaignPersistenceResult::VerificationFailed;
		}

		OutVerifiedSave = FinalVerified;
		return ESPCampaignPersistenceResult::Success;
	}

	template <typename TSaveGame>
	ESPCampaignPersistenceResult ReadTypedSave(
		const FString& Path,
		TSaveGame*& OutSave)
	{
		OutSave = nullptr;
		if (!IFileManager::Get().FileExists(*Path))
		{
			return ESPCampaignPersistenceResult::SlotEmpty;
		}

		TArray<uint8> SaveBytes;
		if (!FFileHelper::LoadFileToArray(SaveBytes, *Path))
		{
			return ESPCampaignPersistenceResult::CorruptData;
		}

		OutSave = LoadTypedSaveFromBytes<TSaveGame>(SaveBytes);
		return OutSave
			? ESPCampaignPersistenceResult::Success
			: ESPCampaignPersistenceResult::CorruptData;
	}
}

bool USPCampaignSubsystem::IsValidCampaignSlot(int32 SlotIndex)
{
	return SlotIndex >= 0 && SlotIndex < CampaignSlotCount;
}

FString USPCampaignSubsystem::GetCampaignSlotName(int32 SlotIndex)
{
	if (!IsValidCampaignSlot(SlotIndex))
	{
		return FString();
	}
	return FString::Printf(
		TEXT("%s%d"),
		*SPCampaignPersistence::CampaignSlotPrefix,
		SlotIndex);
}

FString USPCampaignSubsystem::GetPlayerProfileSlotName()
{
	return SPCampaignPersistence::PlayerProfileSlot;
}

FString USPCampaignSubsystem::GetCampaignFilePath(int32 SlotIndex)
{
	return SPCampaignPersistence::GetFilePathForSlot(GetCampaignSlotName(SlotIndex));
}

FString USPCampaignSubsystem::GetPlayerProfileFilePath()
{
	return SPCampaignPersistence::GetFilePathForSlot(GetPlayerProfileSlotName());
}

ESPCampaignPersistenceResult USPCampaignSubsystem::CreateCampaignSlot(
	int32 SlotIndex,
	const FString& OwnerId)
{
	if (!IsValidCampaignSlot(SlotIndex))
	{
		return ESPCampaignPersistenceResult::InvalidSlot;
	}
	FString NormalizedOwnerId = OwnerId;
	NormalizedOwnerId.TrimStartAndEndInline();
	if (NormalizedOwnerId.IsEmpty())
	{
		return ESPCampaignPersistenceResult::InvalidOwner;
	}

	const FString FilePath = GetCampaignFilePath(SlotIndex);
	if (IFileManager::Get().FileExists(*FilePath))
	{
		return ESPCampaignPersistenceResult::SlotOccupied;
	}

	USPCampaignSaveGame* Candidate = NewObject<USPCampaignSaveGame>(this);
	if (!Candidate->InitializeNewCampaign(NormalizedOwnerId))
	{
		return ESPCampaignPersistenceResult::InvalidOwner;
	}

	USPCampaignSaveGame* VerifiedCampaign = nullptr;
	const ESPCampaignPersistenceResult PersistResult =
		PersistCampaign(Candidate, SlotIndex, VerifiedCampaign);
	if (PersistResult == ESPCampaignPersistenceResult::Success)
	{
		CurrentCampaign = VerifiedCampaign;
		CurrentCampaignSlotIndex = SlotIndex;
	}
	return PersistResult;
}

ESPCampaignPersistenceResult USPCampaignSubsystem::LoadCampaignSlot(
	int32 SlotIndex,
	const FString& ExpectedOwnerId)
{
	if (!IsValidCampaignSlot(SlotIndex))
	{
		return ESPCampaignPersistenceResult::InvalidSlot;
	}

	FString NormalizedOwnerId = ExpectedOwnerId;
	NormalizedOwnerId.TrimStartAndEndInline();
	if (NormalizedOwnerId.IsEmpty())
	{
		return ESPCampaignPersistenceResult::InvalidOwner;
	}

	USPCampaignSaveGame* LoadedCampaign = nullptr;
	const ESPCampaignPersistenceResult ReadResult =
		SPCampaignPersistence::ReadTypedSave(
			GetCampaignFilePath(SlotIndex),
			LoadedCampaign);
	if (ReadResult != ESPCampaignPersistenceResult::Success)
	{
		return ReadResult;
	}
	if (!LoadedCampaign->IsVersionCompatible())
	{
		return ESPCampaignPersistenceResult::VersionMismatch;
	}
	if (!LoadedCampaign->IsStructurallyValid())
	{
		return ESPCampaignPersistenceResult::CorruptData;
	}
	if (LoadedCampaign->GetOwnerId() != NormalizedOwnerId)
	{
		return ESPCampaignPersistenceResult::OwnerMismatch;
	}

	CurrentCampaign = LoadedCampaign;
	CurrentCampaignSlotIndex = SlotIndex;
	return ESPCampaignPersistenceResult::Success;
}

ESPCampaignPersistenceResult USPCampaignSubsystem::SaveCurrentCampaign()
{
	if (!CurrentCampaign || !IsValidCampaignSlot(CurrentCampaignSlotIndex))
	{
		return ESPCampaignPersistenceResult::NoCampaignLoaded;
	}

	USPCampaignSaveGame* VerifiedCampaign = nullptr;
	const ESPCampaignPersistenceResult PersistResult = PersistCampaign(
		CurrentCampaign,
		CurrentCampaignSlotIndex,
		VerifiedCampaign);
	if (PersistResult == ESPCampaignPersistenceResult::Success)
	{
		CurrentCampaign = VerifiedCampaign;
	}
	return PersistResult;
}

ESPCampaignPersistenceResult USPCampaignSubsystem::CommitCampaignTransaction(
	const FSPCampaignTransaction& Transaction,
	ESPCampaignApplyResult& OutApplyResult)
{
	OutApplyResult = ESPCampaignApplyResult::InvalidCampaign;
	if (!CurrentCampaign || !IsValidCampaignSlot(CurrentCampaignSlotIndex))
	{
		return ESPCampaignPersistenceResult::NoCampaignLoaded;
	}

	TArray<uint8> RollbackBytes;
	if (SPCampaignPersistence::SerializeSave(CurrentCampaign, RollbackBytes)
		!= ESPCampaignPersistenceResult::Success)
	{
		return ESPCampaignPersistenceResult::SerializeFailed;
	}

	OutApplyResult = CurrentCampaign->ApplyTransaction(Transaction);
	if (OutApplyResult == ESPCampaignApplyResult::AlreadyProcessed)
	{
		return ESPCampaignPersistenceResult::Success;
	}
	if (OutApplyResult != ESPCampaignApplyResult::Applied)
	{
		return ESPCampaignPersistenceResult::ApplyRejected;
	}

	USPCampaignSaveGame* VerifiedCampaign = nullptr;
	const ESPCampaignPersistenceResult PersistResult = PersistCampaign(
		CurrentCampaign,
		CurrentCampaignSlotIndex,
		VerifiedCampaign);
	if (PersistResult == ESPCampaignPersistenceResult::Success)
	{
		CurrentCampaign = VerifiedCampaign;
		return ESPCampaignPersistenceResult::Success;
	}

	USPCampaignSaveGame* RestoredCampaign =
		SPCampaignPersistence::LoadTypedSaveFromBytes<USPCampaignSaveGame>(
			RollbackBytes);
	if (!RestoredCampaign || !RestoredCampaign->IsStructurallyValid())
	{
		UE_LOG(LogSPCampaignPersistence, Error,
			TEXT("SP_CAMPAIGN_ROLLBACK_FAILED slot=%d transaction=%s"),
			CurrentCampaignSlotIndex,
			*Transaction.TransactionId.ToString(EGuidFormats::DigitsWithHyphensLower));
		return ESPCampaignPersistenceResult::RollbackFailed;
	}

	CurrentCampaign = RestoredCampaign;
	UE_LOG(LogSPCampaignPersistence, Error,
		TEXT("SP_CAMPAIGN_COMMIT_ROLLED_BACK slot=%d transaction=%s persistence_result=%s"),
		CurrentCampaignSlotIndex,
		*Transaction.TransactionId.ToString(EGuidFormats::DigitsWithHyphensLower),
		*StaticEnum<ESPCampaignPersistenceResult>()->GetNameStringByValue(
			static_cast<int64>(PersistResult)));
	return PersistResult;
}

ESPCampaignPersistenceResult USPCampaignSubsystem::CreatePlayerProfile(
	const FString& PlayerId)
{
	FString NormalizedPlayerId = PlayerId;
	NormalizedPlayerId.TrimStartAndEndInline();
	if (NormalizedPlayerId.IsEmpty())
	{
		return ESPCampaignPersistenceResult::InvalidOwner;
	}

	const FString FilePath = GetPlayerProfileFilePath();
	if (IFileManager::Get().FileExists(*FilePath))
	{
		return ESPCampaignPersistenceResult::SlotOccupied;
	}

	USPPlayerProfileSaveGame* Candidate =
		NewObject<USPPlayerProfileSaveGame>(this);
	if (!Candidate->InitializeNewProfile(NormalizedPlayerId))
	{
		return ESPCampaignPersistenceResult::InvalidOwner;
	}

	USPPlayerProfileSaveGame* VerifiedProfile = nullptr;
	const ESPCampaignPersistenceResult PersistResult =
		PersistPlayerProfile(Candidate, VerifiedProfile);
	if (PersistResult == ESPCampaignPersistenceResult::Success)
	{
		CurrentPlayerProfile = VerifiedProfile;
	}
	return PersistResult;
}

ESPCampaignPersistenceResult USPCampaignSubsystem::LoadPlayerProfile(
	const FString& ExpectedPlayerId)
{
	FString NormalizedPlayerId = ExpectedPlayerId;
	NormalizedPlayerId.TrimStartAndEndInline();
	if (NormalizedPlayerId.IsEmpty())
	{
		return ESPCampaignPersistenceResult::InvalidOwner;
	}

	USPPlayerProfileSaveGame* LoadedProfile = nullptr;
	const ESPCampaignPersistenceResult ReadResult =
		SPCampaignPersistence::ReadTypedSave(
			GetPlayerProfileFilePath(),
			LoadedProfile);
	if (ReadResult != ESPCampaignPersistenceResult::Success)
	{
		return ReadResult;
	}
	if (!LoadedProfile->IsVersionCompatible())
	{
		return ESPCampaignPersistenceResult::VersionMismatch;
	}
	if (!LoadedProfile->IsStructurallyValid())
	{
		return ESPCampaignPersistenceResult::CorruptData;
	}
	if (LoadedProfile->GetPlayerId() != NormalizedPlayerId)
	{
		return ESPCampaignPersistenceResult::OwnerMismatch;
	}

	CurrentPlayerProfile = LoadedProfile;
	return ESPCampaignPersistenceResult::Success;
}

ESPCampaignPersistenceResult USPCampaignSubsystem::SaveCurrentPlayerProfile()
{
	if (!CurrentPlayerProfile)
	{
		return ESPCampaignPersistenceResult::NoProfileLoaded;
	}

	USPPlayerProfileSaveGame* VerifiedProfile = nullptr;
	const ESPCampaignPersistenceResult PersistResult =
		PersistPlayerProfile(CurrentPlayerProfile, VerifiedProfile);
	if (PersistResult == ESPCampaignPersistenceResult::Success)
	{
		CurrentPlayerProfile = VerifiedProfile;
	}
	return PersistResult;
}

ESPCampaignPersistenceResult USPCampaignSubsystem::PersistCampaign(
	USPCampaignSaveGame* Campaign,
	int32 SlotIndex,
	USPCampaignSaveGame*& OutVerifiedCampaign)
{
	OutVerifiedCampaign = nullptr;
	if (!Campaign || !Campaign->IsStructurallyValid())
	{
		return ESPCampaignPersistenceResult::CorruptData;
	}
	if (!IsValidCampaignSlot(SlotIndex))
	{
		return ESPCampaignPersistenceResult::InvalidSlot;
	}

#if WITH_DEV_AUTOMATION_TESTS
	if (bFailNextCampaignPersistence)
	{
		bFailNextCampaignPersistence = false;
		return ESPCampaignPersistenceResult::WriteFailed;
	}
#endif

	const FGuid ExpectedCampaignId = Campaign->GetCampaignId();
	const FString ExpectedOwnerId = Campaign->GetOwnerId();
	const int64 ExpectedRevision = Campaign->GetRevision();
	return SPCampaignPersistence::PersistAtomically(
		Campaign,
		GetCampaignFilePath(SlotIndex),
		[&ExpectedCampaignId, &ExpectedOwnerId, ExpectedRevision](
			const USPCampaignSaveGame* Loaded)
		{
			return Loaded
				&& Loaded->IsStructurallyValid()
				&& Loaded->GetCampaignId() == ExpectedCampaignId
				&& Loaded->GetOwnerId() == ExpectedOwnerId
				&& Loaded->GetRevision() == ExpectedRevision;
		},
		OutVerifiedCampaign);
}

ESPCampaignPersistenceResult USPCampaignSubsystem::PersistPlayerProfile(
	USPPlayerProfileSaveGame* Profile,
	USPPlayerProfileSaveGame*& OutVerifiedProfile)
{
	OutVerifiedProfile = nullptr;
	if (!Profile || !Profile->IsStructurallyValid())
	{
		return ESPCampaignPersistenceResult::CorruptData;
	}

	const FString ExpectedPlayerId = Profile->GetPlayerId();
	const int64 ExpectedRevision = Profile->GetRevision();
	return SPCampaignPersistence::PersistAtomically(
		Profile,
		GetPlayerProfileFilePath(),
		[&ExpectedPlayerId, ExpectedRevision](
			const USPPlayerProfileSaveGame* Loaded)
		{
			return Loaded
				&& Loaded->IsStructurallyValid()
				&& Loaded->GetPlayerId() == ExpectedPlayerId
				&& Loaded->GetRevision() == ExpectedRevision;
		},
		OutVerifiedProfile);
}

#if WITH_DEV_AUTOMATION_TESTS
void USPCampaignSubsystem::AdoptCampaignForTesting(
	const USPCampaignSaveGame* Campaign,
	int32 SlotIndex)
{
	CurrentCampaign = Campaign
		? DuplicateObject<USPCampaignSaveGame>(Campaign, this)
		: nullptr;
	CurrentCampaignSlotIndex = IsValidCampaignSlot(SlotIndex)
		? SlotIndex
		: INDEX_NONE;
}

void USPCampaignSubsystem::FailNextCampaignPersistenceForTesting()
{
	bFailNextCampaignPersistence = true;
}
#endif
