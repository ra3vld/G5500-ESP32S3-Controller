# Сборка Arduino и PlatformIO

Канонические исходники: `G5500_Fork/G5500_Fork.ino` и соседние `.cpp/.hpp/.h`. Аппаратные флаги и распиновки описаны в [configuration.md](configuration.md). Сборка не прошивает устройство.

## Зафиксированные зависимости

| Компонент | Версия | Когда нужен |
|---|---|---|
| Arduino-ESP32 | 3.3.11 | всегда |
| pioarduino espressif32 | 55.03.311 | PlatformIO |
| Adafruit GFX | 1.12.1 | TFT |
| Adafruit ILI9341 | 1.6.2 | TFT |
| Adafruit BusIO | 1.17.2 | TFT |
| XPT2046_Touchscreen | официальный tag v1.4 | TFT/touch |
| LiquidCrystal | 1.0.7 | параллельный LCD1602 |

NONE не требует экранных библиотек. LCD требует только LiquidCrystal; TFT не требует LiquidCrystal. На S2/S3 HC-05 использует Arduino HardwareSerial без дополнительной библиотеки. На классическом ESP32 используется SPP API ESP-IDF без библиотеки BluetoothSerial; BLE отключён.

## Arduino IDE 2.x

Добавьте URL `https://espressif.github.io/arduino-esp32/package_esp32_index.json`, установите **esp32 by Espressif Systems 3.3.11** и только библиотеки выбранного дисплея. XPT2046: [официальный ZIP v1.4](https://github.com/PaulStoffregen/XPT2046_Touchscreen/archive/refs/tags/v1.4.zip).

Откройте `G5500_Fork/G5500_Fork.ino`; измените значения в `build_config.hpp`. Все плоские заголовки профилей должны остаться рядом со скетчем. `BOARD_AUTO` выбирает распиновку и Bluetooth по плате IDE. Таблицу разделов выбирайте отдельно в **Tools → Partition Scheme**: в исходной папке нет `partitions.csv`, который перекрывал бы меню.

| Настройка | ESP32 WROOM/WROVER | S3 DevKitC-1 N16R8 | LOLIN S2 Mini | Прежняя S2 |
|---|---|---|---|---|
| Плата Arduino | ESP32 Dev Module | ESP32S3 Dev Module | LOLIN S2 Mini | ESP32S2 Dev Module |
| Flash / PSRAM | 4 МБ / отключена | 16 МБ / OPI 8 МБ | 4 МБ / встроенная 2 МБ | 4 МБ / отключена |
| Flash mode | DIO 40 MHz | QIO 80 MHz | DIO 80 MHz | QIO |
| Native USB | нет, UART bridge | Hardware CDC/JTAG, CDC enabled | CDC enabled | CDC enabled |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) | 16M Flash (3MB APP/9.9MB FATFS) | Huge APP (3MB No OTA/1MB SPIFFS) | Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS) |
| BOARD_PROFILE | AUTO или ESP32 | AUTO или S3 | AUTO или S2_MINI | AUTO или S2_LEGACY |

Arduino-ESP32 генерирует бинарную таблицу из выбранной схемы. На ESP32 с Bluetooth SPP нужен слот 3 МБ: прежний слот 1,97 МБ слишком мал. Flash Size должен совпадать с физической памятью платы. Если используете старую копию скетча с `partitions.csv`, этот файл по-прежнему имеет приоритет перед меню; для сборки по меню удалите его из той копии. NVS у перечисленных схем остаётся на `0x9000`. Наличие двух OTA-слотов не добавляет веб-OTA.

Для прежней разметки проекта используйте `tools/prepare_sketch.py --target esp32|s3|s2-mini|s2` и откройте подготовленную копию. Этот режим намеренно кладёт выбранный CSV под именем `partitions.csv`: ESP32 — `partitions_esp32.csv` (3 МБ), S3 — `partitions_s3.csv` (6 553 600 байт на слот), S2 — `partitions_s2.csv` (1 966 080 байт на слот). Для такой S3-копии выбирайте Custom; для ESP32 и S2 Mini — Huge APP, для S2 Dev Module — Minimal SPIFFS. PlatformIO и CLI-скрипты продолжают использовать эти таблицы; их содержимое и смещения сохранены.

Нажмите **Verify**; COM-порт для этого не нужен. Во время проверки Arduino IDE непосредственно из канонической папки завершите PlatformIO: он создаёт временный `.ino.cpp`. `prepare_sketch.py` исключает этот файл из копии, предотвращая вторые `setup()/loop()`.

## Arduino CLI

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.11 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
# Только выбранный дисплей:
arduino-cli lib install 'LiquidCrystal@1.0.7'
# либо TFT: GFX@1.12.1, ILI9341@1.6.2, BusIO@1.17.2 и официальный ZIP XPT2046 v1.4

