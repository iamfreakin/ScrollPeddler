#include "Framework/ScrollPlayerState.h"

#include "Net/UnrealNetwork.h"

void AScrollPlayerState::SetParticipantSessionId(int32 NewId)
{
	check(HasAuthority());
	ParticipantSessionId = NewId;
}

void AScrollPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AScrollPlayerState, ParticipantSessionId);
}

void AScrollPlayerState::CopyProperties(APlayerState* PlayerState)
{
	Super::CopyProperties(PlayerState);

	// Keep the same participant across seamless travel and inactive-player restore.
	if (AScrollPlayerState* ScrollPlayerState = Cast<AScrollPlayerState>(PlayerState))
	{
		ScrollPlayerState->ParticipantSessionId = ParticipantSessionId;
	}
}
