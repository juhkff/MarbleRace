# MarbleRace

基于 Unreal Engine 5.8 的侧视弹珠竞赛框架。弹珠依靠物理模拟通过赛道，镜头跟随领跑者，并播放对应角色的 BGM；支持角色自定义、冲线排行榜和可选比赛录制。

项目不限定角色主题。仓库提供源码、地图、赛道组件、通用材质和配置模板；个人角色图片、音乐及录制编码程序在本地准备。首次运行没有素材包时，会创建 **12 颗无头像、无 BGM 的彩色弹珠**，可以直接比赛。

## 快速开始

### 从源码运行

1. 克隆仓库：

   ```powershell
   git clone https://github.com/juhkff/MarbleRace.git
   cd MarbleRace
   ```

2. 准备 Unreal Engine 5.8，以及该引擎版本支持的 Visual Studio C++ 工具链和 Windows SDK。当前已验证 Windows 构建与打包；录制编码工具也使用 Windows 版本。
3. 打开 `MarbleRace.uproject`。如果引擎关联无法识别，先将工程关联到本机的 UE 5.8；出现模块编译提示时进行编译。
4. 编辑器默认打开 `MainMenu`。点击编辑器的运行按钮，再点击游戏主菜单的 **开始游戏**。

工程配置还启用了 `ModelContextProtocol`、`Terminal`、`AllToolsets` 开发工具插件。它们不是弹珠竞赛源码的模块依赖；没有安装这些插件的环境，可以在 `MarbleRace.uproject` 的 `Plugins` 列表中将对应项的 `Enabled` 改为 `false`。`ModelingToolsEditorMode` 用于编辑器建模。

也可以在项目根目录使用 PowerShell 编译；将 `$ueRoot` 改为本机引擎路径：

```powershell
$ueRoot = "D:\Coding\Unreal\UE_5.8"
$projectRoot = (Get-Location).Path
& "$ueRoot\Engine\Build\BatchFiles\Build.bat" MarbleRaceEditor Win64 Development "-Project=$projectRoot\MarbleRace.uproject" -WaitMutex -NoHotReloadFromIDE
```

### 运行打包版本

打开打包目录中的 `MarbleRace.exe`。分发时保留整个打包目录，包括资源包和依赖文件，不要只复制 EXE。

## 游戏使用方式

### 比赛流程

1. 在主菜单的 **角色设置** 中选择参赛角色。
2. 在 **游戏设置** 中调整显示、音频和录制选项。
3. 点击 **开始游戏**。弹珠在滚筒内随机生成，默认倒计时 3 秒后开赛。
4. 镜头跟随当前领跑者；默认播放其 BGM，领跑者改变时直接切歌。
5. 每颗弹珠冲线后，屏幕中上方显示约 2 秒的名次提示。
6. 全部冲线后，排行榜显示名次、角色、歌名与用时。每页 10 名、停留 5 秒，最后一页结束后自动返回主菜单；也可以点击 **继续** 提前返回。

比赛用时从倒计时结束开始计算，使用真实时间，不受终点慢动作影响。

### 按键

| 按键 | 功能 |
| --- | --- |
| `L` | 锁定当前 BGM，领跑者变化时保持当前歌曲；左下角出现小红点。 |
| `U` | 解锁 BGM，并立即检查当前领跑者是否需要切歌。 |
| `ESC` | 比赛中立即返回主菜单，并丢弃本场录制；此前保存的视频保留。 |

角色设置和游戏设置中，`ESC` 返回主菜单；编辑文本时，`Enter` 保存，`ESC` 取消输入。在编辑器 PIE 中测试比赛按键时，先点击游戏视口使其获得键盘焦点。

## 角色设置

打开主菜单的 **角色设置**，点击左侧条目，右侧提供三个页签：

- **基本信息**：修改角色名称和不透明底色。左侧的“参赛／停用”决定下一场是否参与；底部可以新建、删除及上下移动条目。
- **头像**：从 `Saved/Portraits/` 选择图片，或输入图片绝对路径后点击“导入”。支持 PNG、JPG、JPEG、BMP；没有头像时使用彩色圆盘与姓名首字。建议使用主体居中的方形图片。
- **主题曲**：选择工程内的音频，播放／暂停试听，点击或拖动时间条定位，并设置首次播放起点。

修改自动保存。运行时导入的头像会随名单保存，之后显示不依赖原始图片路径。

### 配置角色音乐

