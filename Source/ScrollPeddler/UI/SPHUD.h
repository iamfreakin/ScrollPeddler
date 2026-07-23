#pragma once

#include "CoreMinimal.h"
#include "Core/SPTypes.h"
#include "GameFramework/HUD.h"
#include "SPHUD.generated.h"

UCLASS()
class SCROLLPEDDLER_API ASPHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** Stable, font-safe HUD copy for each server pickup outcome. */
	static FString GetPickupResultMessage(ESPPickupResultCode ResultCode);
	static FLinearColor GetPickupResultColor(ESPPickupResultCode ResultCode);

private:
	void DrawCrosshair(const FVector2D& Center, const FLinearColor& Color) const;
	void DrawCenteredPrompt(const FString& Prompt, const FLinearColor& Color, const FVector2D& Center) const;
};