./tools/build_arduino.sh s3
./tools/build_arduino.sh s2
DISPLAY_KIND=2 BLUETOOTH_ENABLED=0 DRIVE_INTERFACE=2 ./tools/build_arduino.sh s2-mini
```

`DISPLAY_KIND`: 0 NONE, 1 TFT, 2 LCD. `DRIVE_INTERFACE`: 0 YAESU, 1 SEPARATE_PWM, 2 BTS7960. Старый `DISPLAY_ENABLED=0/1` остаётся алиасом NONE/TFT; несовместимые флаги отклоняются. Можно передать `EXTRA_BUILD_FLAGS='-DUSER_BUTTON_GPIO=38'`. Без переопределений используется `build_config.hpp`. Скрипт допускает `ARDUINO_CLI`, `ARDUINO_CONFIG_FILE`, `FQBN`, `BUILD_JOBS`; исходники и результаты изолированы в `build/arduino-<target>` и `artifacts/arduino-<target>`.

## PlatformIO

Скрипт выбирает экранные зависимости согласно общему заголовку и параметрам:

```sh
python3 tools/build_platformio.py --env esp32s3_n16r8
python3 tools/build_platformio.py --env esp32s2_mini --display lcd --bluetooth 0 --drive bts7960
python3 tools/build_platformio.py --env esp32s2_mini --display none --drive separate-pwm
```

Сохраняются прямые окружения `esp32s3_n16r8`, `esp32s2_4mb`, оба прежних `_headless`; добавлены `esp32s2_mini`, `esp32s2_mini_headless`, и варианты `_tft`/`_lcd` двух новых плат. Окружения с дисплеем явно задают также совместимый `TFT_DISPLAY_ENABLED`, чтобы PlatformIO LDF правильно выбрал условные библиотеки.

```sh
pio run -e esp32s3_n16r8_tft -j4
pio run -e esp32s2_mini_lcd -j4
pio run -e esp32s2_4mb_headless -j4
```

При изменении DISPLAY_KIND в заголовке используйте `build_platformio.py`, который добавит только выбранные зависимости. Прямое `pio run` использует `lib_deps` выбранного окружения. При параллельных проектах изолируйте `PLATFORMIO_CORE_DIR`, `PLATFORMIO_BUILD_DIR`, `PLATFORMIO_LIBDEPS_DIR`; некоторые версии платформы записывают вспомогательный файл в SDK, поэтому ему нужна отдельная записываемая копия.

## Программная приёмка

```sh
./tools/build_native.sh
python3 tools/check_profiles.py
python3 -m unittest discover -s tests -p test_build_config.py -v
python3 -m unittest discover -s tests -p test_network.py -v
python3 tools/build_matrix.py --tool all --legacy
```

Матрица: ESP32, S3 и S2 Mini × NONE/TFT/LCD × Bluetooth off/on × YAESU/SEPARATE_PWM/BTS7960 = **54 конфигурации каждого инструмента**. Дополнительно `--legacy` проверяет прежнюю S2 с NONE и TFT. Все сборки используют отдельные копии одного зафиксированного снимка исходников. JSON с SHA-256 снимка, размерами Flash/RAM/бинарника, длительностью и результатами находится в `artifacts/build-matrix/{arduino,platformio}.json`; логи — рядом. Размер .bin проверяется по настоящему разделу приложения, а не общему Flash платы.

Для автономного стенда доступны `--arduino-cli`, `--arduino-config`, `--arduino-vendor`, `--pio`, `--platform`, `--library-dir`, `--workers`, `--jobs`. Локальные библиотеки должны иметь зафиксированные версии. Проверка веба: запустить `tools/simulator.py`, затем `npm run test:web`; зависимости браузерного теста заданы в `package-lock.json`. Это проверка общего ядра и интерфейса с виртуальными двигателями.

`npm run test:wifi-web` проверяет форму Wi-Fi с ответами HTTP: отображение сохранённого SSID, передачу спецсимволов через POST, ошибку сохранения, тайм-аут зависшего запроса и отсутствие повторных запросов. Нужен собранный `build/simulator`; подключение к настоящему роутеру этот тест не проверяет. Нативный `status_memory_tests` запрещает C++-выделения памяти во время 10 000 сериализаций и записей в ограниченный журнал, проверяет переполнение буфера, отправку при EAGAIN/частичных записях и отложенную перезагрузку.

## Упаковка

```sh
python3 tools/package_release.py
```

Архив формируется только из Git-учтённых файлов текущей рабочей копии; перед обычной упаковкой новые файлы должны быть добавлены в Git. Проверка незакоммиченной реализации использует отдельный временный индекс, не изменяя индекс репозитория. В ZIP входят все плоские заголовки, три CSV, исходники, документация и схемы; `SOURCE_SHA256SUMS` задаёт хеш каждого файла. Arduino-копия должна содержать единственный `.ino`, все соседние модули и целевой CSV. Упаковка и Verify не означают публикацию или прошивку устройства.

## Сборка классического ESP32

```sh
pio run -e esp32_4mb
pio run -e esp32_4mb_lcd
pio run -e esp32_4mb_tft
python3 tools/build_platformio.py --env esp32_4mb --display tft --bluetooth 1 --drive bts7960
./tools/build_arduino.sh esp32
python3 tools/prepare_sketch.py --target esp32
```

Arduino IDE: открыть исходный `G5500_Fork/G5500_Fork.ino`, выбрать ESP32 Dev Module, Flash 4 MB, DIO 40 MHz, PSRAM Disabled, Core Debug Level None и Partition Scheme **Huge APP (3MB No OTA/1MB SPIFFS)**. Отдельная копия не требуется. Распиновка одинакова для WROOM и WROVER; PSRAM не требуется. Wi-Fi + SPP не помещаются в прежний слот 1,97 MB на Flash 4 MB. Скрипты и PlatformIO используют `partitions_esp32.csv`: один factory-раздел приложения 3 MB, без второго OTA-слота и без изменения смещения NVS.

Проверка только нового профиля: `tools/build_matrix.py --tool all --boards esp32 --run-name esp32`. Регрессионная проверка трёх дисплеев S2/S3: `--boards s3 s2-mini --select smoke`. `--run-name` изолирует логи и сборки разных запусков.
