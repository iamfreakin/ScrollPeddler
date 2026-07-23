#include "UI/SPHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "GameFramework/PlayerController.h"
#include "Player/SPCharacter.h"

namespace
{
	const FLinearColor FocusColor(0.05f, 0.90f, 1.00f, 1.00f);
	const FLinearColor PendingColor(1.00f, 0.82f, 0.05f, 1.00f);
	const FLinearColor SuccessColor(0.15f, 1.00f, 0.25f, 1.00f);
	const FLinearColor RejectionColor(1.00f, 0.15f, 0.10f, 1.00f);
}

void ASPHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}

	const FVector2D Center(Canvas->ClipX * 0.5f, Canvas->ClipY * 0.5f);
	FLinearColor CrosshairColor = FLinearColor::White;
	FString Prompt;
	FLinearColor PromptColor = FLinearColor::White;

	const APlayerController* PlayerController = GetOwningPlayerController();
	const ASPCharacter* Character = PlayerController
		? Cast<ASPCharacter>(PlayerController->GetPawn())
		: nullptr;
	if (Character)
	{
		if (Character->IsPickupRequestPending())
		{
			CrosshairColor = PendingColor;
			PromptColor = PendingColor;
			Prompt = TEXT("PICKING UP...");
		}
		else if (Character->HasActivePickupFeedback())
		{
			if (Character->IsNoTargetPickupFeedback())
			{
				CrosshairColor = RejectionColor;
				PromptColor = RejectionColor;
				Prompt = TEXT("NO TARGET");
			}
			else if (Character->IsPickupFeedbackTimedOut())
			{
				CrosshairColor = RejectionColor;
				PromptColor = RejectionColor;
				Prompt = TEXT("NO RESPONSE");
			}
			else
			{
				const ESPPickupResultCode ResultCode = Character->GetLastPickupResult();
				CrosshairColor = GetPickupResultColor(ResultCode);
				PromptColor = CrosshairColor;
				Prompt = GetPickupResultMessage(ResultCode);
			}
		}
		else if (Character->FindPickupInView())
		{
			CrosshairColor = FocusColor;
			PromptColor = FocusColor;
			Prompt = TEXT("[E] PICK UP");
		}
	}

	DrawCrosshair(Center, CrosshairColor);
	if (!Prompt.IsEmpty())
	{
		DrawCenteredPrompt(Prompt, PromptColor, Center);
	}
}

FString ASPHUD::GetPickupResultMessage(const ESPPickupResultCode ResultCode)
{
	switch (ResultCode)
	{
	case ESPPickupResultCode::Success:
		return TEXT("PICKED UP");
	case ESPPickupResultCode::InvalidRequest:
		return TEXT("INVALID REQUEST");
	case ESPPickupResultCode::OutOfRange:
		return TEXT("TOO FAR");
	case ESPPickupResultCode::InventoryFull:
		return TEXT("INVENTORY FULL");
	case ESPPickupResultCode::Unavailable:
		return TEXT("UNAVAILABLE");
	case ESPPickupResultCode::Obstructed:
		return TEXT("BLOCKED");
	case ESPPickupResultCode::Contested:
		return TEXT("ALREADY CLAIMED");
	case ESPPickupResultCode::ServerError:
	default:
		return TEXT("SERVER ERROR");
	}
}

FLinearColor ASPHUD::GetPickupResultColor(const ESPPickupResultCode ResultCode)
{
	return ResultCode == ESPPickupResultCode::Success ? SuccessColor : RejectionColor;
}

void ASPHUD::DrawCrosshair(const FVector2D& Center, const FLinearColor& Color) const
{
	constexpr float InnerRadius = 3.0f;
	constexpr float OuterRadius = 10.0f;
	constexpr float Thickness = 2.0f;
	Canvas->K2_DrawLine(
		Center + FVector2D(-OuterRadius, 0.0f),
		Center + FVector2D(-InnerRadius, 0.0f), Thickness, Color);
	Canvas->K2_DrawLine(
		Center + FVector2D(InnerRadius, 0.0f),
		Center + FVector2D(OuterRadius, 0.0f), Thickness, Color);
	Canvas->K2_DrawLine(
		Center + FVector2D(0.0f, -OuterRadius),
		Center + FVector2D(0.0f, -InnerRadius), Thickness, Color);
	Canvas->K2_DrawLine(
		Center + FVector2D(0.0f, InnerRadius),
		Center + FVector2D(0.0f, OuterRadius), Thickness, Color);
}

void ASPHUD::DrawCenteredPrompt(
	const FString& Prompt,
	const FLinearColor& Color,
	const FVector2D& Center) const
{
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	Canvas->K2_DrawText(
		Font,
		Prompt,
		Center + FVector2D(0.0f, 20.0f),
		FVector2D::UnitVector,
		Color,
		0.0f,
		FLinearColor::Black,
		FVector2D(1.0f, 1.0f),
		true,
		false,
		true,
		FLinearColor::Black);
}
