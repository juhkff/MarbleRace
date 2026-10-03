#include "Race/MarbleWallResponseComponent.h"

#include "Components/ChildActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "PhysicsEngine/BodyInstance.h"
#include "UObject/ConstructorHelpers.h"

UMarbleWallResponseComponent::UMarbleWallResponseComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	static ConstructorHelpers::FClassFinder<AActor> MarbleFinder(TEXT("/Game/角色/弹珠"));
	if (MarbleFinder.Succeeded()) MarbleClass = MarbleFinder.Class;
}

void UMarbleWallResponseComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bEnabled && WallComponentNames.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s 碰墙反弹已启用，但墙组件名称为空，未监听墙壁碰撞"),
			*GetOwner()->GetName());
	}
	TArray<UChildActorComponent*> Children;
	GetOwner()->GetComponents(Children);
	for (UChildActorComponent* Child : Children)
	{
		if (!Child || !WallComponentNames.Contains(Child->GetFName())) continue;
		AActor* WallActor = Child->GetChildActor();
		UPrimitiveComponent* Wall = WallActor ? Cast<UPrimitiveComponent>(WallActor->GetRootComponent()) : nullptr;
		if (!Wall) continue;
		Walls.Add(Wall, Wall->BodyInstance.bNotifyRigidBodyCollision);
		Wall->SetNotifyRigidBodyCollision(true);
		Wall->OnComponentHit.AddDynamic(this, &UMarbleWallResponseComponent::HandleWallHit);
	}
	BoundWallCount = Walls.Num();
	if (Walls.Num() != WallComponentNames.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("%s 碰墙反弹：找到 %d / %d 个墙组件"),
			*GetOwner()->GetName(), Walls.Num(), WallComponentNames.Num());
	}
}

void UMarbleWallResponseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	IncomingVelocities.Empty();
	if (!bEnabled || !MarbleClass || Walls.IsEmpty()) return;
	// Hits are reported after the solver changes velocity; remember the incoming speed.
	for (TActorIterator<AActor> It(GetWorld(), MarbleClass); It; ++It)
	{
		UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(It->GetRootComponent());
		if (Body && !It->IsHidden() && Body->IsSimulatingPhysics())
			IncomingVelocities.Add(Body, Body->GetPhysicsLinearVelocity());
	}
	for (auto It = LastBounceTimes.CreateIterator(); It; ++It)
		if (!It.Key().IsValid()) It.RemoveCurrent();
}

void UMarbleWallResponseComponent::HandleWallHit(UPrimitiveComponent* Wall, AActor* OtherActor,
	UPrimitiveComponent* Body, FVector NormalImpulse, const FHitResult& Hit)
{
	if (!bEnabled || !Walls.Contains(Wall) || !IsValid(OtherActor) || !MarbleClass ||
		!OtherActor->IsA(MarbleClass) || OtherActor->IsHidden() || !IsValid(Body) ||
		Body->GetOwner() != OtherActor || Body != OtherActor->GetRootComponent() || !Body->IsSimulatingPhysics()) return;

	FVector Normal = Hit.ImpactNormal.GetSafeNormal();
	if (Normal.ContainsNaN() || FMath::Abs(Normal.X) < .8) return; // Ignore the tops and ends of the walls.
	if (FVector::DotProduct(Normal, Body->GetComponentLocation() - Wall->GetComponentLocation()) < 0.) Normal *= -1.;
	Normal = FVector(FMath::Sign(Normal.X), 0., 0.);
	const FVector Current = Body->GetPhysicsLinearVelocity();
	const FVector* Cached = IncomingVelocities.Find(Body);
	const double ImpactSpeed = -FVector::DotProduct(Cached ? *Cached : Current, Normal);
	if (!FMath::IsFinite(ImpactSpeed) || ImpactSpeed < 40.) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (const double* Last = LastBounceTimes.Find(Body); Last && Now - *Last < .12) return;
	LastBounceTimes.Add(Body, Now);
	++BounceCount;

	const float MaxSpeed = FMath::Max(0.f, MaximumBounceSpeed);
	const double Bounce = FMath::Clamp(ImpactSpeed * FMath::Clamp(BounceRatio, 0.f, 1.f),
		static_cast<double>(FMath::Clamp(MinimumBounceSpeed, 0.f, MaxSpeed)), static_cast<double>(MaxSpeed));
	FVector Tangent = Current - FVector::DotProduct(Current, Normal) * Normal;
	Tangent.Y = 0.;
	Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	Body->SetPhysicsLinearVelocity(Normal * Bounce + Tangent * FMath::Clamp(TangentialVelocityRetention, 0.f, 1.f));
}

void UMarbleWallResponseComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const auto& Entry : Walls)
	{
		if (UPrimitiveComponent* Wall = Entry.Key.Get())
		{
			Wall->OnComponentHit.RemoveDynamic(this, &UMarbleWallResponseComponent::HandleWallHit);
			Wall->SetNotifyRigidBodyCollision(Entry.Value);
		}
	}
	Walls.Empty();
	BoundWallCount = 0;
	IncomingVelocities.Empty();
	LastBounceTimes.Empty();
	Super::EndPlay(EndPlayReason);
}
