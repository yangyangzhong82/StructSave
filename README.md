# StructSave

基于 LeviLamina 的结构保存与放置插件。结构以小端二进制 NBT 保存到插件数据目录下的 `structures/<名称>.mcstructure`。

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

- `splace` 是 `structplace` 的别名，读取上述目录中已有的 `.mcstructure` 文件；名称可省略 `.mcstructure` 后缀。
- 在命令执行者所在维度放置，位置是结构的最小坐标角，支持绝对坐标和 `~` 相对坐标。
- 默认放置文件中的方块和实体；只有保存时包含的实体才会恢复。设为 `false true` 可只放置方块，设为 `true false` 可只放置实体。
- 保留保存时的朝向。方块及空气会覆盖目标区域，请选择合适的位置。
- 目标区域需要全部加载且处于维度高度范围内，否则会报错并取消放置。
- 名称沿用保存命令的规则：英文字母、数字、`_`、`-`、`.` 保持不变，其他字符替换为 `_`。
- 两个命令均沿用插件现有的 `Any` 权限；无维度上下文的控制台执行会报错，可使用游戏内命令或带维度上下文的 `execute`。

## 导入内存或世界存档，供结构方块加载

```text
/structload <文件名> [memory|disk] [结构名]
/sload house
/sload house.mcstructure memory custom:house
/sload house.mcstructure disk custom:house
```

- `sload` 是 `structload` 的别名，从插件数据目录的 `structures/` 读取已有 `.mcstructure` 文件，文件名可省略后缀。
- 默认导入 `memory`（内存），服务器重启后需要重新导入。选择 `disk` 则保存到游戏世界存档，重启后仍可加载，并同时更新内存中的同名结构，供结构方块立即使用。
- 不指定结构名时，使用 `mystructure:文件名`（去掉 `.mcstructure` 后缀）；指定名称但未包含命名空间时，自动添加 `mystructure:`。
- 在结构方块中切换到“加载”模式，填写命令返回的完整结构名，例如 `mystructure:house`，即可使用结构方块加载、调整旋转或镜像并放置。
- 同名结构直接覆盖，导入本身不放置方块。导入 `memory` 不会修改世界存档中的同名结构。
- 命令沿用插件的 `Any` 权限，需要可获取世界的命令上下文。

## 删除结构

```text
/structdelete <结构名> <memory|disk>
/sdelete custom:house memory
/sdelete custom:house disk
```

- `memory` 只删除内存中的结构，结构方块仍可重新读取存档中的同名结构。
- 如果删除内存时检测到同名存档，命令会明确提示，并给出对应的 `disk` 删除命令。
- `disk` 删除世界存档中的结构，并清除同名内存缓存，避免结构方块继续使用已删除结构的旧副本。
- 不删除插件 `structures/` 目录下的 NBT 文件，也不移除已放置的方块。

## 导出内存或存档中的结构

```text
/structexport <结构名> <memory|disk> [文件名]
/sexport custom:house memory house_memory
/sexport custom:house disk house_disk
```

- 读取指定来源的结构，导出为插件 `structures/` 目录下的小端二进制 `.mcstructure` 文件；同名文件直接覆盖，可再次使用 `/structload` 导入或 `/structplace` 放置。
- 内存与存档中同名结构的内容可以不同；导出 `disk` 直接读取存档，导出 `memory` 只读取当前内存。
- 默认文件名由完整结构名转换，例如 `custom:house` 导出为 `custom_house.mcstructure`；可指定文件名，后缀可省略。
- 删除和导出时，结构名不带命名空间也会自动添加 `mystructure:`。命令均使用 `Any` 权限。

## 编译

在项目根目录执行：

```text
xmake f -y -p windows -a x64 -m release
xmake -y
```

编译结果位于 `bin/StructSave/`，包含插件 DLL、清单及 `lang/` 语言目录。更新插件时一并复制 `lang/`，语言 JSON 应位于插件的 `lang/` 子目录中；替换后重启服务器加载。

## License

CC0-1.0 © LeviMC(LiteLDev)
