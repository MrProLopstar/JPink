#pragma once

#include <string>
#include <vector>

#include "KanjiData.h"
#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

class KanjiActivity final : public Activity {
 public:
  enum class ViewMode {
    MENU,        // Main selector (6 options)
    KANA_TABLE,  // Interactive Gojūon 5x10 Syllabary Table
    HIRAGANA,    // Hiragana cards
    KATAKANA,    // Katakana cards
    KANJI_N5,    // N5 Kanji cards
    QUIZ_KANA,   // Dedicated Kana Quiz (Hiragana / Katakana)
    QUIZ_KANJI   // Dedicated Kanji Quiz (JLPT N5)
  };

 private:
  ViewMode mode = ViewMode::MENU;
  bool needsCleanRefresh = true;
  int menuIndex = 0;
  size_t cardIndex = 0;
  bool isCardFlipped = false;
  bool shuffleMode = true;
  std::vector<size_t> cardOrder;
  ButtonNavigator buttonNavigator;

  // Kana Table state
  int tableRow = 0;
  int tableCol = 0;
  int scrollRow = 0;
  bool isTableHiragana = true;

  // Animation state for finite e-ink flip (0 = idle, 1 = mid-flip shrink, 2 = flipped)
  uint8_t flipAnimStep = 0;

  // Quiz state
  struct QuizQuestion {
    const char* prompt;
    std::string options[4];
    int correctIndex = 0;
    int selectedIndex = 0;
    bool answered = false;
  };
  QuizQuestion currentQuiz;
  int quizScore = 0;
  int quizTotal = 0;

  void nextCard();
  void prevCard();
  void flipCard();
  void initCardDeck(size_t count);
  void toggleShuffle();
  void generateKanaQuizQuestion();
  void generateKanjiQuizQuestion();
  void checkQuizAnswer(int option);

  void renderMenu(const Rect& content);
  void renderKanaCard(const Rect& content, bool isHiragana);
  void renderKanjiCard(const Rect& content);
  void renderKanaTable(const Rect& content);
  void renderQuiz(const Rect& content, bool isKanaQuiz);

  void drawToriiHeader(int x, int y, int width);
  void drawCardBorder(int x, int y, int width, int height, bool fillWhite = true);

 public:
  explicit KanjiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~KanjiActivity() override = default;

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
