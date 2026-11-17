#include "KanjiActivity.h"

#include <EpdFontData.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdlib>

#include "I18nKeys.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "components/UITheme.h"
#include "fontIds.h"

KanjiActivity::KanjiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Kanji", renderer, mappedInput) {}

void KanjiActivity::onEnter() {
  Activity::onEnter();
  mode = ViewMode::MENU;
  needsCleanRefresh = true;
  menuIndex = 0;
  cardIndex = 0;
  isCardFlipped = false;
  flipAnimStep = 0;
  tableRow = 0;
  tableCol = 0;
  scrollRow = 0;
  isTableHiragana = true;
  quizScore = 0;
  quizTotal = 0;
  cardOrder.clear();
  requestUpdate(true);
}

void KanjiActivity::initCardDeck(size_t count) {
  cardOrder.resize(count);
  for (size_t i = 0; i < count; ++i) {
    cardOrder[i] = i;
  }
  if (shuffleMode && count > 1) {
    // Fisher-Yates shuffle
    for (size_t i = count - 1; i > 0; --i) {
      size_t j = static_cast<size_t>(rand()) % (i + 1);
      std::swap(cardOrder[i], cardOrder[j]);
    }
  }
  cardIndex = 0;
  isCardFlipped = false;
  flipAnimStep = 0;
}

void KanjiActivity::toggleShuffle() {
  shuffleMode = !shuffleMode;
  size_t count = (mode == ViewMode::HIRAGANA) ? kanji_data::HIRAGANA_COUNT
                 : (mode == ViewMode::KATAKANA) ? kanji_data::KATAKANA_COUNT
                 : (mode == ViewMode::KANJI_N5) ? kanji_data::JLPT_N5_COUNT
                 : 0;
  if (count > 0) {
    initCardDeck(count);
  }
  requestUpdate();
}

void KanjiActivity::nextCard() {
  isCardFlipped = false;
  flipAnimStep = 0;
  size_t count = cardOrder.empty() ? 1 : cardOrder.size();
  cardIndex = (cardIndex + 1) % count;
  requestUpdate();
}

void KanjiActivity::prevCard() {
  isCardFlipped = false;
  flipAnimStep = 0;
  size_t count = cardOrder.empty() ? 1 : cardOrder.size();
  cardIndex = (cardIndex == 0) ? count - 1 : cardIndex - 1;
  requestUpdate();
}

void KanjiActivity::flipCard() {
  // 2-frame finite e-ink micro-animation
  flipAnimStep = 1;
  requestUpdate();
}

void KanjiActivity::generateKanaQuizQuestion() {
  currentQuiz.answered = false;
  currentQuiz.selectedIndex = 0;

  bool isHira = (rand() % 2 == 0);
  size_t count = isHira ? kanji_data::HIRAGANA_COUNT : kanji_data::KATAKANA_COUNT;
  size_t idx = rand() % count;
  const auto& item = isHira ? kanji_data::HIRAGANA_SEION[idx] : kanji_data::KATAKANA_SEION[idx];

  currentQuiz.prompt = item.glyph;
  currentQuiz.correctIndex = rand() % 4;

  char buf[32];
  snprintf(buf, sizeof(buf), "[%s] %s", item.romaji, item.cyrillic);
  currentQuiz.options[currentQuiz.correctIndex] = buf;

  for (int i = 0; i < 4; ++i) {
    if (i == currentQuiz.correctIndex) continue;
    size_t randIdx;
    do {
      randIdx = rand() % count;
    } while (randIdx == idx);
    const auto& d = isHira ? kanji_data::HIRAGANA_SEION[randIdx] : kanji_data::KATAKANA_SEION[randIdx];
    snprintf(buf, sizeof(buf), "[%s] %s", d.romaji, d.cyrillic);
    currentQuiz.options[i] = buf;
  }
}

void KanjiActivity::generateKanjiQuizQuestion() {
  currentQuiz.answered = false;
  currentQuiz.selectedIndex = 0;

  size_t idx = rand() % kanji_data::JLPT_N5_COUNT;
  const auto& item = kanji_data::JLPT_N5_KANJI[idx];

  currentQuiz.prompt = item.glyph;
  auto lang = I18n::getInstance().getLanguage();
  const char* meaning = (lang == Language::EN && item.meaningEn && item.meaningEn[0]) ? item.meaningEn : item.meaningRu;
  currentQuiz.correctIndex = rand() % 4;
  currentQuiz.options[currentQuiz.correctIndex] = meaning;

  // Pick 3 random distractors
  for (int i = 0; i < 4; ++i) {
    if (i == currentQuiz.correctIndex) continue;
    size_t randIdx;
    do {
      randIdx = rand() % kanji_data::JLPT_N5_COUNT;
    } while (randIdx == idx);
    const auto& dItem = kanji_data::JLPT_N5_KANJI[randIdx];
    const char* dMeaning = (lang == Language::EN && dItem.meaningEn && dItem.meaningEn[0]) ? dItem.meaningEn : dItem.meaningRu;
    currentQuiz.options[i] = dMeaning;
  }
}

