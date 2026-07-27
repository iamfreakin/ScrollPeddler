#include "World/SPGrayboxThreat.h"

#include "Components/StaticMeshComponent.h"
#include "Data/SPGrayboxThreatDefinition.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/SPPlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Player/SPCharacter.h"
#include "UObject/ConstructorHelpers.h"
#include "World/SPNoiseSubsystem.h"
#include "World/SPThreatTargetInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogSPGrayboxThreat, Log, All);

ASPGrayboxThreat::ASPGrayboxThreat()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
	SetReplicateMovement(true);
	SetCanBeDamaged(false);

	ThreatMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ThreatMesh"));
	SetRootComponent(ThreatMesh);
	ThreatMesh->SetMobility(EComponentMobility::Movable);
	ThreatMesh->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	ThreatMesh->SetGenerateOverlapEvents(false);
	ThreatMesh->SetIsReplicated(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		ThreatMesh->SetStaticMesh(CubeMesh.Object);
	}
	ThreatMesh->SetRelativeScale3D(FVector(0.65f, 0.65f, 1.4f));
}

void ASPGrayboxThreat::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority())
	{
		SetActorTickEnabled(false);
		return;
	}

	BindNoiseSubscription();
}

void ASPGrayboxThreat::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindNoiseSubscription();
	Super::EndPlay(EndPlayReason);
}

void ASPGrayboxThreat::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || !ThreatDefinition
		|| !ThreatDefinition->IsRuntimeDefinitionValid())
	{
		return;
	}

	const double ServerTime = GetServerTimeSeconds();
	if (BehaviorState == ESPThreatBehaviorState::Staggered
		|| BehaviorState == ESPThreatBehaviorState::Retreating)
	{
		TickStaggerOrRetreat(DeltaSeconds, ServerTime);
		return;
	}

	if (ThreatDefinition->Archetype == ESPThreatArchetype::EchoHunter)
	{
		TickEchoHunter(DeltaSeconds, ServerTime);
	}
	else
	{
		TickPaperEater(DeltaSeconds, ServerTime);
	}
}

void ASPGrayboxThreat::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASPGrayboxThreat, BehaviorState);
	DOREPLIFETIME(ASPGrayboxThreat, InvestigationLocation);
	DOREPLIFETIME(ASPGrayboxThreat, TelegraphEndServerTime);
	DOREPLIFETIME(ASPGrayboxThreat, ReplicatedTargetActor);
}

bool ASPGrayboxThreat::AuthoritySetThreatDefinition(
	USPGrayboxThreatDefinition* InDefinition)
{
	if (!HasAuthority() || !IsValid(InDefinition)
		|| !InDefinition->IsRuntimeDefinitionValid())
	{
		return false;
	}

	UnbindNoiseSubscription();
	ThreatDefinition = InDefinition;
	CurrentTargetActor.Reset();
	ReplicatedTargetActor = nullptr;
	InvestigationLocation = FVector::ZeroVector;
	TelegraphEndServerTime = 0.0;
	StateEndServerTime = 0.0;
	NextPaperScanServerTime = 0.0;
	LastInvestigatedNoiseServerTime = -1.0;
	LastInvestigatedNoiseScore = 0.0;
	BehaviorState = ESPThreatBehaviorState::Idle;
	BindNoiseSubscription();
	ForceNetUpdate();
	return true;
}

bool ASPGrayboxThreat::AuthorityStagger(const FVector EffectOrigin)
{
	if (!HasAuthority() || !ThreatDefinition
		|| !ThreatDefinition->IsRuntimeDefinitionValid()
		|| EffectOrigin.ContainsNaN())
	{
		return false;
	}

	CurrentTargetActor.Reset();
	ReplicatedTargetActor = nullptr;
	TelegraphEndServerTime = 0.0;
	RetreatOrigin = EffectOrigin;
	TransitionTo(
		ESPThreatBehaviorState::Staggered,
		GetServerTimeSeconds() + ThreatDefinition->StaggerSeconds);
	return true;
}

bool ASPGrayboxThreat::CanInvestigateNoise(
	const FSPNoiseEvent& NoiseEvent) const
{
	return ThreatDefinition
		&& ThreatDefinition->Archetype == ESPThreatArchetype::EchoHunter
		&& NoiseEvent.IsValid()
		&& SPShouldInvestigateNoise(
			GetActorLocation(),
			NoiseEvent.Location,
			NoiseEvent.Loudness,
			NoiseEvent.Radius,
			ThreatDefinition->MinimumInvestigatedLoudness,
			ThreatDefinition->MaximumHearingRadius,
			ThreatDefinition->HearingRadiusScale);
}

