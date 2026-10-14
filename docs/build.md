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

NONE не требует экранных библиотек. LCD требует только LiquidCrystal; TFT не требует LiquidCrystal. На S2/S3 HC-05 использует Arduino HardwareSerial без дополнительной библиотеки. На классическом ESP32 используется BluetoothSerial из Arduino-ESP32; BLE отключён.

## Arduino IDE 2.x

Добавьте URL `https://espressif.github.io/arduino-esp32/package_esp32_index.json`, установите **esp32 by Espressif Systems 3.3.11** и только библиотеки выбранного дисплея. XPT2046: [официальный ZIP v1.4](https://github.com/PaulStoffregen/XPT2046_Touchscreen/archive/refs/tags/v1.4.zip).

Откройте `G5500_Fork/G5500_Fork.ino`; измените значения в `build_config.hpp`. Все плоские заголовки профилей должны остаться рядом со скетчем. Для S2 подготовьте копию со своей таблицей разделов:

```sh
python3 tools/prepare_sketch.py --target s2-mini
# Открыть build/arduino-s2-mini/G5500_Fork/G5500_Fork.ino
# Для старой проводки: --target s2; для S3: --target s3
```

| Настройка | S3 DevKitC-1 N16R8 | LOLIN S2 Mini | Прежняя S2 |
|---|---|---|---|
| Плата Arduino | ESP32S3 Dev Module | LOLIN S2 Mini | ESP32S2 Dev Module |
| Flash / PSRAM | 16 МБ / OPI 8 МБ | 4 МБ / встроенная 2 МБ | 4 МБ / отключена |
| Flash mode | QIO 80 MHz | DIO 80 MHz | QIO |
| Native USB | Hardware CDC/JTAG, CDC enabled | CDC enabled | CDC enabled |
| Таблица проекта | partitions.csv | partitions_s2.csv → partitions.csv | partitions_s2.csv → partitions.csv |
| BOARD_PROFILE | AUTO или S3 | AUTO или S2_MINI | AUTO или S2_LEGACY |

Для S3 выбирайте Custom. Для S2 скетч содержит свой `partitions.csv`, который имеет приоритет перед таблицей из меню. В CLI предел приложения выставляется по реальному размеру слота; в IDE при необходимости выберите размер приложения не менее фактического бинарника (например, вариант 2 MB), сохранив файл проекта. Меню не должно приводить к удалению поставленного CSV. Реальные слоты: S3 `0x640000` = 6 553 600 байт, S2 `0x1e0000` = 1 966 080 байт; все проверяемые бинарники должны помещаться именно в них. Таблицы и смещения NVS сохранены. Наличие двух OTA-слотов не добавляет веб-OTA.

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
python3 -m unittest discover -s tests -p test_network.py -v
python3 tools/build_matrix.py --tool all --legacy
```

Матрица: ESP32, S3 и S2 Mini × NONE/TFT/LCD × Bluetooth off/on × YAESU/SEPARATE_PWM/BTS7960 = **54 конфигурации каждого инструмента**. Дополнительно `--legacy` проверяет прежнюю S2 с NONE и TFT. Все сборки используют отдельные копии одного зафиксированного снимка исходников. JSON с SHA-256 снимка, размерами Flash/RAM/бинарника, длительностью и результатами находится в `artifacts/build-matrix/{arduino,platformio}.json`; логи — рядом. Размер .bin проверяется по настоящему разделу приложения, а не общему Flash платы.

Для автономного стенда доступны `--arduino-cli`, `--arduino-config`, `--arduino-vendor`, `--pio`, `--platform`, `--library-dir`, `--workers`, `--jobs`. Локальные библиотеки должны иметь зафиксированные версии. Проверка веба: запустить `tools/simulator.py`, затем `npm run test:web`; зависимости браузерного теста заданы в `package-lock.json`. Это проверка общего ядра и интерфейса с виртуальными двигателями.

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

Arduino IDE: открыть подготовленную копию, выбрать ESP32 Dev Module, Flash 4 MB, DIO 40 MHz, PSRAM Disabled и Core Debug Level None. Распиновка одинакова для WROOM и WROVER; PSRAM не требуется. Копия содержит partitions_esp32.csv под именем partitions.csv: один factory-раздел приложения 3 MB, без второго OTA-слота и без изменения смещений NVS. Wi-Fi + SPP не помещаются в прежний слот 1,97 MB на Flash 4 MB. В Arduino IDE выбрать Huge APP (3 MB No OTA), сохранив CSV проекта.

Проверка только нового профиля: `tools/build_matrix.py --tool all --boards esp32 --run-name esp32`. Регрессионная проверка трёх дисплеев S2/S3: `--boards s3 s2-mini --select smoke`. `--run-name` изолирует логи и сборки разных запусков.
