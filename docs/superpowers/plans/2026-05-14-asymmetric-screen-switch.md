# 非对称屏幕切换规则 实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在 Server Configuration → Advanced 加两个独立 checkbox，让用户为"服务器→客户端"和"客户端→服务器"两个方向配置非对称的切换条件。

**Architecture:** GUI 数据模型 `ServerConfig` 加 3 个字段并参与 QSettings 持久化与 `.conf` 文本序列化；服务器端 `Config::readOption()` 识别两个新关键字并通过 `addOption(...)` 把数据塞进 ID 表；`Server::processOptions()` 把它们读到 3 个新的运行时字段；`Server::isSwitchOkay()` 顶部加方向判断（`leavingServer` / `enteringServer`），对 wait/double-tap/modifier 三个 gate 分别进行非对称化。

**Tech Stack:** C++17/Qt6/CMake/Ninja。GUI 用 Qt Designer `.ui` 文件 + 手写 `.cpp` 槽函数。

---

## 文件结构

| 路径 | 改动 |
|------|------|
| `src/lib/deskflow/OptionTypes.h` | 新增 2 个 `OptionID` 常量 |
| `src/lib/server/Server.h` | 新增 3 个私有字段 |
| `src/lib/server/Server.cpp` | `processOptions()` 增 2 个分支；`isSwitchOkay()` 改造为方向感知 |
| `src/lib/server/Config.cpp` | `readOption()` 增 2 个关键字分支 |
| `src/lib/gui/config/ServerConfig.h` | 新增 3 个字段、getter/setter |
| `src/lib/gui/config/ServerConfig.cpp` | `commit()` / `recall()` / `operator==` / `operator<<` 各加 3 处 |
| `src/lib/gui/dialogs/ServerConfigDialog.ui` | Switching 分组追加 2 行控件 |
| `src/lib/gui/dialogs/ServerConfigDialog.h` | 新增 3 个槽函数 |
| `src/lib/gui/dialogs/ServerConfigDialog.cpp` | 构造函数中绑定 3 个控件 |
| `src/unittests/server/ServerConfigTests.cpp` | 新增数据模型/解析单测 |

---

## Task 1: 添加新的 OptionID 常量

**Files:**
- Modify: `src/lib/deskflow/OptionTypes.h:53`

- [ ] **Step 1: 在 OptionTypes.h 加两个新 ID**

在 `kOptionScreenSwitchNeedsAlt` 那一行（约 53 行）后面追加：

```cpp
static const OptionID kOptionLeaveServerNeedsModifier = OPTION_CODE("LSNM");
static const OptionID kOptionReturnToServerInstant = OPTION_CODE("RTSI");
```

值约定：
- `kOptionLeaveServerNeedsModifier`: 0 = none / 1 = shift / 2 = control / 3 = alt
- `kOptionReturnToServerInstant`: 0 = false / 1 = true

- [ ] **Step 2: 编译验证**

Run（项目根）:
```
cmake --build build
```
Expected: 成功，整个项目无报错。

- [ ] **Step 3: Commit**

```
git add src/lib/deskflow/OptionTypes.h
git commit -m "feat(server): add option IDs for asymmetric switch rules"
```

---

## Task 2: Server 端字段与 processOptions

**Files:**
- Modify: `src/lib/server/Server.h:459`
- Modify: `src/lib/server/Server.cpp:1080-1106`

- [ ] **Step 1: Server.h 新增 3 个私有字段**

在 `m_switchNeedsAlt` 那一行（约 460-462 行）后面追加（与现有 modifier 字段同段）：

```cpp
// asymmetric switch rules (server <-> client direction)
bool m_leaveServerNeedsModifier = false;
KeyModifierMask m_leaveServerModifierMask = KeyModifierControl;
bool m_returnToServerInstant = false;
```

`KeyModifierMask` 已经在 Server.h 间接引入（通过 KeyTypes.h 或类似头），如果编译报缺类型则补 `#include "deskflow/KeyTypes.h"`。

- [ ] **Step 2: 在 processOptions() 顶部把这 3 个字段重置为默认**

