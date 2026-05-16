# AGENTS.md
> Руководство для AI-агентов (Claude Code, Codex, Devin и др.), работающих с кодовой базой **Momentum Firmware** — в особенности с подсистемой **SubGHz**.

---

## Содержание

1. [Обзор проекта](#1-обзор-проекта)
2. [Структура репозитория](#2-структура-репозитория)
3. [Сборка и окружение](#3-сборка-и-окружение)
4. [SubGHz: карта кода](#4-subghz-карта-кода)
5. [Архитектурные инварианты](#5-архитектурные-инварианты)
6. [Соглашения по коду](#6-соглашения-по-коду)
7. [Паттерны и антипаттерны](#7-паттерны-и-антипаттерны)
8. [Работа с протоколами](#8-работа-с-протоколами)
9. [Тестирование](#9-тестирование)
10. [Правила для агентов](#10-правила-для-агентов)
11. [Частые задачи и как их делать](#11-частые-задачи-и-как-их-делать)
12. [Чего делать нельзя](#12-чего-делать-нельзя)

---

## 1. Обзор проекта

**Momentum Firmware** — кастомная прошивка для [Flipper Zero](https://flipperzero.one), написанная на C, основанная на официальном OFW и включающая расширения из Unleashed. Целевая платформа: STM32WB55 (Cortex-M4, 256 KB RAM, 1 MB Flash + external).

ОС: **FreeRTOS** через тонкий слой **Furi** (Flipper Universal Registry Interface). Все потоки — `FuriThread`, очереди — `FuriMessageQueue`, мьютексы — `FuriMutex`. Стандартная библиотека C доступна, но ограничена (нет heap realloc в ISR, стек задач мал).

**SubGHz** — приложение и библиотека для работы с радиосигналами в диапазоне sub-1 GHz через CC1101 (внутренний и внешний). Это одна из самых сложных частей прошивки.

---

## 2. Структура репозитория

```
Momentum-Firmware/
├── applications/
│   ├── main/
│   │   └── subghz/               ← Основное приложение SubGHz
│   │       ├── subghz.c/.h       ← Точка входа, alloc/free/main loop
│   │       ├── subghz_i.c/.h     ← Internal helpers (tx_start, rx_start и т.д.)
│   │       ├── subghz_worker.c/.h← Background worker для декодирования
│   │       ├── subghz_history.c/.h← Хранение принятых сигналов в RAM
│   │       ├── subghz_last_settings.c/.h ← Персистентные настройки (SD)
│   │       ├── subghz_dangerous_freq.c   ← Список заблокированных частот
│   │       ├── helpers/
│   │       │   ├── subghz_txrx.c/.h      ← Главный helper: TX/RX/device mgmt
│   │       │   └── subghz_types.h        ← Общие типы приложения
│   │       ├── scenes/           ← Все UI-сцены (по одному файлу на сцену)
│   │       │   ├── subghz_scene_config.h ← Регистрация всех сцен (enum)
│   │       │   ├── subghz_scene_start.c
│   │       │   ├── subghz_scene_receiver.c
│   │       │   ├── subghz_scene_transmitter.c
│   │       │   ├── subghz_scene_read_raw.c
│   │       │   ├── subghz_scene_decode_raw.c
│   │       │   ├── subghz_scene_receiver_info.c
│   │       │   ├── subghz_scene_save_name.c
│   │       │   └── ...
│   │       └── views/            ← Кастомные View (кроме стандартных виджетов)
│   │           ├── receiver.c/.h
│   │           └── transmitter.c/.h
│   │
│   └── drivers/
│       └── subghz/
│           └── cc1101_ext/       ← Драйвер внешнего CC1101
│               ├── cc1101_ext.c  ← Основной драйвер (958 строк)
│               ├── cc1101_ext.h
│               └── cc1101_ext_interconnect.c ← Адаптер к SubGhzDevice API
│
├── lib/
│   └── subghz/                   ← Разделяемая библиотека (используется и main app, и внешними FAP)
│       ├── protocols/            ← Реализации протоколов (>50 файлов)
│       │   ├── protocol_items.c/.h ← Реестр всех протоколов
│       │   ├── keeloq.c/.h
│       │   ├── princeton.c/.h
│       │   └── ...
│       ├── devices/              ← Абстракция устройств (int/ext CC1101)
│       │   ├── devices.c/.h      ← SubGhzDevices API
│       │   ├── cc1101_int/       ← Внутренний CC1101
│       │   └── types.h
│       ├── blocks/               ← Переиспользуемые блоки декодирования
│       │   ├── generic.c/.h
│       │   ├── math.c/.h
│       │   └── custom_btn.c/.h
│       ├── receiver.c/.h         ← SubGhzReceiver (диспетчер декодеров)
│       ├── transmitter.c/.h      ← SubGhzTransmitter
│       ├── subghz_setting.c/.h   ← Частоты, пресеты, валидация
│       └── subghz_protocol_registry.h
│
├── targets/f7/                   ← HAL для STM32WB55
│   └── furi_hal/
│       ├── furi_hal_subghz.c     ← Низкоуровневый HAL для встроенного CC1101
│       └── furi_hal_subghz.h
│
└── applications/momentum/        ← Momentum-специфичные настройки
    └── momentum_settings.c/.h    ← Глобальные настройки прошивки
```

---

## 3. Сборка и окружение

### Требования
- Python 3.8+, GCC ARM (`arm-none-eabi-gcc`), SCons
- Всё управляется через **fbt** (Flipper Build Tool) — обёртка над SCons

### Основные команды
```bash
# Полная сборка и прошивка (Flipper подключён по USB)
./fbt flash_usb_full

# Собрать конкретное приложение и запустить
./fbt launch APPSRC=subghz

# Собрать без прошивки
./fbt updater_package

# Запустить тесты
./fbt test

# Запустить только тесты subghz
./fbt test TESTS=subghz
```

### Важные флаги компилятора
- `-Wundef` — включён, все неопределённые макросы — ошибка. При добавлении нового `#ifdef` убедись что символ определён везде.
- `-Os` — оптимизация по размеру. Не рассчитывай на конкретный layout стека при отладке.
- Нет исключений, нет RTTI — чистый C (файлы `.c`, не `.cpp`).

### Ограничения платформы
| Ресурс | Лимит | Комментарий |
|--------|-------|-------------|
| RAM (total) | 256 KB | Из них ~100 KB — heap для приложений |
| Stack на задачу | 2–4 KB | По умолчанию. Превышение → HardFault |
| Flash | 1 MB | Прошивка + ресурсы. Следи за размером |
| FreeRTOS tick | 1 мс | Минимальная задержка `furi_delay_ms(1)` |
| SPI bus | Shared | Всегда использовать `furi_hal_spi_acquire/release` |

---

## 4. SubGHz: карта кода

### Поток данных при приёме сигнала

```
RF Signal
    │
    ▼
[CC1101 hardware] ──interrupt──▶ [DMA / GPIO callback]
    │                                    │
    │                                    ▼
    │                         [subghz_worker FuriThread]
    │                                    │
    │                         SubGhzWorker.callback()
    │                                    │
    │                                    ▼
    │                         [SubGhzReceiver]
    │                         subghz_receiver_decode()
    │                                    │
    │                    ┌───────────────┴───────────────┐
    │                    ▼                               ▼
    │           [Protocol Decoder 1]          [Protocol Decoder N]
    │           (e.g. keeloq_decoder)         (e.g. princeton)
    │                    │
    │           decoder_feed(level, duration)
    │                    │
    │           (если успешно декодировано)
    │                    ▼
    │           decoder_callback(decoder, event, context)
    │                    │
    │                    ▼
    │           [subghz_scene_receiver.c]
    │           subghz_rx_key_state_add_data_to_history()
    │                    │
    │                    ▼
    │           [SubGhzHistory]
    │           subghz_history_add_to_history()
    │                    │
    │                    ▼
    │           [UI refresh via ViewDispatcher]
```

### Поток данных при передаче

```
User (press OK in Transmitter scene)
    │
    ▼
subghz_scene_transmitter_on_event()
    │
    ▼
subghz_tx_start(subghz, transmitter)  [subghz_i.c]
    │
    ▼
subghz_txrx_tx_start(txrx, transmitter)  [subghz_txrx.c]
    │
    ├─▶ subghz_devices_check_tx(device, freq)   ← проверка региона
    │
    ├─▶ SubGhzTransmitter.encode()              ← протокол → массив duration
    │
    └─▶ subghz_devices_start_async_tx(device, callback, ctx)
            │
            ▼
        [DMA TIM + GPIO]  ──▶  CC1101 TX pin  ──▶  RF Signal
```

### Ключевые типы данных

```c
// Главная структура приложения
typedef struct {
    SceneManager*     scene_manager;
    ViewDispatcher*   view_dispatcher;
    SubGhzTxRx*       txrx;           // весь TX/RX контекст
    SubGhzHistory*    history;         // принятые сигналы
    SubGhzLastSettings* last_settings; // настройки с SD
    // ... views ...
} SubGhz;

// TX/RX контекст (subghz_txrx.h)
typedef struct {
    SubGhzWorker*          worker;
    SubGhzEnvironment*     environment;
    SubGhzReceiver*        receiver;
    SubGhzTransmitter*     transmitter;
    SubGhzRadioDeviceType  radio_device_type;
    SubGhzSetting*         setting;
    // состояние, частота, пресет...
} SubGhzTxRx;

// Запись в истории
typedef struct {
    FlipperFormat*   flipper_format; // сериализованные данные
    FuriString*      item_str;       // отображаемая строка
    SubGhzRadioPreset preset;        // частота + модуляция
    uint8_t          type;           // тип протокола
} SubGhzHistoryItem;
```

---

## 5. Архитектурные инварианты

Эти правила **нельзя нарушать** ни при каком рефакторинге или добавлении фич:

### INV-01: Слоёвая модель (строго сверху вниз)
```
UI (scenes/) → app logic (subghz_i.c) → txrx helper → lib/subghz → HAL → hardware
```
- `scenes/` не вызывают напрямую `furi_hal_subghz_*` или `cc1101_*`
- `lib/subghz/` не знает о `SubGhz` (главной структуре приложения)
- `applications/drivers/` не читают `momentum_settings` — только через переданный конфиг (см. TODO BUG-03)

### INV-02: SPI всегда через acquire/release
```c
// ПРАВИЛЬНО:
furi_hal_spi_acquire(handle);
cc1101_read_reg(handle, addr, &val);
furi_hal_spi_release(handle);

// НЕПРАВИЛЬНО (никогда):
cc1101_read_reg(handle, addr, &val);  // без acquire
```
SPI шина разделяется между несколькими устройствами. Забытый `release` → дедлок всей системы.

### INV-03: Никаких аллокаций в ISR / DMA callbacks
В функциях, вызываемых из прерываний (DMA callback, GPIO interrupt): нельзя `malloc`, нельзя `furi_message_queue_put` с таймаутом > 0, нельзя `FURI_LOG_*` (он блокирующий).

Разрешено в ISR: `furi_message_queue_put(..., 0)` (non-blocking), операции с атомарными переменными, `volatile` флаги.

### INV-04: Состояние CC1101 — конечный автомат
Допустимые переходы:
```
Init → Idle → AsyncRx → Idle
                      ↘ AsyncTx → Idle
```
Нельзя вызывать `start_async_tx` если state != Idle. Нельзя вызывать `stop_async_rx` если state != AsyncRx. Проверяй `furi_assert(state == ожидаемое_состояние)` перед каждой операцией.

### INV-05: Протоколы — stateless между сигналами
Декодер протокола (`SubGhzProtocolDecoderXxx`) обязан корректно работать после `reset()`. Состояние декодера не должно "перетекать" между независимыми сигналами.

---

## 6. Соглашения по коду

### Именование
```c
// Типы: CamelCase с префиксом модуля
typedef struct SubGhzWorker SubGhzWorker;
typedef enum SubGhzWorkerEvent SubGhzWorkerEvent;

// Функции: snake_case с префиксом модуля
SubGhzWorker* subghz_worker_alloc(void);
void          subghz_worker_free(SubGhzWorker* instance);
void          subghz_worker_start(SubGhzWorker* instance);

// Константы/макросы: SCREAMING_SNAKE_CASE с префиксом
#define SUBGHZ_WORKER_OVERRUN_THRESHOLD (50u)

// Приватные функции (static): без суффикса _pub, с кратким именем
static void subghz_worker_thread_callback(void* context);

// Теги для логгера — строго в начале .c файла
#define TAG "SubGhzWorker"
```

### Управление памятью
- Каждый модуль предоставляет пару `_alloc()` / `_free()`
- `_alloc()` инициализирует ВСЕ поля, никаких uninit членов
- `_free()` освобождает все вложенные ресурсы перед `free(instance)`
- Передача владения через комментарий: `// transfers ownership` или `// borrows`

```c
// ПРАВИЛЬНО: явная инициализация
SubGhzWorker* subghz_worker_alloc(void) {
    SubGhzWorker* instance = malloc(sizeof(SubGhzWorker));
    instance->thread   = furi_thread_alloc();
    instance->running  = false;
    instance->callback = NULL;
    instance->context  = NULL;
    return instance;
}

// НЕПРАВИЛЬНО:
SubGhzWorker* subghz_worker_alloc(void) {
    SubGhzWorker* instance = malloc(sizeof(SubGhzWorker));
    // поля не инициализированы — UB при первом обращении
    return instance;
}
```

### Логирование
```c
#define TAG "SubGhzMyModule"

FURI_LOG_E(TAG, "Fatal: %s failed with code %d", op_name, code); // ошибки
FURI_LOG_W(TAG, "Frequency %lu out of range, clamped", freq);    // предупреждения
FURI_LOG_I(TAG, "Init OK, device version: %d", version);         // информация
FURI_LOG_D(TAG, "Reg[%02X] = %02X", addr, val);                  // отладка (только при DEBUG)

// НЕ использовать:
printf("...");   // не попадает в FURI trace
```

### Обработка ошибок
```c
// Используй furi_assert для инвариантов (убивает процесс — только для "не должно случиться")
furi_assert(instance != NULL);
furi_assert(state == SubGhzWorkerStateIdle);

// Используй furi_check для проверок контрактов (с сообщением)
furi_check(subghz_worker != NULL, "Worker must be allocated before start");

// Для recoverable ошибок — возвращай bool или enum
bool subghz_txrx_tx_start(SubGhzTxRx* txrx, SubGhzTransmitter* transmitter);
// false = ошибка, подробности — через FURI_LOG_E
```

---

## 7. Паттерны и антипаттерны

### ✅ Правильный паттерн: Scene On Enter / On Event / On Exit

```c
// subghz_scene_foo.c

void subghz_scene_foo_on_enter(void* context) {
    SubGhz* subghz = context;
    // настроить view, подписаться на callbacks
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewFoo);
}

bool subghz_scene_foo_on_event(void* context, SceneManagerEvent event) {
    SubGhz* subghz = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case SubGhzCustomEventFooAction:
            scene_manager_next_scene(subghz->scene_manager, SubGhzSceneBar);
            consumed = true;
            break;
        }
    }
    return consumed;
}

void subghz_scene_foo_on_exit(void* context) {
    SubGhz* subghz = context;
    // очистить view, отписаться от callbacks
}
```

### ✅ Правильный паттерн: добавление новой настройки

1. Добавить поле в `SubGhzLastSettings` (subghz_last_settings.h)
2. Инициализировать дефолтом в `subghz_last_settings_alloc()`
3. Сохранять/загружать через `FlipperFormat` в `_load()` / `_save()`
4. Использовать в нужной сцене через `subghz->last_settings->my_field`

### ❌ Антипаттерн: прямой доступ к глобальному состоянию из сцены

```c
// ПЛОХО: сцена читает глобальные настройки напрямую
void subghz_scene_foo_on_enter(void* context) {
    if(momentum_settings.subghz_bypass_region) { ... }  // ← нарушение слоёв
}

// ХОРОШО: через txrx или last_settings
void subghz_scene_foo_on_enter(void* context) {
    SubGhz* subghz = context;
    if(subghz->txrx->setting->bypass_region) { ... }
}
```

### ❌ Антипаттерн: long operation в UI callback

```c
// ПЛОХО: тяжёлая операция прямо в on_event
bool subghz_scene_foo_on_event(void* context, SceneManagerEvent event) {
    // 200 мс I/O операция в UI thread → фриз
    subghz_last_settings_save(subghz->last_settings);
    return true;
}

// ХОРОШО: отложить через таймер или выполнить в on_exit
void subghz_scene_foo_on_exit(void* context) {
    SubGhz* subghz = context;
    subghz_last_settings_save(subghz->last_settings);
}
```

### ❌ Антипаттерн: вызов `malloc` в tight loop

```c
// ПЛОХО: аллокация на каждый принятый сигнал
void on_signal_received(...) {
    char* buf = malloc(64);    // ← фрагментация heap
    snprintf(buf, 64, ...);
    display(buf);
    free(buf);
}

// ХОРОШО: статический или стековый буфер
void on_signal_received(...) {
    char buf[64];
    snprintf(buf, sizeof(buf), ...);
    display(buf);
}
```

---

## 8. Работа с протоколами

### Структура протокола

Каждый протокол в `lib/subghz/protocols/` реализует интерфейс через таблицу функций:

```c
// Минимальный шаблон нового протокола: my_protocol.h
#pragma once
#include <lib/subghz/protocols/base.h>

extern const SubGhzProtocolDecoder subghz_protocol_decoder_my_protocol;
extern const SubGhzProtocolEncoder subghz_protocol_encoder_my_protocol;
extern const SubGhzProtocol subghz_protocol_my_protocol;
```

```c
// my_protocol.c
#define TAG "SubGhzMyProtocol"

// Структура состояния декодера
typedef struct {
    SubGhzProtocolDecoderBase base;  // ОБЯЗАТЕЛЬНО первым полем
    // ... поля состояния ...
    uint32_t data;
    uint8_t  data_count_bit;
} SubGhzProtocolDecoderMyProtocol;

// Обязательные функции декодера:
static void* subghz_protocol_decoder_my_protocol_alloc(SubGhzEnvironment* environment);
static void  subghz_protocol_decoder_my_protocol_free(void* context);
static void  subghz_protocol_decoder_my_protocol_reset(void* context);
static void  subghz_protocol_decoder_my_protocol_feed(void* context, bool level, uint32_t duration);
static uint8_t subghz_protocol_decoder_my_protocol_get_hash_data(void* context);
static SubGhzProtocolStatus subghz_protocol_decoder_my_protocol_serialize(void* context, FlipperFormat* flipper_format, SubGhzRadioPreset* preset);
static SubGhzProtocolStatus subghz_protocol_decoder_my_protocol_deserialize(void* context, FlipperFormat* flipper_format);
static void  subghz_protocol_decoder_my_protocol_get_string(void* context, FuriString* output);

// Регистрация
const SubGhzProtocol subghz_protocol_my_protocol = {
    .name     = "MyProtocol",
    .type     = SubGhzProtocolTypeStatic,   // Static / Dynamic / RAW
    .flag     = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM | SubGhzProtocolFlag_Decodable | SubGhzProtocolFlag_Save | SubGhzProtocolFlag_Send,
    .decoder  = &subghz_protocol_decoder_my_protocol,
    .encoder  = &subghz_protocol_encoder_my_protocol,
};
```

После создания файлов — добавить в `lib/subghz/protocols/protocol_items.c`:
```c
// В массив SubGhzProtocolRegistry protocol_items[]:
&subghz_protocol_my_protocol,
```

И в `lib/subghz/protocols/protocol_items.h` — добавить `#include "my_protocol.h"`.

### Параметры флагов протокола

```c
SubGhzProtocolFlag_433    // работает на 433 МГц
SubGhzProtocolFlag_868    // работает на 868 МГц
SubGhzProtocolFlag_315    // работает на 315 МГц
SubGhzProtocolFlag_AM     // AM модуляция
SubGhzProtocolFlag_FM     // FM модуляция
SubGhzProtocolFlag_Decodable  // можно декодировать (иначе только сохранить RAW)
SubGhzProtocolFlag_Save       // можно сохранить в .sub файл
SubGhzProtocolFlag_Send       // можно воспроизвести (re-transmit)
```

### Паттерн декодирования (конечный автомат)

```c
void subghz_protocol_decoder_my_protocol_feed(void* context, bool level, uint32_t duration) {
    SubGhzProtocolDecoderMyProtocol* instance = context;

    switch(instance->parser_step) {
    case MyProtocolStepReset:
        // ждём преамбулу
        if(DURATION_DIFF(duration, MY_PREAMBLE_US) < MY_TOLERANCE) {
            instance->parser_step = MyProtocolStepFoundHeader;
        }
        break;

    case MyProtocolStepFoundHeader:
        // декодируем биты
        if(level && DURATION_DIFF(duration, MY_BIT_1_US) < MY_TOLERANCE) {
            subghz_protocol_blocks_add_bit(&instance->data_count_bit, 1);
            // ...
        }
        // ...
        if(instance->data_count_bit == MY_TOTAL_BITS) {
            // сигнал полностью декодирован
            if(instance->base.callback) {
                instance->base.callback(
                    &instance->base, SubGhzProtocolDecoderBaseEventDataReady, instance->base.context);
            }
            instance->parser_step = MyProtocolStepReset;
        }
        break;
    }
}
```

---

## 9. Тестирование

### Расположение тестов
```
applications/debug/unit_tests/
└── subghz/
    ├── subghz_test.c         ← Главный файл тестов
    └── test_data/            ← .sub файлы как тестовые векторы
        ├── keeloq_test.sub
        ├── princeton_test.sub
        └── ...
```

### Запуск тестов
```bash
./fbt test TESTS=subghz
# или через CLI на устройстве:
# unit_tests subghz
```

### Паттерн написания теста для протокола

```c
// В subghz_test.c
MU_TEST(test_subghz_decode_my_protocol) {
    // Загрузить тестовый .sub файл
    FlipperFormat* flipper_format = flipper_format_file_alloc(storage);
    mu_assert(
        flipper_format_file_open_existing(flipper_format, EXT_PATH("unit_tests/subghz/my_protocol_test.sub")),
        "Cannot open test file");

    // Декодировать
    SubGhzTestData test_data = subghz_test_run_decoder("MyProtocol", flipper_format);

    // Проверить результат
    mu_assert_int_eq(0xDEADBEEF, test_data.data);
    mu_assert_int_eq(24, test_data.count_bit);
    mu_assert_string_eq("MyProtocol", test_data.protocol_name);

    flipper_format_free(flipper_format);
}
```

### Создание тестового .sub файла
```
Filetype: Flipper SubGhz RAW File
Version: 1
Frequency: 433920000
Preset: FuriHalSubGhzPresetOok650Async
Protocol: RAW
RAW_Data: -1000 350 -350 700 -350 350 -700 ...
```
Записать реальный сигнал устройством, сохранить, добавить в `test_data/`.

---

## 10. Правила для агентов

### 🔴 Обязательные проверки перед любым коммитом

1. **Убедись что сборка не сломана:**
   ```bash
   ./fbt
   ```

2. **Запусти тесты subghz:**
   ```bash
   ./fbt test TESTS=subghz
   ```

3. **Проверь флаг -Wundef:** если добавил новый `#ifdef FOO`, убедись что `FOO` определён в `SConstruct` или передаётся через `fbt_options`.

4. **Проверь размер прошивки:**
   ```bash
   ./fbt size
   ```
   Если Flash вырос больше чем на 1 KB за один PR — объясни почему.

### 🟠 Правила изменения cc1101_ext.c

- Любое изменение конфига (новые поля в `SubGhzDeviceConf`) → обновить `SUBGHZ_DEVICE_CC1101_CONFIG_VER`
- Любое изменение, касающееся GPIO, SPI пинов → проверить оба SPI handle варианта (`SpiDefault` и `SpiExtra`)
- Изменения в DMA callback функциях → объяснить в комментарии, что происходит в ISR контексте

### 🟠 Правила изменения протоколов

- Нельзя менять формат сериализации в `_serialize()` / `_deserialize()` без bump версии `.sub` файла
- Нельзя менять `protocol.name` — это ключ для загрузки сохранённых файлов
- При добавлении нового протокола — добавить минимум один `.sub` тестовый вектор

### 🟡 Правила для UI / scenes

- Нельзя блокировать UI thread дольше 50 мс
- Нельзя вызывать `subghz_txrx_*` функции напрямую из draw callbacks
- Все строки, отображаемые пользователю — через `i18n_get("key")` или хотя бы через `#define` константу (не литерал в середине кода)

### 🟡 Правила для настроек

- Новые настройки — добавлять в `SubGhzLastSettings`, не в `momentum_settings`
- `momentum_settings` — только для глобальных настроек firmware, не специфичных для SubGHz app
- Всегда задавать разумное дефолтное значение в `_alloc()`

---

## 11. Частые задачи и как их делать

### Задача: Добавить новую частоту по умолчанию

**Файл:** `lib/subghz/subghz_setting.c`
1. Найти массив `subghz_setting_freq_default_list[]`
2. Добавить частоту в Hz: `868350000UL`
3. Убедиться что частота проходит `subghz_setting_is_frequency_allowed()` для внутреннего CC1101

### Задача: Добавить новый пресет модуляции

**Файл:** `lib/subghz/subghz_setting.c` + `targets/f7/furi_hal/furi_hal_subghz.c`
1. Добавить конфиг регистров CC1101 в HAL
2. Добавить `SubGhzSetting` запись с именем пресета
3. Пресет должен корректно работать и на internal, и на external CC1101

### Задача: Добавить действие в контекстное меню сигнала

**Файл:** `applications/main/subghz/scenes/subghz_scene_receiver_info.c`
1. Добавить пункт в `submenu_add_item()` в `on_enter`
2. Обработать в `on_event` через `switch(event.event)`
3. При необходимости — создать новую сцену и добавить в `SubGhzScene` enum

### Задача: Исправить поведение при определённом состоянии CC1101

1. Найти нужное состояние через `MARCSTATE` регистр (CC1101 datasheet Table 23)
2. В `cc1101_ext.c` или `furi_hal_subghz.c` — добавить/исправить проверку состояния
3. Всегда проверять `CHIP_RDYn` в возвращаемом `CC1101Status` после критических операций

### Задача: Отладить "сигнал не декодируется"

1. Включить RAW запись: SubGHz → Read RAW
2. Сохранить `.sub` файл
3. Запустить `subghz_receiver_decode()` на файле с включённым `FURI_LOG_D`
4. Смотреть в каком `parser_step` декодер застревает
5. Сравнить тайминги с документацией протокола

---

## 12. Чего делать нельзя

| Запрет | Причина |
|--------|---------|
| `malloc` в DMA/GPIO callbacks | ISR контекст, heap не реентерабелен |
| `furi_delay_ms()` в ISR | Блокирующий вызов в прерывании → HardFault |
| `FURI_LOG_*` в tight RX loop | Блокирует поток, пропуск сигналов |
| Изменение `protocol.name` | Сломает загрузку существующих .sub файлов пользователей |
| Прямой вызов `cc1101_*` из scenes | Нарушение слоёв абстракции |
| `momentum_settings` из `lib/subghz/` | lib/ должна быть переносимой |
| Удаление `furi_hal_spi_release` | Дедлок SPI шины, зависание устройства |
| `realloc` на буфере истории в RX callback | Race condition + heap corruption |
| Жёсткое сравнение тайминга без допуска | Реальные сигналы ±20% от идеала |
| `furi_assert` в production release build | assert отключается при `-DNDEBUG`, используй `furi_check` для контрактов |

---

## Справочник: полезные функции

```c
// Работа с частотами
bool subghz_setting_is_frequency_allowed(SubGhzSetting* setting, uint32_t frequency);
uint32_t subghz_setting_get_frequency_default(SubGhzSetting* setting);

// Проверка разрешения TX
SubGhzTxRxBlockReason subghz_devices_check_tx(SubGhzDevice* device, uint32_t frequency);
// BlockReasons: SubGhzTxRxBlockReasonRegion, SubGhzTxRxBlockReasonCustomModule, SubGhzTxRxBlockReasonNone

// Работа с историей
uint16_t subghz_history_get_item(SubGhzHistory* history);
SubGhzProtocol* subghz_history_get_protocol(SubGhzHistory* history, uint16_t index);
bool subghz_history_get_raw_data(SubGhzHistory* history, uint16_t index, FlipperFormat* flipper_format);

// Таймеры тайминга
FuriHalCortexTimer furi_hal_cortex_timer_get(uint32_t timeout_us);
bool furi_hal_cortex_timer_is_expired(FuriHalCortexTimer timer);

// Работа с GPIO в прерывании (безопасно)
void furi_hal_gpio_write(const GpioPin* gpio, bool state); // ok в ISR
bool furi_hal_gpio_read(const GpioPin* gpio);               // ok в ISR

// Математика для декодирования тайминга (из lib/subghz/blocks/math.h)
#define DURATION_DIFF(x, y) ((x) > (y) ? (x) - (y) : (y) - (x))
```

---

*Этот файл описывает кодовую базу начиная с Momentum Firmware dev branch (2024–2026). При значительных рефакторингах — обновлять разделы 2, 4 и 11.*
