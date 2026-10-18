# Сборка Arduino и PlatformIO

Откройте проект: `G5500_Fork/G5500_Fork.ino` и все соседние исходники `.c/.cpp/.hpp/.h`. Аппаратные флаги и распиновки описаны в [configuration.md](configuration.md). Сборка не прошивает устройство.

## Зависимости

| Компонент | Версия | Когда нужен |
|---|---|---|
| Arduino-ESP32 | 3.3.11 | всегда |
| pioarduino espressif32 | 55.03.311 | PlatformIO |
| Adafruit GFX | 1.12.1 | TFT |
| Adafruit ILI9341 | 1.6.2 | TFT |
| Adafruit BusIO | 1.17.2 | TFT |
| XPT2046_Touchscreen | официальный tag v1.4 | TFT/touch |
| LiquidCrystal | 1.0.7 | параллельный LCD1602 |
| LVGL | 9.6.0 | CrowPanel 2.1 |

NONE не требует экранных библиотек. LCD требует только LiquidCrystal; TFT не требует LiquidCrystal. Для Bluetooth дополнительные библиотеки не нужны: на ESP32 доступны встроенный SPP и внешний HC-05, на S2/S3 — HC-05. Выберите транспорт после сборки в веб-настройках оборудования; Bluetooth по умолчанию выключен.

CrowPanel 2.1 (DHE03921D): `pio run -e crowpanel_21` или `./tools/build_arduino.sh crowpanel`. Это отдельный профиль экрана/энкодера/сети без привода; конфигурация, библиотека LVGL и ограничения описаны в [crowpanel-21.md](crowpanel-21.md).

## Arduino IDE 2.x

