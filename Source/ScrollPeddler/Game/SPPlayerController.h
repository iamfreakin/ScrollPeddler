#pragma once

#include "CoreMinimal.h"
#include "Core/SPTypes.h"
#include "GameFramework/PlayerController.h"
#include "SPPlayerController.generated.h"

UCLASS()
class SCROLLPEDDLER_API ASPPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** Starts a raw listen server on the spike map. No online service is involved. */
	UFUNCTION(Exec)
	void SPHost(int32 InExpectedPlayers = 2);

	/** Travels directly to an IP or Unreal travel URL. */
	UFUNCTION(Exec)
	void SPJoin(const FString& Address);

	UFUNCTION(Client, Reliable)
	void ClientCommitSessionResult(const FSPSessionResult& Result);

	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeSessionResult(FGuid SessionId, const FString& ResultHash, bool bSaved);

protected:
	virtual void BeginPlay() override;

private:
	enum class EAutoSpikeStep : uint8
	{
		WaitingForPawn,
		WaitingForPickup,
		WaitingForInventory,
		WaitingForConsumption,
		WaitingForSettlement,
		Finished
	};

	void StartAutoSpikeIfRequested();
	void RunAutoSpikeStep();
	void ScheduleAutoQuitIfRequested();
	void RequestAutoQuit();

	FTimerHandle AutoSpikeTimerHandle;
	FTimerHandle AutoQuitTimerHandle;
	EAutoSpikeStep AutoSpikeStep = EAutoSpikeStep::WaitingForPawn;
	double AutoStepStartedAtSeconds = 0.0;
	double AutoContestedBarrierStartedAtSeconds = 0.0;
	bool bAutoContestedAttemptMade = false;
};
