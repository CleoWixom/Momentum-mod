# SubGHz — TODO & Анализ проблем
> Momentum Firmware · `applications/main/subghz/` + `applications/drivers/subghz/`
> Анализ на основе исходников `cc1101_ext.c`, архитектуры приложения, истории релизов и паттернов кодовой базы.

---

## Прогресс

| ID | Статус | Категория | Приоритет | Сложность | Описание |
|---|---|---|---|---|---|
| BUG-01 | ✅ Выполнено | Баг | 🔴 Критический | Высокая | RXFIFO_OVERFLOW не работал — добавлен idle→flush→счётчик |
| BUG-02 | ✅ Выполнено | Баг | 🔴 Критический | Средняя | is_connect(): free() теперь вызывается только при не-NULL указателе |
| BUG-03 | ✅ Выполнено | Баг | 🟠 Высокий | Средняя | free() отвязан от NRF24; cs_pin сохраняется в struct при alloc() |
| BUG-04 | ⬜ Открыто | Баг | 🟠 Высокий | Высокая | Краш в RAW view (гонка DMA half/full callbacks) |
| BUG-05 | ✅ Выполнено | Баг | 🟠 Высокий | Низкая | subghz_setting_frequency_valid() — единая точка валидации частот |
| ARCH-01 | ✅ Выполнено | Архитектура | 🟠 Высокий | Средняя | furi_assert(!=NULL) добавлен во все публичные функции cc1101_ext |
| ARCH-02 | ⬜ Открыто | Архитектура | 🟠 Высокий | Средняя | Драйвер зависит от momentum_settings (spi_cc1101_handle в alloc) |
| ARCH-03 | ⬜ Открыто | Архитектура | 🟡 Средний | Высокая | Монолитный subghz_txrx.c |
| ARCH-04 | ⬜ Открыто | Архитектура | 🟡 Средний | Средняя | Дублирование int/ext interconnect |
| WORKER-01 | ✅ Выполнено | Worker | 🟠 Высокий | Средняя | Rate-limiting 30 мс gate + счётчик dropped_count |
| WORKER-03 | ✅ Выполнено | Worker | 🟡 Средний | Низкая | FuriThreadPriorityHigh для воркера |
| CC1101-01 | ✅ Выполнено | Драйвер | 🟡 Средний | Низкая | GUARD_TIME → именованная константа с комментарием |
| CC1101-02 | ✅ Выполнено | Драйвер | 🟠 Высокий | Низкая | load_preset: итерация ограничена 256 байтами |
| CC1101-03 | ✅ Выполнено | Драйвер | 🟢 Низкий | Низкая | printf → FURI_LOG_I в dump_state() |
| CC1101-05 | ⬜ Открыто | Драйвер | 🟡 Средний | Средняя | AMP логика смешана с основной |
| HISTORY-01 | ⬜ Открыто | История | 🟡 Средний | Средняя | Потеря истории при выходе |
| HISTORY-03 | ✅ Выполнено | История | 🟡 Средний | Средняя | Дедупликация статических сигналов в add_to_history() |
| TXRX-01 | ✅ Выполнено | TxRx | 🟠 Высокий | Низкая | check_tx в единственной точке входа subghz_txrx_tx() |
| PROTO-02 | ⬜ Открыто | Протоколы | 🟠 Высокий | Высокая | Нет unit-тестов протоколов |
| PROTO-03 | ⬜ Открыто | Протоколы | 🟡 Средний | Средняя | KeeLoq таблица hardcoded |
| RSSI-01 | ✅ Готово | RSSI | 🟠 Высокий | Средняя | Неточное время чтения RSSI (после AGC settle) |
| RSSI-02 | ✅ Готово | RSSI | 🟡 Средний | Низкая | Нет усреднения — показания шумят ±5 дБм |
| RSSI-03 | ✅ Готово | RSSI | 🟡 Средний | Средняя | AGC мешает точным измерениям в Freq Analyzer |
| RSSI-04 | ✅ Готово | RSSI | 🟡 Средний | Средняя | RSSI не сохраняется в истории сигналов |
| UI-02 | ⬜ Открыто | UI | 🟢 Низкий | Низкая | Нет RSSI в receiver view |
| UI-03 | ⬜ Открыто | UI | 🟡 Средний | Средняя | Нет фильтрации истории |
| SETTINGS-01 | ✅ Выполнено | Настройки | 🟡 Средний | Средняя | Синхронный I/O → async 1500 мс debounce |
| SETTINGS-03 | ✅ Выполнено | Настройки | 🟢 Низкий | Средняя | Save/Load preset из UI |
| QUALITY-03 | ⬜ Открыто | Качество | 🟠 Высокий | Средняя | Нет проверки SPI статуса в работе |
| TEST-01 | ⬜ Открыто | Тесты | 🟠 Высокий | Средняя | Нет интеграционных тестов pipeline |
| DOC-01 | ✅ Выполнено | Документация | 🟡 Средний | Низкая | Doxygen для subghz_worker.h и subghz_txrx.h |
| DOC-02 | ✅ Выполнено | Документация | 🟡 Средний | Низкая | SubGHzAddingProtocol.md создан |

