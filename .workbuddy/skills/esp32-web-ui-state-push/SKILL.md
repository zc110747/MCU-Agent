---
name: esp32-web-ui-state-push
description: ESP32（Arduino core）带内嵌网页时，如何把界面从"命令回显"改成"设备真实状态驱动"：WebSocket 服务端周期性推送 state 快照、事件队列与快照分离、前端零轮询；以及没有硬件时如何用 Node vm 沙箱 + 最小 DOM 桩对前端逻辑做行为断言。适用于"网页显示的状态不对""点了按钮界面没反应""GPIO/PWM/ADC 读数要实时更新""去掉彩色高亮改显示实际状态""无法连板怎么验证前端"等场景。与 esp32-arduino-cli-build 互补（那边是构建/烧录，这边是运行时网页与无硬件验证）。
agent_created: true
---

# ESP32 网页：服务端状态推送 + 无硬件前端验证

## 1. 先判断根因

用户提的这类需求——
"点 SET 后要显示真实电平" / "apply 后要实时显示状态" / "数据要实时更新" / "去掉彩色高亮改显示实际状态"——

**几乎都是同一个病：界面显示的是命令回显（下发即渲染），而不是硬件状态。**

故**不要给每个控件打补丁**（给 A 加个回读、给 B 加个定时器……），而是建立统一状态通道，一次解决全部。

## 2. 参考实现（ESP32 + WebSocketsServer，端口 81）

### 2.1 服务端：周期性广播 state 快照

```cpp
// config: constexpr uint32_t STATE_PUSH_INTERVAL_MS = 400;

void WebsocketManager::onConnect() {          // WStype_CONNECTED 回调
    if (_clients < 255) ++_clients;
    _lastState = 0;                            // 让新页面立刻收到首帧
}

void WebsocketManager::taskLoop() {
    srv->loop();
    if (_clients > 0 && (millis() - _lastState >= AppConfig::STATE_PUSH_INTERVAL_MS)) {
        _lastState = millis();
        broadcastState();
    }
}
```

```json
{ "type":"state", "ts":12345678,
  "gpio":[{"pin":4,"state":1,"dir":1}, ...],
  "led":{"pin":48,"mode":4,"mode_str":"cycle","on":1,"r":0,"g":255,"b":0,"ready":1},
  "pwm":{"active":1,"pin":21,"period":1000,"duty":25,"freq":1000,"resolution":12},
  "adc":[{"ch":0,"raw":2048,"voltage":3.300}, ...],
  "adc_src":"internal-adc1", "adc_ready":1 }
```

### 2.2 三条硬性原则

1. **快照不进事件队列。** 周期性大帧塞进事件总线会把事件结构体从 ~220 B 撑到 800 B+，
   深度 64 的队列多占约 37 KB RAM。改为在推送任务里直接**读各模块的缓存快照** + 直接广播。
   **事件队列只承载"变化"，快照承载"当前值"。**
2. **无客户端时零开销。** 用连接回调维护 `_clients` 计数，计数为 0 就别构造 JSON。
3. **字段语义必须是"真实值"而非"输入值"：**
   - GPIO：写后**必 `digitalRead()` 回读**再发布 → 外部拉死时界面如实显示 LOW。
   - PWM：发布 LEDC **量化后的实际** freq / resolution，不是用户输入的 period。
   - LED：除 `mode` 外维护实时输出 `_on/_r/_g/_b`，闪烁/循环的亮灭半周期在界面上可见跳变。
   - ADC：读模块缓存（`latest()`），**不额外占 I2C 总线**。

### 2.3 首屏兼容

`/api/xxx` GET 同步返回同一批状态字段，保证 WebSocket 还没连上时首屏不空白。

### 2.4 前端

- 删掉**全部** `setInterval` 轮询；断线不要 `location.reload()`（丢状态），改 3 s 自动重连 WebSocket。
- 收到 `state` 后 `applyState(m)` 统一分发到各 render 函数，所有控件只读 state。
- 设备回传值回填输入框时，**跳过 `document.activeElement`**，否则用户正在输入会被打断。
- 状态高亮用**中性描边/灰度**，不要彩色（彩色会被读成"状态语义"，而它只是"选中"）。
- 曲线图按峰值自适应缩放（`scale = peak * 1.15`），省掉手选量程。

## 3. 无硬件时的前端验证（关键套路）

连不上板子也能把前端逻辑跑起来做行为断言，三步：

### 3.1 `chk_pages.js` — 静态体检
从内嵌的 C raw string 里正则抽出 HTML → 正则抽 `<script>` → `new Function(code)` 验 JS 语法
（避免 ES6+ 语法在旧内核 / 转义出错）→ 核对所有 `getElementById` 的 id 都存在于 HTML →
核对所有 pane 存在 → 检查无内联彩色样式。

### 3.2 `chk_ui_logic.js` — 行为断言（主力）
Node `vm` 沙箱 + **手写最小 DOM 桩**，注入真实的 `state` JSON，断言每个控件的渲染结果。

必须的桩：`document.getElementById/createElement`、`classList`、`insertRow/insertCell`、
`canvas.getContext()`、`fetch`、`WebSocket`、`Blob`、`FormData`、`setInterval/clearInterval`。

**DOM 桩最大的坑**：`El` 的 `id` 必须是 **getter/setter**，setter 里 `doc._byId[v] = this`：
```js
class El {
  get id() { return this._id; }
  set id(v) { this._id = v; doc._byId[v] = this; }   // 模拟真实 DOM 注册
}
```
否则动态 `createElement` 后赋 `b.id = 'led_g'` 不会进 `getElementById` 索引，
拿到的是新的空 El，报 `xxx.onclick is not a function`。

断言写法示例（22 项覆盖四项需求）：
- GPIO：`state.gpio[0] = {pin:4,state:1,dir:1}` → 文本含 `HIGH` 且 `OUT`
- LED：`{mode:4,mode_str:'cycle',on:1,r:0,g:255,b:0}` → 文本含 `RGB CYCLE` 且 `ON rgb(0,255,0)`，
  且选中按钮只加中性 class 不带颜色
- PWM：→ 显示 `GPIO21 1000us 25% (1.00 kHz)`，且 pin/period/duty 输入框被回填
- ADC：→ `3.300 V` / `2048` / 标题含 `[internal-adc1]`
- 命令侧：点击后经 WS 发出的 JSON 内容正确

### 3.3 Python 端到端脚本
纯 stdlib 实现 RFC 6455 客户端（**客户端帧必须 mask**，支持 ping→pong），
然后"下发命令 → 回读 state 断言"。
验证"实时输出"的技巧：对 LED 设 cycle 模式后，2 s 内收集所有快照，
断言 `(on,r,g,b)` 组合出现 **≥2 种**——证明渲染的是实时输出而不是模式回显。

## 4. 环境坑（仅限无硬件验证脚本）

- **Git Bash 下 node 读不到 `/tmp/x.js`**（`/tmp` 映射到 `E:\tmp` 但与 node 的 Windows 路径解析不一致）
  → 脚本一律落到工程内 `tools/`，用相对路径跑。
- 断言脚本输出用**英文**，避免 Windows GBK 控制台乱码。
- 这两个坑只在"本地跑 Node/Python 验证脚本"时出现；`.bat` 烧录脚本的坑见 `esp32-arduino-cli-build`。
