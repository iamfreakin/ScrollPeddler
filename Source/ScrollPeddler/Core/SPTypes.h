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

/** 권위 서버의 월드 픽업 판정을 owning client에 안전하게 전달하는 결과다. */
UENUM(BlueprintType)
enum class ESPPickupResultCode : uint8
{
	Success,
	InvalidRequest,
	InactivePlayer,
	OutOfRange,
	InventoryFull,
	Unavailable,
	Obstructed,
	Contested,
	ServerError
};

/** 권위 서버의 스크롤 사용 판정을 owning client에 안전하게 전달하는 결과다. */
UENUM(BlueprintType)
enum class ESPScrollUseResultCode : uint8
{
	Success,
	InvalidRequest,
	InactivePlayer,
	NotOwned,
	InvalidDefinition,
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
	 * 결정적인 로컬 무결성 체크섬일 뿐이다. 진위 증명, 변조 방지 서명,
	 * 서버 검증의 대체 수단으로 사용하지 않는다.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, SaveGame)
	FString ResultHash;

	bool IsValid() const
	{
		return SessionId.IsValid() && !PlayerId.IsEmpty() && !ResultHash.IsEmpty();
	}
};

SCROLLPEDDLER_API float SPGetQualityMultiplier(ESPScrollQuality Quality);

/** 정규화된 UTF-8 결과 바이트로 MD5 기반 무결성 체크섬을 만든다. */
SCROLLPEDDLER_API FString SPBuildSessionResultHash(const FSPSessionResult& Result);

/** 결과 형식과 로컬 무결성 체크섬을 검증한다. */
SCROLLPEDDLER_API bool SPIsSessionResultIntegrityValid(const FSPSessionResult& Result);

/**
 * 두 레코드가 정확히 같은 확정 결과를 가리킬 때만 true를 반환한다.
 * PlayerId와 ResultHash는 의도적으로 대소문자를 구분한다.
 */
SCROLLPEDDLER_API bool SPIsIdempotentSessionResult(
	const FSPSessionResult& ExistingResult,
	const FSPSessionResult& IncomingResult);
