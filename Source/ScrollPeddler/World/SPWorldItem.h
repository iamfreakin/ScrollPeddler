#pragma once

#include "CoreMinimal.h"
#include "Core/SPItemTypes.h"
#include "GameFramework/Actor.h"
#include "World/SPInteractable.h"
#include "World/SPThreatTargetInterface.h"
#include "SPWorldItem.generated.h"

class APawn;
class UBoxComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class ESPWorldItemLifecycleState : uint8
{
	Uninitialized,
	Available,
	Claimed,
	Committed
};

UENUM(BlueprintType)
enum class ESPDropPlacementResult : uint8
{
	Success,
	InvalidSource,
	InvalidTransform,
	InvalidDistanceLimit,
	OutOfRange,
	Blocked
};

/**
 * Pure post-sweep validation for authoritative drops.
 *
 * bCollisionAllowsPlacement is the result of the caller's authoritative
 * collision query; keeping it as input makes the remaining rules deterministic
 * and independently testable.
 */
SCROLLPEDDLER_API ESPDropPlacementResult SPValidateDropPlacement(
	const FVector& SourceLocation,
	const FTransform& ProposedTransform,
	float MaxDropDistance,
	bool bCollisionAllowsPlacement);

/**
 * Token-bound claim state. Only Lifecycle and Revision replicate; claimant and
 * tokens remain server-only capabilities.
 */
USTRUCT(BlueprintType)
struct SCROLLPEDDLER_API FSPWorldItemClaimState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ESPWorldItemLifecycleState Lifecycle = ESPWorldItemLifecycleState::Uninitialized;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 Revision = 0;

	bool Initialize();

	ESPInteractionResultCode TryClaim(
		UObject* Claimant,
		const FGuid& ProposedToken,
		int32 ExpectedRevision,
		FGuid& OutActiveToken);

	ESPInteractionResultCode Commit(
		UObject* Claimant,
		const FGuid& ClaimToken,
		int32 ExpectedRevision);

	ESPInteractionResultCode Rollback(
		UObject* Claimant,
		const FGuid& ClaimToken,
		int32 ExpectedRevision);

	bool IsAvailable() const { return Lifecycle == ESPWorldItemLifecycleState::Available; }
	bool IsClaimedBy(const UObject* Candidate) const;
	bool HasAbandonedClaim() const;
	bool RollbackAbandonedClaim();
	bool AdvanceAvailableRevision();

private:
	void AdvanceRevision();

	TWeakObjectPtr<UObject> ActiveClaimant;
	FGuid ActiveClaimToken;
	TWeakObjectPtr<UObject> TerminalClaimant;
	FGuid TerminalClaimToken;
	TWeakObjectPtr<UObject> LastRollbackClaimant;
	FGuid LastRollbackClaimToken;
};

/**
 * Generic replicated world representation of one item stack or unique item.
 *
 * Intended transfer order:
 * 1. Inspect/TryClaim
 * 2. Commit the destination container
 * 3. CommitClaim, or RollbackClaim if the destination mutation failed
 */
UCLASS()
class SCROLLPEDDLER_API ASPWorldItem
	: public AActor
	, public ISPInteractable
	, public ISPThreatItemTarget
{
	GENERATED_BODY()

public:
	ASPWorldItem();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool InitializeItem(const FSPItemInstance& InItemInstance);

	const FSPItemInstance& GetItemInstance() const { return ItemInstance; }
	ESPWorldItemLifecycleState GetLifecycleState() const { return ClaimState.Lifecycle; }
	int32 GetLifecycleRevision() const { return ClaimState.Revision; }
	bool IsAvailable() const { return ClaimState.IsAvailable() && ItemInstance.IsValid(); }

	const UBoxComponent* GetInteractionBounds() const { return InteractionBounds; }
	const UStaticMeshComponent* GetPickupVisual() const { return PickupVisual; }

	/** Read-only authoritative snapshot; does not reserve the item. */
	ESPInteractionResultCode InspectItem(
		const APawn* RequestingPawn,
		const FSPInteractionRequest& Request,
		FSPItemInstance& OutItemSnapshot,
		int32& OutAuthoritativeRevision) const;

	/**
	 * Reserves the item and returns a server-only token. Repeating a claim from
	 * the same claimant returns the existing token without advancing revision.
	 */
	ESPInteractionResultCode TryClaimItem(
		APawn* Claimant,
		const FSPInteractionRequest& Request,
		FGuid& OutClaimToken,
		FSPItemInstance& OutItemSnapshot,
		int32& OutAuthoritativeRevision);

	/** Terminal, idempotent world-side commit after destination commit succeeds. */
	ESPInteractionResultCode CommitClaim(
		APawn* Claimant,
		const FGuid& ClaimToken,
		int32 ExpectedRevision);

	/** Token-specific rollback; an old token cannot release a newer claim. */
	ESPInteractionResultCode RollbackClaim(
		APawn* Claimant,
		const FGuid& ClaimToken,
		int32 ExpectedRevision);

	virtual FText GetInteractionPrompt_Implementation(const APawn* Viewer) const override;
	virtual FVector GetInteractionLocation_Implementation() const override;
	virtual ESPInteractionResultCode ValidateInteraction(
		const APawn* RequestingPawn,
		const FSPInteractionRequest& Request) const override;

	virtual bool IsAvailableToPaperEater_Implementation() const override;
	virtual ESPThreatItemTargetKind GetThreatItemTargetKind_Implementation() const override;
	virtual FVector GetThreatItemTargetLocation_Implementation() const override;
	virtual bool RequestPaperEaterCorruption_Implementation(
		const FSPPaperCorruptionIntent& Intent) override;

private:
	UFUNCTION()
	void OnRep_ItemInstance();

	UFUNCTION()
	void OnRep_ClaimState();

	ESPInteractionResultCode ValidatePickupRequest(
		const APawn* RequestingPawn,
		const FSPInteractionRequest& Request,
		bool bAllowCurrentClaimantReplay) const;

	bool RefreshAbandonedClaim();
	void ApplyLifecyclePresentation();
	void NotifyStateChanged(const TCHAR* Operation);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UBoxComponent> InteractionBounds;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Item", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> PickupVisual;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_ItemInstance, Category = "Item", meta = (AllowPrivateAccess = "true"))
	FSPItemInstance ItemInstance;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_ClaimState, Category = "Item", meta = (AllowPrivateAccess = "true"))
	FSPWorldItemClaimState ClaimState;

	TMap<FGuid, FString> ProcessedCorruptionRequests;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Interaction", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float MaxInteractionDistance = 250.0f;
};
