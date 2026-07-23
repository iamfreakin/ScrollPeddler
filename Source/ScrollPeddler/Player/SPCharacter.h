#pragma once

#include "CoreMinimal.h"
#include "Core/SPTypes.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Character.h"
#include "SPCharacter.generated.h"

class ASPScrollPickup;
class ASPPlayerState;
class UCameraComponent;
class USkeletalMeshComponent;
class USPInventoryComponent;
class UStaticMeshComponent;

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

	/** Returns the exact visibility-trace target shared by the HUD and interact input. */
	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Interaction")
	ASPScrollPickup* FindPickupInView() const;

	bool IsPickupRequestPending() const { return PendingPickupRequestId != 0; }
	bool HasActivePickupFeedback() const;
	bool IsNoTargetPickupFeedback() const { return bPickupFeedbackNoTarget; }
	bool IsPickupFeedbackTimedOut() const { return bPickupFeedbackTimedOut; }
	ESPPickupResultCode GetLastPickupResult() const { return LastPickupResult; }

	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }
	USkeletalMeshComponent* GetFirstPersonHands() const { return FirstPersonHands; }
	UStaticMeshComponent* GetRemoteBodyMesh() const { return RemoteBodyMesh; }

	/** Automation/input helper. Uses the first owner-visible inventory item by InstanceId. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Peddler|Scroll")
	void RequestUseFirst();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Scroll Peddler|Automation")
	void ServerRequestAutoExtract();

	/** Called by the authoritative PlayerState and its extraction rep-notify. */
	void HandleExtractionCommitted();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(Server, Reliable)
	void ServerTryPickup(ASPScrollPickup* Pickup, uint32 RequestId);

	UFUNCTION(Client, Reliable)
	void ClientNotifyPickupResult(uint32 RequestId, ESPPickupResultCode ResultCode);

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
	void RejectPickupRequest(
		uint32 RequestId,
		ESPPickupResultCode ResultCode,
		const TCHAR* Reason,
		const ASPScrollPickup* Pickup);
	void BeginLocalPickupRequest(uint32 RequestId);
	void HandlePickupRequestTimeout(uint32 RequestId);
	void ShowNoTargetPickupFeedback();
	void ShowPickupFeedback(ESPPickupResultCode ResultCode, bool bTimedOut = false);
	uint32 AllocatePickupRequestId();

	UFUNCTION()
	void OnRep_SilenceEndServerTime();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> FirstPersonHands;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USPInventoryComponent> Inventory;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> RemoteBodyMesh;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_SilenceEndServerTime, Category = "Effects", meta = (AllowPrivateAccess = "true"))
	float SilenceEndServerTime = 0.0f;

	UPROPERTY(Transient)
	bool bInteractionDisabled = false;

	FTimerHandle PickupRequestTimeoutHandle;
	uint32 NextPickupRequestId = 1;
	uint32 PendingPickupRequestId = 0;
	double PickupFeedbackExpiresAt = 0.0;
	ESPPickupResultCode LastPickupResult = ESPPickupResultCode::InvalidRequest;
	bool bPickupFeedbackNoTarget = false;
	bool bPickupFeedbackTimedOut = false;

	static constexpr float MaxPickupDistance = 350.0f;
	static constexpr float PickupRequestTimeoutSeconds = 3.0f;
	static constexpr float PickupResultFeedbackSeconds = 1.5f;
	static constexpr float NoTargetFeedbackSeconds = 0.75f;
};
