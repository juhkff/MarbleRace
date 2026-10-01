#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MarbleMovementLibrary.generated.h"

class AActor;

/** Coordinate conversion for movement settings authored in an owning level Blueprint. */
UCLASS()
class MARBLERACE_API UMarbleMovementLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Child/attached actors: endpoint is in the attachment parent's coordinate frame.
	 * Standalone actors: retain the legacy world-axis offset from actor location.
	 */
	UFUNCTION(BlueprintPure, Category="Movement", meta=(DisplayName="Resolve Movement Endpoint"))
	static FVector ResolveMovementEndpoint(AActor* MovingActor, FVector Endpoint);
};
