好的，我現在將提供完整的 `diff` 內容，並針對每個 `SEARCH` 和 `REPLACE` 區塊進行詳細解釋，特別指出哪些是新增的、哪些是取代的，以及為何如此修改。

請仔細閱讀以下內容。當您確認無誤後，我會執行 `replace_in_file` 命令。

### `ThirdParty/grbl/grbl/serial.c` 修改方案 (最小改動版本)

---

**區塊 1：引入 Zephyr 頭文件，定義 UART 設備，並宣告新的 UART 中斷回呼函式**

**`SEARCH` 區塊 (舊程式碼)：**
這部分包含了您原有的 FreeRTOS 暫時替代方案，以及在 `ZEPHYR_ARCH` 條件下引入的 Zephyr 頭文件和 UART 設備定義，還有一個 `serial_glue_callback` 的函式宣告。

```c
#include "grbl.h"

//////////暫時替代方案///////////////////////
typedef void *TaskHandle_t;
TaskHandle_t serialTaskHandle = NULL;
#define xTaskNotifyGive(x)
/////////////////////////////////////////////

#define RX_RING_BUFFER (RX_BUFFER_SIZE + 1)
#define TX_RING_BUFFER (TX_BUFFER_SIZE + 1)

uint8_t serial_rx_buffer[RX_RING_BUFFER];
uint8_t serial_rx_buffer_head = 0;
volatile uint8_t serial_rx_buffer_tail = 0;

uint8_t serial_tx_buffer[TX_RING_BUFFER];
uint8_t serial_tx_buffer_head = 0;
volatile uint8_t serial_tx_buffer_tail = 0;

////////////////////////////////////////
#if defined(ZEPHYR_ARCH)
extern TaskHandle_t serialTaskHandle;
#endif
///////////////////////////////////////

#if defined(ZEPHYR_ARCH)
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <string.h>

#define UART_DEVICE_NODE DT_NODELABEL(usart3)
static const struct device *uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);

void serial_glue_callback(const struct device *dev, void *user_data); /// 新宣告的函式
#endif
```

**`REPLACE` 區塊 (新程式碼)：**
這個新區塊主要做了以下修改：
1.  **移除 FreeRTOS 暫時替代方案**：移除了 `TaskHandle_t`、`serialTaskHandle` 和 `xTaskNotifyGive` 等與 FreeRTOS 相關的宏和變數。因為我們將完全切換到 Zephyr 的中斷驅動方式，這些不再需要。
2.  **保留原有緩衝區定義**：`serial_rx_buffer`, `serial_tx_buffer` 及其 `head/tail` 變數被完整保留，以符合您「最小改動」的要求。
3.  **Zephyr 頭文件和 UART 設備定義**：這部分與舊程式碼中 `ZEPHYR_ARCH` 區塊的內容基本一致，但將 `uart_dev` 宣告為 `const` 指針 (`*const`)，是更嚴謹的 Zephyr 推薦用法。
4.  **新的 UART 中斷回呼函式 `uart_grbl_callback`**：
    *   這是一個**全新的函式**，它將是 Zephyr UART 驅動程式調用來處理所有接收和傳送事件的入口。
    *   函式內部包含了您原先在 `ISR(SERIAL_RX)` 中處理即時命令和緩衝普通字元的邏輯，並加入了 Zephyr UART 傳送的處理。