void KanjiActivity::checkQuizAnswer(int option) {
  if (currentQuiz.answered) {
    if (mode == ViewMode::QUIZ_KANA) {
      generateKanaQuizQuestion();
    } else {
      generateKanjiQuizQuestion();
    }
    requestUpdate();
    return;
  }
  currentQuiz.selectedIndex = option;
  currentQuiz.answered = true;
  quizTotal++;
  if (option == currentQuiz.correctIndex) {
    quizScore++;
  }
  requestUpdate();
}

void KanjiActivity::loop() {
  if (flipAnimStep == 1) {
    flipAnimStep = 0;
    isCardFlipped = !isCardFlipped;
    requestUpdate();
    return;
  }

  // =========================================================================
  // MAIN MENU
  // =========================================================================
  if (mode == ViewMode::MENU) {
    int touchedRow = -1;
    const auto& metrics = UITheme::getInstance().getMetrics();
    int startY = metrics.headerHeight + 45;
    int tileHeight = 65;
    int spacing = 10;
    const auto touch = mappedInput.rowTouch(touchedRow, startY, tileHeight + spacing, 6);
    if (touch != MappedInputManager::RowTouch::None) {
      if (touch == MappedInputManager::RowTouch::Down) {
        if (menuIndex != touchedRow) {
          menuIndex = touchedRow;
          requestUpdate();
        }
      } else {
        menuIndex = touchedRow;
        needsCleanRefresh = true;
        if (menuIndex == 0) {
          mode = ViewMode::KANA_TABLE;
          tableRow = 0;
          tableCol = 0;
          isTableHiragana = true;
        } else if (menuIndex == 1) {
          mode = ViewMode::HIRAGANA;
          initCardDeck(kanji_data::HIRAGANA_COUNT);
        } else if (menuIndex == 2) {
          mode = ViewMode::KATAKANA;
          initCardDeck(kanji_data::KATAKANA_COUNT);
        } else if (menuIndex == 3) {
          mode = ViewMode::KANJI_N5;
          initCardDeck(kanji_data::JLPT_N5_COUNT);
        } else if (menuIndex == 4) {
          mode = ViewMode::QUIZ_KANA;
          quizScore = 0;
          quizTotal = 0;
          generateKanaQuizQuestion();
        } else if (menuIndex == 5) {
          mode = ViewMode::QUIZ_KANJI;
          quizScore = 0;
          quizTotal = 0;
          generateKanjiQuizQuestion();
        }
        requestUpdate();
        return;
      }
    }

    buttonNavigator.onNext([this] {
      menuIndex = (menuIndex + 1) % 6;
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      menuIndex = (menuIndex == 0) ? 5 : menuIndex - 1;
      requestUpdate();
    });
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      needsCleanRefresh = true;
      if (menuIndex == 0) {
        mode = ViewMode::KANA_TABLE;
        tableRow = 0;
        tableCol = 0;
        scrollRow = 0;
        isTableHiragana = true;
      } else if (menuIndex == 1) {
        mode = ViewMode::HIRAGANA;
        initCardDeck(kanji_data::HIRAGANA_COUNT);
      } else if (menuIndex == 2) {
        mode = ViewMode::KATAKANA;
        initCardDeck(kanji_data::KATAKANA_COUNT);
      } else if (menuIndex == 3) {
        mode = ViewMode::KANJI_N5;
        initCardDeck(kanji_data::JLPT_N5_COUNT);
      } else if (menuIndex == 4) {
        mode = ViewMode::QUIZ_KANA;
        quizScore = 0;
        quizTotal = 0;
        generateKanaQuizQuestion();
      } else if (menuIndex == 5) {
        mode = ViewMode::QUIZ_KANJI;
        quizScore = 0;
        quizTotal = 0;
        generateKanjiQuizQuestion();
      }
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return;
    }
    return;
  }

  // =========================================================================
  // KANA TABLE (16 Rows x 5 Cols: Seion + Dakuten + Handakuten)
  // =========================================================================
  if (mode == ViewMode::KANA_TABLE) {
    const auto& metrics = UITheme::getInstance().getMetrics();
    int topY = metrics.topPadding + metrics.batteryBarHeight + 4;
    int bottomLimit = renderer.getScreenHeight() - metrics.buttonHintsHeight - 4;
    int detailH = 76;
    int headerH = 24;
    int gridStartY = topY + detailH + 6 + headerH;
    constexpr int VISIBLE_ROWS = 8;
    int tableH = bottomLimit - gridStartY;
    int cellH = tableH / VISIBLE_ROWS;
    int labelColW = 46;
    int soundColW = 76;
    int totalGridW = labelColW + 5 * soundColW;
    int startX = (renderer.getScreenWidth() - totalGridW) / 2;

    // Direct touch selection on the grid
    int touchX = 0, touchY = 0;
    if (mappedInput.wasScreenTapped(touchX, touchY)) {
      // Check detail panel tap -> toggle Hiragana / Katakana
      if (touchY >= topY && touchY < gridStartY - headerH) {
        isTableHiragana = !isTableHiragana;
        requestUpdate();
        return;
      }
      // Check grid cell tap
      if (touchX >= startX + labelColW && touchX < startX + totalGridW &&
          touchY >= gridStartY && touchY < gridStartY + cellH * VISIBLE_ROWS) {
        int c = (touchX - (startX + labelColW)) / soundColW;
        int vr = (touchY - gridStartY) / cellH;
        int r = scrollRow + vr;
        if (c >= 0 && c < kanji_data::KANA_GRID_COLS && r >= 0 && r < kanji_data::KANA_GRID_ROWS) {
          if (kanji_data::KANA_GRID[r][c].hira != nullptr) {
            tableRow = r;
            tableCol = c;
            requestUpdate();
            return;
          }
        }
      }
    }

    // Physical button navigation - strict 1-cell D-Pad movement
    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      int c = tableCol;
      do {
        c = (c == 0) ? kanji_data::KANA_GRID_COLS - 1 : c - 1;
      } while (kanji_data::KANA_GRID[tableRow][c].hira == nullptr && c != tableCol);
      tableCol = c;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      int c = tableCol;
      do {
        c = (c + 1) % kanji_data::KANA_GRID_COLS;
      } while (kanji_data::KANA_GRID[tableRow][c].hira == nullptr && c != tableCol);
      tableCol = c;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      int r = tableRow;
      do {
        r = (r == 0) ? kanji_data::KANA_GRID_ROWS - 1 : r - 1;
      } while (kanji_data::KANA_GRID[r][tableCol].hira == nullptr && r != tableRow);
      tableRow = r;
      if (tableRow < scrollRow) {
        scrollRow = tableRow;
      } else if (tableRow >= scrollRow + VISIBLE_ROWS) {
        scrollRow = tableRow - VISIBLE_ROWS + 1;
      }
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      int r = tableRow;
      do {
        r = (r + 1) % kanji_data::KANA_GRID_ROWS;
      } while (kanji_data::KANA_GRID[r][tableCol].hira == nullptr && r != tableRow);
      tableRow = r;
      if (tableRow < scrollRow) {
        scrollRow = tableRow;
      } else if (tableRow >= scrollRow + VISIBLE_ROWS) {
        scrollRow = tableRow - VISIBLE_ROWS + 1;
      }
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      // Toggle Hiragana <-> Katakana
      isTableHiragana = !isTableHiragana;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      mode = ViewMode::MENU;
      needsCleanRefresh = true;
      requestUpdate();
      return;
    }
    return;
  }

  // =========================================================================
  // FLASHCARDS (HIRAGANA / KATAKANA / KANJI N5)
  // =========================================================================
  if (mode == ViewMode::HIRAGANA || mode == ViewMode::KATAKANA || mode == ViewMode::KANJI_N5) {
    int touchX = 0, touchY = 0;
    if (mappedInput.wasScreenTapped(touchX, touchY)) {
      if (touchY < 80) {
        toggleShuffle();
        return;
      }
      flipCard();
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
      prevCard();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
        mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
      nextCard();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      toggleShuffle();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      flipCard();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      mode = ViewMode::MENU;
      needsCleanRefresh = true;
      requestUpdate();
      return;
    }
    return;
  }

  // =========================================================================
  // QUIZ (KANA / KANJI)
  // =========================================================================
  if (mode == ViewMode::QUIZ_KANA || mode == ViewMode::QUIZ_KANJI) {
    int touchedOpt = -1;
    const auto optTouch = mappedInput.rowTouch(touchedOpt, 210, 55, 4);
    if (optTouch != MappedInputManager::RowTouch::None) {
      if (optTouch == MappedInputManager::RowTouch::Down) {
        if (currentQuiz.selectedIndex != touchedOpt) {
          currentQuiz.selectedIndex = touchedOpt;
          requestUpdate();
        }
      } else {
        currentQuiz.selectedIndex = touchedOpt;
        if (currentQuiz.answered) {
          if (mode == ViewMode::QUIZ_KANA) {
            generateKanaQuizQuestion();
          } else {
            generateKanjiQuizQuestion();
          }
          requestUpdate();
        } else {
          checkQuizAnswer(currentQuiz.selectedIndex);
        }
        return;
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Down) ||
        mappedInput.wasReleased(MappedInputManager::Button::Right) ||
        mappedInput.wasReleased(MappedInputManager::Button::PageForward)) {
      currentQuiz.selectedIndex = (currentQuiz.selectedIndex + 1) % 4;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Up) ||
        mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::PageBack)) {
      currentQuiz.selectedIndex = (currentQuiz.selectedIndex == 0) ? 3 : currentQuiz.selectedIndex - 1;
      requestUpdate();
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (currentQuiz.answered) {
        if (mode == ViewMode::QUIZ_KANA) {
          generateKanaQuizQuestion();
        } else {
          generateKanjiQuizQuestion();
        }
        requestUpdate();
      } else {
        checkQuizAnswer(currentQuiz.selectedIndex);
      }
      return;
    }
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      mode = ViewMode::MENU;
      needsCleanRefresh = true;
      requestUpdate();
      return;
    }
  }
}

