#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ScrollPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;

/** Input assets the character binds to. All must be set, or the runtime defaults are used. */
USTRUCT(BlueprintType)
struct FScrollInputSet
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> MappingContext = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Move = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Look = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Jump = nullptr;

	/** Held to sprint. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> Sprint = nullptr;

	bool IsComplete() const { return MappingContext && Move && Look && Jump && Sprint; }
	bool IsEmpty() const { return !MappingContext && !Move && !Look && !Jump && !Sprint; }
};

/**
 * The player's own entry point. Server requests from this player will go through here (ActionComponent, D3~D4).
 * For now it owns local input setup.
 */
UCLASS()
class SCROLLPEDDLER_API AScrollPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	const FScrollInputSet& GetInputSet() const { return InputSet; }

protected:
	/** Runs only for local players. */
	virtual void SetupInputComponent() override;

private:
	/** Keyboard/mouse defaults built in code so the project runs before any input assets exist. */
	void BuildDefaultInputSet();

	/** Assign assets in a Blueprint subclass to replace the runtime defaults (needed for key rebinding). */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	FScrollInputSet InputSet;

	/** Mouse look multiplier for the runtime defaults. Replaces the deprecated legacy input scales (2.5). */
	UPROPERTY(EditDefaultsOnly, Category = "Input", meta = (ClampMin = "0.01"))
	float DefaultMouseLookScale = 2.5f;
};