```c
#include "grbl.h"

#define RX_RING_BUFFER (RX_BUFFER_SIZE + 1)
#define TX_RING_BUFFER (TX_BUFFER_SIZE + 1)

uint8_t serial_rx_buffer[RX_RING_BUFFER];
uint8_t serial_rx_buffer_head = 0;
volatile uint8_t serial_rx_buffer_tail = 0;

uint8_t serial_tx_buffer[TX_RING_BUFFER];
uint8_t serial_tx_buffer_head = 0;
volatile uint8_t serial_tx_buffer_tail = 0;

#if defined(ZEPHYR_ARCH)
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/device.h>
#include <string.h>

/* 定義 UART 設備 */
#define UART_NODE DT_NODELABEL(usart3)
static const struct device *const uart_dev = DEVICE_DT_GET(UART_NODE);

/* UART 中斷回呼函式 */
static void uart_grbl_callback(const struct device *dev, void *user_data)
{
    uint8_t c;
    ARG_UNUSED(user_data);

    if (!uart_irq_update(dev)) {
        return;
    }

    /* 處理接收中斷 */
    if (uart_irq_rx_ready(dev)) {
        while (uart_fifo_read(dev, &c, 1) == 1) {
            // 將接收到的字元傳遞給原有的 RX ISR 邏輯（內聯）
            uint8_t next_head;
            switch (c) {
            case CMD_RESET: mc_reset(); break;
            case CMD_STATUS_REPORT: system_set_exec_state_flag(EXEC_STATUS_REPORT); break;
            case CMD_CYCLE_START: system_set_exec_state_flag(EXEC_CYCLE_START); break;
            case CMD_FEED_HOLD: system_set_exec_state_flag(EXEC_FEED_HOLD); break;
            default:
                if (c > 0x7F) { // Real-time control characters are extended ACSII only.
                    switch (c) {
                    case CMD_SAFETY_DOOR: system_set_exec_state_flag(EXEC_SAFETY_DOOR); break;
                    case CMD_JOG_CANCEL:
                        if (sys.state & STATE_JOG) { system_set_exec_state_flag(EXEC_MOTION_CANCEL); } break;
                    case CMD_IO_STATUS_REPORT: system_set_exec_user_defined_flag(EXEC_IO_STATUS_REPORT); break;
#ifdef DEBUG
                    case CMD_DEBUG_REPORT:
                        __disable_irq(); bit_true(sys_rt_exec_debug, EXEC_DEBUG_REPORT); __enable_irq(); break;
#endif
                    case CMD_FEED_OVR_RESET: system_set_exec_motion_override_flag(EXEC_FEED_OVR_RESET); break;
                    case CMD_FEED_OVR_COARSE_PLUS: system_set_exec_motion_override_flag(EXEC_FEED_OVR_COARSE_PLUS); break;
                    case CMD_FEED_OVR_COARSE_MINUS: system_set_exec_motion_override_flag(EXEC_FEED_OVR_COARSE_MINUS); break;
                    case CMD_FEED_OVR_FINE_PLUS: system_set_exec_motion_override_flag(EXEC_FEED_OVR_FINE_PLUS); break;
                    case CMD_FEED_OVR_FINE_MINUS: system_set_exec_motion_override_flag(EXEC_FEED_OVR_FINE_MINUS); break;
                    case CMD_RAPID_OVR_RESET: system_set_exec_motion_override_flag(EXEC_RAPID_OVR_RESET); break;
                    case CMD_RAPID_OVR_MEDIUM: system_set_exec_motion_override_flag(EXEC_RAPID_OVR_MEDIUM); break;
                    case CMD_RAPID_OVR_LOW: system_set_exec_motion_override_flag(EXEC_RAPID_OVR_LOW); break;
                    case CMD_SPINDLE_OVR_RESET: system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_RESET); break;
                    case CMD_SPINDLE_OVR_COARSE_PLUS: system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_COARSE_PLUS); break;
                    case CMD_SPINDLE_OVR_COARSE_MINUS: system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_COARSE_MINUS); break;
                    case CMD_SPINDLE_OVR_FINE_PLUS: system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_FINE_PLUS); break;
                    case CMD_SPINDLE_OVR_FINE_MINUS: system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_FINE_MINUS); break;
                    case CMD_SPINDLE_OVR_STOP: system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_STOP); break;
                    case CMD_COOLANT_FLOOD_OVR_TOGGLE: system_set_exec_accessory_override_flag(EXEC_COOLANT_FLOOD_OVR_TOGGLE); break;
#ifdef ENABLE_M7
                    case CMD_COOLANT_MIST_OVR_TOGGLE: system_set_exec_accessory_override_flag(EXEC_COOLANT_MIST_OVR_TOGGLE); break;
#endif
                    }
                } else { // Write character to buffer
                    next_head = serial_rx_buffer_head + 1;
                    if (next_head == RX_RING_BUFFER) { next_head = 0; }
                    if (next_head != serial_rx_buffer_tail) {
                        serial_rx_buffer[serial_rx_buffer_head] = c;
                        serial_rx_buffer_head = next_head;
                    }
                }
                break;
            }
        }
    }

    /* 處理傳送中斷 */
    if (uart_irq_tx_ready(dev)) {
        uint8_t tail = serial_tx_buffer_tail;
        if (tail != serial_tx_buffer_head) {
            uint8_t tx_data = serial_tx_buffer[tail];
            uart_fifo_fill(dev, &tx_data, 1);
            tail++;
            if (tail == TX_RING_BUFFER) { tail = 0; }
            serial_tx_buffer_tail = tail;
        } else {
            uart_irq_tx_disable(dev);
        }
    }
}
#endif // ZEPHYR_ARCH
```

