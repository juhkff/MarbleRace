# MarbleRace

Unreal Engine 5.8 弹珠竞赛框架，包含角色配置、领跑 BGM、游戏设置、冲线排行、镜头跟随及可选视频录制。

仓库保留源码、地图、赛道组件和通用材质。角色图片、音乐、主题配置及录制编码程序由各使用者自行准备，不参与 Git 提交。新克隆的项目使用 12 颗无头像、无 BGM 的彩色弹珠，可以直接比赛。

## 名单与素材

`Config/Roster/default_roster.json` 是通用模板。复制为 `Config/Roster/local_roster.json` 后可配置自己的名单；这个本地文件会被 Git 忽略。每个角色必需 `name`，其余字段可省略：

```json
{
  "version": 1,
  "characters": [
    {
      "id": "example",
      "name": "示例角色",
      "color": "#3370DB",
      "portrait_asset": "/Game/LocalMedia/Portraits/T_Example.T_Example",
      "music_asset": "/Game/LocalMedia/Music/BGM_Example.BGM_Example",
      "theme_title": "示例歌曲",
      "enabled": true
    }
  ]
}
```

UE 中导入的个人素材可放在 `Content/LocalMedia/`，原始文件可放在 `SourceAssets/LocalMedia/`，两处均被忽略。主题曲可以在角色设置中选择工程内的音频，头像可以从 `Saved/Portraits/` 导入。需要打包个人素材时，在项目的打包设置中将相应资源目录加入“始终烘焙的目录”。

默认名单依次尝试本地名单、兼容的本地 GGST 名单、通用模板；已有玩家存档继续优先使用。修改默认 JSON 不会覆盖玩家存档；需要重新应用默认配置时，关闭游戏，先备份并移走 `Saved/SaveGames/MarbleRaceRoster.sav`，下次启动会重新创建名单。没有可用配置时，代码仍生成彩色弹珠。

`Config/Roster/music_levels.json` 是空的响度配置模板，可将校准数据写入被忽略的 `local_music_levels.json`。未配置的音频使用原始音量。GGST 下载、导入和校准脚本保留在 `Scripts/`，具体用法见 `Docs/GGST角色弹珠.md`；本地 GGST 素材与配置也均被忽略。通用头像材质为兼容已有地图仍使用原来的 `/Game/GGST/Materials/M_GGSTMarble` 路径，默认纹理为引擎白色纹理，不依赖角色头像。

## 可选录制

按照 `Tools/FFmpeg/README.md` 安装编码工具后，在游戏设置中开启录制。未安装时仍可构建、打包及比赛，只是不启用录制。正常结束每场比赛保存独立 MP4，按 ESC 中断则丢弃本场录像。

## 构建与验证

打开 `MarbleRace.uproject` 并编译编辑器目标。默认入口为主菜单，比赛地图是 `MainContent`，两张地图已列入打包配置。通过 Unreal 自动化测试窗口运行 `MarbleRace` 测试；媒体包和编码工具未安装时，只跳过相应的素材与实际编码集成验证，框架测试照常运行。