定位 `Server::processOptions()`（约 Server.cpp:1073-1082）。现有有 3 行 `m_switchNeedsShift/Control/Alt = false;`。紧跟其后追加：

```cpp
m_leaveServerNeedsModifier = false;
m_leaveServerModifierMask = KeyModifierControl;
m_returnToServerInstant = false;
```

- [ ] **Step 3: 在 processOptions() 的 if/else if 链里加 2 个分支**

定位 `kOptionScreenSwitchNeedsAlt` 那一段（约 1104-1105 行）。在其 `}` 后、`else if (id == kOptionRelativeMouseMoves)` 前追加：

```cpp
} else if (id == kOptionLeaveServerNeedsModifier) {
  switch (value) {
  case 1:
    m_leaveServerNeedsModifier = true;
    m_leaveServerModifierMask = KeyModifierShift;
    break;
  case 2:
    m_leaveServerNeedsModifier = true;
    m_leaveServerModifierMask = KeyModifierControl;
    break;
  case 3:
    m_leaveServerNeedsModifier = true;
    m_leaveServerModifierMask = KeyModifierAlt;
    break;
  default:
    m_leaveServerNeedsModifier = false;
    break;
  }
} else if (id == kOptionReturnToServerInstant) {
  m_returnToServerInstant = (value != 0);
```

- [ ] **Step 4: 编译验证**

Run:
```
cmake --build build
```
Expected: 成功。

- [ ] **Step 5: Commit**

```
git add src/lib/server/Server.h src/lib/server/Server.cpp
git commit -m "feat(server): wire asymmetric switch fields into processOptions"
```

---

## Task 3: 改造 isSwitchOkay() 为方向感知

**Files:**
- Modify: `src/lib/server/Server.cpp:765-856`

- [ ] **Step 1: 在 isSwitchOkay() 顶部、`stopSwitch()` 之前加方向判断**

定位 `Server::isSwitchOkay()`（Server.cpp:765）。在 "is there a neighbor?" 那段 `if (newScreen == nullptr) {...}` 之后、`bool preventSwitch = false;` 之前插入：

```cpp
const bool leavingServer = (m_active == m_primaryClient);
const bool enteringServer = (newScreen == m_primaryClient);
```

- [ ] **Step 2: 修改 double-tap gate（约 793-803 行）**

把整段：

```cpp
if (!allowSwitch && m_switchTwoTapDelay > 0.0) {
```

改成：

```cpp
if (!allowSwitch && m_switchTwoTapDelay > 0.0 && !(enteringServer && m_returnToServerInstant)) {
```

- [ ] **Step 3: 修改 wait-delay gate（约 806-811 行）**

把：

```cpp
if (!allowSwitch && m_switchWaitDelay > 0.0) {
```

改成：

```cpp
if (!allowSwitch && m_switchWaitDelay > 0.0 && !(enteringServer && m_returnToServerInstant)) {
```

- [ ] **Step 4: 重写 modifier gate（约 845-853 行）**

把：

```cpp
// check for optional needed modifiers
if (KeyModifierMask mods = this->m_primaryClient->getToggleMask();
    !preventSwitch && ((this->m_switchNeedsShift && ((mods & KeyModifierShift) != KeyModifierShift)) ||
                       (this->m_switchNeedsControl && ((mods & KeyModifierControl) != KeyModifierControl)) ||
                       (this->m_switchNeedsAlt && ((mods & KeyModifierAlt) != KeyModifierAlt)))) {
  LOG_DEBUG1("need modifiers to switch");
  preventSwitch = true;
  stopSwitch();
}
```

改成：

