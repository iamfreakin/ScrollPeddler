#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "SPCharacter.generated.h"

class ASPScrollPickup;
class ASPPlayerState;
class UCameraComponent;
class USPInventoryComponent;
class UStaticMeshComponent;
class USpringArmComponent;

UCLASS()
class SCROLLPEDDLER_API ASPCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASPCharacter();

	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	USPInventoryComponent& GetInventory();
	const USPInventoryComponent& GetInventory() const;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Effects")
	float GetSilenceEndServerTime() const { return SilenceEndServerTime; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Effects")
	bool IsSilenced() const;

	/** Automation/input helper. Routes through the owning character's server RPC when needed. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Peddler|Interaction")
	void RequestPickup(ASPScrollPickup* Pickup);

	/** Automation/input helper. Uses the first owner-visible inventory item by InstanceId. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Peddler|Scroll")
	void RequestUseFirst();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Scroll Peddler|Automation")
	void ServerRequestAutoExtract();

	/** Called by the authoritative PlayerState and its extraction rep-notify. */
	void HandleExtractionCommitted();

protected:
	UFUNCTION(Server, Reliable)
	void ServerTryPickup(ASPScrollPickup* Pickup);

	UFUNCTION(Server, Reliable)
	void ServerUseScroll(FGuid InstanceId);

private:
	void MoveForward(float Value);
	void MoveRight(float Value);
	void Turn(float Value);
	void LookUp(float Value);
	void HandleInteract();
	void HandleUseScroll();
	bool CanRequestInteraction() const;
	bool HasValidOwningController() const;
	bool HasLineOfSightToPickup(const ASPScrollPickup* Pickup) const;
	ASPPlayerState* GetActiveScrollPlayerState() const;
	void LogPickupRejected(const TCHAR* Reason, const ASPScrollPickup* Pickup) const;

	UFUNCTION()
	void OnRep_SilenceEndServerTime();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USPInventoryComponent> Inventory;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Debug", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> DebugBodyMesh;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_SilenceEndServerTime, Category = "Effects", meta = (AllowPrivateAccess = "true"))
	float SilenceEndServerTime = 0.0f;

	UPROPERTY(Transient)
	bool bInteractionDisabled = false;

	static constexpr float MaxPickupDistance = 350.0f;
};
