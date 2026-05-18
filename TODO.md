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
| UI-02 | ⬜ Открыто | UI | 🟢 Низкий | Низкая | Нет RSSI в receiver view |
| UI-03 | ⬜ Открыто | UI | 🟡 Средний | Средняя | Нет фильтрации истории |
| SETTINGS-01 | ✅ Выполнено | Настройки | 🟡 Средний | Средняя | Синхронный I/O → async 1500 мс debounce |
| SETTINGS-03 | ⬜ Открыто | Настройки | 🟢 Низкий | Средняя | Нельзя создавать пресеты из UI |
| QUALITY-03 | ⬜ Открыто | Качество | 🟠 Высокий | Средняя | Нет проверки SPI статуса в работе |
| TEST-01 | ⬜ Открыто | Тесты | 🟠 Высокий | Средняя | Нет интеграционных тестов pipeline |
| DOC-01 | ✅ Выполнено | Документация | 🟡 Средний | Низкая | Doxygen для subghz_worker.h и subghz_txrx.h |
| DOC-02 | ✅ Выполнено | Документация | 🟡 Средний | Низкая | SubGHzAddingProtocol.md создан |

**Итого: 13 ✅ выполнено / 14 ⬜ открыто**

---

## Оглавление

- [Критические баги / риски](#критические-баги--риски)
- [Архитектурные проблемы](#архитектурные-проблемы)
- [subghz_worker](#subghz_worker)
- [cc1101_ext драйвер](#cc1101_ext-драйвер)
- [subghz_history](#subghz_history)
- [subghz_txrx (helper)](#subghz_txrx-helper)
- [Протоколы / lib/subghz](#протоколы--libsubghz)
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

### [SETTINGS-03] Нельзя создавать пресеты из UI
**Что сделать:**
- [ ] "Save current preset as…" и "Load preset from file" в Config scene.

---

### [SETTINGS-03] Нельзя создавать пресеты из UI
**Что сделать:**
- [ ] "Save current preset as…" и "Load preset from file" в Config scene.

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
