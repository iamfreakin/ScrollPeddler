#pragma once

#include "CoreMinimal.h"
#include "Core/SPTypes.h"
#include "UObject/PrimaryAssetId.h"
#include "SPScrollUseTypes.generated.h"

/** Stable vertical-slice gameplay families. Content identity remains the StableId. */
UENUM(BlueprintType)
enum class ESPScrollBaseFamily : uint8
{
	Ruin,
	Restoration,
	Ward,
	Traversal,
	Revelation,
	Resonance
};

/** Atomic commands emitted by the pure scroll resolver for authority code to apply. */
UENUM(BlueprintType)
enum class ESPResolvedScrollEffectKind : uint8
{
	Damage,
	Healing,
	Protection,
	Movement,
	Detection,
	Noise
};

UENUM(BlueprintType)
enum class ESPScrollEffectTarget : uint8
{
	Self,
	AimPoint,
	AlliesInRadius,
	EnemiesInRadius,
	Area
};

UENUM(BlueprintType)
enum class ESPScrollUseResultCode : uint8
{
	Accepted,
	NotAuthority,
	InvalidRequest,
	InvalidScroll,
	InvalidDefinition,
	DefinitionMismatch,
	EngravingMismatch
};

UENUM(BlueprintType)
enum class ESPScrollMalfunctionOutcome : uint8
{
	None,
	Fizzle,
	Delay,
	DirectionShift,
	ExtraNoise
};

/** One data-authored effect before quality and engraving modifiers are applied. */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPScrollEffectSpec
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ESPResolvedScrollEffectKind Kind = ESPResolvedScrollEffectKind::Damage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ESPScrollEffectTarget Target = ESPScrollEffectTarget::AimPoint;

	/**
	 * Damage, healing, protection and movement use a positive amount.
	 * Noise accepts either sign: positive emits noise, negative suppresses it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float Magnitude = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float Radius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float DurationSeconds = 0.0f;

	bool IsValid() const;
};

/**
 * Runtime snapshot of a data-authored base family. It deliberately contains no
 * quality, contamination, engraving or malfunction state.
 */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPScrollFamilyTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FPrimaryAssetId BaseDefinitionId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName StableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ESPScrollBaseFamily Family = ESPScrollBaseFamily::Ruin;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FSPScrollEffectSpec> BaseEffects;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BaseMalfunctionChance = 0.02f;

	/** Chance added linearly between contamination 0 and 100. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ContaminationChanceAtMaximum = 0.48f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float MalfunctionDelaySeconds = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float DirectionShiftDegrees = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float MalfunctionNoiseMagnitude = 1.0f;

	bool IsValid() const;
};

/** Runtime snapshot of the separately-authored engraving axis. */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPScrollEngravingUseTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FPrimaryAssetId EngravingDefinitionId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName StableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.1"))
	float MagnitudeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.1"))
	float RadiusMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.1"))
	float DurationMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float MalfunctionChanceDelta = 0.0f;

	/** An engraving tradeoff may emit noise even when the base family does not. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0"))
	float AddedNoiseMagnitude = 0.0f;

	bool IsValid() const;
};

/** The only untrusted fields a client needs to send for a scroll-use intent. */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPScrollUseRequest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGuid RequestId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGuid ScrollInstanceId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FVector AimDirection = FVector::ForwardVector;
};

/**
 * Trusted C++ state assembled after authority looks up ScrollInstanceId in its
 * inventory. This is intentionally not a reflected/RPC-serializable USTRUCT.
 */
struct SCROLLPEDDLER_API FSPAuthoritativeScrollUseState
{
	FSPScrollInstance Scroll;
	int32 RunSeed = 0;
	bool bServerAuthority = false;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPResolvedScrollEffect
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPResolvedScrollEffectKind Kind = ESPResolvedScrollEffectKind::Damage;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPScrollEffectTarget Target = ESPScrollEffectTarget::AimPoint;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float Magnitude = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float Radius = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float DurationSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector Direction = FVector::ForwardVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bGeneratedByMalfunction = false;
};

USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPScrollUseResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPScrollUseResultCode Code = ESPScrollUseResultCode::InvalidRequest;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGuid RequestId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FGuid ScrollInstanceId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float MalfunctionChance = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float MalfunctionRoll = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPScrollMalfunctionOutcome Malfunction = ESPScrollMalfunctionOutcome::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float ApplicationDelaySeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FSPResolvedScrollEffect> Effects;

	/** Authority must consume the exact instance once before applying Effects. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bShouldConsumeScroll = false;

	bool IsAccepted() const { return Code == ESPScrollUseResultCode::Accepted; }
};
