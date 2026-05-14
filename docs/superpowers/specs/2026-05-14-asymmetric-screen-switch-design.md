# 非对称屏幕切换规则 — 设计

日期: 2026-05-14
状态: 设计阶段已审批
作用范围: 服务器配置 → 高级标签页

## 问题

Deskflow 的撞边切换目前对"服务器→客户端"和"客户端→服务器"两个方向应用同一套规则。
用户希望非对称行为：

- 离开服务器（服务器 → 客户端）时必须按住修饰键（默认 Ctrl），
  防止鼠标误滑进客户端。
- 返回服务器（客户端 → 服务器）时立即生效——不等待、不需双击、不要修饰键。

现存的 `switchNeedsShift/Control/Alt` 选项虽存在，但对两个方向同样生效。
目前没有任何按方向区分的控制能力。

## 目标

- 在 服务器配置 → 高级 → Switching 分组下增加两个新 checkbox，让用户配置非对称切换规则。
- 完全向后兼容：两个 checkbox 都不勾时，行为与今天完全一致。
- 与现有的 wait-delay、double-tap、全局 modifier-needed 选项共存。

## 非目标

- 推广到 N 方向非对称规则（如按边或按客户端各自配置）。
- 替换或迁移现有切换选项。
- 用不兼容方式修改 `[options]` 配置文件语法。

## 用户可见行为

在高级标签页 Switching 分组里、"Switch on double tap" 这行下方新增两个控件：

```
☐ Hold [Ctrl ▼] to leave server      ← Checkbox A + 下拉菜单
☐ Return to server without waiting    ← Checkbox B
```

- 下拉菜单选项：Ctrl（默认）/ Shift / Alt。
- 当 checkbox A 未勾选时下拉菜单灰显（disabled）。
- 两个 checkbox 互相独立，没有联动启用/禁用。

行为矩阵：

| 离开需修饰键 | 返回即时 | 服务器 → 客户端                | 客户端 → 服务器                  |
|--------------|----------|--------------------------------|----------------------------------|
| ☐            | ☐        | 现有行为                       | 现有行为                         |
| ☑ (Ctrl)     | ☐        | 撞边 AND 按住 Ctrl → 切换      | 现有行为（wait/double-tap）      |
| ☐            | ☑        | 现有行为                       | 撞边即切                         |
| ☑ (Ctrl)     | ☑        | **目标场景：** Ctrl + 撞边     | 撞边即切，无需修饰键             |

"现有行为" = 现有的 `switchDelay`、`switchDoubleTap`、`switchNeedsShift/Control/Alt`
按其各自配置生效。

当 "Return to server without waiting" 启用时，客户端→服务器方向会绕过：

- `switchDelay`（等待延迟）
- `switchDoubleTap`（双击要求）
- `switchNeedsShift/Control/Alt`（全局修饰键要求）
- 新增的 `leaveServerNeedsModifier`（按逻辑也不该在进入服务器时生效，
  这里显式列出以求清晰）

不会绕过：

- "没有邻居屏幕"（结构性——无目的地）
- "锁定到屏幕"（用户主动锁定）
- "锁在死角"（安全配置）

这些是正确性 / 安全约束，不是用户体验门槛。

## 架构

改动局部。无新模块，无新抽象。

```
┌─────────────────────────────────────┐
│ ServerConfigDialog (Qt UI)          │
│  - 新增 2 个控件，绑到字段          │
└──────────────┬──────────────────────┘
               │ (Qt 数据绑定)
┌──────────────▼──────────────────────┐
│ ServerConfig (GUI 模型, QSettings)  │
│  - 新增 3 个字段 + getter/setter    │
│  - 序列化为 .conf 文本              │
└──────────────┬──────────────────────┘
               │ (配置文件文本)
┌──────────────▼──────────────────────┐
│ Config (server, 解析 .conf)         │
│  - readOption() 增 2 个关键字       │
│  - 把新值写入 Server 实例           │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│ Server (核心运行时逻辑)             │
│  - 新增 3 个字段                    │
│  - isSwitchOkay() 区分方向          │
└─────────────────────────────────────┘
```

## 详细设计

### UI — `src/lib/gui/dialogs/ServerConfigDialog.ui` + `.cpp/.h`

在 Switching 分组（.ui 文件当前 673-816 行），"Switch on double tap" 行下面追加两行：

- 第 1 行：`QCheckBox cbLeaveServerNeedsModifier`，文案 "Hold modifier to leave server"，
  紧跟一个 `QComboBox cmbLeaveServerModifier`，items 为 `["Ctrl", "Shift", "Alt"]`
  （index 0 = Ctrl 为默认）。
  - 下拉菜单的 `enabled` 属性绑定到 checkbox 的 `checked` 状态。
- 第 2 行：`QCheckBox cbReturnToServerInstant`，文案 "Return to server without waiting"。