1. 在 Unreal 内容浏览器中导入音频，例如 WAV 或 MP3，生成音频资源。
2. 如需歌曲自动循环，在 SoundWave 资源中开启循环播放。
3. 回到角色设置的 **主题曲** 页，选择该音频。
4. 试听定位后点击 **设为当前时间**，或在“首次播放起点”中输入秒数并保存；**归零** 恢复从头播放。

默认模式下，每个角色首次领跑时使用自己的起点；后续再次领跑从断开处继续，歌曲播完后从零循环。切换角色没有淡出。试听不会改变比赛中的播放进度。

**后台播放 BGM** 开启后，歌曲以开赛时刻为共同起点，从 0 秒持续推进，只有领跑者可听到；此模式忽略各角色的首次播放起点，静音期间进度也继续推进。

打包版本的主题曲选择器使用已导入并烘焙的 UE 音频资源，不能通过单独复制 MP3 到运行目录来增加歌曲。

## 游戏设置

选项采用左侧名称、右侧启用／禁用按钮的布局，悬停可查看说明，修改自动保存。

| 页面 | 设置 | 默认 | 作用 |
| --- | --- | --- | --- |
| 按键设置 | 锁定／解锁 BGM、中断比赛 | — | 查看 `L`、`U`、`ESC` 的操作说明。 |
| 显示设置 | 稳定名称显示 | 关闭 | 减轻姓名抖动；关闭时保留原来的滚动显示效果。 |
| 显示设置 | 终点慢动作 | 开启 | 临近冲线时放慢世界，目标约 2.4 秒；BGM 保持正常速度。 |
| 显示设置 | 倒计时缩放过渡 | 开启 | 倒计时期间镜头从近拉远，默认约 1.8 秒完成。 |
| 音频设置 | 统一 BGM 响度 | 开启 | 对有校准数据的歌曲应用音量倍率；未配置的歌曲保持原音量。 |
| 音频设置 | 赛后播完当前 BGM | 关闭 | 全部冲线后继续播放当前歌曲，本轮结束后停止。 |
| 音频设置 | 后台播放 BGM | 关闭 | 所有歌曲从开赛时同步推进，仅领跑角色可听到。 |
| 录制设置 | 自动录制比赛 | 关闭 | 每场录制游戏画面和游戏声音。 |
| 录制设置 | 录制保存位置 | — | 打开实际使用的录像文件夹。 |

返回主菜单时会停止比赛 BGM。因此即使启用了“赛后播完当前 BGM”，排行榜自动返回或手动点击“继续”也会结束播放。

## 默认名单与本地素材

### 使用自己的主题

复制 `Config/Roster/default_roster.json` 为 `Config/Roster/local_roster.json`，填写自己的默认名单。每个角色必需 `name`，其余字段可省略：

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

`color` 使用十六进制色值，运行时保持不透明。头像和音乐字段填写 UE **资源对象路径**，不是磁盘图片或音频文件路径；可以在内容浏览器复制资源引用后，取出其中的 `/Game/...资源名.资源名` 部分。没有头像或音乐时，直接省略对应字段即可。

默认名单按以下顺序尝试：

1. `Config/Roster/local_roster.json`：自定义本地名单。
2. `Config/GGST/roster.json`：兼容已有的本地 GGST 主题。
3. `Config/Roster/default_roster.json`：仓库提供的通用模板。

已有玩家存档优先于默认名单。修改 JSON 不会覆盖存档；若要重新应用默认配置，先关闭游戏，备份并移走运行时 `Saved/SaveGames/MarbleRaceRoster.sav`，下次启动会重新创建名单。

### 素材目录与 Git

| 路径 | 用途 | 是否提交 |
| --- | --- | --- |
| `Config/Roster/default_roster.json` | 通用默认名单模板 | 是 |
| `Config/Roster/music_levels.json` | 通用响度配置模板 | 是 |
| `Config/Roster/local_roster.json`、`local_music_levels.json` | 个人主题和音量校准 | 否 |
| `Content/LocalMedia/` | 已导入的个人 UE 图片／音频资源 | 否 |
| `SourceAssets/LocalMedia/` | 个人素材原始文件 | 否 |
| `Content/GGST/Portraits/`、`Content/GGST/Music/` | 本地 GGST 资源 | 否 |
| `SourceAssets/GGST/`、`Config/GGST/` | GGST 原始素材和配置 | 否 |
| `Tools/FFmpeg/*.exe` | 可选录制编码工具 | 否 |
| `Saved/` | 玩家存档、录像及临时文件 | 否 |