void ASPGrayboxThreat::BindNoiseSubscription()
{
	if (!HasAuthority() || !GetWorld() || !ThreatDefinition
		|| ThreatDefinition->Archetype != ESPThreatArchetype::EchoHunter
		|| NoiseDelegateHandle.IsValid())
	{
		return;
	}

	USPNoiseSubsystem* Subsystem =
		GetWorld()->GetSubsystem<USPNoiseSubsystem>();
	if (!Subsystem || !Subsystem->IsAuthorityWorld())
	{
		return;
	}

	NoiseSubsystem = Subsystem;
	NoiseDelegateHandle =
		Subsystem->OnNoisePublishedNative().AddUObject(
			this,
			&ASPGrayboxThreat::HandleNoisePublished);
}

void ASPGrayboxThreat::UnbindNoiseSubscription()
{
	if (NoiseSubsystem.IsValid() && NoiseDelegateHandle.IsValid())
	{
		NoiseSubsystem->OnNoisePublishedNative().Remove(NoiseDelegateHandle);
	}
	NoiseDelegateHandle.Reset();
	NoiseSubsystem.Reset();
}

void ASPGrayboxThreat::HandleNoisePublished(
	const FSPNoiseEvent& NoiseEvent)
{
	if (!HasAuthority() || !CanInvestigateNoise(NoiseEvent)
		|| NoiseEvent.SourceActor.Get() == this
		|| BehaviorState == ESPThreatBehaviorState::Pursuing
		|| BehaviorState == ESPThreatBehaviorState::Telegraphing
		|| BehaviorState == ESPThreatBehaviorState::Staggered
		|| BehaviorState == ESPThreatBehaviorState::Retreating)
	{
		return;
	}

	const double Score =
		static_cast<double>(NoiseEvent.Loudness)
		* static_cast<double>(NoiseEvent.Radius);
	if (NoiseEvent.ServerTimeSeconds < LastInvestigatedNoiseServerTime
		|| (FMath::IsNearlyEqual(
				NoiseEvent.ServerTimeSeconds,
				LastInvestigatedNoiseServerTime)
			&& Score <= LastInvestigatedNoiseScore))
	{
		return;
	}

	LastInvestigatedNoiseServerTime = NoiseEvent.ServerTimeSeconds;
	LastInvestigatedNoiseScore = Score;
	InvestigationLocation = NoiseEvent.Location;
	CurrentTargetActor.Reset();
	ReplicatedTargetActor = nullptr;
	TransitionTo(ESPThreatBehaviorState::Investigating);
}

void ASPGrayboxThreat::TickEchoHunter(
	const float DeltaSeconds,
	const double ServerTime)
{
	switch (BehaviorState)
	{
	case ESPThreatBehaviorState::Idle:
		break;
	case ESPThreatBehaviorState::Investigating:
		if (APawn* VisiblePlayer = FindNearestVisiblePlayer())
		{
			CurrentTargetActor = VisiblePlayer;
			ReplicatedTargetActor = VisiblePlayer;
			TransitionTo(ESPThreatBehaviorState::Pursuing);
			break;
		}

		MoveToward(
			InvestigationLocation,
			ThreatDefinition->MoveSpeed,
			DeltaSeconds);
		if (FVector::DistSquared(
				GetActorLocation(),
				InvestigationLocation)
			<= FMath::Square(
				ThreatDefinition->InvestigationAcceptanceRadius))
		{
			TransitionTo(ESPThreatBehaviorState::Idle);
		}
		break;
	case ESPThreatBehaviorState::Pursuing:
		{
			APawn* TargetPawn = Cast<APawn>(CurrentTargetActor.Get());
			if (!IsPlayerTargetEligible(TargetPawn))
			{
				CurrentTargetActor.Reset();
				ReplicatedTargetActor = nullptr;
				TransitionTo(ESPThreatBehaviorState::Idle);
				break;
			}

			if (!HasLineOfSightTo(TargetPawn, false))
			{
				InvestigationLocation = TargetPawn->GetActorLocation();
				CurrentTargetActor.Reset();
				ReplicatedTargetActor = nullptr;
				TransitionTo(ESPThreatBehaviorState::Investigating);
				break;
			}

			InvestigationLocation = TargetPawn->GetActorLocation();
			const double DistanceSquared = FVector::DistSquared(
				GetActorLocation(),
				TargetPawn->GetActorLocation());
			if (DistanceSquared
				<= FMath::Square(ThreatDefinition->AttackRange))
			{
				BeginTelegraphedAttack(TargetPawn, ServerTime);
			}
			else
			{
				MoveToward(
					TargetPawn->GetActorLocation(),
					ThreatDefinition->MoveSpeed,
					DeltaSeconds);
			}
		}
		break;
	case ESPThreatBehaviorState::Telegraphing:
		if (ServerTime >= TelegraphEndServerTime)
		{
			ResolveTelegraphedAttack(ServerTime);
		}
		break;
	case ESPThreatBehaviorState::Recovering:
		if (ServerTime >= StateEndServerTime)
		{
			TransitionTo(ESPThreatBehaviorState::Idle);
		}
		break;
	default:
		break;
	}
}

