#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
JPink / CrossPoint Firmware Flasher & Hardware Tool
===================================================
Утилита для прошивки и обслуживания электронной книги на базе ESP32-C3
(XTEINK X4, X3, X4 Pro и др.).

Возможности:
1. Прошивка по USB в 1 клик через PlatformIO
2. Сборка firmware.bin для офлайн-обновления с SD-карты
3. Полный дамп/бэкап памяти устройства (16 МБ SPI Flash)
4. Восстановление прошивки из ранее сохраненного бэкапа
5. Определение типа чипа, MAC-адреса и размера флеш-памяти
6. Монитор порта (Serial Monitor) для отладки
"""

import os
import sys
import time
import shutil
import argparse
import datetime
import subprocess

PROJECT_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
DIST_DIR = os.path.join(PROJECT_DIR, "dist")
BACKUPS_DIR = os.path.join(PROJECT_DIR, "backups")

ENVIRONMENTS = [
    ("default", "ESP32-C3 XTEINK X4 / X3 (16MB Flash) [Основной]"),
    ("x4pro",   "ESP32-C3 XTEINK X4 Pro"),
    ("x4c",     "ESP32-C3 XTEINK X4 Color"),
    ("papermono","PaperMono"),
    ("sticky",  "Sticky E-Ink"),
    ("picco",   "Picco"),
]

def find_platformio():
    """Находит исполняемый файл PlatformIO."""
    # 1. Проверка в PATH
    pio = shutil.which("pio") or shutil.which("platformio")
    if pio:
        return pio

    # 2. Стандартные пути PlatformIO Core
    home = os.path.expanduser("~")
    candidates = [
        os.path.join(home, ".platformio", "penv", "Scripts", "pio.exe"),
        os.path.join(home, ".platformio", "penv", "Scripts", "platformio.exe"),
        os.path.join(home, ".platformio", "penv", "bin", "pio"),
        os.path.join(home, ".platformio", "penv", "bin", "platformio"),
        "/usr/local/bin/pio",
        "/usr/bin/pio",
    ]
    for c in candidates:
        if os.path.isfile(c) and os.access(c, os.X_OK):
            return c

    # 3. Python module fallback
    try:
        res = subprocess.run([sys.executable, "-m", "platformio", "--version"],
                             capture_output=True, text=True)
        if res.returncode == 0:
            return f"{sys.executable} -m platformio"
    except Exception:
        pass

    # 4. WSL PlatformIO fallback
    wsl = shutil.which("wsl")
    if wsl:
        try:
            res = subprocess.run([wsl, "bash", "-c", "/usr/local/bin/pio --version"],
                                 capture_output=True, text=True)
            if res.returncode == 0:
                return "wsl_pio"
        except Exception:
            pass

    return None

def find_esptool():
    """Находит esptool.py."""
    esp = shutil.which("esptool.py") or shutil.which("esptool")
    if esp:
        return esp
    try:
        res = subprocess.run([sys.executable, "-m", "esptool", "version"],
                             capture_output=True, text=True)
        if res.returncode == 0:
            return f"{sys.executable} -m esptool"
    except Exception:
        pass
    return None

def ensure_dependencies():
    """Проверяет и при необходимости устанавливает pyserial и esptool."""
    missing = []
    try:
        import serial
    except ImportError:
        missing.append("pyserial")
    try:
        import esptool
    except ImportError:
        missing.append("esptool")

    if missing:
        print(f"[!] Не установлены необходимые пакеты: {', '.join(missing)}")
        ans = input(f"[?] Установить их автоматически через pip? [Y/n]: ").strip().lower()
        if ans in ("", "y", "yes", "д", "да"):
            print("[*] Установка через pip...")
            subprocess.run([sys.executable, "-m", "pip", "install", *missing], check=True)
            print("[OK] Пакеты успешно установлены!")
        else:
            print("[!] Без pyserial и esptool некоторые функции могут быть недоступны.")

def list_serial_ports():
    """Возвращает список доступных COM/последовательных портов."""
    try:
        from serial.tools import list_ports
        ports = list(list_ports.comports())
        return ports
    except ImportError:
        return []

def select_port(ports=None):
    """Интерактивный выбор COM-порта с фильтрацией Bluetooth и автоопределением."""
    if ports is None:
        ports = list_serial_ports()

    # Разделяем на USB/аппаратные порты и виртуальные (Bluetooth и т.п.)
    usb_ports = []
    other_ports = []
    for p in ports:
        desc = (p.description or "").lower()
        hwid = (p.hwid or "").lower()
        if "bluetooth" in desc or "bth" in hwid:
            other_ports.append(p)
        else:
            usb_ports.append(p)

    print("\n--- Доступные порты ---")
    active_list = usb_ports if usb_ports else ports

    if usb_ports:
        print(" [Физические / USB порты]:")
        likely_idx = 0
        for idx, p in enumerate(usb_ports):
            desc = p.description or ""
            hwid = p.hwid or ""
            is_esp = any(k in (desc + hwid).lower() for k in ["usb", "uart", "cp210", "ch340", "ftdi", "espressif", "jtag", "serial"])
            mark = " (рекомендуется)" if is_esp else ""
            print(f"  [{idx + 1}] {p.device} - {desc}{mark}")
            if is_esp:
                likely_idx = idx

        if other_ports:
            print(f"  ({len(other_ports)} Bluetooth-портов скрыто для удобства)")
    else:
        print("  [!] USB-порты не найдены. Убедитесь, что книга подключена через USB-кабель данных.")

    print("\n  [A] Автоопределение порта (переподключите USB-кабель книги)")
    print("  [M] Ввести имя порта вручную (например COM3 или /dev/ttyUSB0)")
    print("  [B] Показать все порты (включая Bluetooth)")

    default_choice = str(likely_idx + 1) if usb_ports else "A"
    ans = input(f"\nВыберите порт [1-{len(usb_ports)}] или A/M (по умолчанию [{default_choice}]): ").strip().upper()
    if not ans:
        ans = default_choice

    if ans == "A":
        print("\n[*] Отключите кабель книги и подключите его снова...")
        print("    Ожидание появления нового COM-порта (15 сек)...")
        initial_ports = {p.device for p in list_serial_ports()}
        for _ in range(30):
            time.sleep(0.5)
            current_ports = {p.device for p in list_serial_ports()}
            new_ports = current_ports - initial_ports
            if new_ports:
                found_port = list(new_ports)[0]
                print(f"\n[OK] Обнаружено новое устройство: {found_port}!")
                return found_port
        print("[!] Новый порт не обнаружен. Используйте ручной выбор.")
        return select_port()

    if ans == "M":
        port_input = input("    Введите имя порта вручную [Enter для отмены]: ").strip()
        return port_input if port_input else None

    if ans == "B":
        for idx, p in enumerate(ports):
            print(f"  [{idx + 1}] {p.device} - {p.description}")
        sub = input(f"Выберите [1-{len(ports)}]: ").strip()
        try:
            return ports[int(sub) - 1].device
        except Exception:
            return None

    try:
        choice = int(ans) - 1
        if 0 <= choice < len(usb_ports):
            return usb_ports[choice].device
    except ValueError:
        pass

    return ans

def select_environment():
    """Интерактивный выбор целевой платы / окружения PlatformIO."""
    print("\n--- Выбор целевого устройства / экрана ---")
    for idx, (env_name, desc) in enumerate(ENVIRONMENTS):
        print(f"  [{idx + 1}] {desc} ({env_name})")
    print(f"  [{len(ENVIRONMENTS) + 1}] Ввести имя вручную")

    ans = input(f"\nВыберите профиль [1-{len(ENVIRONMENTS) + 1}] (по умолчанию [1]): ").strip()
    if not ans or ans == "1":
        return ENVIRONMENTS[0][0]

    try:
        c = int(ans) - 1
        if 0 <= c < len(ENVIRONMENTS):
            return ENVIRONMENTS[c][0]
    except ValueError:
        pass

    custom = input("Введите имя окружения из platformio.ini: ").strip()
    return custom if custom else "default"

def run_command(cmd, cwd=PROJECT_DIR):
    """Выполняет шелл-команду с выводом в реальном времени."""
    if isinstance(cmd, str):
        shell = True
    else:
        shell = False
    proc = subprocess.Popen(cmd, cwd=cwd, shell=shell)
    proc.communicate()
    return proc.returncode

def flash_via_usb(pio, port=None, env="default"):
    """Сборка и прошивка через USB."""
    ports = list_serial_ports()
    if not port:
        port = select_port(ports)
    if not port:
        print("[!] Прошивка отменена: порт не выбран.")
        return False

    print(f"\n[*] Сборка и прошивка: окружение '{env}', порт '{port}'...")
    upload_arg = f"--upload-port {port}"
    cmd = f"{pio} run -e {env} -t upload {upload_arg}"
    ret = run_command(cmd)
    if ret == 0:
        print("\n" + "=" * 50)
        print(" [OK] Прошивка успешно завершена!")
        print(" Книга перезагружается с новой прошивкой JPink.")
        print("=" * 50)
        return True
    else:
        print("\n" + "!" * 50)
        print(" [ОШИБКА] Не удалось прошить устройство!")
        print(" Совет: если устройство не входит в режим прошивки:")
        print("  1. Зажмите кнопку загрузки (Boot / Down) на плате.")
        print("  2. Кратковременно нажмите Reset.")
        print("  3. Отпустите кнопку Boot и повторите попытку.")
        print("!" * 50)
        return False

def flash_bin_via_esptool(esptool, port=None, bin_path=None):
    """Прямая прошивка готового бинарника через esptool."""
    factory_bin = os.path.join(DIST_DIR, "firmware.factory.bin")
    firmware_bin = os.path.join(DIST_DIR, "firmware.bin")

    if not bin_path:
        if os.path.isfile(factory_bin) and os.path.isfile(firmware_bin):
            print("\n--- Режим USB-прошивки ---")
            print("  [1] Полная прошивка (firmware.factory.bin, offset 0x0)")
            print("      Включает bootloader, таблицу разделов и прошивку. Рекомендуется!")
            print("  [2] Только прошивка (firmware.bin, offset 0x10000)")
            mode = input("Выберите вариант [1-2] (по умолчанию [1]): ").strip()
            if mode == "2":
                bin_path = firmware_bin
                offset = "0x10000"
            else:
                bin_path = factory_bin
                offset = "0x0"
        elif os.path.isfile(factory_bin):
            bin_path = factory_bin
            offset = "0x0"
        elif os.path.isfile(firmware_bin):
            bin_path = firmware_bin
            offset = "0x10000"
        else:
            print(f"[!] Файлы прошивки не найдены в {DIST_DIR}")
            return False
    else:
        offset = "0x0" if "factory" in os.path.basename(bin_path).lower() else "0x10000"

    ports = list_serial_ports()
    if not port:
        port = select_port(ports)
    if not port:
        print("[!] Прошивка отменена: порт не выбран.")
        return False

    size_mb = os.path.getsize(bin_path) / (1024 * 1024)
    print(f"\n[*] Прямая USB-прошивка: {os.path.basename(bin_path)} ({size_mb:.2f} МБ) -> {port} (offset {offset})...")
    cmd = f"{esptool} --chip esp32c3 --port {port} --baud 921600 --before default_reset --after hard_reset write_flash {offset} \"{bin_path}\""
    ret = run_command(cmd)
    if ret == 0:
        print("\n" + "=" * 50)
        print(" [OK] Прошивка успешно завершена!")
        print(" Книга перезагружается с новой прошивкой JPink.")
        print("=" * 50)
        return True
    else:
        print("\n" + "!" * 50)
        print(" [ОШИБКА] Не удалось прошить устройство!")
        print(" Совет: если устройство не входит в режим прошивки:")
        print("  1. Зажмите кнопку загрузки (Boot / Down) на плате.")
        print("  2. Кратковременно нажмите Reset.")
        print("  3. Отпустите кнопку Boot и повторите попытку.")
        print("!" * 50)
        return False

def export_sd_firmware(pio, env="default", copy_to=None):
    """Сборка firmware.bin для обновления через SD-карту."""
    dist_bin = os.path.join(DIST_DIR, "firmware.bin")
    if not pio and os.path.isfile(dist_bin):
        size_mb = os.path.getsize(dist_bin) / (1024 * 1024)
        print(f"\n[OK] Обнаружен готовый бинарник прошивки: {dist_bin} ({size_mb:.2f} МБ)")
        out_bin = dist_bin
    else:
        if not pio:
            print("[!] PlatformIO не установлен и готовый firmware.bin не найден.")
            return False
        print(f"\n[*] Сборка firmware.bin для SD-карты (окружение '{env}')...")
        cmd = f"{pio} run -e {env}"
        ret = run_command(cmd)
        if ret != 0:
            print("[ОШИБКА] Ошибка компиляции прошивки.")
            return False

        src_bin = os.path.join(PROJECT_DIR, ".pio", "build", env, "firmware.bin")
        if not os.path.isfile(src_bin):
            print(f"[ОШИБКА] Файл прошивки не найден по пути: {src_bin}")
            return False

        os.makedirs(DIST_DIR, exist_ok=True)
        out_bin = os.path.join(DIST_DIR, "firmware.bin")
        shutil.copy2(src_bin, out_bin)
        size_mb = os.path.getsize(out_bin) / (1024 * 1024)
        print("\n" + "=" * 60)
        print(f" [OK] Файл прошивки успешно создан: {out_bin} ({size_mb:.2f} МБ)")
        print("=" * 60)

    # Предложение скопировать сразу на SD-карту
    if not copy_to:
        ans = input("\n[?] Скопировать firmware.bin прямо на SD-карту? (введите букву диска, например E: или путь) [Enter чтобы пропустить]: ").strip()
        if ans:
            copy_to = ans

    if copy_to:
        target_dest = copy_to
        if not target_dest.endswith("firmware.bin"):
            target_dest = os.path.join(target_dest, "firmware.bin")
        try:
            shutil.copy2(out_bin, target_dest)
            print(f"[OK] Скопировано на SD-карту: {target_dest}")
        except Exception as e:
            print(f"[!] Не удалось скопировать на SD-карту: {e}")

    print("\nИнструкция по обновлению на книге:")
    print("  1. Скопируйте файл firmware.bin в корень SD-карты.")
    print("  2. Вставьте SD-карту в электронную книгу.")
    print("  3. На книге откройте: Настройки -> Обслуживание -> Обновление с SD-карты.")
    print("  4. Выберите firmware.bin и подтвердите прошивку.")
    print("  5. Дождитесь перезагрузки устройства.")
    return True

def backup_flash(esptool, port=None):
    """Создает полный 16 МБ дамп SPI флеш-памяти устройства."""
    ports = list_serial_ports()
    if not port:
        port = select_port(ports)
    if not port:
        print("[!] Бэкап отменен: порт не выбран.")
        return False

    os.makedirs(BACKUPS_DIR, exist_ok=True)
    ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup_file = os.path.join(BACKUPS_DIR, f"crosspoint_16mb_backup_{ts}.bin")

    print(f"\n[*] Чтение полного дампа флеш-памяти (16 МБ) с порта {port}...")
    print(f"[*] Файл сохранения: {backup_file}")
    cmd = f"{esptool} --port {port} --baud 921600 read_flash 0x0 0x1000000 \"{backup_file}\""
    ret = run_command(cmd)
    if ret == 0 and os.path.isfile(backup_file):
        size_mb = os.path.getsize(backup_file) / (1024 * 1024)
        print("\n" + "=" * 60)
        print(f" [OK] Полный бэкап успешно сохранен: {backup_file} ({size_mb:.1f} МБ)")
        print(" Этот файл содержит заводскую прошивку и все настройки.")
        print(" В любой момент вы можете вернуть устройство в исходное состояние!")
        print("=" * 60)
        return True
    else:
        print("[ОШИБКА] Не удалось прочитать память устройства.")
        return False

def restore_flash(esptool, port=None):
    """Восстанавливает полный дамп флеш-памяти из бэкапа."""
    if not os.path.isdir(BACKUPS_DIR):
        print(f"[!] Папка с бэкапами не найдена: {BACKUPS_DIR}")
        return False

    backups = [f for f in os.listdir(BACKUPS_DIR) if f.endswith(".bin")]
    if not backups:
        print(f"[!] В папке {BACKUPS_DIR} нет файлов бэкапов (.bin).")
        return False

    print("\n--- Доступные файлы бэкапов ---")
    for idx, f in enumerate(backups):
        fpath = os.path.join(BACKUPS_DIR, f)
        sz = os.path.getsize(fpath) / (1024 * 1024)
        print(f"  [{idx + 1}] {f} ({sz:.1f} МБ)")

    ans = input(f"\nВыберите файл для восстановления [1-{len(backups)}]: ").strip()
    try:
        c = int(ans) - 1
        if not (0 <= c < len(backups)):
            print("[!] Неверный выбор.")
            return False
        selected_file = os.path.join(BACKUPS_DIR, backups[c])
    except ValueError:
        print("[!] Отмена.")
        return False

    ports = list_serial_ports()
    if not port:
        port = select_port(ports)
    if not port:
        print("[!] Восстановление отменено: порт не выбран.")
        return False

    confirm = input(f"\n[ВНИМАНИЕ] Вы действительно хотите записать {selected_file} в {port}? [yes/N]: ").strip().lower()
    if confirm not in ("y", "yes", "д", "да"):
        print("[*] Восстановление отменено пользователем.")
        return False

    print(f"\n[*] Восстановление памяти устройства из {selected_file}...")
    cmd = f"{esptool} --port {port} --baud 921600 write_flash 0x0 \"{selected_file}\""
    ret = run_command(cmd)
    if ret == 0:
        print("\n[OK] Память устройства успешно восстановлена из бэкапа!")
        return True
    else:
        print("\n[ОШИБКА] Ошибка при записи во флеш-память.")
        return False

def show_device_info(esptool, port=None):
    """Определяет модель чипа, MAC-адрес и параметры флеш."""
    ports = list_serial_ports()
    if not port:
        port = select_port(ports)
    if not port:
        return

    print(f"\n[*] Запрос информации о чипе на порту {port}...")
    cmd = f"{esptool} --port {port} chip_id"
    run_command(cmd)
    print(f"\n[*] Запрос параметров Flash-памяти...")
    cmd = f"{esptool} --port {port} flash_id"
    run_command(cmd)

def start_serial_monitor(pio, port=None):
    """Запускает монитор порта для отладки."""
    ports = list_serial_ports()
    if not port:
        port = select_port(ports)
    if not port:
        return

    print(f"\n[*] Запуск монитора порта {port} на скорости 115200 бод...")
    print("    (Для выхода нажмите Ctrl+C или Ctrl+])\n")
    cmd = f"{pio} device monitor --port {port} --baud 115200"
    run_command(cmd)

def main():
    parser = argparse.ArgumentParser(description="JPink / CrossPoint Firmware Flasher")
    parser.add_argument("--flash", action="store_true", help="Прошить через USB")
    parser.add_argument("--export-sd", action="store_true", help="Собрать firmware.bin для SD")
    parser.add_argument("--backup", action="store_true", help="Создать полный 16 МБ бэкап памяти")
    parser.add_argument("--restore", action="store_true", help="Восстановить из бэкапа")
    parser.add_argument("--monitor", action="store_true", help="Открыть Serial Monitor")
    parser.add_argument("--info", action="store_true", help="Информация об устройстве")
    parser.add_argument("--port", type=str, default=None, help="COM-порт (например COM3 или /dev/ttyUSB0)")
    parser.add_argument("--env", type=str, default="default", help="Окружение PlatformIO (default, x4pro, etc.)")
    parser.add_argument("--sd-path", type=str, default=None, help="Путь к SD-карте для экспорта")

    args = parser.parse_args()

    ensure_dependencies()

    pio = find_platformio()
    esptool = find_esptool()

    if not pio:
        print("[!] PlatformIO CLI не найден в системе.")
        print("    Установите PlatformIO: pip install platformio")
    if not esptool:
        print("[!] esptool не найден в системе.")
        print("    Установите esptool: pip install esptool")

    # Non-interactive CLI switches
    if args.flash:
        if not pio:
            sys.exit(1)
        flash_via_usb(pio, args.port, args.env)
        return
    if args.export_sd:
        if not pio:
            sys.exit(1)
        export_sd_firmware(pio, args.env, args.sd_path)
        return
    if args.backup:
        if not esptool:
            sys.exit(1)
        backup_flash(esptool, args.port)
        return
    if args.restore:
        if not esptool:
            sys.exit(1)
        restore_flash(esptool, args.port)
        return
    if args.monitor:
        if not pio:
            sys.exit(1)
        start_serial_monitor(pio, args.port)
        return
    if args.info:
        if not esptool:
            sys.exit(1)
        show_device_info(esptool, args.port)
        return

    # Interactive Menu
    selected_env = args.env

    while True:
        print("\n" + "=" * 62)
        print("         JPink (CrossPoint Fork) - Live Flasher & Tool")
        print("=" * 62)
        print(f" Текущее окружение (плата): [{selected_env}]")
        print("-" * 62)
        print("  1. ⚡ Прошить через USB (1-Click USB Flash)")
        print("  2. 💾 Собрать firmware.bin для SD-карты (Офлайн-обновление)")
        print("  3. 📦 Создать полный 16 МБ бэкап устройства (Dump SPI Flash)")
        print("  4. 🔄 Восстановить прошивку из файла бэкапа")
        print("  5. 🔍 Информация об устройстве (Chip ID & Flash Info)")
        print("  6. 🖥️ Монитор порта (Serial Monitor / Отладка)")
        print("  7. ⚙️ Сменить модель платы / экрана")
        print("  0. Выход")
        print("=" * 62)

        try:
            choice = input("\nВыберите действие [0-7]: ").strip()
        except (EOFError, KeyboardInterrupt):
            print("\nВыход.")
            break

        if choice == "1":
            dist_factory = os.path.join(DIST_DIR, "firmware.factory.bin")
            dist_bin = os.path.join(DIST_DIR, "firmware.bin")
            if esptool and (os.path.isfile(dist_factory) or os.path.isfile(dist_bin)):
                flash_bin_via_esptool(esptool, args.port)
            elif pio:
                flash_via_usb(pio, args.port, selected_env)
            else:
                print("[!] Не найден esptool и бинарники в dist/. Сначала соберите проект.")
        elif choice == "2":
            dist_bin = os.path.join(DIST_DIR, "firmware.bin")
            if os.path.isfile(dist_bin) or pio:
                export_sd_firmware(pio, selected_env, args.sd_path)
            else:
                print("[!] PlatformIO не установлен и готовый dist/firmware.bin не найден!")
        elif choice == "3":
            if esptool:
                backup_flash(esptool, args.port)
            else:
                print("[!] esptool не установлен!")
        elif choice == "4":
            if esptool:
                restore_flash(esptool, args.port)
            else:
                print("[!] esptool не установлен!")
        elif choice == "5":
            if esptool:
                show_device_info(esptool, args.port)
            else:
                print("[!] esptool не установлен!")
        elif choice == "6":
            if pio:
                start_serial_monitor(pio, args.port)
            else:
                print("[!] PlatformIO не установлен!")
        elif choice == "7":
            selected_env = select_environment()
        elif choice in ("0", "q", "exit"):
            print("До свидания!")
            break
        else:
            print("[!] Неверный выбор, попробуйте снова.")

if __name__ == "__main__":
    main()
