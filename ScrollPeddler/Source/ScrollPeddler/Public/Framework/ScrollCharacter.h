#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ScrollCharacter.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
struct FInputActionValue;

/**
 * First-person player character.
 * The owner sees through FirstPersonCamera; other players see the placeholder body until a real mesh exists.
 */
UCLASS()
class SCROLLPEDDLER_API AScrollCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AScrollCharacter();

	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

private:
	void Move(const FInputActionValue& Value);
	void Look(const FInputActionValue& Value);

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	/** Grey-box body shown to other players. Hidden from the owner. */
	UPROPERTY(VisibleAnywhere, Category = "Placeholder")
	TObjectPtr<UStaticMeshComponent> PlaceholderBody;

	/** Shows which way the placeholder body faces. */
	UPROPERTY(VisibleAnywhere, Category = "Placeholder")
	TObjectPtr<UStaticMeshComponent> PlaceholderVisor;
};
