# Документация

[О проекте](../README.md)

[Управление через RS‑485](rs485.md): прямое GS‑232 и подключение компьютера через HUD.

## Первый запуск

1. [Выберите конфигурацию](configuration.md#выбор-конфигурации) и [схему подключения](hardware.md).
2. [Соберите и запишите прошивку через USB](build.md).
3. Выполните [первое подключение и настройку](quickstart.md#первое-подключение).
4. Задайте [диапазоны и калибровку](az-range.md), затем [регуляторы](regulator-tuning.md).
5. Перейдите к [управлению](usage.md) и [подключению внешних программ](integrations.md).

## Подключение

- [Платы, начальные GPIO и настройки оборудования](configuration.md); [классический ESP32](classic-esp32.md), [WT32-ETH01 с Ethernet](wt32-eth01.md).
- [G-5500](hardware/g5500-guide.md), [G-800DXA](hardware/g800dxa-guide.md), [BTS7960 / IBT-2](hardware/independent-pwm-bts7960.md), [отдельные направления и PWM](hardware/separate-pwm.md).
- [Входы датчиков ADC](hardware/adc-input.md), [кнопки и джойстик](local-controls.md).
- [CrowPanel: экран и жесты](crowpanel-21.md), [переделка CrowPanel для локального AZ](crowpanel-az.md).

## Управление и обслуживание

- [Веб, режимы, предустановки и скорость](usage.md).
- [HUD: удалённый пульт](hud.md).
- [Логгеры и программы слежения](integrations.md).
- [Wi-Fi и сетевые порты](network.md).
- [Обновление прошивки](ota.md), [диагностика и восстановление](diagnostics.md).

## Внешние API

- [Общие правила и HTTP API](api.md).
- [Modbus RTU: общая линия с маркером и карта регистров](modbus-rtu.md).
- [WebSocket](live.md), [командный API UART/USB, UDP и TCP](command-api.md).
- [Подбор методики автотюна и запись результатов](autotune-experiments.md).

## Лицензии и авторство

Проект — [GNU GPL v3](../LICENSE), исходный автор UA1CFM, изменения форка RA3VLD. Сторонние компоненты: [RGB-драйвер CrowPanel](licenses/crowpanel-rgb-driver.md), [DejaVu](licenses/DejaVu.txt), [Ruckig MIT](licenses/ruckig-MIT.txt), [фавиконка Lucide ISC](licenses/lucide-favicon.md).