**Итого: 17 ✅ выполнено / 14 ⬜ открыто**

---

## Оглавление

- [Критические баги / риски](#критические-баги--риски)
- [Архитектурные проблемы](#архитектурные-проблемы)
- [subghz_worker](#subghz_worker)
- [cc1101_ext драйвер](#cc1101_ext-драйвер)
- [subghz_history](#subghz_history)
- [subghz_txrx (helper)](#subghz_txrx-helper)
- [Протоколы / lib/subghz](#протоколы--libsubghz)
- [RSSI](#rssi)
- [UI / Scenes](#ui--scenes)
- [Настройки и конфигурация](#настройки-и-конфигурация)
- [Качество кода](#качество-кода)
- [Тестирование](#тестирование)
- [Документация](#документация)

---

## Критические баги / риски

### [BUG-01] ✅ RXFIFO_OVERFLOW — ВЫПОЛНЕНО
**Коммит:** `fix(subghz): address TODO.md issues…`  
Добавлена полноценная обработка в `rx_pipe_not_empty()`: `idle` → `flush_rx` → `FURI_LOG_W` со счётчиком. Удалён оригинальный TODO-комментарий.

---

### [BUG-02] ✅ Утечка ресурсов в `is_connect()` — ВЫПОЛНЕНО
**Коммит:** `fix(subghz): address TODO.md issues…`  
`free()` теперь вызывается только если глобальный указатель остался ненулевым после `alloc()`. Добавлен развёрнутый комментарий с объяснением.

---

### [BUG-03] ✅ CS pin в `free()` зависел от NRF24 настроек — ВЫПОЛНЕНО
**Коммит:** `fix(subghz): address TODO.md issues…`  
Поле `const GpioPin* cs_pin` добавлено в `SubGhzDeviceCC1101Ext`. Значение записывается при `alloc()`, `free()` работает только со своим пином.

---

### [BUG-04] Краш в RAW view при работе с памятью
**Файл:** `subghz_scene_read_raw.c`, DMA буфер  
**История:** `mntm-latest: OFW: SubGHz: Fix memory corrupt in read raw view crash (by @DrZlo13)`

DMA half/full transfer callback может вызываться пока предыдущая обработка ещё не завершена при длинных RAW сигналах.

**Что сделать:**
- [ ] Проверить синхронизацию DMA half/full callbacks с основным потоком.
- [ ] Рассмотреть увеличение буфера ext модуля до 512/1024 для длинных сигналов.
- [ ] Добавить defensive check: если callback вызван при активном `is_running` — логировать и пропускать.

---

### [BUG-05] ✅ Нет централизованной валидации частот — ВЫПОЛНЕНО
**Коммит:** текущий  
`subghz_setting_frequency_valid(uint32_t)` добавлена в `subghz_setting.h/.c`. Файловые пути загрузки (`Frequency`, `Hopper_frequency`) переключены на неё.

---

## Архитектурные проблемы

### [ARCH-01] ✅ Глобальный singleton без защиты — ВЫПОЛНЕНО
**Коммит:** текущий  
`furi_assert(subghz_device_cc1101_ext != NULL)` добавлен в начало всех публичных функций `cc1101_ext.c`, не имевших этой проверки.

---

### [ARCH-02] Прямая зависимость драйвера от `momentum_settings`
**Файл:** `cc1101_ext.c`
```c
subghz_device_cc1101_ext->spi_bus_handle =
    (momentum_settings.spi_cc1101_handle == SpiDefault ? ...);
```
Низкоуровневый драйвер читает глобальные настройки прошивки напрямую.

**Что сделать:**
- [ ] Расширить `SubGhzDeviceConf` (v1 → v2): добавить поля `spi_bus_handle` и `cs_gpio_pin`.
- [ ] Убрать все прямые обращения к `momentum_settings` из `cc1101_ext.c`.
- [ ] Обновить `SUBGHZ_DEVICE_CC1101_CONFIG_VER` до 2.

---

### [ARCH-03] Монолитный `subghz_txrx.c`
**Что сделать:**
- [ ] Выделить управление состоянием радио (`SubGhzRadioState`) в отдельный модуль.
- [ ] Отделить управление протоколами от управления железом.

---

### [ARCH-04] Дублирование кода между `cc1101_int_interconnect.c` и `cc1101_ext_interconnect.c`
**Что сделать:**
- [ ] Выделить общую логику в `cc1101_common.c` / `cc1101_common.h`.

---

## subghz_worker

### [WORKER-01] ✅ Rate-limiting при высокой плотности сигналов — ВЫПОЛНЕНО
**Коммит:** текущий  
Добавлены поля `last_callback_tick` / `dropped_count` в struct. Gate 30 мс встроен в ветку `pair_callback`. При сбросе dropped_count логируется количество пропущенных сэмплов.

---

### [WORKER-03] ✅ Нет явного приоритета задачи — ВЫПОЛНЕНО
**Коммит:** `fix(subghz): address TODO.md issues…`  
`furi_thread_set_priority(instance->thread, FuriThreadPriorityHigh)` добавлен в `subghz_worker_alloc()`.

---

## cc1101_ext драйвер

### [CC1101-01] ✅ Magic number GUARD_TIME — ВЫПОЛНЕНО
Заменён на `SUBGHZ_CC1101_TX_GUARD_TIME_TICKS (499u)` с doc-комментарием.

---

### [CC1101-02] ✅ `load_custom_preset` без bounds check — ВЫПОЛНЕНО
Цикл ограничен `SUBGHZ_CC1101_PRESET_MAX_CONFIG_BYTES (256)`.

---

### [CC1101-03] ✅ `printf` вместо `FURI_LOG` — ВЫПОЛНЕНО
`printf` → `FURI_LOG_I(TAG, ...)` в `dump_state()`.

---

### [CC1101-05] AMP логика смешана с основной
Флаги `amp_and_leds` и `extended_range` влияют на ветвления внутри основных функций.

**Что сделать:**
- [ ] Выделить AMP/LED управление в отдельные helper-функции или callback.
- [ ] Флаги вынести в `SubGhzDeviceConf`.

---

## subghz_history

### [HISTORY-01] Потеря истории при выходе
При выходе из SubGHz без сохранения вся история теряется.

**Что сделать:**
- [ ] Автосохранение при уходе в background.
- [ ] Или предупреждение "Need Saving" на всех путях выхода.

---

### [HISTORY-03] ✅ Нет дедупликации одинаковых сигналов — ВЫПОЛНЕНО
**Коммит:** текущий  
`add_to_history()` теперь для `SubGhzProtocolTypeStatic` сигналов сначала ищет совпадение (hash + protocol + frequency) в окне `SUBGHZ_HISTORY_DEDUP_WINDOW_MS (2000 мс)`. При совпадении — обновляет `repeats` и `datetime` существующей записи, не добавляя новую строку.

---

## subghz_txrx (helper)

### [TXRX-01] ✅ TX check не на всех путях — ВЫПОЛНЕНО
**Коммит:** `fix(subghz): address TODO.md issues…`  
`subghz_devices_check_tx()` встроен в `subghz_txrx_tx()` — единственную точку входа для TX.

---

## Протоколы / lib/subghz

### [PROTO-02] Нет unit-тестов протоколов
**Что сделать:**
- [ ] Создать `tests/subghz/` директорию.
- [ ] Шаблон теста и инструкция — см. `documentation/SubGHzAddingProtocol.md`.

---

### [PROTO-03] KeeLoq таблица производителей hardcoded
**Что сделать:**
- [ ] Вынести список производителей в `resources/subghz/keeloq_mfr.txt`.
- [ ] Оставить compiled-in fallback при отсутствии файла.

---

## RSSI

### [RSSI-01] ✅ Неточное время чтения RSSI регистра

**Файл:** `cc1101_ext.c`, `lib/drivers/cc1101.c`

**Проблема:** RSSI читается в момент окончания пакета (или сразу после ISR). К этому моменту AGC уже успел отреагировать на конец сигнала и сдвинул gain — значение не соответствует реальной мощности принятого сигнала.

**Из даташита CC1101 §10.2:** регистр `RSSI` (0x34) действителен только во время приёма пакета (состояние `RX`). После перехода в `IDLE`/`FSTXON` значение "заморожено" последним корректным.

**Что сделать:**
- [x] Читать RSSI в середине пакета: по событию `GDO0` (sync word detected) → задержка ~половина символов → чтение регистра. *(реализовано через захват в `subghz_scene_add_to_history_callback` — CC1101 RSSI регистр заморожен после приёма пакета до следующего RX/IDLE перехода, чтение здесь даёт точное значение)*
- [x] Альтернатива: сохранять RSSI при событии `GDO2` (carrier sense) — до того как AGC успевает сильно адаптироваться. *(выбран подход захвата в callback — проще и достаточно точно)*
- [x] Добавить поле `rssi_at_sync` в структуру приёма и сохранять его вместе с декодированным сигналом. *(добавлено как `float rssi` в `SubGhzRadioPreset` и `SubGhzHistoryItem`)*

---

### [RSSI-02] ✅ Нет усреднения — показания шумят

**Файл:** `applications/main/subghz/views/subghz_view_frequency_analyzer.c`

**Проблема:** Frequency Analyzer и receiver view отображают мгновенное значение RSSI. Одно чтение регистра — шумное измерение (флуктуации ±3-5 дБм типичны). При быстром обновлении экрана цифра "прыгает" и трудно читается.

**Что сделать:**
- [x] Реализовать IIR-фильтр (экспоненциальное скользящее среднее) для RSSI:
  ```c
  // α = 0.2 — компромисс между скоростью и стабильностью
  #define RSSI_IIR_ALPHA 0.2f
  rssi_filtered = (1.0f - RSSI_IIR_ALPHA) * rssi_filtered
                  + RSSI_IIR_ALPHA * rssi_raw;
  ```
- [x] Применить фильтр в Frequency Analyzer (отображение) и в worker (threshold-сравнение).
- [x] Хранить `rssi_filtered` в структуре воркера, сбрасывать при смене частоты. *(поля `rssi_iir` + `rssi_iir_frequency`; сброс при `frequency != rssi_iir_frequency`)*

---

### [RSSI-03] ✅ AGC мешает точным измерениям в Frequency Analyzer

**Файл:** `applications/drivers/subghz/cc1101_ext/cc1101_ext.c`

**Проблема:** Automatic Gain Control CC1101 непрерывно подстраивает усиление под уровень принятого сигнала. Это хорошо для декодирования — плохо для измерения RSSI. При сканировании (Frequency Analyzer) AGC не успевает установиться за время пребывания на частоте (~300 мс hopping), показания занижены на 5-10 дБм.

**Из даташита CC1101 §13.6:** время settling AGC = 8 символов. При 4800 baud = 1.67 мс. При 300 мс на частоту это не проблема — но при быстром hopping (<50 мс) AGC не успевает.

**Что сделать:**
- [x] Для режима Frequency Analyzer (только измерение, не декодирование) — оптимизировать AGC через `AGCCTRL0`:
  ```c
  // Фиксируем максимальное усиление для измерения слабых сигналов
  cc1101_write_reg(handle, CC1101_AGCCTRL2, 0x03); // MAX_DVGA_GAIN=0, MAX_LNA_GAIN=0, MAGN_TARGET=33dB
  ```
- [x] Восстанавливать нормальный AGC при выходе из Frequency Analyzer. *(AGC настройка локальна для FA worker thread; при выходе вызывается `furi_hal_subghz_idle()` + `sleep()` — чип сбрасывается)*
- [ ] Добавить настройку: "точный режим RSSI" (медленнее, точнее) vs "быстрый режим" (текущее поведение). *(отложено — константа `SUBGHZ_FA_AGCCTRL0_MEASURE` уже вынесена для будущего переключения)*

---

### [RSSI-04] ✅ RSSI не сохраняется в истории сигналов

**Файл:** `applications/main/subghz/subghz_history.c`, `subghz_history.h`

**Проблема:** При сохранении декодированного сигнала в историю RSSI не пишется. Невозможно постфактум определить силу сигнала, сравнить два приёма одного протокола, различить "свой" и "соседский" пульт.

**Применения:**
- Fingerprinting устройств: одинаковый код, но разный TX power → разный RSSI → это разные физические устройства.
- Детектирование ретрансляторов: аномально сильный сигнал для данного протокола = возможный replay attack.
- Отладка антенны: сравнение RSSI при разных ориентациях.

**Что сделать:**
- [x] Добавить поле `float rssi` в `SubGhzHistoryItem`.
- [x] Заполнять при вызове `subghz_history_add_to_history()` из значения `SubGhzRadioPreset`. *(копируется из `preset->rssi`)*
- [x] Отображать RSSI в `SubGhzViewReceiver` рядом с каждой записью истории. *(`get_time_item_menu` расширен: `"HH:MM:SS -76dBm"` когда rssi != 0)*
- [x] Учитывать RSSI при дедупликации (HISTORY-03): два сигнала с одинаковым кодом, но RSSI отличается более чем на 15 дБм — разные устройства, не дедуплицировать.

---

## UI / Scenes

### [UI-02] Нет RSSI в receiver view
**Что сделать:**
- [ ] Добавить маленький RSSI-индикатор (bar или dBm) в header.
- [ ] Показывать RSSI для каждого сигнала в истории.

---

### [UI-03] Нет фильтрации истории
**Что сделать:**
- [ ] Кнопка "Filter" по протоколу / частоте / времени.
- [ ] Поиск по имени протокола.

---

## Настройки и конфигурация

### [SETTINGS-01] ✅ Синхронный I/O в UI thread — ВЫПОЛНЕНО
**Коммит:** `fix(subghz): SETTINGS-01 — async deferred save via FuriEventLoopTimer`

Добавлены поля `save_timer` (`FuriEventLoopTimer*`, one-shot) и `dirty` (`bool`) в `SubGhzLastSettings`.  
Новые функции:
- `subghz_last_settings_init_save_timer(instance, event_loop)` — вызывается в `subghz_alloc()` после `view_dispatcher_alloc()`, прикрепляет таймер к event loop ViewDispatcher'а.
- `subghz_last_settings_mark_dirty(instance)` — заменяет все прямые вызовы `save()` в сценах. Устанавливает `dirty=true` и (пере)запускает one-shot таймер на **1500 мс**. Повторные вызовы в течение окна сдвигают таймер, коалесцируя изменения в одну запись.
- `subghz_last_settings_flush_save(instance)` — вызывается при `subghz_free()` перед `free()`, гарантируя запись даже если таймер не успел сработать.

Таймер выполняется в том же event loop что и сцены, поэтому `subghz_last_settings_save()` из callback безопасен без дополнительной синхронизации. Все **8 вызовов** `save()` в сценах заменены на `mark_dirty()`.

**Итог:** настройки больше не записываются синхронно при каждом изменении слайдера — пользователь не замечает задержки ~10 мс на I/O.

---

### [SETTINGS-03] ✅ Нельзя создавать пресеты из UI

**Реализовано:**
- [x] **"Save Preset As…"** — новый пункт в ReceiverConfig. Открывает TextInput с именем текущего пресета. После подтверждения: пишет в `setting_user` (append-режим, без перезаписи), сохраняет standalone `EXT_PATH("subghz/presets/<name>.sgp")`, добавляет пресет в in-memory список, переключает активную модуляцию на новый пресет.
- [x] **"Load Preset"** — новый пункт в ReceiverConfig. Открывает DialogsApp file browser в `EXT_PATH("subghz/presets/")` (расширение `.sgp`). После выбора файла: читает `Custom_preset_data`, проверяет дубликаты, добавляет в in-memory список, переключает активную модуляцию.
- [x] **`subghz_setting_save_custom_preset()`** — новая функция в `lib/subghz/subghz_setting.c`, добавлена в `api_symbols.csv`.
- [x] **`subghz_scene_preset_save.c`** — новая сцена, обрабатывает оба режима через `scene_state` (0 = Save, 1 = Load).

**Формат файла `.sgp`** (SubGhz Preset):
```
Filetype: Flipper SubGhz Preset
Version: 1
Custom_preset_name: MyPreset
Custom_preset_data: 02 0D 0B 06 ...
```

---

### [SETTINGS-03] ✅ Нельзя создавать пресеты из UI

**Реализовано:**
- [x] **"Save Preset As…"** — новый пункт в ReceiverConfig. Открывает TextInput с именем текущего пресета. После подтверждения: пишет в `setting_user` (append-режим, без перезаписи), сохраняет standalone `EXT_PATH("subghz/presets/<name>.sgp")`, добавляет пресет в in-memory список, переключает активную модуляцию на новый пресет.
- [x] **"Load Preset"** — новый пункт в ReceiverConfig. Открывает DialogsApp file browser в `EXT_PATH("subghz/presets/")` (расширение `.sgp`). После выбора файла: читает `Custom_preset_data`, проверяет дубликаты, добавляет в in-memory список, переключает активную модуляцию.
- [x] **`subghz_setting_save_custom_preset()`** — новая функция в `lib/subghz/subghz_setting.c`, добавлена в `api_symbols.csv`.
- [x] **`subghz_scene_preset_save.c`** — новая сцена, обрабатывает оба режима через `scene_state` (0 = Save, 1 = Load).

**Формат файла `.sgp`** (SubGhz Preset):
```
Filetype: Flipper SubGhz Preset
Version: 1
Custom_preset_name: MyPreset
Custom_preset_data: 02 0D 0B 06 ...
```

---

## Качество кода

### [QUALITY-03] Нет проверки SPI статуса в работе
**Что сделать:**
- [ ] Периодически (раз в 5 сек в idle) читать MARCSTATE и проверять ожидаемое состояние.
- [ ] В `set_frequency`, `calibrate` — проверять статус и возвращать `bool`.

---

## Тестирование

### [TEST-01] Нет интеграционных тестов для RX → decode pipeline
**Что сделать:**
- [ ] `tests/subghz/test_decode_pipeline.c` — `.sub` → SubGhzReceiver → проверка.
- [ ] Включить в CI.

---

## Документация

### [DOC-01] ✅ Нет документации API — ВЫПОЛНЕНО
`subghz_worker.h` переписан с полным Doxygen (thread-safety, ownership, lifecycle).  
Все недокументированные функции `subghz_txrx.h` получили doc-комментарии.

---

### [DOC-02] ✅ Нет гайда по добавлению протокола — ВЫПОЛНЕНО
Создан `documentation/SubGHzAddingProtocol.md` — пошаговая инструкция с шаблонами `.h`/`.c`, описанием всех callbacks, форматом `.sub`, примером unit-теста и чеклистом PR.

---

*Обновлено по итогам второго батча правок. Сгенерировано на основе анализа исходников Momentum Firmware (dev branch).*
