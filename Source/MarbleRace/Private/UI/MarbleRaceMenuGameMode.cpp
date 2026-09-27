// 比赛与主菜单的游戏模式。


#include "UI/MarbleRaceMenuGameMode.h"

#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "UI/MarbleRaceMenuHUD.h"
#include "Roster/MarbleRaceRosterSubsystem.h"

AMarbleRaceMenuGameMode::AMarbleRaceMenuGameMode()
{
	// 这一关不需要游戏模式每帧更新；菜单由界面驱动。
	PrimaryActorTick.bCanEverTick = false;

	// 没有棋子：菜单用鼠标操作，键盘进界面上的文本框。
	DefaultPawnClass = nullptr;
	HUDClass = AMarbleRaceMenuHUD::StaticClass();

	// 和比赛模式最终用的是同一控制器类（基类默认）；写明
	// 是为了默认值以后变了，两关也不会分开。
	PlayerControllerClass = APlayerController::StaticClass();

	// 以观察者开局，没有棋子是预期情况，而不是生成失败。
	bStartPlayersAsSpectators = true;
}

void AMarbleRaceMenuGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 记一次日志，用来确认菜单关卡启动时用的就是
	// 比赛要用的那份名单；名单归游戏实例，切关卡还在。
	if (const UGameInstance* MenuGameInstance = GetGameInstance())
	{
		if (const UMarbleRaceRosterSubsystem* Roster = MenuGameInstance->GetSubsystem<UMarbleRaceRosterSubsystem>())
		{
			UE_LOG(LogTemp, Log, TEXT("主菜单关卡已启动：角色名单共 %d 个小球"), Roster->GetEntryCount());
			return;
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("主菜单关卡已启动，但没有找到角色名单子系统"));
}
