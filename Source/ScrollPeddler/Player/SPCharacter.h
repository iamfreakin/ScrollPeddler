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

	/** 자동화·입력 의도를 필요할 때 owning character의 서버 RPC로 전달한다. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Peddler|Interaction")
	void RequestPickup(ASPScrollPickup* Pickup);

	/** HUD와 상호작용 입력이 공유하는 Visibility trace 대상을 반환한다. */
	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Interaction")
	ASPScrollPickup* FindPickupInView() const;

	bool IsPickupRequestPending() const { return PendingPickupRequestId != 0; }
	bool HasActivePickupFeedback() const;
	bool IsNoTargetPickupFeedback() const { return bPickupFeedbackNoTarget; }
	bool IsPickupFeedbackTimedOut() const { return bPickupFeedbackTimedOut; }
	ESPPickupResultCode GetLastPickupResult() const { return LastPickupResult; }
	bool IsScrollUseRequestPending() const { return PendingScrollUseRequestId != 0; }
	bool HasActiveScrollUseFeedback() const;
	bool IsScrollUseFeedbackTimedOut() const { return bScrollUseFeedbackTimedOut; }
	ESPScrollUseResultCode GetLastScrollUseResult() const { return LastScrollUseResult; }

	UCameraComponent* GetFirstPersonCamera() const { return FirstPersonCamera; }
	USkeletalMeshComponent* GetFirstPersonHands() const { return FirstPersonHands; }
	UStaticMeshComponent* GetRemoteBodyMesh() const { return RemoteBodyMesh; }

	/** 자동화·입력에서 소유자에게 보이는 첫 인벤토리 InstanceId를 사용한다. */
	UFUNCTION(BlueprintCallable, Category = "Scroll Peddler|Scroll")
	void RequestUseFirst();

#if !UE_BUILD_SHIPPING
	/** 지정한 Pickup·Stable ID·RequestId를 실제 서버 픽업 RPC로 전달하는 테스트 seam이다. */
	bool DevelopmentRequestPickup(ASPScrollPickup* Pickup, const FGuid& TargetInstanceId, uint32 RequestId);
	/** 지정한 InstanceId·RequestId를 실제 서버 소비 RPC로 전달하는 테스트 seam이다. */
	bool DevelopmentRequestUseScroll(const FGuid& InstanceId, uint32 RequestId);
	uint32 GetLastCompletedPickupRequestId() const { return LastCompletedPickupRequestId; }
	uint32 GetLastCompletedUseRequestId() const { return LastCompletedUseRequestId; }
	uint32 GetPickupCompletionSerial() const { return PickupCompletionSerial; }
	uint32 GetScrollUseCompletionSerial() const { return ScrollUseCompletionSerial; }
#endif

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Scroll Peddler|Automation")
	void ServerRequestAutoExtract();

	/** 권위 PlayerState와 extraction rep-notify에서 호출한다. */
	void HandleExtractionCommitted();

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Pickup actor와 Stable ID의 일치를 확인하고 RequestId 기준으로 멱등 처리한다. */
	UFUNCTION(Server, Reliable)
	void ServerTryPickup(ASPScrollPickup* Pickup, FGuid TargetInstanceId, uint32 RequestId);

	UFUNCTION(Client, Reliable)
	void ClientNotifyPickupResult(uint32 RequestId, ESPPickupResultCode ResultCode);

	/** 소유 중인 InstanceId를 RequestId 기준으로 한 번만 소비한다. */
	UFUNCTION(Server, Reliable)
	void ServerUseScroll(FGuid InstanceId, uint32 RequestId);

	UFUNCTION(Client, Reliable)
	void ClientNotifyScrollUseResult(uint32 RequestId, ESPScrollUseResultCode ResultCode);

