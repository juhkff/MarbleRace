#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MarbleRaceLevelGameMode.generated.h"

/**
 * 比赛关卡的游戏模式。
 * 不画主菜单。开局把画面切到关卡里已有的摄像机上。
 */
UCLASS(meta=(DisplayName="比赛关卡模式"))
class MARBLERACE_API AMarbleRaceLevelGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMarbleRaceLevelGameMode();

protected:
	virtual void BeginPlay() override;
};
