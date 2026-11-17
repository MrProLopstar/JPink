#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

struct AnkiCard {
  std::string front;
  std::string back;
  std::string reading;  // Optional furigana / kana reading
  std::string extra;    // Optional example sentence

  // SM-2 Spaced Repetition fields
  uint16_t reps = 0;
  uint16_t intervalDays = 0;
  uint16_t easeFactor = 250;  // 250 = 2.50
  uint32_t dueTimestamp = 0;
  uint8_t lapses = 0;
};

struct AnkiDeck {
  std::string name;
  std::string filepath;
  std::vector<AnkiCard> cards;
  int dueCount = 0;
};

class AnkiActivity final : public Activity {
 public:
  enum class State {
    DECK_LIST,
    CARD_FRONT,
    CARD_BACK,
    SESSION_COMPLETE
  };

 private:
  State state = State::DECK_LIST;
  bool needsCleanRefresh = true;
  std::vector<AnkiDeck> decks;
  int selectedDeckIndex = 0;
  size_t currentCardIndex = 0;
  std::vector<size_t> studySessionIndices;  // Cards queued for current session
  size_t sessionProgress = 0;

  // Rating selection
  int selectedRating = 2;  // default to Good (0: Again, 1: Hard, 2: Good, 3: Easy)

  // Animation state (0: idle, 1: card shrinking to horizontal line, 2: expanded back)
  uint8_t flipAnimStep = 0;

  // Statistics
  int reviewedInSession = 0;
  int correctInSession = 0;

  ButtonNavigator buttonNavigator;

  void loadDecks();
  void initBuiltinDeck();
  void parseDeckFile(const std::string& path, AnkiDeck& deck);
  void saveDeckProgress(const AnkiDeck& deck);
  void loadDeckProgress(AnkiDeck& deck);

  void startDeckSession(int deckIdx);
  void applyRating(int rating);  // 0: Again, 1: Hard, 2: Good, 3: Easy

  void renderDeckList(const Rect& content);
  void renderCardFront(const Rect& content);
  void renderCardBack(const Rect& content);
  void renderSessionComplete(const Rect& content);

  void drawCardFrame(int x, int y, int width, int height, bool fillWhite = true);
  void drawProgressBar(int x, int y, int width, int height, float progress);
  void drawCelebrationBadge(int cx, int cy);

 public:
  explicit AnkiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~AnkiActivity() override = default;

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