void KanjiActivity::drawToriiHeader(int x, int y, int width) {
  int midX = x + width / 2;
  // Kasagi (top curved roof)
  renderer.fillRect(midX - 44, y, 88, 3);
  renderer.fillRect(midX - 48, y + 1, 96, 2);
  // Shimaki & Nuki
  renderer.fillRect(midX - 36, y + 6, 72, 3);
  // Hashira (pillars)
  renderer.fillRect(midX - 24, y + 4, 4, 18);
  renderer.fillRect(midX + 20, y + 4, 4, 18);
}

void KanjiActivity::drawCardBorder(int x, int y, int width, int height, bool fillWhite) {
  if (fillWhite) {
    renderer.fillRect(x, y, width, height, false);
  }
  renderer.drawRect(x, y, width, height, true);
  renderer.drawRect(x + 3, y + 3, width - 6, height - 6, true);
  renderer.fillRectDither(x + 4, y + height, width, 4, Color::LightGray);
  renderer.fillRectDither(x + width, y + 4, 4, height, Color::LightGray);
}

void KanjiActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = UITheme::getInstance().getContentArea(renderer);
  const int screenW = renderer.getScreenWidth();

  // Header Title
  const char* title = (mode == ViewMode::MENU)         ? tr(STR_KANJI_TITLE)
                      : (mode == ViewMode::KANA_TABLE) ? (isTableHiragana ? tr(STR_KANJI_TABLE_HIRA) : tr(STR_KANJI_TABLE_KATA))
                      : (mode == ViewMode::HIRAGANA)   ? tr(STR_KANJI_HIRAGANA)
                      : (mode == ViewMode::KATAKANA)   ? tr(STR_KANJI_KATAKANA)
                      : (mode == ViewMode::KANJI_N5)   ? tr(STR_KANJI_N5)
                      : (mode == ViewMode::QUIZ_KANA)  ? tr(STR_KANJI_QUIZ_KANA)
                                                       : tr(STR_KANJI_QUIZ_KANJI);

  if (mode == ViewMode::KANA_TABLE) {
    // Battery and status on right; omit title in drawHeader to prevent fui::header underline from intersecting text
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.batteryBarHeight}, nullptr, nullptr, false);
    // Elevated title directly at battery indicator level (comfortably below bezel)
    renderer.drawText(UI_12_FONT_ID, 16, metrics.topPadding + 2, title, true, EpdFontFamily::BOLD);
    // Clean horizontal rule cleanly underneath the title
    int lineY = metrics.topPadding + 32;
    renderer.drawLine(0, lineY, screenW, lineY);
  } else {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.headerHeight}, title, nullptr, true);
  }

  if (mode == ViewMode::MENU) {
    renderMenu(content);
  } else if (mode == ViewMode::KANA_TABLE) {
    renderKanaTable(content);
  } else if (mode == ViewMode::HIRAGANA) {
    renderKanaCard(content, true);
  } else if (mode == ViewMode::KATAKANA) {
    renderKanaCard(content, false);
  } else if (mode == ViewMode::KANJI_N5) {
    renderKanjiCard(content);
  } else if (mode == ViewMode::QUIZ_KANA) {
    renderQuiz(content, true);
  } else if (mode == ViewMode::QUIZ_KANJI) {
    renderQuiz(content, false);
  }

  // Button hints
  if (mode == ViewMode::MENU) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (mode == ViewMode::KANA_TABLE) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), isTableHiragana ? tr(STR_KANJI_KATAKANA) : tr(STR_KANJI_HIRAGANA), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (mode == ViewMode::QUIZ_KANA || mode == ViewMode::QUIZ_KANJI) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), currentQuiz.answered ? tr(STR_KANJI_NEXT) : tr(STR_SELECT),
                                              tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else {
    // Flashcard hints: "Ответ" is concise and will never wrap!
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_KANJI_REVEAL), tr(STR_DIR_LEFT), tr(STR_DIR_RIGHT));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  if (needsCleanRefresh) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    needsCleanRefresh = false;
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

