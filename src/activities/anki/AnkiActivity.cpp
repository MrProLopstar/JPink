#include "AnkiActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>
#include <sstream>

#include "I18nKeys.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
struct StarterCard {
  const char* front;
  const char* reading;
  const char* back;
};

const StarterCard STARTER_CARDS[] = {
    {"日", "ひ / にち", "День, солнце"},
    {"本", "ほん", "Книга, основа"},
    {"食べる", "たべる", "Есть, кушать"},
    {"飲む", "のむ", "Пить"},
    {"見る", "みる", "Видеть, смотреть"},
    {"行く", "いく", "Идти, ехать"},
    {"来る", "くる", "Приходить, приезжать"},
    {"話す", "はなす", "Говорить, беседовать"},
    {"買う", "かう", "Покупать"},
    {"読む", "よむ", "Читать"},
    {"書く", "かく", "Писать"},
    {"聞く", "きく", "Слушать, слышать"},
    {"大きい", "おおきい", "Большой"},
    {"小さい", "ちいさい", "Маленький"},
    {"新しい", "あたらしい", "Новый"},
    {"古い", "ふるい", "Старый"},
    {"良い", "いい / よい", "Хороший"},
    {"悪い", "わるい", "Плохой"},
    {"高い", "たかい", "Высокий, дорогой"},
    {"安い", "やすい", "Дешёвый, спокойный"},
    {"学校", "がっこう", "Школа"},
    {"先生", "せんせい", "Учитель, мастер"},
    {"学生", "がくせい", "Студент, ученик"},
    {"友達", "ともだち", "Друг, приятель"},
    {"人", "ひと", "Человек"},
    {"時間", "じかん", "Время, часы"},
    {"今日", "きょう", "Сегодня"},
    {"明日", "あした", "Завтра"},
    {"昨日", "きのう", "Вчера"},
    {"今", "いま", "Сейчас, теперь"},
    {"ありがとう", "arigatou", "Спасибо (благодарность)"},
    {"おはよう", "ohayou", "Доброе утро"},
    {"さようなら", "sayounara", "До свидания, прощай"},
    {"はい", "hai", "Да (согласие)"},
    {"いいえ", "iie", "Нет (отрицание)"},
    {"水", "みず", "Вода"},
    {"お茶", "おちゃ", "Зелёный чай"},
    {"ご飯", "ごはん", "Варёный рис, еда"},
    {"肉", "にく", "Мясо"},
    {"魚", "さかな", "Рыба"},
    {"駅", "えき", "Станция, вокзал"},
    {"電車", "でんしゃ", "Поезд, электричка"},
    {"車", "くるま", "Автомобиль, машина"},
    {"道", "みち", "Дорога, путь"},
    {"家", "いえ / うち", "Дом, семья"},
    {"部屋", "へや", "Комната"},
    {"山", "やま", "Гора"},
    {"川", "かわ", "Река"},
    {"雨", "あめ", "Дождь"},
    {"空", "そら", "Небо"},
};
constexpr size_t STARTER_CARDS_COUNT = sizeof(STARTER_CARDS) / sizeof(STARTER_CARDS[0]);
}  // namespace

AnkiActivity::AnkiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Anki", renderer, mappedInput) {}

void AnkiActivity::onEnter() {
  Activity::onEnter();
  state = State::DECK_LIST;
  needsCleanRefresh = true;
  selectedDeckIndex = 0;
  flipAnimStep = 0;
  loadDecks();
  requestUpdate(true);
}

void AnkiActivity::initBuiltinDeck() {
  AnkiDeck deck;
  deck.name = "JLPT N5 Core Vocabulary";
  deck.filepath = "/anki/JLPT_N5_Core_Vocabulary.tsv";

  for (size_t i = 0; i < STARTER_CARDS_COUNT; ++i) {
    AnkiCard card;
    card.front = STARTER_CARDS[i].front;
    card.reading = STARTER_CARDS[i].reading;
    card.back = STARTER_CARDS[i].back;
    deck.cards.push_back(card);
  }
  deck.dueCount = static_cast<int>(deck.cards.size());
  decks.push_back(deck);

  // Write default starter deck to SD card if /anki exists or can be created
  Storage.ensureDirectoryExists("/anki");
  if (!Storage.exists(deck.filepath.c_str())) {
    String tsvData;
    for (size_t i = 0; i < STARTER_CARDS_COUNT; ++i) {
      tsvData += STARTER_CARDS[i].front;
      tsvData += "\t";
      tsvData += STARTER_CARDS[i].back;
      tsvData += "\t";
      tsvData += STARTER_CARDS[i].reading;
      tsvData += "\n";
    }
    Storage.writeFile(deck.filepath.c_str(), tsvData);
  }
}

