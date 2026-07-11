#include "World/SPGrayboxLighting.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/TextureCube.h"
#include "UObject/ConstructorHelpers.h"

ASPGrayboxLighting::ASPGrayboxLighting()
{
	PrimaryActorTick.bCanEverTick = false;
	bAlwaysRelevant = true;
	SetReplicates(true);
	SetReplicateMovement(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	DirectionalLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("DirectionalLight"));
	DirectionalLight->SetupAttachment(SceneRoot);
	DirectionalLight->SetRelativeRotation(FRotator(-48.0f, -32.0f, 0.0f));
	DirectionalLight->SetMobility(EComponentMobility::Movable);
	DirectionalLight->SetIntensity(8.0f);
	DirectionalLight->SetLightColor(FLinearColor(1.0f, 0.93f, 0.82f));
	DirectionalLight->SetCastShadows(true);
	DirectionalLight->SetAtmosphereSunLight(true);
	DirectionalLight->SetIsReplicated(true);

	SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
	SkyLight->SetupAttachment(SceneRoot);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SetIntensity(0.8f);
	SkyLight->SetRealTimeCapture(false);
	SkyLight->SetIsReplicated(true);

	static ConstructorHelpers::FObjectFinder<UTextureCube> AmbientCube(
		TEXT("/Engine/EngineResources/GrayLightTextureCube.GrayLightTextureCube"));
	if (AmbientCube.Succeeded())
	{
		SkyLight->SourceType = SLS_SpecifiedCubemap;
		SkyLight->Cubemap = AmbientCube.Object;
	}
}

void ASPGrayboxLighting::BeginPlay()
{
	Super::BeginPlay();

	if (SkyLight)
	{
		SkyLight->RecaptureSky();
	}
}
