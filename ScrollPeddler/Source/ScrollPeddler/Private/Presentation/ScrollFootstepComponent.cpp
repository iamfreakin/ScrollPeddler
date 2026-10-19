#include "Presentation/ScrollFootstepComponent.h"

#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Framework/ScrollCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ScrollLog.h"
#include "Sound/SoundBase.h"

namespace
{
	TAutoConsoleVariable<bool> CVarFootstepDebug(
		TEXT("Scroll.Footsteps.Debug"),
		false,
		TEXT("Log and draw every footstep with its surface and walk/run choice."));

	USoundBase* PickSound(const TArray<TObjectPtr<USoundBase>>& Choices, int32& InOutLastIndex)
	{
		if (Choices.IsEmpty())
		{
			return nullptr;
		}
		int32 Index = FMath::RandRange(0, Choices.Num() - 1);
		if (Choices.Num() > 1 && Index == InOutLastIndex)
		{
			Index = (Index + 1) % Choices.Num();
		}
		InOutLastIndex = Index;
		return Choices[Index];
	}
}

UScrollFootstepComponent::UScrollFootstepComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// One mannequin height is about 192 cm: audible nearby, silent at five heights (9.6 m).
	// Linear falloff keeps intermediate distances audible and easy to distinguish.
	Attenuation.bAttenuate = true;
	Attenuation.bSpatialize = true;
	Attenuation.DistanceAlgorithm = EAttenuationDistanceModel::Linear;
	Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Attenuation.AttenuationShapeExtents = FVector(200.f, 0.f, 0.f);
	Attenuation.FalloffDistance = 760.f;
	// Own steps (about 1.6 m below the camera) play centred instead of trailing behind while moving;
	// others' steps further away stay fully 3D. Blends from 3.5 m in to fully centred at 2 m.
	Attenuation.NonSpatializedRadiusStart = 350.f;
	Attenuation.NonSpatializedRadiusEnd = 200.f;
}

float UScrollFootstepComponent::GetRunAlpha(const ACharacter& Character)
{
	const UCharacterMovementComponent* Movement = Character.GetCharacterMovement();
	if (!Movement)
	{
		return 0.f;
	}
	float WalkSpeed = Movement->MaxWalkSpeed;
	float SprintSpeed = WalkSpeed;
	if (const UScrollCharacterMovementComponent* ScrollMovement = Cast<UScrollCharacterMovementComponent>(Movement))
	{
		WalkSpeed = ScrollMovement->GetWalkSpeed();
		SprintSpeed = ScrollMovement->GetSprintSpeed();
	}
	if (SprintSpeed <= WalkSpeed)
	{
		return 0.f;
	}
	return (Movement->Velocity.Size2D() - WalkSpeed) / (SprintSpeed - WalkSpeed);
}

void UScrollFootstepComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetNetMode() == NM_DedicatedServer)
	{
		SetComponentTickEnabled(false);
		return;
	}

	RuntimeAttenuation = NewObject<USoundAttenuation>(this);
	RuntimeAttenuation->Attenuation = Attenuation;

	if (ACharacter* Character = GetOwner<ACharacter>())
	{
		Character->MovementModeChangedDelegate.AddDynamic(this, &UScrollFootstepComponent::HandleMovementModeChanged);
	}
}

void UScrollFootstepComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (ACharacter* Character = GetOwner<ACharacter>())
	{
		Character->MovementModeChangedDelegate.RemoveDynamic(this, &UScrollFootstepComponent::HandleMovementModeChanged);
	}
	Super::EndPlay(EndPlayReason);
}

void UScrollFootstepComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const ACharacter* Character = GetOwner<ACharacter>();
	const UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
	if (!Movement)
	{
		return;
	}

	const FVector Location = Character->GetActorLocation();
	const float Distance = bHasLastLocation ? FVector::Dist2D(Location, LastLocation) : 0.f;
	LastLocation = Location;
	bHasLastLocation = true;

	// Teleports and big network corrections are not walking.
	if (!Movement->IsMovingOnGround() || Distance > SprintStepLength)
	{
		return;
	}

	const float RunAlpha = GetRunAlpha(*Character);
	const float StepLength = FMath::Lerp(WalkStepLength, SprintStepLength, FMath::Clamp(RunAlpha, 0.f, 1.f));
	const float PreviousCycles = StepCycles;
	StepCycles += Distance / StepLength;
	if (FMath::FloorToInt(StepCycles) > FMath::FloorToInt(PreviousCycles))
	{
		PlayFootstep(RunAlpha >= RunThreshold);
	}
}

void UScrollFootstepComponent::HandleMovementModeChanged(ACharacter* Character, EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	// Landing: a heavier step. Movement mode replicates, so this fires for other players' characters too.
	if (PrevMovementMode == MOVE_Falling && Character && Character->GetCharacterMovement()->IsMovingOnGround())
	{
		PlayFootstep(true);
	}
}

void UScrollFootstepComponent::PlayFootstep(bool bRun)
{
	const ACharacter* Character = GetOwner<ACharacter>();
	if (!Character || !RuntimeAttenuation)
	{
		return;
	}

	const FVector Start = Character->GetActorLocation();
	const FVector End = Start - FVector(0.f, 0.f, Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + GroundTraceExtra);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ScrollFootstep), false, Character);
	Params.bReturnPhysicalMaterial = true;

	FHitResult Hit;
	EPhysicalSurface Surface = SurfaceType_Default;
	FVector SoundLocation = End + FVector(0.f, 0.f, GroundTraceExtra);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		Surface = UPhysicalMaterial::DetermineSurfaceType(Hit.PhysMaterial.Get());
		SoundLocation = Hit.ImpactPoint;
	}

	const FScrollFootstepSounds& Sounds = GetSoundsFor(Surface);
	const bool bUseRun = bRun && !Sounds.Run.IsEmpty();
	USoundBase* Sound = PickSound(bUseRun ? Sounds.Run : Sounds.Walk, bUseRun ? LastRunIndex : LastWalkIndex);

	if (CVarFootstepDebug.GetValueOnGameThread())
	{
		UE_LOG(LogScroll, Log, TEXT("Footstep %s: %s %s%s"),
			*Character->GetName(),
			*StaticEnum<EPhysicalSurface>()->GetDisplayNameTextByValue(Surface).ToString(),
			bRun ? TEXT("Run") : TEXT("Walk"),
			Sound ? TEXT("") : TEXT(" (no sound assigned)"));
		DrawDebugSphere(GetWorld(), SoundLocation, 8.f, 8, bRun ? FColor::Orange : FColor::Green, false, 1.f);
	}

	if (Sound)
	{
		const float Pitch = Sounds.Pitch * (1.f + FMath::FRandRange(-PitchVariation, PitchVariation));
		UGameplayStatics::PlaySoundAtLocation(this, Sound, SoundLocation, FRotator::ZeroRotator,
			bRun ? RunVolume : WalkVolume, Pitch, 0.f, RuntimeAttenuation, nullptr, Character);
	}
}

const FScrollFootstepSounds& UScrollFootstepComponent::GetSoundsFor(EPhysicalSurface Surface) const
{
	const FScrollFootstepSounds* Sounds = SurfaceSounds.Find(Surface);
	return Sounds && !Sounds->IsEmpty() ? *Sounds : DefaultSounds;
}