private:
	enum class EInteractionRequestAction : uint8
	{
		Pickup,
		UseScroll
	};

	enum class EInteractionRequestDisposition : uint8
	{
		NewRequest,
		ExactReplay,
		Conflict,
		InvalidRequestId,
		LedgerFull
	};

	struct FInteractionRequestRecord
	{
		EInteractionRequestAction Action = EInteractionRequestAction::Pickup;
		FGuid TargetId;
		uint8 ResultCode = 0;
	};

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
		const FGuid& TargetInstanceId,
		ESPPickupResultCode ResultCode,
		const TCHAR* Reason,
		const ASPScrollPickup* Pickup);
	void CompletePickupRequest(
		uint32 RequestId,
		const FGuid& TargetInstanceId,
		ESPPickupResultCode ResultCode);
	void RejectScrollUseRequest(
		uint32 RequestId,
		const FGuid& InstanceId,
		ESPScrollUseResultCode ResultCode,
		const TCHAR* Reason);
	void CompleteScrollUseRequest(
		uint32 RequestId,
		const FGuid& InstanceId,
		ESPScrollUseResultCode ResultCode);
	bool HandleExistingInteractionRequest(
		uint32 RequestId,
		EInteractionRequestAction Action,
		const FGuid& TargetId);
	bool TryRecordInteractionRequest(
		uint32 RequestId,
		EInteractionRequestAction Action,
		const FGuid& TargetId,
		uint8 ResultCode);
	static EInteractionRequestDisposition ClassifyInteractionRequest(
		const TMap<uint32, FInteractionRequestRecord>& Ledger,
		uint32 RequestId,
		EInteractionRequestAction Action,
		const FGuid& TargetId);
	static bool TryRecordInteractionRequestInLedger(
		TMap<uint32, FInteractionRequestRecord>& Ledger,
		uint32 RequestId,
		EInteractionRequestAction Action,
		const FGuid& TargetId,
		uint8 ResultCode);
	void NotifyInteractionResult(
		uint32 RequestId,
		EInteractionRequestAction Action,
		uint8 ResultCode);
	void BeginLocalPickupRequest(uint32 RequestId);
	void HandlePickupRequestTimeout(uint32 RequestId);
	void ShowNoTargetPickupFeedback();
	void ShowPickupFeedback(ESPPickupResultCode ResultCode, bool bTimedOut = false);
	void BeginLocalScrollUseRequest(uint32 RequestId);
	void HandleScrollUseRequestTimeout(uint32 RequestId);
	void ShowScrollUseFeedback(ESPScrollUseResultCode ResultCode, bool bTimedOut = false);
	uint32 AllocateInteractionRequestId();
	static uint32 AllocateInteractionRequestIdFromCounter(uint32& NextRequestId);
	static bool IsPickupWithinRange(const FVector& CharacterLocation, const FVector& PickupLocation);

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
	FTimerHandle ScrollUseRequestTimeoutHandle;
	uint32 NextInteractionRequestId = 1;
	uint32 PendingPickupRequestId = 0;
	uint32 PendingScrollUseRequestId = 0;
	uint32 LastCompletedPickupRequestId = 0;
	uint32 LastCompletedUseRequestId = 0;
	uint32 PickupCompletionSerial = 0;
	uint32 ScrollUseCompletionSerial = 0;
	double PickupFeedbackExpiresAt = 0.0;
	double ScrollUseFeedbackExpiresAt = 0.0;
	ESPPickupResultCode LastPickupResult = ESPPickupResultCode::InvalidRequest;
	ESPScrollUseResultCode LastScrollUseResult = ESPScrollUseResultCode::InvalidRequest;
	bool bPickupFeedbackNoTarget = false;
	bool bPickupFeedbackTimedOut = false;
	bool bScrollUseFeedbackTimedOut = false;
	TMap<uint32, FInteractionRequestRecord> ServerInteractionRequestLedger;

	static constexpr float MaxPickupDistance = 350.0f;
	static constexpr float PickupRequestTimeoutSeconds = 3.0f;
	static constexpr float PickupResultFeedbackSeconds = 1.5f;
	static constexpr float ScrollUseRequestTimeoutSeconds = 3.0f;
	static constexpr float ScrollUseResultFeedbackSeconds = 1.5f;
	static constexpr float NoTargetFeedbackSeconds = 0.75f;
	static constexpr int32 MaxInteractionRequestLedgerEntries = 1024;

	friend class FSPInteractionRequestLedgerTest;
	friend class FSPInteractionRequestIdTest;
	friend class FSPPickupDistanceBoundaryTest;
};