void KanjiActivity::renderMenu(const Rect& content) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int startY = metrics.headerHeight + 20;

  drawToriiHeader(content.x, startY, content.width);
  startY += 26;

  const char* titles[] = {
      tr(STR_KANJI_MENU_TABLE),
      tr(STR_KANJI_MENU_HIRA),
      tr(STR_KANJI_MENU_KATA),
      tr(STR_KANJI_MENU_N5),
      tr(STR_KANJI_MENU_QUIZ_KANA),
      tr(STR_KANJI_MENU_QUIZ_KANJI),
  };
  const char* subtitles[] = {
      tr(STR_KANJI_MENU_TABLE_SUB),
      tr(STR_KANJI_MENU_HIRA_SUB),
      tr(STR_KANJI_MENU_KATA_SUB),
      tr(STR_KANJI_MENU_N5_SUB),
      tr(STR_KANJI_MENU_QUIZ_KANA_SUB),
      tr(STR_KANJI_MENU_QUIZ_KANJI_SUB),
  };

  int tileHeight = 76;
  int spacing = 8;

  for (int i = 0; i < 6; ++i) {
    int y = startY + i * (tileHeight + spacing);
    int x = content.x + 10;
    int w = content.width - 20;

    bool selected = (menuIndex == i);

    if (selected) {
      renderer.fillRoundedRect(x, y, w, tileHeight, 8, Color::LightGray);
      renderer.drawRect(x, y, w, tileHeight, true);
      renderer.drawRect(x + 1, y + 1, w - 2, tileHeight - 2, true);
    } else {
      renderer.drawRect(x, y, w, tileHeight, true);
      renderer.fillRectDither(x + 2, y + tileHeight, w, 2, Color::LightGray);
    }

    renderer.drawText(UI_12_FONT_ID, x + 14, y + 10, titles[i], true, EpdFontFamily::BOLD);
    const auto subLines = renderer.wrappedText(UI_10_FONT_ID, subtitles[i], w - 28, 2);
    for (size_t li = 0; li < subLines.size(); ++li) {
      renderer.drawText(UI_10_FONT_ID, x + 14, y + 34 + static_cast<int>(li) * 18, subLines[li].c_str(), true);
    }
  }
}