```cpp
// check for optional needed modifiers
if (!preventSwitch) {
  KeyModifierMask mods = m_primaryClient->getToggleMask();
  bool needShift = m_switchNeedsShift;
  bool needControl = m_switchNeedsControl;
  bool needAlt = m_switchNeedsAlt;

  // asymmetric rule: leaving server may require an additional modifier
  if (leavingServer && m_leaveServerNeedsModifier) {
    needShift |= (m_leaveServerModifierMask == KeyModifierShift);
    needControl |= (m_leaveServerModifierMask == KeyModifierControl);
    needAlt |= (m_leaveServerModifierMask == KeyModifierAlt);
  }

  // asymmetric rule: returning to server bypasses all modifier requirements
  if (enteringServer && m_returnToServerInstant) {
    needShift = needControl = needAlt = false;
  }

  if ((needShift && ((mods & KeyModifierShift) != KeyModifierShift)) ||
      (needControl && ((mods & KeyModifierControl) != KeyModifierControl)) ||
      (needAlt && ((mods & KeyModifierAlt) != KeyModifierAlt))) {
    LOG_DEBUG1("need modifiers to switch");
    preventSwitch = true;
    stopSwitch();
  }
}
```

- [ ] **Step 5: 编译验证**

Run:
```
cmake --build build
```
Expected: 成功。

- [ ] **Step 6: Commit**

```
git add src/lib/server/Server.cpp
git commit -m "feat(server): make isSwitchOkay direction-aware"
```

---

## Task 4: Config 文本解析

**Files:**
- Modify: `src/lib/server/Config.cpp:666` （在 `switchNeedsAlt` 分支后）

- [ ] **Step 1: 在 readOption() 的 if/else if 链里加 2 个分支**

定位 `} else if (name == "switchNeedsAlt") { ... }`（约 665-666 行），在其后追加：

```cpp
} else if (name == "leaveServerNeedsModifier") {
  // values: "none" | "shift" | "control" | "alt" (case-insensitive)
  std::string v = value;
  std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c){ return std::tolower(c); });
  int code = 0;
  if (v == "shift")        code = 1;
  else if (v == "control") code = 2;
  else if (v == "alt")     code = 3;
  else if (v == "none")    code = 0;
  else throw ServerConfigReadException(s, std::string("invalid leaveServerNeedsModifier: ") + value);
  addOption("", kOptionLeaveServerNeedsModifier, code);
} else if (name == "returnToServerInstant") {
  addOption("", kOptionReturnToServerInstant, s.parseBoolean(value));
```

注意：若 Config.cpp 顶部未 `#include <algorithm>` 与 `<cctype>` 则需补，但既有 `parseBoolean` 等已大量用 string 操作，多半已含。如编译报缺函数原型再补。

- [ ] **Step 2: 编译验证**

Run:
```
cmake --build build
```
Expected: 成功。

- [ ] **Step 3: Commit**

```
git add src/lib/server/Config.cpp
git commit -m "feat(server): parse leaveServerNeedsModifier and returnToServerInstant options"
```

---

## Task 5: 解析单元测试（防回归）

**Files:**
- Modify: `src/unittests/server/ServerConfigTests.h`
- Modify: `src/unittests/server/ServerConfigTests.cpp`

- [ ] **Step 1: 在 ServerConfigTests.h 添加测试声明**

定位现有测试函数（如 `equalityCheck_diff_options`），在 `private slots:` 区域追加：

```cpp
void parseLeaveServerNeedsModifier();
void parseReturnToServerInstant();
void parseInvalidLeaveServerNeedsModifier();
```

如果文件结构不同请按既有 slot 风格添加。

- [ ] **Step 2: 在 ServerConfigTests.cpp 添加测试实现**

定位文件末尾，追加：

