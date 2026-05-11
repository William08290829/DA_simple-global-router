# Timer 使用說明

`Timer` 是一個輕量的 C++ 計時工具，適合拿來量測整體流程時間，或是把不同階段拆開分段計時。它使用 `std::chrono::steady_clock`，輸出單位為秒，預設顯示到小數點後 6 位。

目前這個類別支援兩種計時方式：

- `totalTimer`：追蹤整個程式或整段流程的總時間
- named timers：用字串名稱管理多個分段計時器，例如 `parse`、`route`、`write`

## 檔案位置

- 標頭檔：[Timer.hpp](./Timer.hpp)

## Quick Start

```cpp
#include "Timer/Timer.hpp"

int main() {
    Timer timer;

    timer.startTotalTimer();

    timer.startTimer("parse");
    // ... parsing ...
    timer.stopTimer("parse");

    timer.startTimer("route");
    // ... routing ...
    timer.stopTimer("route");

    timer.stopTotalTimer();

    timer.printTimer("parse");
    timer.printTotal();
    timer.printAllTimers();
}
```

## 常見使用情境

### 1. 只量測整體執行時間

```cpp
Timer timer;
timer.startTotalTimer();

// ... whole program ...

timer.stopTotalTimer();
timer.printTotal();
```

### 2. 分段量測不同流程

```cpp
Timer timer;

timer.startTimer("read_input");
// ...
timer.stopTimer("read_input");

timer.startTimer("global_route");
// ...
timer.stopTimer("global_route");

timer.printAllTimers();
```

### 3. 計時尚未停止時先查詢目前經過時間

```cpp
Timer timer;
timer.startTimer("search");

// ...
double elapsed = timer.elapsedSec("search");
```

## API 說明

### `Timer()`

建立一個新的 `Timer` 物件。建構後不會自動開始總計時，必須自行呼叫 `startTotalTimer()`。

### `void startTotalTimer()`

開始總計時。如果總計時已經在執行中，這個呼叫不會重複啟動。

### `void stopTotalTimer()`

停止總計時，並保存結果。如果總計時尚未開始，這個呼叫不會報錯，也不會有輸出。

### `double totalElapsedSec() const`

回傳總計時秒數：

- 若總計時仍在執行，回傳「目前累積時間」
- 若總計時已停止，回傳「最後一次停止後的時間」

### `void printTotal() const`

輸出總計時結果，格式如下：

```text
[Timer] Total time: 0.123456s
```

### `void startTimer(const std::string& timerName)`

開始一個分段計時器。

- 若名稱不存在，會自動建立新的 timer
- 若名稱已存在，會重新記錄新的開始時間
- 這個類別目前是「覆蓋式」計時，不會自動累加同名 timer 的多次結果

### `void stopTimer(const std::string& timerName)`

停止指定名稱的分段計時器。

錯誤情況：

- 若 `timerName` 不存在，會輸出錯誤訊息
- 若 `timerName` 已存在但目前沒有在執行，會輸出錯誤訊息

### `double elapsedSec(const std::string& timerName) const`

查詢指定 timer 的秒數。

- 若 timer 正在執行，回傳從 `startTimer()` 到目前為止的時間
- 若 timer 已停止，回傳最後一次量測結果
- 若 timer 不存在，回傳 `0.0`

### `void printTimer(const std::string& timerName)`

輸出單一 timer 的結果。

例如：

```text
parse:  0.001234s
```

錯誤情況：

- 若 timer 不存在，會輸出錯誤訊息
- 若 timer 還在執行中，會輸出錯誤訊息

### `void printAllTimers() const`

用表格列出所有 named timers 與總計時。

如果某個 timer 或總計時仍在執行中，會顯示 `[Running]`，例如：

```text
========= Timer Report =========
Timer Name    Duration (seconds)
--------------------------------
parse         0.001245
route         0.023801
Total         0.025456
================================
```

## 行為說明與限制

### 分段 timer 不會自動累加

如果你對同一個名稱重複呼叫：

```cpp
timer.startTimer("route");
timer.stopTimer("route");

timer.startTimer("route");
timer.stopTimer("route");
```

最後保存的是第二次量測結果，不是兩次相加。

### `printAllTimers()` 會依照第一次啟動 timer 的順序輸出

named timers 內部雖然仍然使用 `std::unordered_map` 做名稱查找，但 `Timer` 另外維護了一份順序清單，所以 `printAllTimers()` 會按照第一次呼叫 `startTimer()` 的順序列印。

例如：

- 先呼叫 `startTimer("Parse")`
- 再呼叫 `startTimer("Route")`
- 最後呼叫 `startTimer("Output")`

那麼輸出順序就會是：

- `Parse`
- `Route`
- `Output`
- `Total`

### 時間單位固定為秒

所有查詢與輸出都使用秒為單位，型別為 `double`。

### `totalTimer` 與 named timers 互相獨立

總計時不會自動包含或管理各個 named timer，它只是另一個獨立的計時器。你可以只使用其中一種，也可以兩種一起使用。

## 錯誤訊息範例

若操作不存在或狀態不正確的 timer，會從 `std::cerr` 輸出錯誤訊息，例如：

```text
[Timer Error] Cannot stop timer. Timer "route" does not exist.
[Timer Error] Cannot stop timer. Timer "route" is not running.
[Timer Error] Cannot print timer. Timer "route" is still running.
```

## 建議使用方式

- 在程式進入主要流程時呼叫 `startTotalTimer()`
- 每個重要階段各自使用一個有意義的名稱，例如 `parse`、`place`、`route`、`write_output`
- 在除錯或效能分析時用 `printAllTimers()` 一次查看完整資訊
- 若只想快速看總時間，用 `printTotal()` 即可

## 與目前專案的整合方式

在目前專案的 [main.cpp](../../src/main.cpp) 中，`Timer` 已經被拿來量測整體程式執行時間：

```cpp
Timer timer;
timer.startTotalTimer();

// main flow

timer.stopTotalTimer();
timer.printAllTimers();
```

如果之後要把 parser、routing、輸出等階段分開量測，可以直接在各段前後加上：

```cpp
timer.startTimer("stage_name");
// ...
timer.stopTimer("stage_name");
```