// ===========================================================================
// GOJŪON SYLLABARY TABLE + DAKUTEN / HANDAKUTEN (16 Rows x 5 Cols)
// ===========================================================================
void KanjiActivity::renderKanaTable(const Rect& /*content*/) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int topY = metrics.topPadding + 36;
  int bottomLimit = renderer.getScreenHeight() - metrics.buttonHintsHeight - 4;
  int detailH = 76;
  int headerH = 22;
  constexpr int VISIBLE_ROWS = 8;
  int labelColW = 46;
  int soundColW = 76;
  int totalGridW = labelColW + 5 * soundColW;
  int startX = (renderer.getScreenWidth() - totalGridW) / 2;

  // 1. DETAIL PREVIEW PANEL AT TOP
  renderer.fillRoundedRect(startX, topY, totalGridW, detailH, 6, Color::White);
  renderer.drawRect(startX, topY, totalGridW, detailH, true);
  renderer.drawRect(startX + 2, topY + 2, totalGridW - 4, detailH - 4, true);

  const auto& cell = kanji_data::KANA_GRID[tableRow][tableCol];
  const char* curGlyph = isTableHiragana ? cell.hira : cell.kata;
  const char* curOrigin = isTableHiragana ? cell.originHira : cell.originKata;

  if (curGlyph != nullptr) {
    // Sharp glyph box
    int glyphBoxSize = 68;
    int boxX = startX + 6;
    int boxY = topY + 4;
    renderer.fillRectDither(boxX, boxY, glyphBoxSize, glyphBoxSize, Color::LightGray);
    renderer.drawRect(boxX, boxY, glyphBoxSize, glyphBoxSize, true);

    // Native glyph centered inside the box, guaranteed never to overflow
    renderer.setClipRect(boxX + 2, boxY + 2, glyphBoxSize - 4, glyphBoxSize - 4);
    const unsigned char* uptr = reinterpret_cast<const unsigned char*>(curGlyph);
    uint32_t cp = utf8NextCodepoint(&uptr);
    int32_t advFP = 0;
    int topBearing = 0;
    if (renderer.getCodepointMetrics(KANJI_36_FONT_ID, cp, EpdFontFamily::REGULAR, advFP, topBearing)) {
      int glyphW = fp4::toPixel(advFP);
      int maxDim = std::max(glyphW, topBearing);
      int scale256 = 256;
      if (maxDim > 52) {
        scale256 = (52 * 256) / maxDim;
      }
      int scaledW = (glyphW * scale256) / 256;
      int scaledH = (topBearing * scale256) / 256;
      int gx = boxX + (glyphBoxSize - scaledW) / 2;
      int baselineY = boxY + (glyphBoxSize + scaledH) / 2 - 2;
      renderer.drawScaledCodepoint(KANJI_36_FONT_ID, cp, EpdFontFamily::REGULAR, gx, baselineY, scale256);
    } else {
      int glyphW = renderer.getTextWidth(KANJI_36_FONT_ID, curGlyph);
      renderer.drawText(KANJI_36_FONT_ID, boxX + (glyphBoxSize - glyphW) / 2, boxY - 18, curGlyph, true);
    }
    renderer.setClipRect(0, 0, renderer.getScreenWidth(), renderer.getScreenHeight());

    // Detail text on the right
    int infoX = boxX + glyphBoxSize + 12;
    char romajiLine[48];
    snprintf(romajiLine, sizeof(romajiLine), "%s:  %s", tr(STR_KANJI_ROMAJI), cell.romaji);
    renderer.drawText(UI_10_FONT_ID, infoX, boxY + 4, romajiLine, true, EpdFontFamily::BOLD);

    char polyLine[48];
    snprintf(polyLine, sizeof(polyLine), "%s:  %s", tr(STR_KANJI_CYRILLIC), cell.cyrillic);
    renderer.drawText(UI_10_FONT_ID, infoX, boxY + 24, polyLine, true, EpdFontFamily::BOLD);

    char descLine[64];
    if (tableRow < 11) {
      snprintf(descLine, sizeof(descLine), tr(STR_KANJI_ORIGIN), curOrigin);
    } else if (tableRow == 15) {
      snprintf(descLine, sizeof(descLine), "%s", tr(STR_KANJI_HANDAKUTEN));
    } else {
      snprintf(descLine, sizeof(descLine), "%s", tr(STR_KANJI_DAKUTEN));
    }
    renderer.drawText(UI_10_FONT_ID, infoX, boxY + 44, descLine, true);
  }

  // 2. KANA MATRIX WITH SCROLLING
  int gridHeaderY = topY + detailH - 2;
  int gridStartY = gridHeaderY + headerH;
  int tableH = bottomLimit - gridStartY;
  int cellH = tableH / VISIBLE_ROWS;

  // Column header labels: Ряд / Row / 行
  renderer.drawText(UI_10_FONT_ID, startX - 2, gridHeaderY, tr(STR_KANJI_ROW), true, EpdFontFamily::BOLD);
  const char* colHeaders[] = {"- a -", "- i -", "- u -", "- e -", "- o -"};
  for (int c = 0; c < kanji_data::KANA_GRID_COLS; ++c) {
    int cx = startX + labelColW + c * soundColW;
    int hw = renderer.getTextWidth(UI_10_FONT_ID, colHeaders[c]);
    renderer.drawText(UI_10_FONT_ID, cx + (soundColW - hw) / 2, gridHeaderY, colHeaders[c], true,
                      EpdFontFamily::BOLD);
  }

  // Ensure scrollRow is valid
  if (scrollRow < 0) scrollRow = 0;
  if (scrollRow > kanji_data::KANA_GRID_ROWS - VISIBLE_ROWS) {
    scrollRow = kanji_data::KANA_GRID_ROWS - VISIBLE_ROWS;
  }

  for (int vr = 0; vr < VISIBLE_ROWS; ++vr) {
    int r = scrollRow + vr;
    if (r >= kanji_data::KANA_GRID_ROWS) break;
    int cy = gridStartY + vr * cellH;

    // Row label cell on the left
    renderer.drawRect(startX, cy + 1, labelColW, cellH - 2, true);
    auto currentLang = I18n::getInstance().getLanguage();
    const char* rowStr = (currentLang == Language::RU) ? kanji_data::KANA_ROW_LABELS_RU[r]
                       : (currentLang == Language::JA) ? kanji_data::KANA_ROW_LABELS_JA[r]
                                                       : kanji_data::KANA_ROW_LABELS_EN[r];
    if (rowStr[0] != '\0') {
      int rw = renderer.getTextWidth(UI_10_FONT_ID, rowStr);
      renderer.drawText(UI_10_FONT_ID, startX + (labelColW - rw) / 2, cy + (cellH - 18) / 2,
                        rowStr, true, EpdFontFamily::BOLD);
    }

    for (int c = 0; c < kanji_data::KANA_GRID_COLS; ++c) {
      int cx = startX + labelColW + c * soundColW;
      const auto& item = kanji_data::KANA_GRID[r][c];
      const char* glyph = isTableHiragana ? item.hira : item.kata;
      bool isSelected = (r == tableRow && c == tableCol);

      if (isSelected) {
        renderer.fillRoundedRect(cx + 2, cy + 2, soundColW - 4, cellH - 4, 4, Color::LightGray);
        renderer.drawRect(cx + 1, cy + 1, soundColW - 2, cellH - 2, true);
        renderer.drawRect(cx + 2, cy + 2, soundColW - 4, cellH - 4, true);
      } else {
        renderer.drawRect(cx + 1, cy + 1, soundColW - 2, cellH - 2, true);
      }

      if (glyph != nullptr) {
        int textW = renderer.getTextWidth(UI_12_FONT_ID, glyph, EpdFontFamily::BOLD);
        renderer.drawText(UI_12_FONT_ID, cx + (soundColW - textW) / 2, cy + (cellH - 20) / 2, glyph, true,
                          EpdFontFamily::BOLD);
      } else {
        // Empty cell
        int dw = renderer.getTextWidth(UI_10_FONT_ID, "·");
        renderer.drawText(UI_10_FONT_ID, cx + (soundColW - dw) / 2, cy + (cellH - 18) / 2, "·", true);
      }
    }
  }

  // 3. SCROLLBAR TRACK ON THE RIGHT
  int trackX = startX + totalGridW + 4;
  int trackW = 4;
  int trackY = gridStartY;
  int trackH = VISIBLE_ROWS * cellH;
  int thumbH = (VISIBLE_ROWS * trackH) / kanji_data::KANA_GRID_ROWS;
  int maxScroll = kanji_data::KANA_GRID_ROWS - VISIBLE_ROWS;
  int thumbY = trackY + (maxScroll > 0 ? (scrollRow * (trackH - thumbH)) / maxScroll : 0);

  renderer.drawRect(trackX, trackY, trackW, trackH, true);
  renderer.fillRect(trackX, thumbY, trackW, thumbH, true);
}

