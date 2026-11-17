# JPink Reader 🌸

<div align="center">

[![Release](https://img.shields.io/github/v/release/MrProLopstar/JPink?style=for-the-badge&color=e056fd)](https://github.com/MrProLopstar/JPink/releases)
[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg?style=for-the-badge)](LICENSE)
[![Fork of CrossPoint](https://img.shields.io/badge/Fork_of-CrossPoint_Reader-ff79c6?style=for-the-badge&logo=github)](https://github.com/crosspoint-reader/crosspoint-reader)
[![Platform: ESP32](https://img.shields.io/badge/Platform-ESP32--C3%20%7C%20ESP32--S3-orange.svg?style=for-the-badge)](https://www.espressif.com/)

**The ultimate open-source, Japanese-learning & productivity firmware for E-Ink readers.**  
*Forked from [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).*

[English](#-english) • [Русский](#-русский)

</div>

---

## 🌐 English

### About JPink
**JPink** is an enhanced, feature-packed fork of the open-source **[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)** firmware created by the CrossPoint Reader organization.

While preserving the core EPUB reading engine, typography, and battery efficiency of CrossPoint, **JPink** adds an integrated Japanese learning suite, interactive calendar, RSS news reader, custom themes, and system stability improvements.

---

### 🌟 Key Features

#### 🎌 Japanese Language Suite
* **Interactive Kana Charts**: Complete Hiragana and Katakana tables (Gojūon, Dakuten, Handakuten, Yōon) with quick navigation and Hepburn / Polivanov readings.
* **Kanji Cards (JLPT N5 – N1)**: Full-screen character cards with On'yomi, Kun'yomi, radicals, stroke count, and English & Russian meanings.
* **Practice & Quizzes**: Rapid flashcard drills and recognition quizzes.
* **Built-in Japanese Typography**: Native high-legibility CJK kanji and kana glyphs.

#### 📅 Interactive E-Ink Calendar
* High-contrast monthly calendar view with clear current-day highlighting.
* Intuitive controls: step through days, hold Left/Right to flip months, hold Up/Down to flip years.

#### 📰 RSS News Reader
* Download RSS/Atom feeds via Wi-Fi for distraction-free offline reading.

#### 🧠 Anki Spaced Repetition
* Review Anki flashcards directly on your e-reader screen without eye fatigue.

#### 🎨 Modern UI & New Themes
* **Themes**: Classic, Lyra, Rounded Raff, Minimalist, and Brutalist.
* **Scroll Indicator (Slider)**: Dynamic slider bar on the Home screen for smooth navigation.
* **Kaomoji Status**: Real-time battery and network status shown via cute kaomoji (`(^_^)`, `(o_o)`).
* **Custom Sleep Screens**: Multiple artistic sleep screen options.

#### ⏱️ Manual Clock & NTP Sync
* Set time and date manually in `Settings → System → Clock → Set clock manually`.
* Automatic background SNTP synchronization over Wi-Fi.

#### 🛡️ Stability & Network Fixes
* **POSIX DNS**: Safe `getaddrinfo` resolution preventing lwIP lock panics during downloads.
* **Stable Wi-Fi**: Disabled aggressive modem sleep (`WiFi.setSleep(false)`) and enabled auto-reconnect.

---

### 📸 Screenshots

| Home Menu (Lyra) | Interactive Calendar | Kanji Study Card |
|:---:|:---:|:---:|
| <img src="./docs/images/jpink/home_menu.png" width="240"/> | <img src="./docs/images/jpink/calendar_view.png" width="240"/> | <img src="./docs/images/jpink/kanji_details.png" width="240"/> |

| Hiragana / Katakana Table | Night Sleep Screen | Reading View |
|:---:|:---:|:---:|
| <img src="./docs/images/jpink/kana_table.png" width="240"/> | <img src="./docs/images/jpink/sleep_screen.png" width="240"/> | <img src="./docs/images/jpink/reading_view.png" width="240"/> |

---

### 📱 Supported Devices

| Device | MCU | Display Controller | Touch | Frontlight |
|:---|:---|:---|:---:|:---:|
| **Xteink X4** | ESP32-C3 | SSD1677 | No | Yes |
| **Xteink X3** | ESP32-C3 | SSD1677 | No | No |
| **Xteink X4Pro** | ESP32-S3 | UC8253 | Yes | Yes |
| **Xteink X4Classic** | ESP32-S3 | SSD1677 | No | Yes |
| **reTerminal Sticky** | ESP32-S3 | ED2208 | Yes | No |

---

### 🚀 Flashing & Quick Start

#### Method 1: 1-Click Flash Script (Recommended)
Ready-to-flash binaries are located in [`dist/`](dist/).
1. Connect your e-reader to your computer via USB-C.
2. Run the flashing script:
   * **Windows**: Double-click `flash.bat`
   * **Linux / macOS**: Run `./flash.sh`

#### Method 2: Web Installer
1. Open https://crosspointreader.com/#flash-tools in Chrome or Edge.
2. Select **Custom .bin** and upload [`dist/firmware.factory.bin`](dist/firmware.factory.bin).
3. Click **Flash**.

---

### 🛠️ Building from Source

```bash
git clone --recursive https://github.com/MrProLopstar/JPink.git
cd JPink

# Build for Xteink X4 (default)
pio run -e default
```

---

<br/>

---

## 🇷🇺 Русский

### О проекте JPink
**JPink** — это расширенный форк открытой прошивки **[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)** (Copyright (c) 2025 CrossPoint Reader organization).

Сохраняя надежный движок для чтения книг и энергоэффективность оригинального CrossPoint, **JPink** превращает устройство в карманный помощник для изучения японского языка, интерактивный календарь и читалку новостей.

---

### 🌟 Основные возможности

* **Изучение японского языка**: интерактивные таблицы Хираганы и Катаканы (Хэпбёрн / Поливанов), карточки Кандзи уровней JLPT N5–N1 с онъёми, кунъёми, ключами и переводами, тренировочные тесты.
* **E-Ink Календарь**: четкая сетка месяца, удобное переключение дней, месяцев и лет, выделение сегодняшнего числа.
* **Чтение RSS новостей**: загрузка лент новостей по Wi-Fi для комфортного чтения офлайн.
* **Карточки Anki**: повторение интервальных карточек прямо на ридере.
* **Интерфейс и темы**: темы Classic, Lyra, Rounded Raff, Minimalist и Brutalist, индикатор прокрутки (слайдер) в главном меню, каомодзи статуса батареи `(^_^)`, новые экраны сна.
* **Часы и дата**: ручная установка даты и времени в настройках системы + автосинхронизация по NTP через интернет.
* **Стабильность сети**: устранение падений lwIP при скачивании по HTTPS, стабильный Wi-Fi без засыпания радиомодуля.

---

### 🚀 Быстрая прошивка

Готовые бинарные файлы находятся в каталоге [`dist/`](dist/).
1. Подключите читалку к компьютеру кабелем USB-C.
2. Запустите скрипт:
   * **Windows**: запустите `flash.bat`
   * **Linux / macOS**: запустите `./flash.sh`

---

### 📜 Лицензия

* Оригинальный исходный код: **MIT License** (CrossPoint Reader organization).
* Скомпилированная прошивка: **GNU General Public License v3 (GPLv3)** из-за использования криптографического стека wolfSSL.
* Подробности: [LICENSE](LICENSE) и [licenses/README.md](licenses/README.md).