名单配置中的头像、音乐资源必须实际存在，才会显示和播放。通用头像材质为兼容已有地图保留在 `/Game/GGST/Materials/M_GGSTMarble`；它使用引擎白色默认纹理，不依赖 GGST 头像。

GGST 下载、导入及响度分析脚本位于 `Scripts/`，具体使用方式见 [GGST 角色弹珠说明](Docs/GGST角色弹珠.md)。它是可选主题包，不是框架运行的前提。

### 自定义响度校准

将 `Config/Roster/music_levels.json` 复制为 `Config/Roster/local_music_levels.json`，按音频对象路径配置播放倍率：

```json
{
  "target_lufs": -18,
  "tracks": [
    {
      "music_asset": "/Game/LocalMedia/Music/BGM_Example.BGM_Example",
      "gain": 0.5
    }
  ]
}
```

`gain` 是正的线性音量倍率，`1.0` 表示原音量；示例的 `0.5` 表示衰减，并不意味着该歌曲已经完成响度测量。运行时只应用提供的倍率，不自动分析歌曲；启用统一响度后，菜单试听和比赛共用校准数据。

## 比赛录制

1. 按 [FFmpeg 安装说明](Tools/FFmpeg/README.md) 下载并校验 Windows 编码工具。
2. 将 `ffmpeg.exe` 放在项目的 `Tools/FFmpeg/` 中；`ffprobe.exe` 用于实际编码的自动化验证。
3. 在 **游戏设置 → 录制设置** 中启用 **自动录制比赛**。
4. 开始比赛，正常返回主菜单后在录制设置中点击 **打开文件夹** 查看视频。

录制从进入比赛场景开始，包含倒计时和赛后排行榜，仅截取实际游戏画面，排除窗口黑边，不采集桌面或麦克风。输出为保持画面比例的 H.264/AAC MP4，最高按 1080p、30 FPS 输出。

每场比赛单独生成带时间与随机编号的文件：

- **正常结束／点击继续**：返回菜单时结束录制，后台编码保存。
- **按 ESC 中断**：丢弃本场视频及临时采集文件，之前的录像保留。
- **连续多场比赛**：每场单独保存，不拼接成一个视频。
- **退出游戏**：若有录像正在保存，会等待保存完成后退出。

录像位于运行时 `Saved/Recordings/`，以“打开文件夹”显示的实际位置为准。没有安装 FFmpeg 时仍可构建、打包及比赛；开启录制会显示缺少编码工具的状态。

## 打包 Windows 游戏

1. 编译工程，并在编辑器中保存地图和资源。
2. 在项目设置的 **打包** 中检查地图列表，包含 `/Game/Maps/MainMenu` 和 `/Game/Maps/MainContent`；本项目已配置。
3. 如使用 `Content/LocalMedia/` 等个人资源目录，将它们加入 **始终烘焙的目录**，确保通过 JSON 或玩家存档引用的图片、音乐能随游戏打包。
4. 如需录制，在打包前准备好 `Tools/FFmpeg/ffmpeg.exe`；存在时会自动随包附带编码器、许可和说明。
5. 使用编辑器的 Windows 打包入口，选择输出目录。分享时提供整个输出目录。

当前配置使用 Pak 与 IoStore，并关闭 ZenStore。没有本地 GGST 配置或 FFmpeg 程序时，不会将它们作为必需的暂存文件。

## 工程结构与验证

```text
Config/Roster/             通用默认名单、响度模板与本地配置
Content/Maps/              MainMenu 主菜单、MainContent 比赛地图
Content/关卡/              各段赛道蓝图
Content/赛道组件/          滚筒、终点、跳板、墙壁等
Source/MarbleRace/         比赛、角色、音乐、设置、录制与界面源码
Scripts/                  可选主题素材处理脚本
Tools/FFmpeg/             编码工具安装说明和许可
Docs/                     功能与组件说明
Saved/                    运行时数据及构建生成文件，不提交
```

在 Unreal 自动化测试窗口运行 `MarbleRace` 测试。没有安装可选主题包或编码工具时，跳过对应素材加载与实际编码的集成验证；通用名单、比赛流程和录制控制等框架检查照常运行。

组件调整可参考 [追逐斜坡碰墙说明](Docs/追逐斜坡碰墙.md) 和 [平滑制动区域说明](Docs/平滑制动区域.md)。例如在“弹性三角2”的“跳板弹射”组件中，可调整同一颗弹珠两次触发的冷却时间，当前该组件配置为 2 秒。