// ===========================================================================
// FLASHCARDS: KANA
// ===========================================================================
void KanjiActivity::renderKanaCard(const Rect& content, bool isHiragana) {
  size_t actualIdx = (cardIndex < cardOrder.size()) ? cardOrder[cardIndex] : cardIndex;
  const auto& item = isHiragana ? kanji_data::HIRAGANA_SEION[actualIdx] : kanji_data::KATAKANA_SEION[actualIdx];
  size_t total = isHiragana ? kanji_data::HIRAGANA_COUNT : kanji_data::KATAKANA_COUNT;

  int cardX = content.x + 15;
  int cardY = content.y + 10;
  int cardW = content.width - 30;
  int cardH = content.height - 70;

  if (flipAnimStep == 1) {
    int compressH = cardH / 4;
    int centerY = cardY + cardH / 2;
    drawCardBorder(cardX, centerY - compressH / 2, cardW, compressH, true);
    return;
  }

  drawCardBorder(cardX, cardY, cardW, cardH, true);

  // Counter
  char counterBuf[32];
  snprintf(counterBuf, sizeof(counterBuf), "[ %u / %u ]", static_cast<unsigned>(cardIndex + 1),
           static_cast<unsigned>(total));
  renderer.drawText(UI_10_FONT_ID, cardX + 16, cardY + 16, counterBuf, true);

  // Syllabary tag & shuffle mode indicator
  char tagBuf[64];
  const char* kanaName = isHiragana ? tr(STR_KANJI_HIRAGANA) : tr(STR_KANJI_KATAKANA);
  const char* orderName = shuffleMode ? tr(STR_KANJI_ORDER_RANDOM) : tr(STR_KANJI_ORDER_SEQ);
  snprintf(tagBuf, sizeof(tagBuf), "%s · %s", kanaName, orderName);
  int tagW = renderer.getTextWidth(UI_10_FONT_ID, tagBuf);
  renderer.drawText(UI_10_FONT_ID, cardX + cardW - 16 - tagW, cardY + 16, tagBuf, true, EpdFontFamily::BOLD);

  renderer.drawLine(cardX + 12, cardY + 42, cardX + cardW - 12, cardY + 42);

  // Render crystal sharp 36pt native font!
  int glyphY = cardY + 75;
  int glyphW = renderer.getTextWidth(KANJI_36_FONT_ID, item.glyph);
  renderer.drawText(KANJI_36_FONT_ID, cardX + (cardW - glyphW) / 2, glyphY, item.glyph, true);

  renderer.drawLine(cardX + 24, cardY + 180, cardX + cardW - 24, cardY + 180);

  if (!isCardFlipped) {
    renderer.drawCenteredText(UI_12_FONT_ID, cardY + 225, tr(STR_KANJI_PROMPT_KANA), true);
    renderer.drawCenteredText(UI_10_FONT_ID, cardY + 265, tr(STR_KANJI_HINT_KANA), true);
  } else {
    char romajiBuf[48];
    snprintf(romajiBuf, sizeof(romajiBuf), "%s:  %s", tr(STR_KANJI_ROMAJI), item.romaji);
    renderer.drawText(UI_12_FONT_ID, cardX + 32, cardY + 215, romajiBuf, true, EpdFontFamily::BOLD);

    char readingBuf[48];
    snprintf(readingBuf, sizeof(readingBuf), "%s:  %s", tr(STR_KANJI_CYRILLIC), item.cyrillic);
    renderer.drawText(UI_12_FONT_ID, cardX + 32, cardY + 250, readingBuf, true, EpdFontFamily::BOLD);

    char mnemonicBuf[64];
    snprintf(mnemonicBuf, sizeof(mnemonicBuf), tr(STR_KANJI_ORIGIN), item.mnemonic);
    renderer.drawText(UI_10_FONT_ID, cardX + 32, cardY + 285, mnemonicBuf, true);
  }
}

