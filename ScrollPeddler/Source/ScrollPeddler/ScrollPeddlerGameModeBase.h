// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ScrollPeddlerGameModeBase.generated.h"

/**
 * Server-only rules for a session. Fills the AScrollGameMode role from T8.
 * Max players (4) comes from [/Script/Engine.GameSession] in DefaultGame.ini.
 */
UCLASS()
class SCROLLPEDDLER_API AScrollPeddlerGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	AScrollPeddlerGameModeBase();

protected:
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal = TEXT("")) override;

private:
	/** Next participant session id. Never reused within a session, so a rejoin gets a new number. */
	int32 NextParticipantSessionId = 1;
};