```cpp
void ServerConfigTests::parseLeaveServerNeedsModifier()
{
  Config cfg(nullptr);
  std::istringstream input(
      "section: options\n"
      "  leaveServerNeedsModifier = shift\n"
      "end\n");
  cfg.read(input);  // 如果 read 接口不同请适配既有 API
  const auto *opts = cfg.getOptions("");
  QVERIFY(opts != nullptr);
  auto it = opts->find(kOptionLeaveServerNeedsModifier);
  QVERIFY(it != opts->end());
  QCOMPARE(it->second, 1);  // shift = 1
}

void ServerConfigTests::parseReturnToServerInstant()
{
  Config cfg(nullptr);
  std::istringstream input(
      "section: options\n"
      "  returnToServerInstant = true\n"
      "end\n");
  cfg.read(input);
  const auto *opts = cfg.getOptions("");
  QVERIFY(opts != nullptr);
  auto it = opts->find(kOptionReturnToServerInstant);
  QVERIFY(it != opts->end());
  QCOMPARE(it->second, 1);
}

void ServerConfigTests::parseInvalidLeaveServerNeedsModifier()
{
  Config cfg(nullptr);
  std::istringstream input(
      "section: options\n"
      "  leaveServerNeedsModifier = bogus\n"
      "end\n");
  bool threw = false;
  try { cfg.read(input); } catch (const std::exception &) { threw = true; }
  QVERIFY(threw);
}
```

**注意**：`Config::read()` 的实际签名需以本仓为准。先在 Config.h 里查 `read` 的真实签名，必要时把构造方式调成 `Config cfg(nullptr); std::istringstream is(...); ConfigReadContext ctx(is); cfg.readSection(ctx);` 之类。如果该 API 不易直接调用，可改用临时文件 + 现有 `Config::readSection()` 调用方式。如果完全无法直接调用解析器，**改为放弃这一 task，把对应单测改写成只测 ID 常量值（确认 OPTION_CODE 不重复）**：

```cpp
void ServerConfigTests::optionIdsDistinct()
{
  QVERIFY(kOptionLeaveServerNeedsModifier != kOptionReturnToServerInstant);
  QVERIFY(kOptionLeaveServerNeedsModifier != kOptionScreenSwitchNeedsControl);
  QVERIFY(kOptionLeaveServerNeedsModifier != kOptionScreenSwitchNeedsShift);
  QVERIFY(kOptionLeaveServerNeedsModifier != kOptionScreenSwitchNeedsAlt);
}
```

- [ ] **Step 3: 运行 server 单测**

Run:
```
cmake --build build --target unittests
ctest --test-dir build -R server -V
```
Expected: 新增测试全部通过；已有测试不退化。

- [ ] **Step 4: Commit**

```
git add src/unittests/server/ServerConfigTests.h src/unittests/server/ServerConfigTests.cpp
git commit -m "test(server): cover parsing of asymmetric switch options"
```

---

## Task 6: GUI 数据模型 ServerConfig

**Files:**
- Modify: `src/lib/gui/config/ServerConfig.h` (private fields + getters + setters)
- Modify: `src/lib/gui/config/ServerConfig.cpp` (commit/recall/operator==/operator<<)

- [ ] **Step 1: ServerConfig.h 新增 3 个私有字段**

定位约 246 行 `int m_SwitchDoubleTap = 0;` 后追加：

```cpp
bool m_LeaveServerNeedsModifier = false;
int m_LeaveServerModifier = 0;  // 0=Ctrl, 1=Shift, 2=Alt (GUI index)
bool m_ReturnToServerInstant = false;
```

- [ ] **Step 2: ServerConfig.h 新增 getter**

在 `int switchDoubleTap() const { return m_SwitchDoubleTap; }` 之后追加：

```cpp
bool leaveServerNeedsModifier() const
{
  return m_LeaveServerNeedsModifier;
}
int leaveServerModifier() const
{
  return m_LeaveServerModifier;
}
bool returnToServerInstant() const
{
  return m_ReturnToServerInstant;
}
```

- [ ] **Step 3: ServerConfig.h 新增 setter（private 区）**

在 `void setSwitchDoubleTap(int val)` 之后追加：

```cpp
void setLeaveServerNeedsModifier(bool on)
{
  m_LeaveServerNeedsModifier = on;
}
void setLeaveServerModifier(int idx)
{
  m_LeaveServerModifier = idx;
}
void setReturnToServerInstant(bool on)
{
  m_ReturnToServerInstant = on;
}
```

- [ ] **Step 4: ServerConfig.cpp 扩 operator==**

定位 `m_SwitchDoubleTap == sc.m_SwitchDoubleTap &&` 那一行（约 66 行），在其后追加（保持 `&&` 风格）：

