#include "Framework/ScrollPlayerController.h"

#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/GameModeBase.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "ScrollLog.h"
#include "Tests/ScrollDevWalkerController.h"

void AScrollPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (!InputSet.IsComplete())
	{
		if (!InputSet.IsEmpty())
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

void AScrollPlayerController::ScrollSpawnWalker(float Radius)
{
	ServerSpawnWalker(Radius);
}

void AScrollPlayerController::ScrollClearWalkers()
{
	ServerClearWalkers();
}

void AScrollPlayerController::ServerSpawnWalker_Implementation(float Radius)
{
#if !UE_BUILD_SHIPPING
	const APawn* MyPawn = GetPawn();
	AGameModeBase* GameMode = GetWorld()->GetAuthGameMode();
	if (!MyPawn || !GameMode)
	{
		return;
	}

	const FVector Center = MyPawn->GetActorLocation() + MyPawn->GetActorForwardVector() * 400.f;
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	APawn* Walker = GetWorld()->SpawnActor<APawn>(GameMode->GetDefaultPawnClassForController(this), Center, FRotator::ZeroRotator, SpawnParams);
	AScrollDevWalkerController* Brain = GetWorld()->SpawnActor<AScrollDevWalkerController>();
	if (!Walker || !Brain)
	{
		return;
	}
	Brain->StartWalking(Center, Radius);
	Brain->Possess(Walker);
	UE_LOG(LogScroll, Log, TEXT("Spawned walker %s (radius %.0f cm)"), *Walker->GetName(), Radius);
#endif
}

void AScrollPlayerController::ServerClearWalkers_Implementation()
{
#if !UE_BUILD_SHIPPING
	for (TActorIterator<AScrollDevWalkerController> It(GetWorld()); It; ++It)
	{
		if (APawn* Walker = It->GetPawn())
		{
			Walker->Destroy();
		}
		It->Destroy();
	}
#endif
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

	UInputAction* Sprint = NewObject<UInputAction>(this, TEXT("IA_Sprint"));
	Sprint->ValueType = EInputActionValueType::Boolean;

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
	MapKey(Sprint, EKeys::LeftShift, false, false);

	InputSet.MappingContext = Context;
	InputSet.Move = Move;
	InputSet.Look = Look;
	InputSet.Jump = Jump;
	InputSet.Sprint = Sprint;
}
