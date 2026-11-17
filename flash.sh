#!/usr/bin/env bash
set -e

cd "$(dirname "$0")"

echo "========================================================"
echo "      JPink (CrossPoint Fork) - Live Hardware Flasher"
echo "========================================================"
echo ""

if ! command -v python3 &> /dev/null && ! command -v python &> /dev/null; then
    echo "[ОШИБКА] Python 3 не найден в системе!"
    echo "Установите python3 через менеджер пакетов (например: sudo apt install python3 python3-pip)"
    exit 1
fi

PYTHON_CMD="python3"
if ! command -v python3 &> /dev/null; then
    PYTHON_CMD="python"
fi

$PYTHON_CMD tools/flash_tool.py "$@"
