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

	/** 각 서버 픽업 결과에 대응하는 안정적인 font-safe HUD 문구를 반환한다. */
	static FString GetPickupResultMessage(ESPPickupResultCode ResultCode);
	static FLinearColor GetPickupResultColor(ESPPickupResultCode ResultCode);
	/** 각 서버 소비 결과에 대응하는 HUD 문구와 색상을 반환한다. */
	static FString GetScrollUseResultMessage(ESPScrollUseResultCode ResultCode);
	static FLinearColor GetScrollUseResultColor(ESPScrollUseResultCode ResultCode);

private:
	void DrawCrosshair(const FVector2D& Center, const FLinearColor& Color) const;
	void DrawCenteredPrompt(const FString& Prompt, const FLinearColor& Color, const FVector2D& Center) const;
};
