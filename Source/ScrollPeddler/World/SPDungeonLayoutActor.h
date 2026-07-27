#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "World/SPDungeonLayoutTypes.h"
#include "SPDungeonLayoutActor.generated.h"

class USPDungeonSeedDefinition;
class USceneComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FSPDungeonLayoutChangedSignature,
	const FSPDungeonLayout&,
	Layout);

/**
 * Replicates the server-generated dungeon recipe. It intentionally owns no
 * room meshes; the map/spawn layer consumes the immutable layout snapshot.
 */
UCLASS()
class SCROLLPEDDLER_API ASPDungeonLayoutActor : public AActor
{
	GENERATED_BODY()

public:
	ASPDungeonLayoutActor();

	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "Scroll Peddler|Dungeon")
	bool AuthorityGenerateLayout(
		const USPDungeonSeedDefinition* Definition,
		FSPDungeonLayoutValidationReport& OutReport);

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Dungeon")
	bool HasGeneratedLayout() const { return !Layout.Rooms.IsEmpty(); }

	UFUNCTION(BlueprintPure, Category = "Scroll Peddler|Dungeon")
	FSPDungeonLayout GetLayoutCopy() const { return Layout; }

	const FSPDungeonLayout& GetLayout() const { return Layout; }

	UPROPERTY(BlueprintAssignable, Category = "Scroll Peddler|Dungeon")
	FSPDungeonLayoutChangedSignature OnLayoutChanged;

private:
	UFUNCTION()
	void OnRep_Layout();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Scroll Peddler|Dungeon", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(ReplicatedUsing = OnRep_Layout)
	FSPDungeonLayout Layout;
};
