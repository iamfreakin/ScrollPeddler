// Fill out your copyright notice in the Description page of Project Settings.


#include "ScrollPeddlerGameModeBase.h"

#include "Framework/ScrollCharacter.h"
#include "Framework/ScrollGameState.h"
#include "Framework/ScrollPlayerController.h"
#include "Framework/ScrollPlayerState.h"
#include "ScrollLog.h"

AScrollPeddlerGameModeBase::AScrollPeddlerGameModeBase()
{
	DefaultPawnClass = AScrollCharacter::StaticClass();
	PlayerControllerClass = AScrollPlayerController::StaticClass();
	PlayerStateClass = AScrollPlayerState::StaticClass();
	GameStateClass = AScrollGameState::StaticClass();
}

FString AScrollPeddlerGameModeBase::InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	const FString ErrorMessage = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);

	if (ErrorMessage.IsEmpty())
	{
		if (AScrollPlayerState* ScrollPlayerState = NewPlayerController->GetPlayerState<AScrollPlayerState>())
		{
			ScrollPlayerState->SetParticipantSessionId(NextParticipantSessionId++);
			UE_LOG(LogScroll, Log, TEXT("Participant %d joined (%s, local=%d)"),
				ScrollPlayerState->GetParticipantSessionId(),
				*NewPlayerController->GetName(),
				NewPlayerController->IsLocalController() ? 1 : 0);
		}
	}

	return ErrorMessage;
}
