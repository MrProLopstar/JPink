#pragma once

#include <vector>
#include <string>
#include <ctime>

#include "activities/Activity.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

struct NewsSource {
  std::string name;
  std::string url;
  bool enabled = true;
};

struct NewsArticle {
  std::string filename;
  std::string title;
  std::string sourceName;
  std::string pubDate;
  std::string link;
  std::string body;
  uint32_t timestamp = 0;
};

class NewsActivity : public Activity {
 public:
  enum class ViewMode {
    ARTICLES_LIST,
    ARTICLE_VIEW,
    SOURCES_LIST,
  };

  NewsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~NewsActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ViewMode mode = ViewMode::ARTICLES_LIST;
  bool needsCleanRefresh = true;
  ButtonNavigator buttonNavigator;

  std::vector<NewsSource> sources;
  std::vector<NewsArticle> articles;

  int selectedArticleIndex = 0;
  int selectedSourceIndex = 0;

  // Article reading pagination
  int currentArticlePage = 0;
  std::vector<std::string> articlePages;

  // Status notification banner
  std::string statusMessage;
  unsigned long statusMessageUntil = 0;

  // Periodic network sync (1 hour interval)
  unsigned long lastSyncMillis = 0;
  bool isSyncing = false;
  bool pendingRefresh = false;

  void setStatus(const std::string& msg, uint32_t durationMs = 4000);

  // Storage and configuration
  void loadSources();
  void saveSources();
  void loadCachedArticles();
  void checkAndPruneCache();

  // Network and RSS parsing
  void launchWifiSelection(bool autoRefreshAfter = true);
  void onWifiSelectionComplete(bool connected, bool autoRefreshAfter);
  void refreshAllFeeds(bool manual);
  bool fetchFeed(const NewsSource& source);
  void parseRssFeedFile(const std::string& filePath, const std::string& sourceName);

  // View pagination and text layout
  void prepareArticlePages(const NewsArticle& article);

  // Rendering
  void renderArticlesList(const Rect& content);
  void renderArticleView(const Rect& content);
  void renderSourcesList(const Rect& content);
  void drawStatusBanner(int x, int y, int w);
};