// ===========================================================================
// FLASHCARDS: KANJI JLPT N5
// ===========================================================================
void KanjiActivity::renderKanjiCard(const Rect& content) {
  size_t actualIdx = (cardIndex < cardOrder.size()) ? cardOrder[cardIndex] : cardIndex;
  const auto& item = kanji_data::JLPT_N5_KANJI[actualIdx];

  int cardX = content.x + 15;
  int cardY = content.y + 10;
  int cardW = content.width - 30;
  int cardH = content.height - 65;

  if (flipAnimStep == 1) {
    int compressH = cardH / 4;
    int centerY = cardY + cardH / 2;
    drawCardBorder(cardX, centerY - compressH / 2, cardW, compressH, true);
    return;
  }

  drawCardBorder(cardX, cardY, cardW, cardH, true);

  char counterBuf[32];
  snprintf(counterBuf, sizeof(counterBuf), "[ %u / %u ]", static_cast<unsigned>(cardIndex + 1),
           static_cast<unsigned>(kanji_data::JLPT_N5_COUNT));
  renderer.drawText(UI_10_FONT_ID, cardX + 16, cardY + 16, counterBuf, true);

  char strokeBuf[48];
  char strBuf[16];
  snprintf(strBuf, sizeof(strBuf), tr(STR_KANJI_STROKES), static_cast<unsigned>(item.strokes));
  const char* kanjiOrderName = shuffleMode ? tr(STR_KANJI_ORDER_RANDOM) : tr(STR_KANJI_ORDER_SEQ);
  snprintf(strokeBuf, sizeof(strokeBuf), "%s · %s", strBuf, kanjiOrderName);
  int strokeW = renderer.getTextWidth(UI_10_FONT_ID, strokeBuf);
  renderer.drawText(UI_10_FONT_ID, cardX + cardW - 16 - strokeW, cardY + 16, strokeBuf, true, EpdFontFamily::BOLD);

  renderer.drawLine(cardX + 12, cardY + 40, cardX + cardW - 12, cardY + 40);

  // Render crystal sharp 36pt native font!
  int glyphY = cardY + 70;
  int glyphW = renderer.getTextWidth(KANJI_36_FONT_ID, item.glyph);
  renderer.drawText(KANJI_36_FONT_ID, cardX + (cardW - glyphW) / 2, glyphY, item.glyph, true);

  renderer.drawLine(cardX + 16, cardY + 175, cardX + cardW - 16, cardY + 175);

  if (!isCardFlipped) {
    renderer.drawCenteredText(UI_12_FONT_ID, cardY + 215, tr(STR_KANJI_PROMPT_KANJI), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, cardY + 250, tr(STR_KANJI_HINT_KANJI), true);
  } else {
    int infoY = cardY + 190;

    std::string onStr = std::string(tr(STR_KANJI_ONYOMI)) + item.onyomi;
    renderer.drawText(UI_10_FONT_ID, cardX + 20, infoY, onStr.c_str(), true, EpdFontFamily::BOLD);
    infoY += 24;

    std::string kunStr = std::string(tr(STR_KANJI_KUNYOMI)) + (item.kunyomi[0] ? item.kunyomi : "—");
    renderer.drawText(UI_10_FONT_ID, cardX + 20, infoY, kunStr.c_str(), true);
    infoY += 28;

    auto lang = I18n::getInstance().getLanguage();
    const char* meaning = (lang == Language::EN && item.meaningEn && item.meaningEn[0]) ? item.meaningEn : item.meaningRu;
    std::string meanStr = std::string(tr(STR_KANJI_MEANING)) + meaning;
    renderer.drawText(UI_12_FONT_ID, cardX + 20, infoY, meanStr.c_str(), true, EpdFontFamily::BOLD);
    infoY += 32;

    renderer.drawRect(cardX + 16, infoY, cardW - 32, 74, true);

    renderer.drawText(UI_10_FONT_ID, cardX + 24, infoY + 8, tr(STR_KANJI_EXAMPLES), true, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, cardX + 24, infoY + 28, item.example1, true);
    renderer.drawText(UI_10_FONT_ID, cardX + 24, infoY + 48, item.example2, true);
  }
}

