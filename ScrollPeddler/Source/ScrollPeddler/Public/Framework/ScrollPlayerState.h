#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "ScrollPlayerState.generated.h"

/** Per-participant state for the current session. Not tied to a persistent account. */
UCLASS()
class SCROLLPEDDLER_API AScrollPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	/** Join-order number (1, 2, ...) assigned by the server. Never reused within a session; 0 means not assigned yet. */
	int32 GetParticipantSessionId() const { return ParticipantSessionId; }

	/** Server only. */
	void SetParticipantSessionId(int32 NewId);

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void CopyProperties(APlayerState* PlayerState) override;

private:
	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "Session")
	int32 ParticipantSessionId = 0;
};
