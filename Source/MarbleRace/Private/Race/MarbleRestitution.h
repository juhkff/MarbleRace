#pragma once

#include "CoreMinimal.h"

class UPrimitiveComponent;

namespace MarbleRace
{
	/** Change a simulated body's restitution through an isolated transient material override. */
	bool SetBodyRestitution(UPrimitiveComponent* Body, float Restitution);
}
