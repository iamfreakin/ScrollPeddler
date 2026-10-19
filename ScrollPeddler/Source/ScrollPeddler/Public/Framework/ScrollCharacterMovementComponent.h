#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "ScrollCharacterMovementComponent.generated.h"

/**
 * Character movement for people and character-based creatures (T8).
 * Sprint rides in the saved-move flags, so the owning client predicts it and the server replays the same moves.
 */
UCLASS()
class SCROLLPEDDLER_API UScrollCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

	friend class FScrollSavedMove;

public:
	UScrollCharacterMovementComponent();

	/** Call on the owning client (or listen-server host) from input. */
	void SetWantsToSprint(bool bNewWantsToSprint) { bWantsToSprint = bNewWantsToSprint; }
	bool WantsToSprint() const { return bWantsToSprint; }

	/** Sprint input held, on the ground, and actually moving. */
	bool IsSprinting() const;

	float GetWalkSpeed() const { return MaxWalkSpeed; }
	float GetSprintSpeed() const { return SprintSpeed; }

	virtual float GetMaxSpeed() const override;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;

private:
	/** Starting estimate. T3 assumes 3 m/s walking; running speed is not decided yet. */
	UPROPERTY(EditDefaultsOnly, Category = "Character Movement: Sprint", meta = (ClampMin = "0", Units = "cm/s"))
	float SprintSpeed = 500.f;

	bool bWantsToSprint = false;
};
