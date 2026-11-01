# G5500: минимальная прошивка Classic ESP32 SPP

Wi-Fi, компактный веб-интерфейс, встроенный Bluetooth Classic SPP, USB GS-232, rotctld / Hamlib, N1MM и PstRotator
для ESP32 WROOM/WROVER с Flash 4 МБ. Прямое управление PWM с сохранёнными
ограничениями движения и защитами. Веб содержит управление, калибровку AZ/EL,
настройки и сброс ошибок. PID, автотюн, рампа, HUD, WebSocket, MQTT и OTA
в основном профиле отключены; отдельный профиль добавляет HUD по UDP.

[Сборка, USB-прошивка, подключение и управление](docs/boards/esp32-spp.md)

```sh
python3 tools/build_esp32_spp.py --pio pio
```

Скрипт собирает изолированный оптимизированный SDK и приложение. Готовые образы
появляются в `build/esp32-spp-firmware/`. Для HUD добавьте `--hud`.

При совместной работе HTTP и SPP возможны сбросы HTTP-соединения; порядок
проверки настроек описан в [диагностике](docs/boards/esp32-spp.md#диагностика).

## Лицензия

[GNU GPL v3](LICENSE). Автор [исходного проекта](https://github.com/ua1cfm/G5500-ESP32S3-Controller) — UA1CFM; автор изменений форка — RA3VLD. [Лицензии сторонних компонентов](docs/index.md#лицензии-и-авторство).
