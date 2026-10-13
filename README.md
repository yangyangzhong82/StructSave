# StructSave

基于 LeviLamina 的结构保存与放置插件。结构以小端二进制 NBT 保存到插件数据目录下的 `structures/<名称>.nbt`。

## 保存结构

```text
/structsave <名称> <起点 x y z> <终点 x y z> [包含实体] [包含方块]
/ssave house ~ ~ ~ ~9 ~9 ~9 true true
```

`ssave` 是 `structsave` 的别名。默认不保存实体、保存方块；两个端点都包含在保存范围内。

## 放置结构

```text
/structplace <名称> <位置 x y z> [包含实体] [包含方块]
/splace house ~ ~ ~
/splace house 100 64 100 false true
```

- `splace` 是 `structplace` 的别名，读取上述目录中已有的 `.nbt` 文件；名称可省略 `.nbt` 后缀。
- 在命令执行者所在维度放置，位置是结构的最小坐标角，支持绝对坐标和 `~` 相对坐标。
- 默认放置文件中的方块和实体；只有保存时包含的实体才会恢复。设为 `false true` 可只放置方块，设为 `true false` 可只放置实体。
- 保留保存时的朝向。方块及空气会覆盖目标区域，请选择合适的位置。
- 目标区域需要全部加载且处于维度高度范围内，否则会报错并取消放置。
- 名称沿用保存命令的规则：英文字母、数字、`_`、`-`、`.` 保持不变，其他字符替换为 `_`。
- 两个命令均沿用插件现有的 `Any` 权限；无维度上下文的控制台执行会报错，可使用游戏内命令或带维度上下文的 `execute`。

## 编译

在项目根目录执行：

```text
xmake f -y -p windows -a x64 -m release
xmake -y
```

编译结果位于 `bin/my-mod/`，包含插件 DLL、清单及语言文件。

## License

CC0-1.0 © LeviMC(LiteLDev)