void AnkiActivity::parseDeckFile(const std::string& path, AnkiDeck& deck) {
  std::string content;
  if (!Storage.readFileToString("ANKI", path, 1024 * 512, content)) return;

  std::stringstream ss(content);
  std::string line;
  while (std::getline(ss, line)) {
    if (line.empty() || line[0] == '#') continue;
    // Strip trailing carriage return
    if (!line.empty() && line.back() == '\r') line.pop_back();

    std::vector<std::string> parts;
    size_t start = 0;
    while (start < line.size()) {
      size_t tab = line.find('\t', start);
      if (tab == std::string::npos) {
        parts.push_back(line.substr(start));
        break;
      }
      parts.push_back(line.substr(start, tab - start));
      start = tab + 1;
    }

    if (parts.size() >= 2) {
      AnkiCard card;
      card.front = parts[0];
      card.back = parts[1];
      if (parts.size() >= 3) card.reading = parts[2];
      if (parts.size() >= 4) card.extra = parts[3];
      deck.cards.push_back(card);
    }
  }
  deck.dueCount = static_cast<int>(deck.cards.size());
}

void AnkiActivity::loadDecks() {
  decks.clear();
  Storage.ensureDirectoryExists("/anki");

  auto files = Storage.listFiles("/anki", 50);
  for (const auto& f : files) {
    std::string filename = f.c_str();
    if (filename.size() > 4 && (filename.rfind(".tsv") == filename.size() - 4 ||
                                filename.rfind(".txt") == filename.size() - 4)) {
      if (filename.find("/.") != std::string::npos) continue;  // skip hidden progress files
      AnkiDeck deck;
      // Derive name from filename
      size_t slash = filename.find_last_of('/');
      std::string basename = (slash == std::string::npos) ? filename : filename.substr(slash + 1);
      size_t dot = basename.find_last_of('.');
      deck.name = (dot == std::string::npos) ? basename : basename.substr(0, dot);
      deck.filepath = filename;
      parseDeckFile(filename, deck);
      if (!deck.cards.empty()) {
        decks.push_back(deck);
      }
    }
  }

  // If no decks found on card, populate builtin deck
  if (decks.empty()) {
    initBuiltinDeck();
  }
}

void AnkiActivity::startDeckSession(int deckIdx) {
  if (deckIdx < 0 || deckIdx >= static_cast<int>(decks.size())) return;
  selectedDeckIndex = deckIdx;
  const auto& deck = decks[deckIdx];

  studySessionIndices.clear();
  for (size_t i = 0; i < deck.cards.size(); ++i) {
    studySessionIndices.push_back(i);
  }

  sessionProgress = 0;
  reviewedInSession = 0;
  correctInSession = 0;
  selectedRating = 2;  // Good

  needsCleanRefresh = true;
  if (!studySessionIndices.empty()) {
    currentCardIndex = studySessionIndices[0];
    state = State::CARD_FRONT;
  } else {
    state = State::SESSION_COMPLETE;
  }
  requestUpdate();
}

void AnkiActivity::applyRating(int rating) {
  if (selectedDeckIndex >= static_cast<int>(decks.size())) return;
  auto& deck = decks[selectedDeckIndex];
  if (currentCardIndex >= deck.cards.size()) return;
  auto& card = deck.cards[currentCardIndex];

  reviewedInSession++;
  if (rating >= 2) {
    correctInSession++;
  }

  // SM-2 Spaced Repetition calculation
  if (rating == 0) {  // Again
    card.reps = 0;
    card.intervalDays = 0;
    card.lapses++;
    if (card.easeFactor > 150) card.easeFactor -= 20;
    // Re-queue card to end of session so user reviews it again!
    studySessionIndices.push_back(currentCardIndex);
  } else if (rating == 1) {  // Hard
    card.intervalDays = (card.intervalDays == 0) ? 1 : (card.intervalDays * 12 / 10);
    if (card.easeFactor > 140) card.easeFactor -= 15;
    card.reps++;
  } else if (rating == 2) {  // Good
    if (card.reps == 0) {
      card.intervalDays = 1;
    } else if (card.reps == 1) {
      card.intervalDays = 3;
    } else {
      card.intervalDays = (card.intervalDays * card.easeFactor) / 100;
    }
    card.reps++;
  } else if (rating == 3) {  // Easy
    if (card.reps == 0) {
      card.intervalDays = 3;
    } else if (card.reps == 1) {
      card.intervalDays = 6;
    } else {
      card.intervalDays = (card.intervalDays * card.easeFactor * 13) / 1000;
    }
    card.easeFactor += 15;
    card.reps++;
  }

  sessionProgress++;
  needsCleanRefresh = true;
  if (sessionProgress < studySessionIndices.size()) {
    currentCardIndex = studySessionIndices[sessionProgress];
    state = State::CARD_FRONT;
    selectedRating = 2;
  } else {
    state = State::SESSION_COMPLETE;
  }
  requestUpdate();
}

