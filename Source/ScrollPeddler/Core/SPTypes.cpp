#include "Core/SPTypes.h"

#include "Containers/StringConv.h"
#include "Misc/SecureHash.h"

float SPGetQualityMultiplier(const ESPScrollQuality Quality)
{
	switch (Quality)
	{
	case ESPScrollQuality::D:
		return 0.70f;
	case ESPScrollQuality::C:
		return 0.85f;
	case ESPScrollQuality::B:
		return 1.00f;
	case ESPScrollQuality::A:
		return 1.20f;
	case ESPScrollQuality::S:
		return 1.45f;
	default:
		return 1.00f;
	}
}

FString SPBuildSessionResultHash(const FSPSessionResult& Result)
{
	const FString Canonical = FString::Printf(
		TEXT("%s|%s|%s|%d|%d|%d|%d|%d|%d|%lld"),
		*Result.SessionId.ToString(EGuidFormats::DigitsWithHyphensLower),
		*Result.PlayerId,
		*Result.BuildVersion,
		Result.PartySize,
		Result.bExtracted ? 1 : 0,
		Result.PickedUpCount,
		Result.ConsumedScrollCount,
		Result.ExtractedScrollCount,
		Result.GoldDelta,
		Result.CompletedAtUnixSeconds);

	const auto CanonicalUtf8 = StringCast<UTF8CHAR>(*Canonical, Canonical.Len());

	// Integrity checksum only: MD5 detects mismatched local records, but does not
	// provide authenticity or meaningful protection against deliberate tampering.
	return FMD5::HashBytes(
		reinterpret_cast<const uint8*>(CanonicalUtf8.Get()),
		static_cast<uint64>(CanonicalUtf8.Length()));
}

bool SPIsSessionResultIntegrityValid(const FSPSessionResult& Result)
{
	return Result.IsValid()
		&& Result.ResultHash.Equals(
			SPBuildSessionResultHash(Result), ESearchCase::CaseSensitive);
}

bool SPIsIdempotentSessionResult(
	const FSPSessionResult& ExistingResult,
	const FSPSessionResult& IncomingResult)
{
	return ExistingResult.SessionId == IncomingResult.SessionId
		&& ExistingResult.PlayerId.Equals(
			IncomingResult.PlayerId, ESearchCase::CaseSensitive)
		&& ExistingResult.ResultHash.Equals(
			IncomingResult.ResultHash, ESearchCase::CaseSensitive);
}
