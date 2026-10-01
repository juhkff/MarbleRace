#include "MarbleRestitution.h"

#include "Components/PrimitiveComponent.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

bool MarbleRace::SetBodyRestitution(UPrimitiveComponent* Body, const float Restitution)
{
	if (!IsValid(Body) || !Body->IsSimulatingPhysics() || !FMath::IsFinite(Restitution))
	{
		return false;
	}
	const FBodyInstance* Instance = Body->GetBodyInstance();
	UPhysicalMaterial* SourceMaterial = Instance ? Instance->GetSimplePhysicalMaterial() : nullptr;
	if (!IsValid(SourceMaterial))
	{
		return false;
	}

	// Share the same per-body override across detection lines and teleport entrances.
	UPhysicalMaterial* RuntimeMaterial = SourceMaterial;
	if (SourceMaterial->GetOuter() != Body || !SourceMaterial->HasAnyFlags(RF_Transient) ||
		!SourceMaterial->GetName().StartsWith(TEXT("CrossingRestitution")))
	{
		RuntimeMaterial = DuplicateObject<UPhysicalMaterial>(SourceMaterial, Body,
			MakeUniqueObjectName(Body, SourceMaterial->GetClass(), TEXT("CrossingRestitution")));
		RuntimeMaterial->ClearFlags(RF_Public | RF_Standalone);
		RuntimeMaterial->SetFlags(RF_Transient);
	}
	RuntimeMaterial->Restitution = FMath::Clamp(Restitution, 0.f, 1.f);
	FChaosEngineInterface::UpdateMaterial(RuntimeMaterial->GetPhysicsMaterial(), RuntimeMaterial);
	Body->SetPhysMaterialOverride(RuntimeMaterial);
	return true;
}