void AnkiActivity::loop() {
  if (flipAnimStep == 1) {
    // Progress flip animation
    flipAnimStep = 0;
    state = State::CARD_BACK;
    requestUpdate();
    return;
  }

  if (state == State::DECK_LIST) {
    int touchedDeck = -1;
    const auto& metrics = UITheme::getInstance().getMetrics();
    int startY = metrics.topPadding + metrics.headerHeight + 34;
    int rowHeight = 70;
    int spacing = 10;
    const int count = static_cast<int>(decks.size());
    const auto touch = mappedInput.rowTouch(touchedDeck, startY, rowHeight + spacing, count);
    if (touch != MappedInputManager::RowTouch::None) {
      if (touch == MappedInputManager::RowTouch::Down) {
        if (selectedDeckIndex != touchedDeck) {
          selectedDeckIndex = touchedDeck;
          requestUpdate();
        }
      } else {
        selectedDeckIndex = touchedDeck;
        startDeckSession(selectedDeckIndex);
        return;
      }
    }

    buttonNavigator.onNext([this] {
      if (!decks.empty()) {
        selectedDeckIndex = (selectedDeckIndex + 1) % decks.size();
        requestUpdate();
      }
    });
    buttonNavigator.onPrevious([this] {
      if (!decks.empty()) {
        selectedDeckIndex = (selectedDeckIndex == 0) ? decks.size() - 1 : selectedDeckIndex - 1;
        requestUpdate();
      }
    });
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      startDeckSession(selectedDeckIndex);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    return;
  }

  if (state == State::CARD_FRONT) {
    int dummy = -1;
    const auto cardTouch = mappedInput.rowTouch(dummy, 60, 320, 1);
    if (cardTouch == MappedInputManager::RowTouch::Tap) {
      flipAnimStep = 1;
      requestUpdate(true);
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      // Trigger finite 2-frame flip animation
      flipAnimStep = 1;
      requestUpdate(true);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = State::DECK_LIST;
      needsCleanRefresh = true;
      requestUpdate(true);
      return;
    }
    return;
  }

  if (state == State::CARD_BACK) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      applyRating(2);  // Good
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      applyRating(3);  // Easy
      return;
    }
    // 4 rating choices
    buttonNavigator.onNext([this] {
      selectedRating = (selectedRating + 1) % 4;
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      selectedRating = (selectedRating == 0) ? 3 : selectedRating - 1;
      requestUpdate();
    });
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      applyRating(selectedRating);
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      // Back button in rating screen acts as "Again" (0)
      applyRating(0);
      return;
    }
    return;
  }

  if (state == State::SESSION_COMPLETE) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) ||
        mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      state = State::DECK_LIST;
      needsCleanRefresh = true;
      requestUpdate();
      return;
    }
  }
}

void AnkiActivity::drawCardFrame(int x, int y, int width, int height, bool fillWhite) {
  if (fillWhite) {
    renderer.fillRect(x, y, width, height, false);
  }
  renderer.drawRect(x, y, width, height, true);
  renderer.drawRect(x + 2, y + 2, width - 4, height - 4, true);
  // Dithered drop shadow
  renderer.fillRectDither(x + 4, y + height, width, 4, Color::LightGray);
  renderer.fillRectDither(x + width, y + 4, 4, height, Color::LightGray);
}

void AnkiActivity::drawProgressBar(int x, int y, int width, int height, float progress) {
  renderer.drawRect(x, y, width, height, true);
  int fillW = static_cast<int>(progress * (width - 4));
  if (fillW > 0) {
    renderer.fillRect(x + 2, y + 2, fillW, height - 4, true);
  }
}

void AnkiActivity::drawCelebrationBadge(int cx, int cy) {
  // Award ribbon badge
  renderer.fillRoundedRect(cx - 35, cy - 35, 70, 70, 20, Color::LightGray);
  renderer.drawRoundedRect(cx - 35, cy - 35, 70, 70, 2, 20, true);
  // Star points in center
  renderer.drawText(UI_12_FONT_ID, cx - 14, cy - 8, "★", true, EpdFontFamily::BOLD);
}

void AnkiActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = UITheme::getInstance().getContentArea(renderer);
  const int screenW = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.headerHeight}, tr(STR_ANKI), nullptr, true);

  if (state == State::DECK_LIST) {
    renderDeckList(content);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == State::CARD_FRONT) {
    renderCardFront(content);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_ANKI_SHOW_ANSWER), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == State::CARD_BACK) {
    renderCardBack(content);
    const auto labels =
        mappedInput.mapLabels(tr(STR_ANKI_AGAIN), tr(STR_SELECT), tr(STR_ANKI_GOOD), tr(STR_ANKI_EASY));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == State::SESSION_COMPLETE) {
    renderSessionComplete(content);
    const auto labels = mappedInput.mapLabels(tr(STR_ANKI_RETURN), tr(STR_SELECT), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  if (needsCleanRefresh) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    needsCleanRefresh = false;
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

void AnkiActivity::renderDeckList(const Rect& content) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int startY = metrics.topPadding + metrics.headerHeight + 8;
  renderer.drawText(UI_12_FONT_ID, content.x + 10, startY, tr(STR_ANKI_DECKS_AVAILABLE), true, EpdFontFamily::BOLD);
  startY += 26;

  int tileHeight = 65;
  int spacing = 10;

  for (size_t i = 0; i < decks.size(); ++i) {
    int y = startY + i * (tileHeight + spacing);
    int x = content.x + 10;
    int w = content.width - 20;

    bool selected = (selectedDeckIndex == static_cast<int>(i));

    if (selected) {
      renderer.fillRoundedRect(x, y, w, tileHeight, 6, Color::LightGray);
      renderer.drawRect(x, y, w, tileHeight, true);
    } else {
      renderer.drawRect(x, y, w, tileHeight, true);
      renderer.fillRectDither(x + 2, y + tileHeight, w, 2, Color::LightGray);
    }

    // Deck Name
    renderer.drawText(UI_12_FONT_ID, x + 16, y + 12, decks[i].name.c_str(), true, EpdFontFamily::BOLD);

    // Cards count
    char countBuf[128];
    snprintf(countBuf, sizeof(countBuf), tr(STR_ANKI_TOTAL_CARDS),
             static_cast<unsigned>(decks[i].cards.size()), decks[i].dueCount);
    renderer.drawText(UI_10_FONT_ID, x + 16, y + 38, countBuf, true);
  }
}

void AnkiActivity::renderCardFront(const Rect& content) {
  if (selectedDeckIndex >= static_cast<int>(decks.size())) return;
  const auto& deck = decks[selectedDeckIndex];
  if (currentCardIndex >= deck.cards.size()) return;
  const auto& card = deck.cards[currentCardIndex];

  const auto& metrics = UITheme::getInstance().getMetrics();
  int cardX = content.x + 15;
  int cardY = metrics.topPadding + metrics.headerHeight + 8;
  int cardW = content.width - 30;
  int cardH = content.y + content.height - cardY - metrics.buttonHintsHeight - 8;

  if (flipAnimStep == 1) {
    int compressH = cardH / 4;
    int centerY = cardY + cardH / 2;
    drawCardFrame(cardX, centerY - compressH / 2, cardW, compressH, true);
    return;
  }

  drawCardFrame(cardX, cardY, cardW, cardH, true);

  // Deck title & Progress
  char progressBuf[32];
  snprintf(progressBuf, sizeof(progressBuf), "[ %u / %u ]", static_cast<unsigned>(sessionProgress + 1),
           static_cast<unsigned>(studySessionIndices.size()));
  renderer.drawText(UI_10_FONT_ID, cardX + 16, cardY + 14, progressBuf, true, EpdFontFamily::BOLD);

  // Progress bar
  float pct = (studySessionIndices.empty()) ? 0.0f
                                           : static_cast<float>(sessionProgress) / studySessionIndices.size();
  drawProgressBar(cardX + cardW - 120, cardY + 16, 100, 10, pct);

  // Divider
  renderer.drawLine(cardX + 12, cardY + 36, cardX + cardW - 12, cardY + 36);

  // Question Prompt (Front)
  int centerY = cardY + cardH / 2 - 20;
  renderer.drawCenteredText(UI_12_FONT_ID, centerY - 30, card.front.c_str(), true, EpdFontFamily::BOLD);

  if (!card.reading.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY + 10, card.reading.c_str(), true);
  }

  renderer.drawLine(cardX + 24, cardY + cardH - 60, cardX + cardW - 24, cardY + cardH - 60);
  renderer.drawCenteredText(UI_10_FONT_ID, cardY + cardH - 42, tr(STR_ANKI_PRESS_SHOW), true);
}

