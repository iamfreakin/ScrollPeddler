#include "Framework/ScrollPlayerController.h"

#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "ScrollLog.h"

void AScrollPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputSet.IsComplete())
	{
		if (InputSet.MappingContext || InputSet.Move || InputSet.Look || InputSet.Jump)
		{
			UE_LOG(LogScroll, Warning, TEXT("%s: InputSet is only partly assigned; using runtime defaults instead."), *GetName());
		}
		BuildDefaultInputSet();
	}

	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(InputSet.MappingContext, 0);
	}
}

void AScrollPlayerController::BuildDefaultInputSet()
{
	UInputMappingContext* Context = NewObject<UInputMappingContext>(this, TEXT("IMC_RuntimeDefault"));

	UInputAction* Move = NewObject<UInputAction>(this, TEXT("IA_Move"));
	Move->ValueType = EInputActionValueType::Axis2D;

	UInputAction* Look = NewObject<UInputAction>(this, TEXT("IA_Look"));
	Look->ValueType = EInputActionValueType::Axis2D;

	UInputAction* Jump = NewObject<UInputAction>(this, TEXT("IA_Jump"));
	Jump->ValueType = EInputActionValueType::Boolean;

	// A single key gives +X. Swizzle moves it to Y (forward/back); Negate flips the sign.
	auto MapKey = [Context](const UInputAction* Action, const FKey& Key, bool bSwizzle, bool bNegate) -> FEnhancedActionKeyMapping&
	{
		FEnhancedActionKeyMapping& Mapping = Context->MapKey(Action, Key);
		if (bSwizzle)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));
		}
		if (bNegate)
		{
			Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
		}
		return Mapping;
	};

	MapKey(Move, EKeys::W, true, false);
	MapKey(Move, EKeys::S, true, true);
	MapKey(Move, EKeys::D, false, false);
	MapKey(Move, EKeys::A, false, true);

	// Mouse up is +Y and AddControllerPitchInput(+) looks up, so no Negate once legacy input scales are off.
	FEnhancedActionKeyMapping& MouseLook = MapKey(Look, EKeys::Mouse2D, false, false);
	UInputModifierScalar* LookScale = NewObject<UInputModifierScalar>(Context);
	LookScale->Scalar = FVector(DefaultMouseLookScale, DefaultMouseLookScale, 1.0);
	MouseLook.Modifiers.Add(LookScale);

	MapKey(Jump, EKeys::SpaceBar, false, false);

	InputSet.MappingContext = Context;
	InputSet.Move = Move;
	InputSet.Look = Look;
	InputSet.Jump = Jump;
}
