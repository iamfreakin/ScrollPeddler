#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Controller.h"
#include "ScrollDevWalkerController.generated.h"

/**
 * Development only. Server-side brain that walks a character in a circle, switching between walk and sprint,
 * so one person can listen to another character's footsteps. Spawned by the ScrollSpawnWalker console command.
 */
UCLASS(NotBlueprintable)
class SCROLLPEDDLER_API AScrollDevWalkerController : public AController
{
	GENERATED_BODY()

public:
	AScrollDevWalkerController();

	void StartWalking(const FVector& InCenter, float InRadius);

	virtual void Tick(float DeltaSeconds) override;

private:
	FVector Center = FVector::ZeroVector;
	float Radius = 300.f;
	/** Walk for this long, then sprint for this long, and repeat. */
	float PhaseSeconds = 4.f;
	float PhaseTime = 0.f;
	bool bSprinting = false;
};