---

**區塊 2：`serial_init()` 函式 - Zephyr 架構初始化**

**`SEARCH` 區塊 (舊程式碼)：**
這是 `serial_init()` 中 `ZEPHYR_ARCH` 條件下的舊初始化邏輯，主要用於清空緩衝區。

```c
#elif defined(ZEPHYR_ARCH)
  // clear ring buffer
  serial_rx_buffer_head = 0;
  serial_rx_buffer_tail = 0;
  serial_tx_buffer_head = 0;
  serial_tx_buffer_tail = 0;
#endif // AVR_ARCH
```

**`REPLACE` 區塊 (新程式碼)：**
這個新區塊將取代舊的 Zephyr 初始化邏輯，加入了 Zephyr UART 驅動的初始化步驟。
1.  **設備就緒檢查**：`if (!device_is_ready(uart_dev))` 確保 UART 設備已準備好使用。
2.  **UART 配置**：`struct uart_config` 結構體用於設定 UART 的波特率、資料位、奇偶校驗、停止位和流控制。即使 Devicetree 已有設定，在這裡明確配置也是一個好習慣，可以覆蓋或確認。
3.  **錯誤檢查**：`uart_configure(uart_dev, &uart_cfg)` 嘗試配置 UART，並檢查是否成功。
4.  **設定中斷回呼**：`uart_irq_callback_set(uart_dev, uart_grbl_callback)` 將我們前面定義的 `uart_grbl_callback` 註冊為 UART 的中斷處理函式。
5.  **啟用接收中斷**：`uart_irq_rx_enable(uart_dev)` 啟用 UART 接收中斷，這樣當有資料到達時，`uart_grbl_callback` 就會被觸發。
6.  **保留緩衝區清空**：`serial_rx_buffer_head = 0;` 等行保留，確保緩衝區在初始化時被清空。

```c
#elif defined(ZEPHYR_ARCH)
    if (!device_is_ready(uart_dev)) {
        return;
    }

    /* 設定 UART 波特率 (如果 Devicetree 未設定或需覆蓋) */
    struct uart_config uart_cfg = {
        .baudrate = 115200,
        .data_bits = UART_CFG_DATA_BITS_8,
        .parity = UART_CFG_PARITY_NONE,
        .stop_bits = UART_CFG_STOP_BITS_1,
        .flow_ctrl = UART_CFG_FLOW_CTRL_NONE
    };

    int err = uart_configure(uart_dev, &uart_cfg);
    if (err) {
        return;
    }

    /* 設定中斷與回呼 */
    uart_irq_callback_set(uart_dev, uart_grbl_callback);
    
    /* 啟用接收中斷 */
    uart_irq_rx_enable(uart_dev);

  // clear ring buffer
  serial_rx_buffer_head = 0;
  serial_rx_buffer_tail = 0;
  serial_tx_buffer_head = 0;
  serial_tx_buffer_tail = 0;
#endif // AVR_ARCH
```

---

**區塊 3：`serial_write()` 函式 - Zephyr 架構傳送邏輯**

**`SEARCH` 區塊 (舊程式碼)：**
這是 `serial_write()` 中 `ZEPHYR_ARCH` 條件下的舊邏輯，主要通過 `xTaskNotifyGive` 通知 FreeRTOS 任務。

```c
#elif defined(ZEPHYR_ARCH)
  // give the task notification to the serial task
  xTaskNotifyGive(serialTaskHandle);
#endif // AVR_ARCH
```