void ASPGrayboxThreat::TickPaperEater(
	const float DeltaSeconds,
	const double ServerTime)
{
	if (BehaviorState == ESPThreatBehaviorState::Recovering)
	{
		if (ServerTime >= StateEndServerTime)
		{
			TransitionTo(ESPThreatBehaviorState::Idle);
		}
		return;
	}

	if (BehaviorState == ESPThreatBehaviorState::Idle)
	{
		if (ServerTime < NextPaperScanServerTime)
		{
			return;
		}

		NextPaperScanServerTime =
			ServerTime + ThreatDefinition->ItemScanIntervalSeconds;
		if (AActor* Target = FindBestPaperTarget())
		{
			CurrentTargetActor = Target;
			ReplicatedTargetActor = Target;
			TransitionTo(ESPThreatBehaviorState::Pursuing);
		}
		return;
	}

	if (BehaviorState != ESPThreatBehaviorState::Pursuing)
	{
		return;
	}

	AActor* TargetActor = CurrentTargetActor.Get();
	if (!IsPaperTargetAvailable(TargetActor))
	{
		CurrentTargetActor.Reset();
		ReplicatedTargetActor = nullptr;
		TransitionTo(ESPThreatBehaviorState::Idle);
		return;
	}

	const FVector TargetLocation =
		ISPThreatItemTarget::Execute_GetThreatItemTargetLocation(TargetActor);
	if (FVector::DistSquared(GetActorLocation(), TargetLocation)
		<= FMath::Square(ThreatDefinition->ItemInteractionRange))
	{
		RequestPaperCorruption(TargetActor, ServerTime);
		return;
	}

	MoveToward(TargetLocation, ThreatDefinition->MoveSpeed, DeltaSeconds);
}

void ASPGrayboxThreat::TickStaggerOrRetreat(
	const float DeltaSeconds,
	const double ServerTime)
{
	if (BehaviorState == ESPThreatBehaviorState::Staggered)
	{
		if (ServerTime >= StateEndServerTime)
		{
			TransitionTo(
				ESPThreatBehaviorState::Retreating,
				ServerTime + ThreatDefinition->RetreatSeconds);
		}
		return;
	}

	MoveAwayFrom(
		RetreatOrigin,
		ThreatDefinition->RetreatSpeed,
		DeltaSeconds);
	if (ServerTime >= StateEndServerTime)
	{
		TransitionTo(ESPThreatBehaviorState::Idle);
	}
}

void ASPGrayboxThreat::TransitionTo(
	const ESPThreatBehaviorState NewState,
	const double UntilServerTime)
{
	BehaviorState = NewState;
	StateEndServerTime = FMath::Max(0.0, UntilServerTime);
	if (NewState != ESPThreatBehaviorState::Telegraphing)
	{
		TelegraphEndServerTime = 0.0;
	}
	ForceNetUpdate();
}

void ASPGrayboxThreat::MoveToward(
	const FVector& Destination,
	const float Speed,
	const float DeltaSeconds)
{
	FVector Direction = Destination - GetActorLocation();
	Direction.Z = 0.0;
	if (!Direction.Normalize())
	{
		return;
	}

	const FVector Delta =
		Direction * Speed * FMath::Max(0.0f, DeltaSeconds);
	FHitResult Hit;
	SetActorLocation(GetActorLocation() + Delta, true, &Hit);
	SetActorRotation(Direction.Rotation());
}

void ASPGrayboxThreat::MoveAwayFrom(
	const FVector& Origin,
	const float Speed,
	const float DeltaSeconds)
{
	FVector Direction = GetActorLocation() - Origin;
	Direction.Z = 0.0;
	if (!Direction.Normalize())
	{
		Direction = -GetActorForwardVector().GetSafeNormal2D();
	}
	MoveToward(
		GetActorLocation() + Direction * 1000.0f,
		Speed,
		DeltaSeconds);
}

