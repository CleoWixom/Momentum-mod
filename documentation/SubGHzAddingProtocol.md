# Добавление нового SubGHz протокола

> Momentum Firmware · `lib/subghz/protocols/`  
> Инструкция актуальна для ветки `dev`. Последнее обновление — см. git blame.

---

## Оглавление

1. [Обзор архитектуры](#обзор-архитектуры)
2. [Быстрый старт](#быстрый-старт)
3. [Шаблон `.h`](#шаблон-h)
4. [Шаблон `.c`](#шаблон-c)
5. [Регистрация протокола](#регистрация-протокола)
6. [Описание callback-ов](#описание-callback-ов)
7. [Константы и допуски (te_delta)](#константы-и-допуски-te_delta)
8. [Сериализация — формат `.sub` файла](#сериализация--формат-sub-файла)
9. [Пример unit-теста](#пример-unit-теста)
10. [Чеклист перед PR](#чеклист-перед-pr)

---

## Обзор архитектуры

```
CC1101 GPIO edge ISR
        │
        ▼
SubGhzWorker::rx_callback()   ← ISR, пишет в stream-buffer
        │
        ▼ (worker thread)
SubGhzReceiver::feed()
        │
        ▼
SubGhzProtocolDecoder::feed() ← ВАШ КОД
        │
        ▼
SubGhzHistory / RPC / UI
```

Каждый протокол реализует два интерфейса:

| Интерфейс | Назначение |
|-----------|-----------|
| `SubGhzProtocolDecoder` | Разбирает поток level-duration пар из эфира |
| `SubGhzProtocolEncoder` | Генерирует поток level-duration пар для передачи |

Оба интерфейса объединяются в `SubGhzProtocol` — единственный объект, добавляемый в реестр.

---

## Быстрый старт

```
cp lib/subghz/protocols/nice_flo.c  lib/subghz/protocols/my_protocol.c
cp lib/subghz/protocols/nice_flo.h  lib/subghz/protocols/my_protocol.h
```

Далее в этой инструкции — пошаговое описание каждого файла.

---

## Шаблон `.h`

```c
// lib/subghz/protocols/my_protocol.h
#pragma once
#include "base.h"

/** Имя протокола — отображается в UI и используется в .sub файлах */
#define SUBGHZ_PROTOCOL_MY_PROTOCOL_NAME "My Protocol"

typedef struct SubGhzProtocolDecoderMyProtocol SubGhzProtocolDecoderMyProtocol;
typedef struct SubGhzProtocolEncoderMyProtocol SubGhzProtocolEncoderMyProtocol;

extern const SubGhzProtocolDecoder subghz_protocol_my_protocol_decoder;
extern const SubGhzProtocolEncoder subghz_protocol_my_protocol_encoder;
extern const SubGhzProtocol        subghz_protocol_my_protocol;

/* --- Decoder --- */
void*  subghz_protocol_decoder_my_protocol_alloc(SubGhzEnvironment* environment);
void   subghz_protocol_decoder_my_protocol_free(void* context);
void   subghz_protocol_decoder_my_protocol_reset(void* context);
void   subghz_protocol_decoder_my_protocol_feed(void* context, bool level, uint32_t duration);
uint32_t subghz_protocol_decoder_my_protocol_get_hash_data(void* context);
SubGhzProtocolStatus subghz_protocol_decoder_my_protocol_serialize(
    void* context, FlipperFormat* flipper_format, SubGhzRadioPreset* preset);
SubGhzProtocolStatus subghz_protocol_decoder_my_protocol_deserialize(
    void* context, FlipperFormat* flipper_format);
void subghz_protocol_decoder_my_protocol_get_string(void* context, FuriString* output);

/* --- Encoder (опционально, если протокол поддерживает отправку) --- */
void*  subghz_protocol_encoder_my_protocol_alloc(SubGhzEnvironment* environment);
void   subghz_protocol_encoder_my_protocol_free(void* context);
SubGhzProtocolStatus subghz_protocol_encoder_my_protocol_deserialize(
    void* context, FlipperFormat* flipper_format);
void          subghz_protocol_encoder_my_protocol_stop(void* context);
LevelDuration subghz_protocol_encoder_my_protocol_yield(void* context);
```

---

## Шаблон `.c`

```c
// lib/subghz/protocols/my_protocol.c
#include "my_protocol.h"
#include "../blocks/const.h"
#include "../blocks/decoder.h"
#include "../blocks/encoder.h"
#include "../blocks/generic.h"
#include "../blocks/math.h"

#define TAG "SubGhzProtocolMyProtocol"

// ── 1. Временны́е константы ────────────────────────────────────────────────────
// Значения в микросекундах. Снимите с реального сигнала через RAW capture.
static const SubGhzBlockConst subghz_protocol_my_protocol_const = {
    .te_short            = 400,   // короткий pulse/gap, мкс
    .te_long             = 800,   // длинный pulse/gap, мкс
    .te_delta            = 100,   // допуск ±100 мкс
    .min_count_bit_for_found = 24, // минимум бит для успешного декодирования
};

// ── 2. Структуры ──────────────────────────────────────────────────────────────
struct SubGhzProtocolDecoderMyProtocol {
    SubGhzProtocolDecoderBase base;  // ОБЯЗАТЕЛЬНО первым полем
    SubGhzBlockDecoder        decoder;
    SubGhzBlockGeneric        generic;
};

struct SubGhzProtocolEncoderMyProtocol {
    SubGhzProtocolEncoderBase    base;  // ОБЯЗАТЕЛЬНО первым полем
    SubGhzProtocolBlockEncoder   encoder;
    SubGhzBlockGeneric           generic;
};

// ── 3. Шаги декодера (state machine) ─────────────────────────────────────────
typedef enum {
    MyProtocolDecoderStepReset = 0,
    MyProtocolDecoderStepFoundStartBit,
    MyProtocolDecoderStepSaveDuration,
    MyProtocolDecoderStepCheckDuration,
} MyProtocolDecoderStep;

// ── 4. Таблицы vtable ─────────────────────────────────────────────────────────
const SubGhzProtocolDecoder subghz_protocol_my_protocol_decoder = {
    .alloc             = subghz_protocol_decoder_my_protocol_alloc,
    .free              = subghz_protocol_decoder_my_protocol_free,
    .feed              = subghz_protocol_decoder_my_protocol_feed,
    .reset             = subghz_protocol_decoder_my_protocol_reset,
    .get_hash_data     = NULL,
    .get_hash_data_long = subghz_protocol_decoder_my_protocol_get_hash_data,
    .serialize         = subghz_protocol_decoder_my_protocol_serialize,
    .deserialize       = subghz_protocol_decoder_my_protocol_deserialize,
    .get_string        = subghz_protocol_decoder_my_protocol_get_string,
    .get_string_brief  = NULL,  // опционально: короткая строка для истории
};

const SubGhzProtocolEncoder subghz_protocol_my_protocol_encoder = {
    .alloc       = subghz_protocol_encoder_my_protocol_alloc,
    .free        = subghz_protocol_encoder_my_protocol_free,
    .deserialize = subghz_protocol_encoder_my_protocol_deserialize,
    .stop        = subghz_protocol_encoder_my_protocol_stop,
    .yield       = subghz_protocol_encoder_my_protocol_yield,
};

// ── 5. Главный объект протокола ───────────────────────────────────────────────
const SubGhzProtocol subghz_protocol_my_protocol = {
    .name    = SUBGHZ_PROTOCOL_MY_PROTOCOL_NAME,
    .type    = SubGhzProtocolTypeStatic,   // или Dynamic, RAW, BinRAW
    // Флаги: выберите поддерживаемые частотные диапазоны и возможности
    .flag    = SubGhzProtocolFlag_433 | SubGhzProtocolFlag_AM |
               SubGhzProtocolFlag_Decodable |
               SubGhzProtocolFlag_Load | SubGhzProtocolFlag_Save |
               SubGhzProtocolFlag_Send,
    .decoder = &subghz_protocol_my_protocol_decoder,
    .encoder = &subghz_protocol_my_protocol_encoder,
};

// ── 6. Реализация декодера ────────────────────────────────────────────────────
void* subghz_protocol_decoder_my_protocol_alloc(SubGhzEnvironment* environment) {
    UNUSED(environment);
    SubGhzProtocolDecoderMyProtocol* instance =
        malloc(sizeof(SubGhzProtocolDecoderMyProtocol));
    instance->base.protocol = &subghz_protocol_my_protocol;
    instance->generic.protocol_name = instance->base.protocol->name;
    return instance;
}

void subghz_protocol_decoder_my_protocol_free(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMyProtocol* instance = context;
    free(instance);
}

void subghz_protocol_decoder_my_protocol_reset(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMyProtocol* instance = context;
    instance->decoder.parser_step = MyProtocolDecoderStepReset;
}

void subghz_protocol_decoder_my_protocol_feed(
    void* context, bool level, uint32_t duration) {
    furi_assert(context);
    SubGhzProtocolDecoderMyProtocol* instance = context;
    // TODO: реализуйте вашу state machine здесь.
    // Используйте макросы из blocks/decoder.h:
    //   DURATION_DIFF(duration, te_short) < te_delta → короткий импульс
    //   subghz_block_decoder_add_bit(&instance->decoder, bit)
    //   subghz_protocol_blocks_get_hash_data → hash для дедупликации
    UNUSED(instance); UNUSED(level); UNUSED(duration);
}

uint32_t subghz_protocol_decoder_my_protocol_get_hash_data(void* context) {
    furi_assert(context);
    SubGhzProtocolDecoderMyProtocol* instance = context;
    return subghz_protocol_blocks_get_hash_data_long(
        &instance->decoder, sizeof(SubGhzProtocolDecoderMyProtocol));
}

SubGhzProtocolStatus subghz_protocol_decoder_my_protocol_serialize(
    void* context,
    FlipperFormat* flipper_format,
    SubGhzRadioPreset* preset) {
    furi_assert(context);
    SubGhzProtocolDecoderMyProtocol* instance = context;
    SubGhzProtocolStatus res =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    // Записывайте дополнительные поля через flipper_format_write_*()
    return res;
}

SubGhzProtocolStatus subghz_protocol_decoder_my_protocol_deserialize(
    void* context, FlipperFormat* flipper_format) {
    furi_assert(context);
    SubGhzProtocolDecoderMyProtocol* instance = context;
    SubGhzProtocolStatus res = SubGhzProtocolStatusError;
    do {
        if(SubGhzProtocolStatusOk !=
           subghz_block_generic_deserialize(&instance->generic, flipper_format)) {
            FURI_LOG_E(TAG, "Deserialize error");
            break;
        }
        res = SubGhzProtocolStatusOk;
    } while(false);
    return res;
}

void subghz_protocol_decoder_my_protocol_get_string(
    void* context, FuriString* output) {
    furi_assert(context);
    SubGhzProtocolDecoderMyProtocol* instance = context;
    furi_string_printf(
        output,
        "%s %dbit\r\n"
        "Key:0x%08lX\r\n",
        instance->generic.protocol_name,
        instance->generic.data_count_bit,
        (uint32_t)(instance->generic.data >> 32),
        (uint32_t)(instance->generic.data));
}
```

---

## Регистрация протокола

### 1. Добавить `#include` в `protocol_items.h`

```c
// lib/subghz/protocols/protocol_items.h
#include "my_protocol.h"
```

### 2. Добавить в реестр в `protocol_items.c`

```c
// lib/subghz/protocols/protocol_items.c
const SubGhzProtocol* const subghz_protocol_registry_items[] = {
    // ... существующие протоколы ...
    &subghz_protocol_my_protocol,   // ← добавить здесь
    NULL,
};
```

### 3. Добавить в `SConscript`

```python
# lib/subghz/SConscript  (или соответствующий CMakeLists.txt / fbt target)
sources += ["protocols/my_protocol.c"]
```

> **Порядок в реестре влияет на производительность:** протоколы, сканируемые чаще,
> стоит размещать ближе к началу списка.

---

## Описание callback-ов

### Decoder callbacks

| Callback | Когда вызывается | Что делать |
|----------|-----------------|-----------|
| `alloc` | Один раз при старте RX | `malloc` структуры, проставить `base.protocol` |
| `free` | При остановке RX | Освободить всю память включая FuriString |
| `reset` | После каждого успешного декодирования и при старте | Сбросить state machine в `Reset` |
| `feed` | Для каждого edge ISR (~2–20 мкс) | **Не блокировать.** Реализовать state machine. При успешном декодировании вызвать `subghz_protocol_decoder_base_rx_callback` |
| `get_hash_data_long` | При добавлении в историю | Возвращает 32-битный хэш декодированных данных. Используется для дедупликации |
| `serialize` | При сохранении в `.sub` файл | Записать все поля через `flipper_format_write_*` |
| `deserialize` | При загрузке `.sub` файла | Читать и валидировать поля |
| `get_string` | Отображение в UI | Многострочный текст: название, данные, доп. поля |
| `get_string_brief` | Строка в списке истории (опционально) | Одна строка ≤ 30 символов |

### Encoder callbacks

| Callback | Назначение |
|----------|-----------|
| `alloc` / `free` | Аналогично decoder |
| `deserialize` | Загрузить данные из FlipperFormat для последующей передачи |
| `yield` | Возвращает следующую `LevelDuration` для DMA. Вызывается из ISR контекста |
| `stop` | Принудительная остановка передачи |

---

## Константы и допуски (te_delta)

```
Правило: te_delta ≈ te_short * 0.2 … 0.3
```

- Слишком маленький `te_delta` → ложные ошибки декодирования на реальном железе
- Слишком большой `te_delta` → конфликты с другими протоколами на той же частоте

Рекомендуется:
1. Захватить 10+ реальных посылок через RAW capture
2. Замерить разброс каждого типа импульса
3. Взять max_deviation × 1.5 как te_delta

---

## Сериализация — формат `.sub` файла

Минимальный валидный `.sub` файл:

```
Filetype: Flipper SubGhz Key File
Version: 1
Frequency: 433920000
Preset: AM650
Protocol: My Protocol
Bit: 24
Key: 0A 1B 2C
```

Поля `Frequency`, `Preset`, `Protocol` обязательны — они записываются
автоматически через `subghz_block_generic_serialize()`.

Дополнительные поля пишите сразу после вызова `subghz_block_generic_serialize`:

```c
SubGhzProtocolStatus subghz_protocol_decoder_my_protocol_serialize(...) {
    SubGhzProtocolStatus res =
        subghz_block_generic_serialize(&instance->generic, flipper_format, preset);
    if(res != SubGhzProtocolStatusOk) return res;

    // Дополнительные поля
    if(!flipper_format_write_uint32(flipper_format, "Channel", &instance->channel, 1))
        return SubGhzProtocolStatusErrorParserOthers;

    return SubGhzProtocolStatusOk;
}
```

При `deserialize` читайте поля в том же порядке, в котором они записаны:

```c
uint32_t channel = 0;
if(!flipper_format_read_uint32(flipper_format, "Channel", &channel, 1)) {
    FURI_LOG_E(TAG, "Missing Channel");
    return SubGhzProtocolStatusErrorParserOthers;
}
instance->channel = (uint8_t)channel;
```

---

## Пример unit-теста

Создайте `tests/subghz/test_my_protocol.c`:

```c
#include <furi.h>
#include <lib/subghz/protocols/my_protocol.h>
#include <lib/subghz/receiver.h>
#include "../minunit.h"

// Захваченный .sub файл в виде массива level-duration пар
// (снять через SubGHz → Read RAW → сохранить → открыть .sub hex-редактором)
static const LevelDuration test_signal[] = {
    level_duration_make(true,  400),
    level_duration_make(false, 800),
    // ... полная посылка ...
};

MU_TEST(test_my_protocol_decode) {
    SubGhzEnvironment* env = subghz_environment_alloc();
    void* decoder = subghz_protocol_my_protocol.decoder->alloc(env);

    bool decoded = false;
    for(size_t i = 0; i < COUNT_OF(test_signal); i++) {
        subghz_protocol_my_protocol.decoder->feed(
            decoder,
            level_duration_get_level(test_signal[i]),
            level_duration_get_duration(test_signal[i]));
    }

    // Проверьте что декодирование прошло успешно
    FuriString* result = furi_string_alloc();
    subghz_protocol_my_protocol.decoder->get_string(decoder, result);
    mu_check(furi_string_size(result) > 0);
    furi_string_free(result);

    subghz_protocol_my_protocol.decoder->free(decoder);
    subghz_environment_free(env);
}

MU_TEST_SUITE(test_suite) {
    MU_RUN_TEST(test_my_protocol_decode);
}

int test_my_protocol(void) {
    MU_RUN_SUITE(test_suite);
    return MU_EXIT_CODE;
}
```

Запуск тестов: `./fbt test TESTS=subghz`

Подробнее о написании тестов — см. `documentation/UnitTests.md`.

---

## Чеклист перед PR

- [ ] `#include "my_protocol.h"` добавлен в `protocol_items.h`
- [ ] `&subghz_protocol_my_protocol` добавлен в `subghz_protocol_registry_items[]`
- [ ] `protocols/my_protocol.c` добавлен в `SConscript` / систему сборки
- [ ] `alloc` проставляет `base.protocol` и `generic.protocol_name`
- [ ] `free` освобождает **все** поля включая `FuriString*`
- [ ] `reset` сбрасывает `parser_step` в `Reset`
- [ ] `serialize` + `deserialize` — round-trip без потерь данных
- [ ] `get_hash_data_long` возвращает детерминированный хэш (одинаковые данные → одинаковый хэш)
- [ ] `SubGhzProtocol.type` выставлен корректно (`Static` / `Dynamic` / `RAW`)
- [ ] Флаги частотных диапазонов соответствуют реальным характеристикам устройства
- [ ] Unit-тест с реальными захваченными данными
- [ ] `clang-format` применён: `./fbt lint_all`
- [ ] Протокол добавлен в `documentation/SubGHzSupportedSystems.md`
