#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SPGrayboxLighting.generated.h"

class UDirectionalLightComponent;
class USceneComponent;
class USkyLightComponent;

/** Reproducible runtime lighting for the code-generated technical-spike graybox. */
UCLASS()
class SCROLLPEDDLER_API ASPGrayboxLighting : public AActor
{
	GENERATED_BODY()

public:
	ASPGrayboxLighting();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Graybox|Lighting")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Graybox|Lighting")
	TObjectPtr<UDirectionalLightComponent> DirectionalLight;

	UPROPERTY(VisibleAnywhere, Category = "Graybox|Lighting")
	TObjectPtr<USkyLightComponent> SkyLight;
};