APawn* ASPGrayboxThreat::FindNearestVisiblePlayer() const
{
	UWorld* World = GetWorld();
	if (!World || !ThreatDefinition)
	{
		return nullptr;
	}

	APawn* BestPawn = nullptr;
	double BestDistanceSquared =
		FMath::Square(
			static_cast<double>(ThreatDefinition->ConfirmationSightRadius));
	for (TActorIterator<APawn> Iterator(World); Iterator; ++Iterator)
	{
		APawn* Pawn = *Iterator;
		const double DistanceSquared = FVector::DistSquared(
			GetActorLocation(),
			Pawn->GetActorLocation());
		if (DistanceSquared <= BestDistanceSquared
			&& IsPlayerTargetEligible(Pawn)
			&& HasLineOfSightTo(Pawn, true))
		{
			BestPawn = Pawn;
			BestDistanceSquared = DistanceSquared;
		}
	}
	return BestPawn;
}

bool ASPGrayboxThreat::IsPlayerTargetEligible(const APawn* Pawn) const
{
	const ASPPlayerState* PlayerState = Pawn
		? Cast<ASPPlayerState>(Pawn->GetPlayerState())
		: nullptr;
	if (!PlayerState
		|| PlayerState->GetPlayerCondition() == ESPPlayerCondition::Missing
		|| PlayerState->IsExtracted())
	{
		return false;
	}

	const ESPParticipationState Participation =
		PlayerState->GetParticipationState();
	return Participation == ESPParticipationState::Active;
}

bool ASPGrayboxThreat::HasLineOfSightTo(
	const AActor* TargetActor,
	const bool bRequireViewCone) const
{
	const UWorld* World = GetWorld();
	if (!World || !TargetActor || !ThreatDefinition)
	{
		return false;
	}

	const FVector Start = GetActorLocation() + FVector(0.0, 0.0, 55.0);
	const FVector End =
		TargetActor->GetActorLocation() + FVector(0.0, 0.0, 50.0);
	const FVector ToTarget = (End - Start).GetSafeNormal();
	if (bRequireViewCone)
	{
		const double MinimumDot = FMath::Cos(
			FMath::DegreesToRadians(
				static_cast<double>(
					ThreatDefinition->ConfirmationHalfAngleDegrees)));
		if (FVector::DotProduct(
				GetActorForwardVector().GetSafeNormal(),
				ToTarget) < MinimumDot)
		{
			return false;
		}
	}

	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(SPThreatSight),
		false,
		this);
	QueryParams.AddIgnoredActor(this);
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(
		Hit,
		Start,
		End,
		ECC_Visibility,
		QueryParams);
	return !bHit || Hit.GetActor() == TargetActor;
}

void ASPGrayboxThreat::BeginTelegraphedAttack(
	APawn* TargetPawn,
	const double ServerTime)
{
	if (!TargetPawn || !ThreatDefinition)
	{
		return;
	}

	CurrentTargetActor = TargetPawn;
	ReplicatedTargetActor = TargetPawn;
	TelegraphEndServerTime =
		ServerTime + ThreatDefinition->AttackPattern.TelegraphSeconds;
	BehaviorState = ESPThreatBehaviorState::Telegraphing;
	StateEndServerTime = TelegraphEndServerTime;
	ForceNetUpdate();
}

void ASPGrayboxThreat::ResolveTelegraphedAttack(
	const double ServerTime)
{
	APawn* TargetPawn = Cast<APawn>(CurrentTargetActor.Get());
	ASPPlayerState* PlayerState = TargetPawn
		? Cast<ASPPlayerState>(TargetPawn->GetPlayerState())
		: nullptr;
	const bool bInRange = TargetPawn && ThreatDefinition
		&& FVector::DistSquared(
			GetActorLocation(),
			TargetPawn->GetActorLocation())
			<= FMath::Square(ThreatDefinition->AttackRange);
	if (!PlayerState || !IsPlayerTargetEligible(TargetPawn)
		|| !bInRange || !HasLineOfSightTo(TargetPawn, false))
	{
		if (TargetPawn)
		{
			InvestigationLocation = TargetPawn->GetActorLocation();
		}
		CurrentTargetActor.Reset();
		ReplicatedTargetActor = nullptr;
		TransitionTo(ESPThreatBehaviorState::Investigating);
		return;
	}

	const FSPThreatAttackPattern& Pattern =
		ThreatDefinition->AttackPattern;
	if (ASPCharacter* TargetCharacter =
		Cast<ASPCharacter>(TargetPawn);
		TargetCharacter
		&& TargetCharacter->AuthorityConsumeScrollProtection())
	{
		UE_LOG(LogSPGrayboxThreat, Display,
			TEXT("SP_THREAT_ATTACK_BLOCKED threat=%s target=%s"),
			*GetNameSafe(this),
			*GetNameSafe(TargetCharacter));
		CurrentTargetActor.Reset();
		ReplicatedTargetActor = nullptr;
		TransitionTo(
			ESPThreatBehaviorState::Recovering,
			ServerTime + ThreatDefinition->AttackRecoverySeconds);
		return;
	}

	if (PlayerState->AuthorityTransitionCondition(Pattern.ResultCondition))
	{
		FSPThreatAttackIntent Intent;
		Intent.IntentId = FGuid::NewGuid();
		Intent.ThreatActor = this;
		Intent.TargetActor = TargetPawn;
		Intent.ResultCondition = Pattern.ResultCondition;
		Intent.bRequestHandItemDrop = Pattern.bDropHandItem;
		Intent.ImpactLocation = TargetPawn->GetActorLocation();
		Intent.ServerTimeSeconds = ServerTime;
		OnAttackIntent.Broadcast(Intent);
	}

	CurrentTargetActor.Reset();
	ReplicatedTargetActor = nullptr;
	TransitionTo(
		ESPThreatBehaviorState::Recovering,
		ServerTime + ThreatDefinition->AttackRecoverySeconds);
}

