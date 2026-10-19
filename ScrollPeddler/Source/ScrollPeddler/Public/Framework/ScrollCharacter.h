#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ScrollCharacter.generated.h"

class UCameraComponent;
class UScrollCharacterMovementComponent;
class UScrollHeadBobComponent;
class USkeletalMeshComponent;
struct FInputActionValue;

/**
 * First-person player character, laid out like the engine's First Person template:
 * the owner sees FirstPersonMesh (arms) through a camera on its head socket; everyone else sees GetMesh() (full body).
 * Meshes and anim blueprints are assigned in BP_SpikeCharacter.
 */
UCLASS()
class SCROLLPEDDLER_API AScrollCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AScrollCharacter(const FObjectInitializer& ObjectInitializer);

	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }
	UScrollCharacterMovementComponent* GetScrollMovement() const;

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);
	void StartSprint();
	void StopSprint();

	/** Only the owner sees this. Drawn with the camera's first-person FOV and scale so it does not clip into walls. */
	UPROPERTY(VisibleAnywhere, Category = "Mesh")
	TObjectPtr<USkeletalMeshComponent> FirstPersonMesh;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UScrollHeadBobComponent> HeadBob;
};