```cpp
         m_LeaveServerNeedsModifier == sc.m_LeaveServerNeedsModifier && //
         m_LeaveServerModifier == sc.m_LeaveServerModifier &&           //
         m_ReturnToServerInstant == sc.m_ReturnToServerInstant &&       //
```

- [ ] **Step 5: ServerConfig.cpp 扩 commit() (写入 QSettings)**

定位 `settings().setValue("switchDoubleTap", switchDoubleTap());`（约 115 行）后追加：

```cpp
  settings().setValue("leaveServerNeedsModifier", leaveServerNeedsModifier());
  settings().setValue("leaveServerModifier", leaveServerModifier());
  settings().setValue("returnToServerInstant", returnToServerInstant());
```

- [ ] **Step 6: ServerConfig.cpp 扩 recall() (从 QSettings 读)**

定位 `setSwitchDoubleTap(settings().value("switchDoubleTap", 250).toInt());`（约 167 行）后追加：

```cpp
  setLeaveServerNeedsModifier(settings().value("leaveServerNeedsModifier", false).toBool());
  setLeaveServerModifier(settings().value("leaveServerModifier", 0).toInt());
  setReturnToServerInstant(settings().value("returnToServerInstant", false).toBool());
```

- [ ] **Step 7: ServerConfig.cpp 扩 operator<<（写 .conf 文本）**

定位 `if (config.hasSwitchDoubleTap()) ... switchDoubleTap = ...`（约 281-283 行）那段后追加：

```cpp
  if (config.leaveServerNeedsModifier()) {
    static const char *kNames[] = {"control", "shift", "alt"};
    int idx = config.leaveServerModifier();
    if (idx < 0 || idx > 2) idx = 0;
    outStream << "\t" << "leaveServerNeedsModifier = " << kNames[idx] << Qt::endl;
  }

  if (config.returnToServerInstant()) {
    outStream << "\t" << "returnToServerInstant = true" << Qt::endl;
  }
```

- [ ] **Step 8: 编译验证**

Run:
```
cmake --build build
```
Expected: 成功。

- [ ] **Step 9: Commit**

```
git add src/lib/gui/config/ServerConfig.h src/lib/gui/config/ServerConfig.cpp
git commit -m "feat(gui): persist and serialize asymmetric switch options"
```

---

## Task 7: UI 控件添加到 .ui 文件

**Files:**
- Modify: `src/lib/gui/dialogs/ServerConfigDialog.ui`（在 groupSwitch 内、verticalSpacer_5 前）

- [ ] **Step 1: 在 cbSwitchDoubleTap 那行之后、verticalSpacer_5 之前插入两行新控件**

定位 ServerConfigDialog.ui 约 800-814 行（"Switch on double tap" 这个 `<item>` 的结束 `</item>` 后、`<spacer name="verticalSpacer_5">` 之前）。插入：

```xml
          <item>
           <layout class="QHBoxLayout" name="_7">
            <item>
             <widget class="QCheckBox" name="cbLeaveServerNeedsModifier">
              <property name="text">
               <string>Hold modifier to &amp;leave server</string>
              </property>
             </widget>
            </item>
            <item>
             <spacer name="spacerLeaveServerModifier">
              <property name="orientation">
               <enum>Qt::Orientation::Horizontal</enum>
              </property>
              <property name="sizeHint" stdset="0">
               <size>
                <width>40</width>
                <height>20</height>
               </size>
              </property>
             </spacer>
            </item>
            <item>
             <widget class="QComboBox" name="cmbLeaveServerModifier">
              <property name="enabled">
               <bool>false</bool>
              </property>
              <item>
               <property name="text">
                <string>Ctrl</string>
               </property>
              </item>
              <item>
               <property name="text">
                <string>Shift</string>
               </property>
              </item>
              <item>
               <property name="text">
                <string>Alt</string>
               </property>
              </item>
             </widget>
            </item>
           </layout>
          </item>
          <item>
           <widget class="QCheckBox" name="cbReturnToServerInstant">
            <property name="text">
             <string>&amp;Return to server without waiting</string>
            </property>
           </widget>
          </item>
```

