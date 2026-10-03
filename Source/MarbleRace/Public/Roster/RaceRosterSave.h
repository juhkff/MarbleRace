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
	/** 保持旧读取默认值：旧存档可能省略与默认值相同的字段。新存档显式写入版本 2。 */
	UPROPERTY(SaveGame)
	int32 Version = 1;

	UPROPERTY(SaveGame)
	TArray<FRaceCharacterEntry> Entries;
};
