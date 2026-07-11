#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SPGrayboxBlock.generated.h"

class UStaticMeshComponent;

UCLASS()
class SCROLLPEDDLER_API ASPGrayboxBlock : public AActor
{
	GENERATED_BODY()

public:
	ASPGrayboxBlock();

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Graybox", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UStaticMeshComponent> BlockMesh;
};
