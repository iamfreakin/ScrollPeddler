#pragma once

#include "CoreMinimal.h"
#include "UObject/PrimaryAssetId.h"
#include "SPTypes.generated.h"

UENUM(BlueprintType)
enum class ESPScrollQuality : uint8
{
	D UMETA(DisplayName = "D - Crude"),
	C UMETA(DisplayName = "C - Common"),
	B UMETA(DisplayName = "B - Sound"),
	A UMETA(DisplayName = "A - Refined"),
	S UMETA(DisplayName = "S - Masterwork")
};

UENUM(BlueprintType)
enum class ESPMisfireType : uint8
{
	None,
	Delay,
	DirectionShift,
	ExtraNoise
};

UENUM(BlueprintType)
enum class ESPScrollEffectKind : uint8
{
	Silence
};

UENUM(BlueprintType)
enum class ESPSessionPhase : uint8
{
	LobbyCreated,
	PlayersJoining,
	InExpedition,
	Extraction,
	SettlementPending,
	SettlementCommitted
};

/** Owning-client-safe outcome of an authoritative world-pickup request. */
UENUM(BlueprintType)
enum class ESPPickupResultCode : uint8
{
	Success,
	InvalidRequest,
	OutOfRange,
	InventoryFull,
	Unavailable,
	Obstructed,
	Contested,
	ServerError
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPScrollInstance
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FGuid InstanceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FPrimaryAssetId BaseDefinitionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	FPrimaryAssetId EngravingDefinitionId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	ESPScrollQuality Quality = ESPScrollQuality::B;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float Contamination = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
	ESPMisfireType Misfire = ESPMisfireType::None;

	bool IsValid() const
	{
		return InstanceId.IsValid() && BaseDefinitionId.IsValid() && EngravingDefinitionId.IsValid();
	}
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPSessionResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FGuid SessionId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FString PlayerId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FString BuildVersion = TEXT("0.1.0-tech-spike");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	int32 PartySize = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	bool bExtracted = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	int32 PickedUpCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	int32 ConsumedScrollCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	int32 ExtractedScrollCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	int32 GoldDelta = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	int64 CompletedAtUnixSeconds = 0;

	/**
	 * Deterministic local integrity checksum only. This is not an authenticity
	 * proof, anti-tamper signature, or substitute for server-side validation.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FString ResultHash;

	bool IsValid() const
	{
		return SessionId.IsValid() && !PlayerId.IsEmpty() && !ResultHash.IsEmpty();
	}
};

SCROLLPEDDLER_API float SPGetQualityMultiplier(ESPScrollQuality Quality);

/** Builds an MD5-based integrity checksum over the canonical UTF-8 result bytes. */
SCROLLPEDDLER_API FString SPBuildSessionResultHash(const FSPSessionResult& Result);

/** Validates the result shape and its local integrity checksum. */
SCROLLPEDDLER_API bool SPIsSessionResultIntegrityValid(const FSPSessionResult& Result);

/**
 * Returns true only when two records identify the same exact committed result.
 * PlayerId and ResultHash comparisons are deliberately case-sensitive.
 */
SCROLLPEDDLER_API bool SPIsIdempotentSessionResult(
	const FSPSessionResult& ExistingResult,
	const FSPSessionResult& IncomingResult);
