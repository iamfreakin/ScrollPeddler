#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SPExtractionZone.generated.h"

class ASPCharacter;
class UBoxComponent;
class UPrimitiveComponent;
class UStaticMeshComponent;

UCLASS()
class SCROLLPEDDLER_API ASPExtractionZone : public AActor
{
	GENERATED_BODY()

public:
	ASPExtractionZone();

	/** Authority-only validation. A request outside MaxExtractionDistance is rejected. */
	bool TryExtract(ASPCharacter* Character);

	FVector GetExtractionPoint() const;
	float GetMaxExtractionDistance() const { return MaxExtractionDistance; }

private:
	UFUNCTION()
	void OnExtractionOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComponent,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, Category = "Scroll Peddler|Extraction")
	TObjectPtr<UBoxComponent> ExtractionBounds;

	UPROPERTY(VisibleAnywhere, Category = "Scroll Peddler|Extraction")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	UPROPERTY(EditDefaultsOnly, Category = "Scroll Peddler|Extraction", meta = (ClampMin = "1.0"))
	float MaxExtractionDistance = 350.0f;
};