在 `ServerConfigDialog.cpp` 中：
- 构造函数（或现有控件绑定的地方）把 3 个新控件的值与 `m_serverConfig` 双向绑定。
- 把 leave-server checkbox 的 `toggled(bool)` 信号接到下拉菜单的 `setEnabled` 槽。

### 数据模型 — `src/lib/gui/config/ServerConfig.h/cpp`

在 `m_SwitchDelay` 等字段附近（约 243-246 行）新增私有字段：

```cpp
bool m_LeaveServerNeedsModifier = false;
int  m_LeaveServerModifier      = 0;   // 0=Ctrl, 1=Shift, 2=Alt
bool m_ReturnToServerInstant    = false;
```

按既有命名风格添加 3 对 getter/setter，例如
`hasLeaveServerNeedsModifier()`、`setLeaveServerNeedsModifier(bool)`、
`leaveServerModifier()`、`setLeaveServerModifier(int)` 等。

持久化：
- `ServerConfig::save()` / `loadSettings()`（QSettings）：模仿 `m_SwitchDelay` 现有写法，
  新增 3 组读 / 写。
- 序列化到 `.conf` 文本：在输出 `switchDelay` 等关键字的函数里追加：
  - 若 `m_LeaveServerNeedsModifier`: 输出 `leaveServerNeedsModifier = <control|shift|alt>`
  - 若 `m_ReturnToServerInstant`: 输出 `returnToServerInstant = true`

### 配置解析 — `src/lib/server/Config.cpp`

在 `Config::readOption()`（约 657 行，解析 `switchDelay` 等的地方）追加两个分支：

```cpp
else if (CaselessCmp::equal(name, "leaveServerNeedsModifier")) {
    // 取值: "none" | "shift" | "control" | "alt"（大小写不敏感）
    // 调用 m_server->setLeaveServerNeedsModifier(...) 与对应 mask
}
else if (CaselessCmp::equal(name, "returnToServerInstant")) {
    // 取值: "true" | "false"
    // 调用 m_server->setReturnToServerInstant(...)
}
```

输出端：在把 `switchDelay` 等写回文本的函数里，根据两个 flag 的值反向输出关键字。

### 运行时逻辑 — `src/lib/server/Server.h/cpp`

在 `Server.h` 中、`m_switchNeedsControl` 附近（约 384-406 行）增加：

```cpp
bool m_leaveServerNeedsModifier = false;
KeyModifierMask m_leaveServerModifierMask = KeyModifierControl;
bool m_returnToServerInstant = false;
```

加上对应公开 setter，供 `Config` 调用。

修改 `Server::isSwitchOkay()`（Server.cpp:765）。在函数顶部增加方向判断：

```cpp
const bool leavingServer  = (m_active   == m_primaryClient);
const bool enteringServer = (newScreen  == m_primaryClient);
```

然后改三处现有 gate：

1. 双击 gate（当前 793-803 行）：若 `enteringServer && m_returnToServerInstant`，整段跳过。
2. 等待 gate（当前 806-811 行）：同上条件跳过。
3. 修饰键 gate（当前 846-853 行）：重写——当 `leavingServer` 时 OR 进新的"离开服务器需修饰键"，
   当 `enteringServer && m_returnToServerInstant` 时把所有修饰键要求全部置为 false。

三个结构性 gate——无邻居（772-778 行）、死角锁定（819-836 行）、屏幕锁定（839-843 行）——保持不变。

## 测试

在 `src/unittests/server/` 下补充覆盖 `isSwitchOkay()` 决策的单元测试，
复用现有测试已建好的 `BaseClientProxy` mock / fake。

按行为矩阵的 4 个组合：

1. 两选项都关 → 回归测试，验证现有行为不变。
2. 仅 `leaveServerNeedsModifier=Ctrl`：
   - 离开服务器、未按 Ctrl → 返回 false。
   - 离开服务器、按住 Ctrl → 返回 true。
   - 进入服务器仍走 wait-delay 路径。
3. 仅 `returnToServerInstant=true`：
   - 离开服务器仍走 wait-delay。
   - 进入服务器绕过 wait-delay、double-tap、全局修饰键检查。
4. 两选项都开 → 目标场景，两条规则同时生效。

现有的 wait / double-tap 单元测试不修改（作为回归网）。

手测路径：
- Windows 起 server + Linux/Mac 起 client。
- 走完矩阵 4 行。
- 多客户端布局（左右各一个客户端）：Ctrl 门同时管两个出口边；两边回来都是即时。

## 向后兼容性

- 两个新字段默认 `false`。升级后什么也不改的用户看到完全相同的行为。
- 新 `.conf` 关键字是叠加式的。老 .conf 文件解析不变。
- 新 GUI 字段不与现存（内部专用的）`switchNeedsShift/Control/Alt` 冲突；
  若用户同时设两者，离开服务器时取"取并集"语义。

## 开放问题

设计阶段无遗留问题。实现期间若遇到命名或信号绑线的细节，再于 code review 中确认。
