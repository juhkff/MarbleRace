#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Roster/RaceRosterTypes.h"
#include "RaceRosterSave.generated.h"

/** 从游戏内角色设置页编辑、并保存下来的小球名单。 */
UCLASS(meta=(DisplayName="角色名单存档"))
class MARBLERACE_API URaceRosterSave : public USaveGame
{
	GENERATED_BODY()

public:
	/** 条目结构变化时把版本号加一，以便迁移或忽略旧存档。 */
	UPROPERTY(SaveGame)
	int32 Version = 1;

	UPROPERTY(SaveGame)
	TArray<FRaceCharacterEntry> Entries;
};
