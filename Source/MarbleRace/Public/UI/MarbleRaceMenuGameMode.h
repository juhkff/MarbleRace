#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "MarbleRaceMenuGameMode.generated.h"

/**
 * 主菜单关卡的游戏模式。
 *
 * 菜单是纯画布界面：这个关卡没有导演、没有小球、也没有玩家Pawn。
 * 名单放在游戏实例子系统里，这样进入比赛关卡后仍然还在。
 * 镜头不用动，因为菜单会画一整屏不透明的背景盖住它。
 */
UCLASS(meta=(DisplayName="主菜单模式"))
class MARBLERACE_API AMarbleRaceMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AMarbleRaceMenuGameMode();

protected:
	virtual void BeginPlay() override;
};
