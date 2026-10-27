# Сборка и первая прошивка

[Документация](index.md)

Откройте проект: `G5500_Fork/G5500_Fork.ino` и все соседние исходники `.c/.cpp/.hpp/.h`. Аппаратные флаги и распиновки описаны в [configuration.md](setup/configuration.md). Сборка не прошивает устройство.

Установите один инструмент: [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) или Arduino IDE/CLI. Все команды ниже выполняются из корня проекта. Выберите [окружение своей платы](setup/configuration.md#выбор-конфигурации), затем используйте соответствующий способ сборки.

## Зависимости

| Компонент | Версия | Когда нужен |
|---|---|---|
| Arduino-ESP32 | 3.3.11 | всегда |
| pioarduino espressif32 | 55.03.311 | PlatformIO |
| Adafruit GFX | 1.12.1 | ILI9341 и GC9A01 |
| Adafruit ILI9341 | 1.6.2 | TFT |
| Adafruit GC9A01A | 1.1.0 | ESP32-2424S012C; также нужны GFX и BusIO |
| Adafruit BusIO | 1.17.2 | ILI9341 и GC9A01 |
| XPT2046_Touchscreen | официальный tag v1.4 | TFT/touch |
| LiquidCrystal | 1.0.7 | параллельный LCD1602 |
| LVGL | 9.6.0 | CrowPanel 2.1, ILI9341 и GC9A01 |

NONE не требует экранных библиотек. LCD требует только LiquidCrystal; TFT не требует LiquidCrystal. Для Bluetooth дополнительные библиотеки не нужны: на ESP32 доступны встроенный SPP и внешний HC-05, на S2/S3 — HC-05. Выберите транспорт после сборки в веб-настройках оборудования; Bluetooth по умолчанию выключен.

Для [ESP32-C3 без дисплея](boards/esp32-c3.md) используйте `pio run -e esp32c3_4mb` или `./tools/build_arduino.sh esp32-c3`. Профиль рассчитан на Flash 4 МиБ, включает веб-интерфейс и native USB, использует один раздел приложения 3 МиБ без OTA. GPIO привода назначаются при первой настройке; библиотеки дисплея не нужны.

CrowPanel 2.1 (DHE03921D): `pio run -e crowpanel_21` или `./tools/build_arduino.sh crowpanel`. Для переделанной платы с локальным AZ используйте `pio run -e crowpanel_21_az` по [инструкции подключения](boards/crowpanel-az.md). Обычный `crowpanel_21` — профиль экрана/энкодера/сети без привода; конфигурация, библиотека LVGL и ограничения описаны в [crowpanel-21.md](boards/crowpanel-21.md).

Для [ESP32-2424S012C-Y(B)](boards/esp32-2424s012c.md) на ESP32-C3 используйте `pio run -e esp32_2424s012c` или `./tools/build_arduino.sh esp32-2424s012c`. Это сенсорная плата GC9A01/CST816 с назначением местного оборудования на свободные GPIO, ролью HUD по умолчанию и обновлением через USB. В Arduino IDE выберите **ESP32C3 Dev Module**, Flash 4 MB, Flash Mode DIO, **USB CDC On Boot: Enabled**, Partition Scheme **Huge APP (3MB No OTA/1MB SPIFFS)**; в `build_config.hpp` задайте `BOARD_ESP32_2424S012C` и `DISPLAY_GC9A01`.

Для [ESP32-3248S035C со ST7796/GT911](boards/esp32-3248s035c.md) используйте `pio run -e esp32_3248s035c` или `./tools/build_arduino.sh esp32-3248s035c`. Профиль использует общий LVGL-интерфейс 480×320, один раздел приложения 3 МиБ и обновление через USB.

## Arduino IDE 2.x

Перед сборкой из Git выполните `python3 tools/project_version.py`, чтобы обновить версию и хеш коммита в прошивке. Повторяйте команду после изменения исходников или переключения коммита. PlatformIO, `build_arduino.sh` и `prepare_sketch.py` делают это автоматически. В архиве исходников сохраняются сведения о версии на момент упаковки. Если ни Git, ни сохранённых сведений нет, версия получает суффикс `+unknown`, а коммит — значение `unknown`.

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

Для прежней S2 с двумя OTA-разделами и MQTT установите `WEB_LIVE_SUPPORT=0` и `ARDUINO_OTA_SUPPORT=0`: сохраняются HTTP-управление, браузерное OTA и MQTT/Home Assistant. В PlatformIO это профиль `esp32s2_4mb_ota_compact`. Стандартные `esp32s2_4mb` / `_headless` сохраняют WebSocket и ArduinoOTA, исключая новый MQTT при сборке, чтобы поместиться в прежний раздел.

Arduino-ESP32 генерирует бинарную таблицу из выбранной схемы. На ESP32 выбирайте слот приложения 3 МБ: встроенный SPP включён в образ независимо от положения веб-переключателя. Flash Size должен совпадать с физической памятью платы. Если используете старую копию скетча с `partitions.csv`, этот файл по-прежнему имеет приоритет перед меню; для сборки по меню удалите его из той копии. NVS у перечисленных схем остаётся на `0x9000`. Для [обновления по Wi-Fi](maintenance/ota.md) нужны два подходящих раздела приложения: Huge APP OTA не поддерживает.

Для отдельной Arduino-копии с таблицей разделов проекта используйте `tools/prepare_sketch.py --target esp32|s3|s2-mini|s2` и откройте подготовленную копию. Скрипт копирует выбранный CSV под именем `partitions.csv`: ESP32 и S2 Mini — `partitions_esp32.csv` (один раздел 3 МиБ), S3 — `partitions_s3.csv` (6 553 600 байт на слот), прежняя S2 — `partitions_s2.csv` (два раздела по 1 966 080 байт). В подготовленной копии `partitions.csv` имеет приоритет над меню Partition Scheme. PlatformIO и CLI-скрипты также используют таблицы проекта.

Для графических экранов используйте Arduino CLI через `build_arduino.sh` либо PlatformIO: они передают путь к `lv_conf.h` в компиляторы C и C++. Для классического ESP32 с ILI9341 и для S2 они также включают оптимизацию LTO. При собственной команде Arduino CLI добавьте `-flto` в `compiler.c.extra_flags`, `compiler.cpp.extra_flags` и `compiler.c.elf.extra_flags`.

**ESP32-S2 Mini:** все варианты `esp32s2_mini`, включая `_tft`, используют один раздел приложения 3 МиБ. Он вмещает общий интерфейс LVGL с веб-интерфейсом; обновление выполняется через USB. При переходе с прежней OTA-разметки запишите через USB полный комплект сборки с таблицей разделов. NVS остаётся на `0x9000`; стирать всю Flash для этого не требуется.

Нажмите **Verify**; COM-порт для этого не нужен. При сборке в Arduino IDE из исходной папки завершите PlatformIO и удалите оставшийся `G5500_Fork.ino.cpp`, если IDE сообщает о повторных `setup()/loop()`. Для отдельной копии используйте `prepare_sketch.py`: он исключает этот временный файл.

## Arduino CLI

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.11 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
# Только выбранный дисплей:
arduino-cli lib install 'LiquidCrystal@1.0.7'
# либо ILI9341: lvgl@9.6.0, GFX@1.12.1, ILI9341@1.6.2, BusIO@1.17.2 и официальный ZIP XPT2046 v1.4

./tools/build_arduino.sh s3
./tools/build_arduino.sh s2
DISPLAY_KIND=2 ./tools/build_arduino.sh s2-mini
DISPLAY_KIND=1 ./tools/build_arduino.sh s2-mini
```

`DISPLAY_KIND`: 0 NONE, 1 ILI9341, 2 LCD1602, 3 CrowPanel, 4 GC9A01. CrowPanel и GC9A01 требуют соответствующего профиля платы. Старый `DISPLAY_ENABLED=0/1` остаётся алиасом NONE/TFT; несовместимые флаги отклоняются. Привод, Bluetooth и кнопка выбираются в вебе после сборки. Скрипт допускает `ARDUINO_CLI`, `ARDUINO_CONFIG_FILE`, `FQBN`, `BUILD_JOBS`; исходники и результаты изолированы в `build/arduino-<target>` и `artifacts/arduino-<target>`. Прежние аппаратные `-D` задают только начальные значения нового устройства; сохранённая конфигурация имеет приоритет.

## PlatformIO

Скрипт выбирает экранные зависимости согласно общему заголовку и параметрам:

```sh
python3 tools/build_platformio.py --env esp32s3_n16r8
python3 tools/build_platformio.py --env esp32s2_mini --display lcd
python3 tools/build_platformio.py --env esp32s2_mini --display none
```

В веб-интерфейсе **Настройки → Регуляторы и остановка** выберите общую частоту контура и датчиков AZ/EL: **50, 100 или 200 Гц**. Смена частоты останавливает обе оси, сохраняется и действует без перезапуска. При отсутствии сохранённого значения используется частота сборки, обычно 50 Гц. Для другого начального значения добавьте `--motion-hz 100` или `--motion-hz 200`; например:

```sh
python3 tools/build_platformio.py --env esp32_4mb --display none --motion-hz 200
```

После установки проверьте шум неподвижного датчика и фактическую частоту по [диагностике контура](api/index.md#api-регуляторов-движения), затем заново проверьте регулятор под нагрузкой. 200 Гц — экспериментальный режим. Выбранная частота задаёт номинальный период; задержки других операций могут привести к пропускам. Для Arduino IDE или прямого вызова PlatformIO начальное значение задаётся общим флагом `-DMOTION_CONTROL_PERIOD_MS=5` (200 Гц), `10` (100 Гц) или `20` (50 Гц).

Выберите окружение своей платы: `esp32_4mb`, `esp32s3_n16r8`, `esp32s2_mini` или `esp32s2_4mb_ota_compact` для прежней проводки S2 с MQTT и браузерным OTA. Для ESP32, S3 и S2 Mini доступны варианты `_tft` и `_lcd`. Профиль обычной CrowPanel — `crowpanel_21`, переделанной платы с локальным AZ — `crowpanel_21_az`. Суффикс `_headless` у S2/S3 выбирает сборку без дисплея; веб в этих вариантах включён. `esp32_4mb_noweb` — классический ESP32 без PSRAM, дисплея, HTTP/WebSocket и Bluetooth, с двумя OTA-разделами и [командным API](api/command-api.md).

```sh
pio run -e esp32s3_n16r8_tft -j4
pio run -e esp32s2_mini_lcd -j4
pio run -e esp32s2_4mb_ota_compact -j4
pio run -e esp32_4mb_noweb -j4
```

При изменении DISPLAY_KIND в заголовке используйте `build_platformio.py`, который добавит только выбранные зависимости. Прямое `pio run` использует `lib_deps` выбранного окружения.

Для [локальных кнопок или аналогового джойстика](control/local-controls.md) используйте обычный профиль своей платы, например `esp32s3_n16r8`. После прошивки выберите входы управления и назначьте GPIO в **Настройки → Оборудование**.

Для [WT32-ETH01 с Ethernet](boards/wt32-eth01.md) используйте `pio run -e wt32_eth01` либо `./tools/build_arduino.sh wt32-eth01`. Профиль имеет свою распиновку, работает без дисплея и требует внешнего USB–UART для записи. Вариант `wt32_eth01_noweb` поддерживает ArduinoOTA через командный API.

Для [Waveshare ESP32-S3-ETH с камерой и двумя BTS7960](boards/waveshare-esp32-s3-eth.md) используйте `pio run -e waveshare_esp32s3_eth`. Линии слота microSD отданы датчикам и ENABLE. Вариант `waveshare_esp32s3_eth_nocam` сохраняет привод и Ethernet, отключая камеру.

## Сборка классического ESP32

```sh
pio run -e esp32_4mb
pio run -e esp32_4mb_lcd
pio run -e esp32_4mb_tft
python3 tools/build_platformio.py --env esp32_4mb --display tft
./tools/build_arduino.sh esp32
python3 tools/prepare_sketch.py --target esp32
```

Для обычного ESP32 используйте [параметры IDE](#arduino-ide-2x) выше. Распиновка одинакова для WROOM и WROVER; PSRAM не требуется. Скрипты и профиль `esp32_4mb` используют `partitions_esp32.csv`: один factory-раздел 3 МиБ без OTA. Профиль `esp32_4mb_noweb` использует `partitions_s2.csv`: два OTA-слота по 1 966 080 байт. NVS в обоих случаях начинается с `0x9000`; смена разметки требует записи через USB.

Для универсальной прошивки обычного ESP32 с OTA используйте `esp32_8mb_ota` при физических 8 МиБ Flash. Профиль `esp32_4mb_ota` имеет меньшие разделы и для этого образа не подходит. Arduino-копия для 8 МиБ: `python3 tools/prepare_sketch.py --target esp32-8mb`, CLI-сборка: `./tools/build_arduino.sh esp32-8mb`. [Выбор разделов и обновление](maintenance/ota.md).

Обычный ESP32 с физическими 4 МиБ Flash не вмещает два экземпляра универсального образа со SPP. Выключение Bluetooth в настройках не уменьшает этот образ. Для 4 МиБ с OTA доступны `esp32_4mb_noweb` и `esp32_4mb_ota_compact`.

`pio run -e esp32_4mb_ota_compact` сохраняет веб, браузерное OTA, MQTT/Home Assistant и внешний HC-05. В нём исключены встроенный SPP, WebSocket, ArduinoOTA и несжатые копии HTML. Браузер использует gzip и периодический HTTP-опрос. Первая запись с OTA-разметкой выполняется через USB; дальнейшее обновление — через веб.

Ядро S-профилей Ruckig Community включено в исходники и не требует установки библиотеки. Условия приведены в [лицензии MIT](licenses/ruckig-MIT.txt).

## Первая прошивка через USB

1. Отключите силовое питание привода. Для CrowPanel с локальным AZ это обязательно перед BOOT/RESET: [особенности BOOT](boards/crowpanel-az.md#полярность-boot-и-загрузка).
2. Подключите плату USB-кабелем с передачей данных. На плате с двумя разъёмами различайте USB–UART и native USB. На ESP32 обычно используется мост USB–UART; на S2/S3 доступен native USB, на S3 DevKit также может быть UART-мост.
3. Найдите порт командой `pio device list` или `arduino-cli board list`. В Windows это `COM…`, в Linux — обычно `/dev/ttyUSB…` или `/dev/ttyACM…`, в macOS — `/dev/cu.…`. Закройте программы, занявшие порт.
4. Если загрузчик не отвечает, удерживайте **BOOT**, нажмите и отпустите **RESET/EN**, затем отпустите BOOT. Снова проверьте список: native USB может получить другой порт.
5. Запишите прошивку одним из способов ниже. После успешной записи при необходимости нажмите RESET; после запуска native USB снова может сменить имя порта.

**PlatformIO**, пример S3:

```sh
pio run -e esp32s3_n16r8 -t upload --upload-port /dev/ttyACM0
```

Замените окружение и порт своими. Если собирали через `build_platformio.py`, загружайте с созданной им конфигурацией, чтобы сохранить выбранные параметры. Пример для `--env esp32s2_mini`:

```sh
pio run -c build/platformio-config/esp32s2_mini.ini -e selected -t upload --upload-port /dev/ttyACM0
```

**Arduino IDE:** оставьте плату, Flash/PSRAM и Partition Scheme выбранной сборки, задайте **Tools → Port** и нажмите **Upload**. Verify только компилирует.

**Arduino CLI**, после `./tools/build_arduino.sh s3`:

```sh
arduino-cli upload --port /dev/ttyACM0 \
  --fqbn 'esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=custom,USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=qio' \
  --input-dir artifacts/arduino-s3 build/arduino-s3/G5500_Fork
```

Для другой платы используйте тот же FQBN и каталог, что при сборке; значения по умолчанию указаны в [build_arduino.sh](../tools/build_arduino.sh). CLI upload сам не компилирует. Справочники: [PlatformIO upload](https://docs.platformio.org/en/latest/core/userguide/cmd_run.html#cmdoption-pio-run-upload-port), [Arduino CLI upload](https://docs.arduino.cc/arduino-cli/commands-reference/arduino-cli_upload).

Дождитесь успешного завершения записи. После сброса проверьте появление сети устройства или ответ [командного API](api/command-api.md). Ошибка записи — повод проверить порт, кабель и режим загрузчика; для обычной установки стирание всей Flash не требуется.

Далее: [первое подключение](quickstart.md#первое-подключение).
