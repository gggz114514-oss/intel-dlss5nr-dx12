最新 Full、Small 已集成，同一套宿主与模型供两种前置挂载方式使用；不提供整套旧版本。
Latest Full and Small integrated; both attachment modes share the same current host and models. No complete legacy version.
Small 保持封存 3b35fb14；Full 基于封存 62d438f6 补回旧 CRT 兼容构建选项，交付哈希 7fc56d5a。
Small retains sealed 3b35fb14. Full is rebuilt from sealed 62d438f6 with the older CRT compatibility option; delivered hash 7fc56d5a.
Full 兼容构建五组 35 帧输出与封存版逐位一致；该构建未重新测速。
The Full compatibility build passed five media cases, all 35 frames bit-exact to sealed Full; performance was not remeasured for this build.
封存 Full 含 history 均值节省 0.0337 ms，标准区段慢 0.0358 ms；Small 含 history 节省 1.3895 ms。
Sealed Full saves 0.0337 ms including history while its standard interval is 0.0358 ms slower; Small saves 1.3895 ms including history.
最新模型尚未换装游戏，游戏内效果待复验。既有三款游戏测试结果指宿主与通道的先前模型配置。
Latest models have not been deployed to games; in-game verification is pending. Prior three-game results cover the host/routing with earlier model builds.

残差外推默认关闭。开启后输入 1–4 倍，按回车应用。新配置预填 2 倍，保存设置可保留开关和倍率。
Residual extrapolation starts off. Enable it, enter a factor from 1 to 4 and press Enter. Fresh configurations start with a factor of 2. Save Settings retains both values.
