# Сборка


## Зафиксированные зависимости

| Компонент | Версия |
|---|---|
| Arduino-ESP32 | 3.3.11 |
| pioarduino espressif32 | 55.03.311 |
| Adafruit GFX Library | 1.12.1 |
| Adafruit ILI9341 | 1.6.2 |
| Adafruit BusIO | 1.17.2 |
| XPT2046_Touchscreen | v1.4, официальный tag PaulStoffregen |

## Arduino IDE 2.x

1. В Additional Boards Manager URLs добавить `https://espressif.github.io/arduino-esp32/package_esp32_index.json`.
2. Установить **esp32 by Espressif Systems 3.3.11**. В Library Manager выбрать точные версии библиотек из таблицы. XPT2046_Touchscreen установить из ZIP https://github.com/PaulStoffregen/XPT2046_Touchscreen/archive/refs/tags/v1.4.zip.
3. Открыть `G5500_Fork/G5500_Fork.ino`. `cat_image.h` и `partitions.csv` должны находиться рядом со скетчем.
4. Выбрать **ESP32S3 Dev Module** и настройки:

| Параметр | Значение |
|---|---|
| CPU Frequency | 240 MHz |
| Flash Size | 16 MB (128 Mb) |
| Flash Mode | QIO 80 MHz |
| PSRAM | OPI PSRAM (N16R8, 8 MB) |
| Partition Scheme | Custom |
| USB Mode | Hardware CDC and JTAG |
| USB CDC On Boot | Enabled |
| Upload Mode | UART0 / Hardware CDC |
| Erase All Flash Before Sketch Upload | Disabled |

5. Нажать **Verify**. Для проверки сборки COM-порт не нужен. Для загрузки выбрать порт подключённой платы и нажать **Upload**.

`Custom` использует поставляемый `partitions.csv`: два OTA-раздела по `0x640000` (6 553 600 байт), NVS, SPIFFS и coredump. Это размер разделов, а не добавление веб-OTA: обновление по воздуху здесь не реализовано.

Arduino обрабатывает `.cpp` обычным компилятором; JavaScript внутри веб-страницы не проходит автогенерацию прототипов `.ino`. См. [процесс сборки Arduino](https://docs.arduino.cc/arduino-cli/sketch-build-process/).

## Arduino CLI

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.11 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install 'Adafruit GFX Library@1.12.1' 'Adafruit ILI9341@1.6.2' 'Adafruit BusIO@1.17.2'
# XPT2046 v1.4: установить официальный ZIP через IDE или arduino-cli lib install --zip-path ...
./tools/build_arduino.sh s3
# Для ESP32-S2 с Flash 4 MB:
./tools/build_arduino.sh s2
```

Аргументы CLI и меню IDE соответствуют друг другу. Без аргумента скрипт собирает S3; `s2` выбирает S2 и её таблицу разделов. Все модули копируются из канонической папки в отдельный скетч `build/arduino-s3/G5500_Fork/` или `build/arduino-s2/G5500_Fork/`. Скрипт разрешает `ARDUINO_CLI`, `ARDUINO_CONFIG_FILE`, `FQBN` и `BUILD_JOBS` для изолированного стенда; обычный пакет имеет FQBN `esp32:esp32:esp32s3` или `esp32:esp32:esp32s2`.

## PlatformIO

```sh
pio run -t clean
pio run -j4
```

PlatformIO временно создаёт `.ino.cpp` при конвертации скетча. Скрипт `build_arduino.sh` использует отдельную подготовленную копию и исключает пересечение с PlatformIO. Проверку в Arduino IDE непосредственно из канонической папки выполнять после завершения PlatformIO.

`platformio.ini` фиксирует платформу и библиотеки. Выход: `.pio/build/esp32s3_n16r8/firmware.bin` и `firmware.factory.bin`. При параллельных проектах следует изолировать `PLATFORMIO_CORE_DIR`, чтобы не менять общий кэш.

## Host

```sh
./tools/build_native.sh
python3 -m unittest discover -s tests -p test_network.py -v
python3 tools/simulator.py --log /tmp/g5500-packets.jsonl
```

Симулятор доступен на http://127.0.0.1:8080. Для подключения логгера с другого компьютера используйте `--host 0.0.0.0` и IP компьютера с симулятором. Симулятор моделирует датчики и движение без управления GPIO. `Ctrl+C` завершает симулятор.


## Проверка веб-интерфейса

Нужны Node.js 20 или новее и Playwright:

```sh
npm ci
npx playwright install chromium
# При запущенном симуляторе:
npm run test:web
```

Для установленного системного Chromium можно задать `CHROMIUM_PATH` вместо загрузки браузера. `SIM_URL` меняет HTTP-адрес симулятора, `SIM_N1MM_PORT` — порт отправки UDP-команд на localhost (по умолчанию 12040). Снимки сохраняются в игнорируемом `artifacts/screenshots/`.

## Архив исходников

```sh
python3 tools/package_release.py
```

Скрипт упаковывает файлы, добавленные в Git, и создаёт `artifacts/g5500-controller-sources.zip`, `SOURCE_SHA256SUMS` и `SHA256SUMS`. Команда `sha256sum -c artifacts/SOURCE_SHA256SUMS` проверяет исходники из корня проекта; в `artifacts/` команда `sha256sum -c SHA256SUMS` проверяет архив. Сборки и результаты тестов в архив не входят.