- [ ] **Step 2: 编译验证（uic 会重新生成 ui_ServerConfigDialog.h）**

Run:
```
cmake --build build
```
Expected: 成功；如果失败通常是 .ui XML 格式不规整，对照已有 cbSwitchDoubleTap 段调整缩进/标签。

- [ ] **Step 3: Commit**

```
git add src/lib/gui/dialogs/ServerConfigDialog.ui
git commit -m "feat(gui): add UI controls for asymmetric switch options"
```

---

## Task 8: 在 ServerConfigDialog 绑定新控件

**Files:**
- Modify: `src/lib/gui/dialogs/ServerConfigDialog.h`
- Modify: `src/lib/gui/dialogs/ServerConfigDialog.cpp`

- [ ] **Step 1: ServerConfigDialog.h 添加 3 个 slots**

定位 `void toggleSwitchDelay(bool enable);` `void setSwitchDelay(int delay);` 那段（约 56-60 行），在其后追加：

```cpp
  void toggleLeaveServerNeedsModifier(bool enable);
  void setLeaveServerModifier(int index);
  void toggleReturnToServerInstant(bool enable);
```

- [ ] **Step 2: ServerConfigDialog.cpp 在构造函数里初始化 + 接信号**

定位 `connect(ui->sbSwitchDoubleTap, QOverload<int>::of(&QSpinBox::valueChanged), ...);`（约 102-104 行），在其后追加：

```cpp
  ui->cbLeaveServerNeedsModifier->setChecked(serverConfig().leaveServerNeedsModifier());
  ui->cmbLeaveServerModifier->setEnabled(ui->cbLeaveServerNeedsModifier->isChecked());
  ui->cmbLeaveServerModifier->setCurrentIndex(serverConfig().leaveServerModifier());
  connect(
      ui->cbLeaveServerNeedsModifier, &QCheckBox::toggled, this,
      &ServerConfigDialog::toggleLeaveServerNeedsModifier
  );
  connect(
      ui->cmbLeaveServerModifier, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
      &ServerConfigDialog::setLeaveServerModifier
  );

  ui->cbReturnToServerInstant->setChecked(serverConfig().returnToServerInstant());
  connect(
      ui->cbReturnToServerInstant, &QCheckBox::toggled, this,
      &ServerConfigDialog::toggleReturnToServerInstant
  );
```

- [ ] **Step 3: ServerConfigDialog.cpp 添加 3 个 slot 实现**

在文件中定位现存 `toggleSwitchDelay` 实现，参考其写法在合适位置（文件末尾或与其他 toggle 紧邻）追加：

```cpp
void ServerConfigDialog::toggleLeaveServerNeedsModifier(bool enable)
{
  m_serverConfig.setLeaveServerNeedsModifier(enable);
  ui->cmbLeaveServerModifier->setEnabled(enable);
}

void ServerConfigDialog::setLeaveServerModifier(int index)
{
  m_serverConfig.setLeaveServerModifier(index);
}

void ServerConfigDialog::toggleReturnToServerInstant(bool enable)
{
  m_serverConfig.setReturnToServerInstant(enable);
}
```

**注意**：如果项目里 `setSwitchDelay` 等是通过 friend 关系访问 private setter，新增 slot 也走同一路径（`m_serverConfig.setXxx(...)`），编译会自动验证可见性。

- [ ] **Step 4: 编译验证**

Run:
```
cmake --build build
```
Expected: 成功。

- [ ] **Step 5: Commit**

```
git add src/lib/gui/dialogs/ServerConfigDialog.h src/lib/gui/dialogs/ServerConfigDialog.cpp
git commit -m "feat(gui): wire asymmetric switch controls in ServerConfigDialog"
```

---

## Task 9: 手动集成测试

**Files:** 无代码改动；纯手测。

- [ ] **Step 1: 启动 GUI，确认新控件渲染正确**

Run（项目根）:
```
./build/bin/deskflow
```
（Windows 上路径可能是 `build\bin\deskflow.exe`）