// ===========================================================================
// QUIZ (KANA / KANJI)
// ===========================================================================
void KanjiActivity::renderQuiz(const Rect& content, bool isKanaQuiz) {
  int cardX = content.x + 15;
  int cardY = content.y + 5;
  int cardW = content.width - 30;
  int cardH = content.height - 65;

  drawCardBorder(cardX, cardY, cardW, cardH, true);

  // Score
  char scoreBuf[48];
  snprintf(scoreBuf, sizeof(scoreBuf), tr(STR_KANJI_SCORE), quizScore, quizTotal);
  renderer.drawText(UI_10_FONT_ID, cardX + 16, cardY + 14, scoreBuf, true, EpdFontFamily::BOLD);

  const char* quizTag = isKanaQuiz ? tr(STR_KANJI_QUIZ_KANA) : tr(STR_KANJI_QUIZ_KANJI);
  int tagW = renderer.getTextWidth(UI_10_FONT_ID, quizTag);
  renderer.drawText(UI_10_FONT_ID, cardX + cardW - 16 - tagW, cardY + 14, quizTag, true);

  renderer.drawLine(cardX + 12, cardY + 36, cardX + cardW - 12, cardY + 36);

  // Prompt Character - Crisp 36pt font!
  int glyphY = cardY + 55;
  int glyphW = renderer.getTextWidth(KANJI_36_FONT_ID, currentQuiz.prompt);
  renderer.drawText(KANJI_36_FONT_ID, cardX + (cardW - glyphW) / 2, glyphY, currentQuiz.prompt, true);

  renderer.drawLine(cardX + 16, cardY + 150, cardX + cardW - 16, cardY + 150);

  // 4 Option Buttons
  int optStartY = cardY + 165;
  int btnH = 46;
  int btnGap = 8;

  for (int i = 0; i < 4; ++i) {
    int y = optStartY + i * (btnH + btnGap);
    int x = cardX + 16;
    int w = cardW - 32;

    bool isSelected = (currentQuiz.selectedIndex == i);

    if (currentQuiz.answered) {
      if (i == currentQuiz.correctIndex) {
        renderer.fillRoundedRect(x, y, w, btnH, 6, Color::LightGray);
        renderer.drawRect(x, y, w, btnH, true);
        // Draw crisp vector checkmark badge
        renderer.drawLine(x + 10, y + 24, x + 16, y + 30);
        renderer.drawLine(x + 10, y + 25, x + 16, y + 31);
        renderer.drawLine(x + 16, y + 30, x + 25, y + 17);
        renderer.drawLine(x + 16, y + 31, x + 25, y + 18);
      } else if (isSelected) {
        renderer.drawRect(x, y, w, btnH, true);
        // Draw crisp vector cross
        renderer.drawLine(x + 12, y + 18, x + 22, y + 28);
        renderer.drawLine(x + 12, y + 19, x + 22, y + 29);
        renderer.drawLine(x + 22, y + 18, x + 12, y + 28);
        renderer.drawLine(x + 22, y + 19, x + 12, y + 29);
      } else {
        renderer.drawRect(x, y, w, btnH, true);
      }
    } else {
      if (isSelected) {
        renderer.fillRoundedRect(x, y, w, btnH, 6, Color::LightGray);
      }
      renderer.drawRect(x, y, w, btnH, true);
    }

    renderer.drawText(UI_10_FONT_ID, x + 34, y + 15, currentQuiz.options[i].c_str(), true,
                      (isSelected || (currentQuiz.answered && i == currentQuiz.correctIndex)) ? EpdFontFamily::BOLD
                                                                                             : EpdFontFamily::REGULAR);
  }
}
