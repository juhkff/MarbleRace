#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MarbleRaceGameMode.generated.h"

class ACameraActor;
class AMarbleRaceDirector;

/** 比赛关卡的游戏模式：找导演、跟镜头。 */
UCLASS(meta=(DisplayName="比赛模式"))
class MARBLERACE_API AMarbleRaceGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMarbleRaceGameMode();
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

	/** 可选的导演子类。留空则使用 C++ 导演。 */
	UPROPERTY(EditDefaultsOnly, Category="比赛", meta=(DisplayName="导演类"))
	TSubclassOf<AMarbleRaceDirector> DirectorClass;

	/** 镜头在所跟小球下方多远，用来露出前方赛道。 */
	UPROPERTY(EditDefaultsOnly, Category="比赛|镜头", meta=(DisplayName="镜头下移"))
	float CameraFollowOffsetZ = 200.0f;

	/**
	 * 跟随快慢。太低时球会先冲出画面上沿，镜头才赶上来。
	 * 调高会跟得更紧、更少漂。
	 */
	UPROPERTY(EditDefaultsOnly, Category="比赛|镜头", meta=(DisplayName="镜头跟随速度", ClampMin="1.0", ClampMax="40.0"))
	float CameraFollowInterpSpeed = 10.0f;

private:
	TWeakObjectPtr<class ACameraActor> FollowCamera;
	TWeakObjectPtr<AActor> RaceMarble;
	TWeakObjectPtr<AMarbleRaceDirector> RaceDirector;

	/** 找到关卡里的导演。没有就在运行时生成一个，旧地图也能开赛。 */
	void ResolveDirector();
};