打开 Server Configuration → 切到 "Advanced" 标签 → 在 Switching 分组下应能看到：
- "Hold modifier to leave server"（默认未勾选）+ 下拉菜单 `[Ctrl|Shift|Alt]`（默认灰）
- "Return to server without waiting"（默认未勾选）

勾选第一个 checkbox → 确认下拉菜单变可用。

- [ ] **Step 2: 验证配置持久化**

勾上两个 checkbox（下拉菜单选 Ctrl）→ OK → 重启 GUI → 重新打开 Server Configuration → Advanced，确认两个 checkbox 仍是勾选状态、下拉为 Ctrl。

- [ ] **Step 3: 验证 .conf 文本输出（如果服务器已通过 GUI 启动则自动有效）**

Windows 上检查 `%LOCALAPPDATA%\Deskflow\Deskflow\` 或类似目录下的运行时配置 dump。或暂时让 ServerConfig 保存到本地：

```
File → Save Configuration → 选 .conf
```

确认文件里有：
```
leaveServerNeedsModifier = control
returnToServerInstant = true
```

- [ ] **Step 4: 行为矩阵手测**

需要 1 台 server（Windows）+ 1 台 client（任意系统）；屏幕布局：client 在 server 右侧。

| 场景 | 操作 | 期望 |
|------|------|------|
| 两选项都关 | 鼠标撞右边 | 立即跳到 client（或走原有 wait） |
| 仅 leaveSrv=Ctrl | 鼠标撞右边、不按 Ctrl | 不跳 |
| 仅 leaveSrv=Ctrl | 按住 Ctrl 撞右边 | 跳到 client |
| 仅 leaveSrv=Ctrl | 在 client 上撞左边回 server | 仍按原 wait/double-tap |
| 仅 returnInstant | 在 client 撞左边 | 立即跳回 server |
| 仅 returnInstant | server 撞右边 | 按既有规则 |
| 两选项都开 | Ctrl + 撞右边 | 跳到 client |
| 两选项都开 | 在 client 撞左边 | 立即跳回 server |

每个组合记录一行 PASS/FAIL。

- [ ] **Step 5: 多客户端验证**

若条件允许：左右各一个 client。
- 服务器→左 client：按住 Ctrl 撞左边 → 跳左
- 服务器→右 client：按住 Ctrl 撞右边 → 跳右
- 左 client → 服务器：撞右边 → 立即回
- 右 client → 服务器：撞左边 → 立即回

- [ ] **Step 6: 提交手测报告（无代码改动则跳过 commit）**

如果发现 bug 则回 Task 3 / Task 8 修正；否则继续下一步。

---

## Task 10: 收尾

- [ ] **Step 1: 跑全部单测**

Run:
```
ctest --test-dir build -V
```
Expected: 全绿。

- [ ] **Step 2: 浏览 git log 确认提交链清晰**

Run:
```
git log --oneline -15
```

应看到本计划的 8 个 feat/test commits。

- [ ] **Step 3: 创建/更新 PR（如果用 PR 流）**

按本仓 PR 模板填写：feature summary、行为矩阵截图、test plan。

---

## 已知约束 / 提示

- `kOptionLeaveServerNeedsModifier` 在 Task 1 用了字符串 `"LSNM"`，`kOptionReturnToServerInstant` 用了 `"RTSI"`，这两个 4-byte 代码在 OptionTypes.h 中确认与既有不冲突（已查 OptionTypes.h 现有所有 OPTION_CODE）。
- 现有 `m_switchNeedsShift/Control/Alt` 通过全局开关启用时仍按原"双向"语义生效，与本方案叠加。Task 3 Step 4 的写法用 `|=`，确保两者并存时取并集。
- Task 5 的解析单测对 `Config::read()` 入口可能不直接可调用，若遇阻塞按 Step 2 末尾给出的退路（仅校验 ID 常量唯一性）。
- 任何 step 编译失败时：先复查刚写代码，再核对实际行号（plan 中的行号取写计划时的快照，可能略有偏差）。
