#include "Framework/ScrollCharacterMovementComponent.h"

#include "GameFramework/Character.h"

/** Client-side record of one move, including whether sprint was held, so it can be resent and replayed. */
class FScrollSavedMove : public FSavedMove_Character
{
	using Super = FSavedMove_Character;

public:
	virtual void Clear() override
	{
		Super::Clear();
		bSavedWantsToSprint = false;
	}

	virtual uint8 GetCompressedFlags() const override
	{
		uint8 Flags = Super::GetCompressedFlags();
		if (bSavedWantsToSprint)
		{
			Flags |= FLAG_Custom_0;
		}
		return Flags;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* InCharacter, float MaxDelta) const override
	{
		if (bSavedWantsToSprint != static_cast<const FScrollSavedMove*>(NewMove.Get())->bSavedWantsToSprint)
		{
			return false;
		}
		return Super::CanCombineWith(NewMove, InCharacter, MaxDelta);
	}

	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel, FNetworkPredictionData_Client_Character& ClientData) override
	{
		Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
		if (const UScrollCharacterMovementComponent* Movement = Cast<UScrollCharacterMovementComponent>(Character->GetCharacterMovement()))
		{
			bSavedWantsToSprint = Movement->bWantsToSprint;
		}
	}

	virtual void PrepMoveFor(ACharacter* Character) override
	{
		Super::PrepMoveFor(Character);
		if (UScrollCharacterMovementComponent* Movement = Cast<UScrollCharacterMovementComponent>(Character->GetCharacterMovement()))
		{
			Movement->bWantsToSprint = bSavedWantsToSprint;
		}
	}

private:
	bool bSavedWantsToSprint = false;
};

class FScrollNetworkPredictionData_Client : public FNetworkPredictionData_Client_Character
{
public:
	explicit FScrollNetworkPredictionData_Client(const UCharacterMovementComponent& ClientMovement)
		: FNetworkPredictionData_Client_Character(ClientMovement)
	{
	}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FScrollSavedMove());
	}
};

UScrollCharacterMovementComponent::UScrollCharacterMovementComponent()
{
	// Starting estimate from T3 (3 m/s walking).
	MaxWalkSpeed = 300.f;
}

bool UScrollCharacterMovementComponent::IsSprinting() const
{
	return bWantsToSprint && IsMovingOnGround() && Velocity.SizeSquared2D() > UE_KINDA_SMALL_NUMBER;
}

float UScrollCharacterMovementComponent::GetMaxSpeed() const
{
	if (bWantsToSprint && IsMovingOnGround())
	{
		return SprintSpeed;
	}
	return Super::GetMaxSpeed();
}

void UScrollCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
}

FNetworkPredictionData_Client* UScrollCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UScrollCharacterMovementComponent* MutableThis = const_cast<UScrollCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FScrollNetworkPredictionData_Client(*this);
	}
	return ClientPredictionData;
}