**`REPLACE` 區塊 (新程式碼)：**
這個新區塊將取代 FreeRTOS 的通知機制，改用 Zephyr UART 的中斷驅動傳送。
1.  **保留緩衝區寫入邏輯**：資料依然先寫入 `serial_tx_buffer` 並更新 `serial_tx_buffer_head`，與原有邏輯完全一致。
2.  **`k_yield()`**：當傳送緩衝區滿時，讓出 CPU，避免忙等待。
3.  **啟用傳送中斷**：`uart_irq_tx_enable(uart_dev)` 啟用 UART 傳送中斷。這會觸發 `uart_grbl_callback` 的傳送部分來實際從緩衝區取出資料並發送。這取代了 FreeRTOS 的任務通知機制。

```c
#elif defined(ZEPHYR_ARCH)
  // Calculate next head
  uint8_t next_head = serial_tx_buffer_head + 1;
  if (next_head == TX_RING_BUFFER)
  {
    next_head = 0;
  }

  // Wait until there is space in the buffer
  while (next_head == serial_tx_buffer_tail)
  {
    if (sys_rt_exec_state & EXEC_RESET)
    { return; }
    k_yield(); // 讓出 CPU
  }

  // Store data and advance head
  serial_tx_buffer[serial_tx_buffer_head] = data;
  serial_tx_buffer_head = next_head;

  // 啟動傳送中斷
  uart_irq_tx_enable(uart_dev);
#endif // AVR_ARCH
```

---

**區塊 4：`serial_tx_irq()` 函式 - Zephyr 架構被取代**

**`SEARCH` 區塊 (舊程式碼)：**
這是 `serial_tx_irq()` 中 `ZEPHYR_ARCH` 條件下的舊邏輯。

```c
#elif defined(ZEPHYR_ARCH)
uint8_t serial_tx_irq()
{
  uint8_t UDR0;
#endif // AVR_ARCH

  uint8_t tail = serial_tx_buffer_tail; // Temporary serial_tx_buffer_tail (to optimize for volatile)

  // Send a byte from the buffer
  UDR0 = serial_tx_buffer[tail];

  // Update tail position
  tail++;
  if (tail == TX_RING_BUFFER)
  {
    tail = 0;
  }

  serial_tx_buffer_tail = tail;

#if defined(AVR_ARCH)
  // Turn off Data Register Empty Interrupt to stop tx-streaming if this concludes the transfer
  if (tail == serial_tx_buffer_head)
  {
    UCSR0B &= ~(1 << UDRIE0);
  }
#elif defined(ZEPHYR_ARCH)
  return UDR0;
#endif // AVR_ARCH
}
```

**`REPLACE` 區塊 (新程式碼)：**
這個區塊被完全移除。`serial_tx_irq()` 的邏輯已**被內聯到 `uart_grbl_callback` 的傳送中斷處理部分**。Zephyr 使用統一的中斷回呼函式來處理所有 UART 事件，而不是獨立的 TX ISR 函式。

```c
#endif // AVR_ARCH
}
```

---

**區塊 5：`serial_rx_irq()` 函式 - Zephyr 架構被取代，邏輯內聯**

**`SEARCH` 區塊 (舊程式碼)：**
這是 `serial_rx_irq()` 中 `ZEPHYR_ARCH` 條件下的舊邏輯。

