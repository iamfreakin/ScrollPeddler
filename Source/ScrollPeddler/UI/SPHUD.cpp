#include "UI/SPHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "Game/SPGameState.h"
#include "Game/SPPartyState.h"
#include "Game/SPPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Player/SPCharacter.h"
#include "Player/SPInventoryComponent.h"

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
		else if (Character->HasPickupTargetInView())
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
	if (Character)
	{
		DrawRunAndInventoryStatus(Character, PlayerController);
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

void ASPHUD::DrawRunAndInventoryStatus(
	const ASPCharacter* Character,
	const APlayerController* PlayerController) const
{
	if (!Canvas || !Character || !PlayerController)
	{
		return;
	}

	const ASPGameState* ScrollGameState =
		PlayerController->GetWorld()
		? PlayerController->GetWorld()->GetGameState<ASPGameState>()
		: nullptr;
	const ASPPlayerState* ScrollPlayerState =
		Character->GetPlayerState<ASPPlayerState>();
	float LeftY = 24.0f;
	if (ScrollGameState)
	{
		const int32 RemainingSeconds = FMath::CeilToInt(
			ScrollGameState->GetSecondsUntilResolution());
		DrawHudLine(
			FString::Printf(
				TEXT("RUN %s  %02d:%02d"),
				*StaticEnum<ESPRunPhase>()->GetNameStringByValue(
					static_cast<int64>(
						ScrollGameState->GetRunPhase())),
				RemainingSeconds / 60,
				RemainingSeconds % 60),
			24.0f,
			LeftY,
			FLinearColor(0.75f, 0.9f, 1.0f));
		LeftY += 18.0f;
	}

	const FString ConditionName = ScrollPlayerState
		? StaticEnum<ESPPlayerCondition>()->GetNameStringByValue(
			static_cast<int64>(
				ScrollPlayerState->GetPlayerCondition()))
		: TEXT("Unknown");
	DrawHudLine(
		FString::Printf(
			TEXT("STATE %s  STAMINA %.1f/6.0"),
			*ConditionName,
			Character->GetStaminaSeconds()),
		24.0f,
		LeftY);
	LeftY += 18.0f;

	TArray<FString> ActiveEffects;
	if (Character->IsSilenced())
	{
		ActiveEffects.Add(TEXT("SILENCE"));
	}
	if (Character->IsProtectedByScroll())
	{
		ActiveEffects.Add(TEXT("WARD"));
	}
	if (Character->IsRevelationActive())
	{
		ActiveEffects.Add(TEXT("REVELATION"));
	}
	if (Character->IsCarryingLargeCargo())
	{
		ActiveEffects.Add(TEXT("HEAVY CARGO"));
	}
	if (!ActiveEffects.IsEmpty())
	{
		DrawHudLine(
			FString::Printf(
				TEXT("EFFECTS %s"),
				*FString::Join(ActiveEffects, TEXT(" | "))),
			24.0f,
			LeftY,
			PendingColor);
		LeftY += 18.0f;
	}

	const FSPInventoryState& InventoryState =
		Character->GetInventory().GetInventoryState();
	const FString HandText = InventoryState.HandSlot.bOccupied
		? FString::Printf(
			TEXT("HAND %s x%d"),
			*InventoryState.HandSlot.Item.DefinitionId.PrimaryAssetName.ToString(),
			InventoryState.HandSlot.Item.Quantity)
		: TEXT("HAND EMPTY");
	DrawHudLine(HandText, 24.0f, LeftY);
	LeftY += 18.0f;
	for (int32 BagIndex = 0;
		BagIndex < InventoryState.BagSlots.Num();
		++BagIndex)
	{
		const FSPInventorySlot& Slot =
			InventoryState.BagSlots[BagIndex];
		DrawHudLine(
			Slot.bOccupied
				? FString::Printf(
					TEXT("%d: %s x%d"),
					BagIndex + 1,
					*Slot.Item.DefinitionId.PrimaryAssetName.ToString(),
					Slot.Item.Quantity)
				: FString::Printf(TEXT("%d: EMPTY"), BagIndex + 1),
			24.0f,
			LeftY,
			FLinearColor(0.85f, 0.85f, 0.85f));
		LeftY += 16.0f;
	}

	const UWorld* World = PlayerController->GetWorld();
	const ASPPartyState* PartyState = nullptr;
	if (World)
	{
		TActorIterator<ASPPartyState> Iterator(World);
		if (Iterator)
		{
			PartyState = *Iterator;
		}
	}
	if (!PartyState)
	{
		return;
	}

	float RightY = 24.0f;
	const float RightX = FMath::Max(24.0f, Canvas->ClipX - 260.0f);
	DrawHudLine(TEXT("PARTY"), RightX, RightY, FocusColor);
	RightY += 18.0f;
	for (const FSPPartyMemberState& Member
		: PartyState->GetGovernanceState().Members)
	{
		DrawHudLine(
			FString::Printf(
				TEXT("%s%s  %s%s"),
				Member.bIsHost ? TEXT("*") : TEXT(""),
				*Member.DisplayName,
				Member.bReady ? TEXT("READY") : TEXT("WAIT"),
				Member.bConnected ? TEXT("") : TEXT(" (OFFLINE)")),
			RightX,
			RightY,
			Member.bReady ? SuccessColor : FLinearColor::White);
		RightY += 16.0f;
	}

	const TArray<FSPPartyChatMessage>& Messages =
		PartyState->GetGovernanceState().ChatMessages;
	float ChatY = Canvas->ClipY - 100.0f;
	const int32 FirstMessageIndex = FMath::Max(0, Messages.Num() - 4);
	for (int32 MessageIndex = FirstMessageIndex;
		MessageIndex < Messages.Num();
		++MessageIndex)
	{
		const FSPPartyChatMessage& Message = Messages[MessageIndex];
		DrawHudLine(
			FString::Printf(
				TEXT("%s: %s"),
				*Message.SenderMemberId,
				*Message.Message),
			24.0f,
			ChatY,
			FLinearColor(0.8f, 0.9f, 1.0f));
		ChatY += 16.0f;
	}
}

void ASPHUD::DrawHudLine(
	const FString& Text,
	const float X,
	const float Y,
	const FLinearColor& Color) const
{
	UFont* Font = GEngine ? GEngine->GetSmallFont() : nullptr;
	Canvas->K2_DrawText(
		Font,
		Text,
		FVector2D(X, Y),
		FVector2D::UnitVector,
		Color,
		0.0f,
		FLinearColor::Black,
		FVector2D::UnitVector,
		false,
		false,
		true,
		FLinearColor::Black);
}