Добавьте URL `https://espressif.github.io/arduino-esp32/package_esp32_index.json`, установите **esp32 by Espressif Systems 3.3.11** и только библиотеки выбранного дисплея. XPT2046: [официальный ZIP v1.4](https://github.com/PaulStoffregen/XPT2046_Touchscreen/archive/refs/tags/v1.4.zip).

Откройте `G5500_Fork/G5500_Fork.ino`; измените значения в `build_config.hpp`. Все заголовки профилей должны остаться рядом со скетчем. `BOARD_AUTO` выбирает распиновку по плате IDE. При включённом Bluetooth `BT_AUTO` выбирает транспорт по чипу. Таблицу разделов выбирайте отдельно в **Tools → Partition Scheme**: в исходной папке нет `partitions.csv`, который перекрывал бы меню.

| Настройка | ESP32 WROOM/WROVER | S3 DevKitC-1 N16R8 | LOLIN S2 Mini | Прежняя S2 |
|---|---|---|---|---|
| Плата Arduino | ESP32 Dev Module | ESP32S3 Dev Module | LOLIN S2 Mini | ESP32S2 Dev Module |
| Flash / PSRAM | 4 МБ / отключена | 16 МБ / OPI 8 МБ | 4 МБ / встроенная 2 МБ | 4 МБ / отключена |
| Flash mode | DIO 40 MHz | QIO 80 MHz | DIO 80 MHz | QIO |
| Native USB | нет, UART bridge | Hardware CDC/JTAG, CDC enabled | CDC enabled | CDC enabled |
| Partition Scheme | Huge APP (3MB No OTA/1MB SPIFFS) | 16M Flash (3MB APP/9.9MB FATFS) | Huge APP (3MB No OTA/1MB SPIFFS) | Minimal SPIFFS (1.9MB APP with OTA/128KB SPIFFS) |
| BOARD_PROFILE | AUTO или ESP32 | AUTO или S3 | AUTO или S2_MINI | AUTO или S2_LEGACY |

Arduino-ESP32 генерирует бинарную таблицу из выбранной схемы. На ESP32 выбирайте слот приложения 3 МБ: встроенный SPP включён в образ независимо от положения веб-переключателя. Flash Size должен совпадать с физической памятью платы. Если используете старую копию скетча с `partitions.csv`, этот файл по-прежнему имеет приоритет перед меню; для сборки по меню удалите его из той копии. NVS у перечисленных схем остаётся на `0x9000`. Для [обновления по Wi-Fi](ota.md) нужны два подходящих раздела приложения: Huge APP OTA не поддерживает.

Для отдельной Arduino-копии с таблицей разделов проекта используйте `tools/prepare_sketch.py --target esp32|s3|s2-mini|s2` и откройте подготовленную копию. Скрипт копирует выбранный CSV под именем `partitions.csv`: ESP32 — `partitions_esp32.csv` (3 МБ), S3 — `partitions_s3.csv` (6 553 600 байт на слот), S2 — `partitions_s2.csv` (1 966 080 байт на слот). В подготовленной копии `partitions.csv` имеет приоритет над меню Partition Scheme. PlatformIO и CLI-скрипты также используют таблицы проекта.

Нажмите **Verify**; COM-порт для этого не нужен. При сборке в Arduino IDE из исходной папки завершите PlatformIO и удалите оставшийся `G5500_Fork.ino.cpp`, если IDE сообщает о повторных `setup()/loop()`. Для отдельной копии используйте `prepare_sketch.py`: он исключает этот временный файл.

## Arduino CLI

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.11 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
# Только выбранный дисплей:
arduino-cli lib install 'LiquidCrystal@1.0.7'
# либо TFT: GFX@1.12.1, ILI9341@1.6.2, BusIO@1.17.2 и официальный ZIP XPT2046 v1.4

./tools/build_arduino.sh s3
./tools/build_arduino.sh s2
DISPLAY_KIND=2 ./tools/build_arduino.sh s2-mini
```

`DISPLAY_KIND`: 0 NONE, 1 TFT, 2 LCD. Старый `DISPLAY_ENABLED=0/1` остаётся алиасом NONE/TFT; несовместимые флаги отклоняются. Привод, Bluetooth и кнопка выбираются в вебе после сборки. Скрипт допускает `ARDUINO_CLI`, `ARDUINO_CONFIG_FILE`, `FQBN`, `BUILD_JOBS`; исходники и результаты изолированы в `build/arduino-<target>` и `artifacts/arduino-<target>`. Прежние аппаратные `-D` задают только начальные значения нового устройства; сохранённая конфигурация имеет приоритет.

## PlatformIO

Скрипт выбирает экранные зависимости согласно общему заголовку и параметрам:

```sh
python3 tools/build_platformio.py --env esp32s3_n16r8
python3 tools/build_platformio.py --env esp32s2_mini --display lcd
python3 tools/build_platformio.py --env esp32s2_mini --display none
```

Выберите окружение своей платы: `esp32_4mb`, `esp32s3_n16r8`, `esp32s2_mini` или `esp32s2_4mb` для прежней проводки S2. Для ESP32, S3 и S2 Mini доступны варианты `_tft` и `_lcd`. Профиль CrowPanel — `crowpanel_21`. Суффикс `_headless` у S2/S3 выбирает сборку без дисплея.

```sh
pio run -e esp32s3_n16r8_tft -j4
pio run -e esp32s2_mini_lcd -j4
pio run -e esp32s2_4mb_headless -j4
```

При изменении DISPLAY_KIND в заголовке используйте `build_platformio.py`, который добавит только выбранные зависимости. Прямое `pio run` использует `lib_deps` выбранного окружения.

## Сборка классического ESP32

```sh
pio run -e esp32_4mb
pio run -e esp32_4mb_lcd
pio run -e esp32_4mb_tft
python3 tools/build_platformio.py --env esp32_4mb --display tft
./tools/build_arduino.sh esp32
python3 tools/prepare_sketch.py --target esp32
```

Arduino IDE: открыть исходный `G5500_Fork/G5500_Fork.ino`, выбрать ESP32 Dev Module, Flash 4 MB, DIO 40 MHz, PSRAM Disabled, Core Debug Level None и Partition Scheme **Huge APP (3MB No OTA/1MB SPIFFS)**. Отдельная копия не требуется. Распиновка одинакова для WROOM и WROVER; PSRAM не требуется. Для универсального ESP32-образа используйте раздел приложения 3 MB. Скрипты и PlatformIO используют `partitions_esp32.csv`: один factory-раздел приложения 3 MB, без второго OTA-слота; NVS начинается с `0x9000`.

Для универсальной прошивки обычного ESP32 с OTA используйте `esp32_8mb_ota` при физических 8 МиБ Flash. Профиль `esp32_4mb_ota` имеет меньшие разделы и для этого образа не подходит. Arduino-копия для 8 МиБ: `python3 tools/prepare_sketch.py --target esp32-8mb`, CLI-сборка: `./tools/build_arduino.sh esp32-8mb`. [Выбор разделов и обновление](ota.md).

После первой загрузки выполните [настройку оборудования и подтверждение доступа](configuration.md#применение-и-восстановление-доступа). Обычный ESP32 с физическими 4 МиБ Flash не вмещает два экземпляра универсального образа со SPP: используйте профиль без OTA. Для OTA на обычном ESP32 нужна достаточная физическая Flash, например профиль `esp32_8mb_ota`.

Ядро S-профилей Ruckig Community включено в исходники и не требует установки библиотеки. Условия приведены в [лицензии MIT](licenses/ruckig-MIT.txt).