```c
#elif defined(ZEPHYR_ARCH)
void serial_rx_irq(uint8_t data)
{
#endif
  uint8_t next_head;

  // Pick off realtime command characters directly from the serial stream. These characters are
  // not passed into the main buffer, but these set system state flag bits for realtime execution.
  switch (data)
  {
  case CMD_RESET:
    mc_reset();
    break; // Call motion control reset routine.
  case CMD_STATUS_REPORT:
    system_set_exec_state_flag(EXEC_STATUS_REPORT);
    break; // Set as true
  case CMD_CYCLE_START:
    system_set_exec_state_flag(EXEC_CYCLE_START);
    break; // Set as true
  case CMD_FEED_HOLD:
    system_set_exec_state_flag(EXEC_FEED_HOLD);
    break; // Set as true
  default:
    if (data > 0x7F)
    { // Real-time control characters are extended ACSII only.
      switch (data)
      {
      case CMD_SAFETY_DOOR:
        system_set_exec_state_flag(EXEC_SAFETY_DOOR);
        break; // Set as true
      case CMD_JOG_CANCEL:
        if (sys.state & STATE_JOG)
        { // Block all other states from invoking motion cancel.
          system_set_exec_state_flag(EXEC_MOTION_CANCEL);
        }
        break;
#if defined(ZEPHYR_ARCH)
      case CMD_IO_STATUS_REPORT:
        system_set_exec_user_defined_flag(EXEC_IO_STATUS_REPORT);
        break;
#endif
#ifdef DEBUG
#if defined(AVR_ARCH)
      case CMD_DEBUG_REPORT:
      {
        uint8_t sreg = SREG;
        cli();
        bit_true(sys_rt_exec_debug, EXEC_DEBUG_REPORT);
        SREG = sreg;
      }
      break;
#elif defined(ZEPHYR_ARCH)
      case CMD_DEBUG_REPORT:
      {
        __disable_irq();
        bit_true(sys_rt_exec_debug, EXEC_DEBUG_REPORT);
        __enable_irq();
      }
      break;
#endif
#endif
      case CMD_FEED_OVR_RESET:
        system_set_exec_motion_override_flag(EXEC_FEED_OVR_RESET);
        break;
      case CMD_FEED_OVR_COARSE_PLUS:
        system_set_exec_motion_override_flag(EXEC_FEED_OVR_COARSE_PLUS);
        break;
      case CMD_FEED_OVR_COARSE_MINUS:
        system_set_exec_motion_override_flag(EXEC_FEED_OVR_COARSE_MINUS);
        break;
      case CMD_FEED_OVR_FINE_PLUS:
        system_set_exec_motion_override_flag(EXEC_FEED_OVR_FINE_PLUS);
        break;
      case CMD_FEED_OVR_FINE_MINUS:
        system_set_exec_motion_override_flag(EXEC_FEED_OVR_FINE_MINUS);
        break;
      case CMD_RAPID_OVR_RESET:
        system_set_exec_motion_override_flag(EXEC_RAPID_OVR_RESET);
        break;
      case CMD_RAPID_OVR_MEDIUM:
        system_set_exec_motion_override_flag(EXEC_RAPID_OVR_MEDIUM);
        break;
      case CMD_RAPID_OVR_LOW:
        system_set_exec_motion_override_flag(EXEC_RAPID_OVR_LOW);
        break;
      case CMD_SPINDLE_OVR_RESET:
        system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_RESET);
        break;
      case CMD_SPINDLE_OVR_COARSE_PLUS:
        system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_COARSE_PLUS);
        break;
      case CMD_SPINDLE_OVR_COARSE_MINUS:
        system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_COARSE_MINUS);
        break;
      case CMD_SPINDLE_OVR_FINE_PLUS:
        system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_FINE_PLUS);
        break;
      case CMD_SPINDLE_OVR_FINE_MINUS:
        system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_FINE_MINUS);
        break;
      case CMD_SPINDLE_OVR_STOP:
        system_set_exec_accessory_override_flag(EXEC_SPINDLE_OVR_STOP);
        break;
      case CMD_COOLANT_FLOOD_OVR_TOGGLE:
        system_set_exec_accessory_override_flag(EXEC_COOLANT_FLOOD_OVR_TOGGLE);
        break;
#ifdef ENABLE_M7
      case CMD_COOLANT_MIST_OVR_TOGGLE:
        system_set_exec_accessory_override_flag(EXEC_COOLANT_MIST_OVR_TOGGLE);
        break;
#endif
      }
      // Throw away any unfound extended-ASCII character by not passing it to the serial buffer.
    }
    else
    { // Write character to buffer
      next_head = serial_rx_buffer_head + 1;
      if (next_head == RX_RING_BUFFER)
      {
        next_head = 0;
      }

      // Write data to buffer unless it is full.
      if (next_head != serial_rx_buffer_tail)
      {
        serial_rx_buffer[serial_rx_buffer_head] = data;
        serial_rx_buffer_head = next_head;
      }
    }
  }
#endif // AVR_ARCH
}
```

**`REPLACE` 區塊 (新程式碼)：**
這個區塊也被完全移除。`serial_rx_irq()` 的邏輯已**被內聯到 `uart_grbl_callback` 的接收中斷處理部分**。Zephyr 的 UART 中斷模型使用單一回呼函式來處理所有事件。

```c
#endif // AVR_ARCH
}
```

---