void AnkiActivity::renderCardBack(const Rect& content) {
  if (selectedDeckIndex >= static_cast<int>(decks.size())) return;
  const auto& deck = decks[selectedDeckIndex];
  if (currentCardIndex >= deck.cards.size()) return;
  const auto& card = deck.cards[currentCardIndex];

  const auto& metrics = UITheme::getInstance().getMetrics();
  int cardX = content.x + 15;
  int cardY = metrics.topPadding + metrics.headerHeight + 8;
  int cardW = content.width - 30;
  int cardH = content.y + content.height - cardY - metrics.buttonHintsHeight - 8;

  drawCardFrame(cardX, cardY, cardW, cardH, true);

  // Front prompt top banner
  renderer.drawCenteredText(UI_10_FONT_ID, cardY + 12, card.front.c_str(), true);
  if (!card.reading.empty()) {
    renderer.drawCenteredText(UI_10_FONT_ID, cardY + 28, card.reading.c_str(), true);
  }
  renderer.drawLine(cardX + 12, cardY + 48, cardX + cardW - 12, cardY + 48);

  // Answer Section (Back)
  renderer.drawCenteredText(UI_10_FONT_ID, cardY + 70, tr(STR_ANKI_TRANSLATION_ANSWER), true);
  renderer.drawCenteredText(UI_12_FONT_ID, cardY + 98, card.back.c_str(), true, EpdFontFamily::BOLD);

  // Rating buttons (4 boxes)
  int btnStartY = cardY + 175;
  const char* ratings[] = {
      tr(STR_ANKI_RATING_AGAIN),
      tr(STR_ANKI_RATING_HARD),
      tr(STR_ANKI_RATING_GOOD),
      tr(STR_ANKI_RATING_EASY),
  };
  const char* subratings[] = {
      tr(STR_ANKI_SUBRATING_AGAIN),
      tr(STR_ANKI_SUBRATING_HARD),
      tr(STR_ANKI_SUBRATING_GOOD),
      tr(STR_ANKI_SUBRATING_EASY),
  };

  int btnH = 44;
  int btnGap = 6;

  for (int i = 0; i < 4; ++i) {
    int y = btnStartY + i * (btnH + btnGap);
    int x = cardX + 16;
    int w = cardW - 32;

    bool isSelected = (selectedRating == i);

    if (isSelected) {
      renderer.fillRoundedRect(x, y, w, btnH, 6, Color::LightGray);
    }
    renderer.drawRect(x, y, w, btnH, true);

    renderer.drawText(UI_10_FONT_ID, x + 14, y + 8, ratings[i], true,
                      isSelected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, x + 14, y + 24, subratings[i], true);
  }
}

void AnkiActivity::renderSessionComplete(const Rect& content) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int cardX = content.x + 15;
  int cardY = metrics.topPadding + metrics.headerHeight + 8;
  int cardW = content.width - 30;
  int cardH = content.y + content.height - cardY - metrics.buttonHintsHeight - 8;

  drawCardFrame(cardX, cardY, cardW, cardH, true);

  int midX = cardX + cardW / 2;
  drawCelebrationBadge(midX, cardY + 75);

  renderer.drawCenteredText(UI_12_FONT_ID, cardY + 135, tr(STR_ANKI_SESSION_DONE), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, cardY + 165, tr(STR_ANKI_SESSION_GREAT), true);

  // Stats Box
  int statY = cardY + 205;
  renderer.fillRectDither(cardX + 20, statY, cardW - 40, 70, Color::LightGray);
  renderer.drawRect(cardX + 20, statY, cardW - 40, 70, true);

  char statBuf1[64];
  snprintf(statBuf1, sizeof(statBuf1), tr(STR_ANKI_REVIEWS_DONE), reviewedInSession);
  renderer.drawText(UI_10_FONT_ID, cardX + 32, statY + 16, statBuf1, true, EpdFontFamily::BOLD);

  char statBuf2[64];
  snprintf(statBuf2, sizeof(statBuf2), tr(STR_ANKI_CORRECT_COUNT), correctInSession);
  renderer.drawText(UI_10_FONT_ID, cardX + 32, statY + 40, statBuf2, true);
}