AActor* ASPGrayboxThreat::FindBestPaperTarget() const
{
	UWorld* World = GetWorld();
	if (!World || !ThreatDefinition)
	{
		return nullptr;
	}

	AActor* BestTarget = nullptr;
	double BestPriority = -1.0;
	const double MaximumDistanceSquared =
		FMath::Square(
			static_cast<double>(ThreatDefinition->ItemSenseRadius));
	for (TActorIterator<AActor> Iterator(World); Iterator; ++Iterator)
	{
		AActor* Candidate = *Iterator;
		if (Candidate == this || !IsPaperTargetAvailable(Candidate))
		{
			continue;
		}

		const FVector TargetLocation =
			ISPThreatItemTarget::Execute_GetThreatItemTargetLocation(
				Candidate);
		const double DistanceSquared = FVector::DistSquared(
			GetActorLocation(),
			TargetLocation);
		if (DistanceSquared > MaximumDistanceSquared)
		{
			continue;
		}

		const ESPThreatItemTargetKind Kind =
			ISPThreatItemTarget::Execute_GetThreatItemTargetKind(
				Candidate);
		const double Priority =
			SPCalculatePaperTargetPriority(Kind, DistanceSquared);
		if (Priority > BestPriority)
		{
			BestPriority = Priority;
			BestTarget = Candidate;
		}
	}
	return BestTarget;
}

bool ASPGrayboxThreat::IsPaperTargetAvailable(
	const AActor* TargetActor) const
{
	return IsValid(TargetActor)
		&& TargetActor->GetClass()->ImplementsInterface(
			USPThreatItemTarget::StaticClass())
		&& ISPThreatItemTarget::Execute_IsAvailableToPaperEater(
			const_cast<AActor*>(TargetActor));
}

void ASPGrayboxThreat::RequestPaperCorruption(
	AActor* TargetActor,
	const double ServerTime)
{
	if (!IsPaperTargetAvailable(TargetActor) || !ThreatDefinition)
	{
		CurrentTargetActor.Reset();
		ReplicatedTargetActor = nullptr;
		TransitionTo(ESPThreatBehaviorState::Idle);
		return;
	}

	FSPPaperCorruptionIntent Intent;
	Intent.RequestId = FGuid::NewGuid();
	Intent.ThreatActor = this;
	Intent.TargetActor = TargetActor;
	Intent.TargetKind =
		ISPThreatItemTarget::Execute_GetThreatItemTargetKind(TargetActor);
	Intent.ContaminationDelta = ThreatDefinition->ContaminationDelta;
	Intent.bRequestItemDamage = ThreatDefinition->bRequestItemDamage;
	Intent.ServerTimeSeconds = ServerTime;
	OnPaperCorruptionIntent.Broadcast(Intent);

	const bool bAccepted =
		ISPThreatItemTarget::Execute_RequestPaperEaterCorruption(
			TargetActor,
			Intent);
	CurrentTargetActor.Reset();
	ReplicatedTargetActor = nullptr;
	TransitionTo(
		bAccepted
			? ESPThreatBehaviorState::Recovering
			: ESPThreatBehaviorState::Idle,
		bAccepted
			? ServerTime + ThreatDefinition->CorruptionRecoverySeconds
			: 0.0);
}

double ASPGrayboxThreat::GetServerTimeSeconds() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0;
	}

	if (const AGameStateBase* GameState = World->GetGameState())
	{
		return GameState->GetServerWorldTimeSeconds();
	}
	return static_cast<double>(World->GetTimeSeconds());
}
