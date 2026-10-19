#include "Tests/ScrollDevWalkerController.h"

#include "Framework/ScrollCharacterMovementComponent.h"
#include "GameFramework/Pawn.h"

AScrollDevWalkerController::AScrollDevWalkerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

void AScrollDevWalkerController::StartWalking(const FVector& InCenter, float InRadius)
{
	Center = InCenter;
	Radius = FMath::Max(InRadius, 50.f);
}

void AScrollDevWalkerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APawn* Walker = GetPawn();
	if (!Walker)
	{
		return;
	}

	PhaseTime += DeltaSeconds;
	if (PhaseTime >= PhaseSeconds)
	{
		PhaseTime = 0.f;
		bSprinting = !bSprinting;
		if (UScrollCharacterMovementComponent* Movement = Cast<UScrollCharacterMovementComponent>(Walker->GetMovementComponent()))
		{
			Movement->SetWantsToSprint(bSprinting);
		}
	}

	// Head for a point a little further along the circle, so the walker keeps circling counterclockwise.
	const FVector Offset = Walker->GetActorLocation() - Center;
	const float AheadAngle = FMath::Atan2(Offset.Y, Offset.X) + 0.5f;
	const FVector Target = Center + FVector(FMath::Cos(AheadAngle), FMath::Sin(AheadAngle), 0.f) * Radius;
	const FVector Direction = (Target - Walker->GetActorLocation()).GetSafeNormal2D();

	SetControlRotation(Direction.Rotation());
	Walker->AddMovementInput(Direction, 1.f);
}
