#pragma once

#include "CoreMinimal.h"
#include "Core/SPInteractionTypes.h"
#include "Core/SPTypes.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Character.h"
#include "SPCharacter.generated.h"

class ASPScrollPickup;
class ASPWorldItem;
class ASPPlayerState;
class UAnimSequence;
class UBlendSpace;
class UCameraComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class USPInventoryComponent;
struct FSPItemInstance;
struct FSPResolvedScrollEffect;
struct FSPScrollUseResult;

UCLASS()
class SCROLLPEDDLER_API ASPCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ASPCharacter();

	virtual void Tick(float DeltaSeconds) override;
	virtual void PostInitializeComponents() override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void OnJumped_Implementation() override;
	virtual void Landed(const FHitResult& Hit) override;

	USPInventoryComponent& GetInventory();
	const USPInventoryComponent& GetInventory() const;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Effects")
	float GetSilenceEndServerTime() const { return SilenceEndServerTime; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Effects")
	bool IsSilenced() const;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Effects")
	bool IsProtectedByScroll() const;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Effects")
	bool IsRevelationActive() const;

	/** Automation/input helper. Routes through the owning character's server RPC when needed. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Peddler|Interaction")
	void RequestPickup(ASPScrollPickup* Pickup);

	/** Returns the exact visibility-trace target shared by the HUD and interact input. */
	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Interaction")
	ASPScrollPickup* FindPickupInView() const;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Interaction")
	ASPWorldItem* FindWorldItemInView() const;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Interaction")
	bool HasPickupTargetInView() const;

	bool IsPickupRequestPending() const { return PendingPickupRequestId != 0; }
	bool HasActivePickupFeedback() const;
	bool IsNoTargetPickupFeedback() const { return bPickupFeedbackNoTarget; }
	bool IsPickupFeedbackTimedOut() const { return bPickupFeedbackTimedOut; }
	ESPPickupResultCode GetLastPickupResult() const { return LastPickupResult; }

	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }
	USkeletalMeshComponent* GetFirstPersonBody() const { return FirstPersonBody; }
	USkeletalMeshComponent* GetWorldBodyMesh() const { return GetMesh(); }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Movement")
	float GetStaminaSeconds() const { return StaminaSeconds; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Movement")
	bool IsSprinting() const { return bSprinting; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Inventory")
	bool IsCarryingLargeCargo() const;

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	bool IsSelfTreatmentInProgress() const { return bSelfTreatmentInProgress; }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Run")
	double GetTreatmentEndServerTime() const { return TreatmentEndServerTime; }

	/** Automation/input helper. Uses the first owner-visible inventory item by InstanceId. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Peddler|Scroll")
	void RequestUseFirst();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Scroll Peddler|Automation")
	void ServerRequestAutoExtract();

	/** Called by the authoritative PlayerState and its extraction rep-notify. */
	void HandleExtractionCommitted();

	/** Server-only response to an authoritative threat attack intent. */
	bool AuthorityDropHandItem(const FVector& PreferredWorldLocation);

	/** Server-only single-use ward interception for a resolved threat hit. */
	bool AuthorityConsumeScrollProtection();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(Server, Reliable)
	void ServerTryPickup(ASPScrollPickup* Pickup, uint32 RequestId);

	UFUNCTION(Server, Reliable)
	void ServerTryPickupWorldItem(
		ASPWorldItem* WorldItem,
		const FSPInteractionRequest& Request);

	UFUNCTION(Client, Reliable)
	void ClientNotifyPickupResult(uint32 RequestId, ESPPickupResultCode ResultCode);

	UFUNCTION(Server, Reliable)
	void ServerUseScroll(
		FGuid InstanceId,
		FGuid RequestId,
		int32 ExpectedInventoryRevision,
		FVector_NetQuantizeNormal AimDirection);

	UFUNCTION(Server, Unreliable)
	void ServerSetSprinting(bool bRequested);

	UFUNCTION(Server, Reliable)
	void ServerSwapHandWithBag(int32 BagIndex, int32 ExpectedRevision, int64 RequestId);

	UFUNCTION(Server, Reliable)
	void ServerDropHandItem(
		FGuid ExpectedInstanceId,
		int32 ExpectedRevision,
		int64 RequestId);

	UFUNCTION(Client, Reliable)
	void ClientNotifyInventoryAction(const FSPInteractionResult& Result);

	UFUNCTION(Server, Reliable)
	void ServerSetSelfTreatment(bool bRequested);

private:
	void MoveForward(float Value);
	void MoveRight(float Value);
	void Turn(float Value);
	void LookUp(float Value);
	void HandleJumpPressed();
	void HandleJumpReleased();
	void HandleSprintPressed();
	void HandleSprintReleased();
	void HandleCrouchPressed();
	void HandleCrouchReleased();
	void HandleInteract();
	void HandleUseScroll();
	void HandleSwapBag0();
	void HandleSwapBag1();
	void HandleSwapBag2();
	void HandleSwapBag3();
	void HandleSelfTreatment();
	void HandleDropHandItem();
	void RequestSwapBagSlot(int32 BagIndex);
	void RequestPickupWorldItem(ASPWorldItem* WorldItem);
	bool DropHandItemInternal(
		const FVector& PreferredWorldLocation,
		int32 ExpectedRevision,
		FSPItemInstance& OutDroppedItem);
	bool CanRequestInteraction() const;
	bool CanPerformFieldGameplayAction() const;
	bool HasValidOwningController() const;
	bool HasLineOfSightToPickup(const ASPScrollPickup* Pickup) const;
	bool CanSprint() const;
	void UpdateStamina(float DeltaSeconds);
	void ApplyMovementTuning();
	void InitializePresentation();
	void UpdateMovementNoise();
	void EmitGameplayNoise(FName TagName, float Loudness, float Radius);
	void CancelSelfTreatment();
	void CompleteSelfTreatment();
	void ApplyResolvedScrollUse(const FSPScrollUseResult& UseResult);
	void ApplyResolvedScrollEffect(const FSPResolvedScrollEffect& Effect);
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

	UFUNCTION()
	void OnRep_ScrollEffectState();

	UFUNCTION()
	void OnRep_MovementState();

	UFUNCTION()
	void OnRep_TreatmentState();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UCameraComponent> FirstPersonCamera;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Presentation", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USkeletalMeshComponent> FirstPersonBody;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USPInventoryComponent> Inventory;

	UPROPERTY(EditDefaultsOnly, Category = "Presentation|KayKit", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<USkeletalMesh> BodyMeshAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Presentation|KayKit", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UBlendSpace> LocomotionBlendSpaceAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Presentation|KayKit", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UBlendSpace> CrouchBlendSpaceAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Presentation|KayKit", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UAnimSequence> JumpStartAnimationAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Presentation|KayKit", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UAnimSequence> FallingAnimationAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Presentation|KayKit", meta = (AllowPrivateAccess = "true"))
	TSoftObjectPtr<UAnimSequence> LandingAnimationAsset;

	UPROPERTY(EditDefaultsOnly, Category = "Presentation|KayKit", meta = (AllowPrivateAccess = "true"))
	TArray<FName> FirstPersonHiddenMaterialSlots;

	UPROPERTY(Transient)
	TObjectPtr<UBlendSpace> LocomotionBlendSpace;

	UPROPERTY(Transient)
	TObjectPtr<UBlendSpace> CrouchBlendSpace;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> JumpStartAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> FallingAnimation;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> LandingAnimation;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_SilenceEndServerTime, Category = "Effects", meta = (AllowPrivateAccess = "true"))
	float SilenceEndServerTime = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_ScrollEffectState, Category = "Effects", meta = (AllowPrivateAccess = "true"))
	float ProtectionEndServerTime = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_ScrollEffectState, Category = "Effects", meta = (AllowPrivateAccess = "true"))
	float RevelationEndServerTime = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_MovementState, Category = "Movement", meta = (AllowPrivateAccess = "true"))
	float StaminaSeconds = 6.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_MovementState, Category = "Movement", meta = (AllowPrivateAccess = "true"))
	bool bSprinting = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_TreatmentState, Category = "Run", meta = (AllowPrivateAccess = "true"))
	bool bSelfTreatmentInProgress = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_TreatmentState, Category = "Run", meta = (AllowPrivateAccess = "true"))
	double TreatmentEndServerTime = 0.0;

	UPROPERTY(Transient)
	bool bInteractionDisabled = false;

	FTimerHandle PickupRequestTimeoutHandle;
	FTimerHandle SelfTreatmentTimerHandle;
	uint32 NextPickupRequestId = 1;
	int64 NextInventoryRequestId = 1;
	uint32 PendingPickupRequestId = 0;
	double PickupFeedbackExpiresAt = 0.0;
	ESPPickupResultCode LastPickupResult = ESPPickupResultCode::InvalidRequest;
	bool bPickupFeedbackNoTarget = false;
	bool bPickupFeedbackTimedOut = false;
	FSPInteractionReplayLedger PickupReplayLedger;
	FSPInteractionReplayLedger InventoryReplayLedger;
	double NextMovementNoiseServerTime = 0.0;
	double NextInjuredGroanServerTime = 0.0;

	static constexpr float MaxPickupDistance = 350.0f;
	static constexpr float MaxStaminaSeconds = 6.0f;
	static constexpr float StaminaRecoveryPerSecond = 0.75f;
	static constexpr float WalkSpeed = 400.0f;
	static constexpr float SprintSpeed = 500.0f;
	static constexpr float CrouchSpeed = 100.0f;
	static constexpr float BackwardSpeed = 120.0f;
	static constexpr float InjuredSpeedMultiplier = 0.85f;
	static constexpr float LargeCargoSpeedMultiplier = 0.72f;
	static constexpr float SelfTreatmentSeconds = 20.0f;
	static constexpr float PickupRequestTimeoutSeconds = 3.0f;
	static constexpr float PickupResultFeedbackSeconds = 1.5f;
	static constexpr float NoTargetFeedbackSeconds = 0.75f;
};
