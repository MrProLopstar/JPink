#include "NewsActivity.h"

#include <FsHelpers.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "I18nKeys.h"
#include "WifiCredentialStore.h"
#include "activities/RenderLock.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

namespace {

constexpr uint64_t MAX_CACHE_BYTES = 512ULL * 1024ULL * 1024ULL;  // 512 MB ceiling
constexpr uint64_t PRUNE_TARGET_BYTES = 400ULL * 1024ULL * 1024ULL;

std::string cleanHtml(const std::string& input) {
  std::string out;
  out.reserve(input.size());
  bool inTag = false;
  std::string currentTag;

  for (size_t i = 0; i < input.size(); ++i) {
    char c = input[i];
    if (c == '<') {
      inTag = true;
      currentTag.clear();
      continue;
    }
    if (inTag) {
      if (c == '>') {
        inTag = false;
        std::string tagLower = currentTag;
        std::transform(tagLower.begin(), tagLower.end(), tagLower.begin(), ::tolower);
        if (tagLower == "br" || tagLower == "br/" || tagLower == "br /" ||
            tagLower == "/p" || tagLower == "/div" || tagLower == "/li" || tagLower == "/h1" ||
            tagLower == "/h2" || tagLower == "/h3" || tagLower == "/h4") {
          out += '\n';
          if (tagLower != "br" && tagLower != "br/") {
            out += '\n';
          }
        }
      } else {
        currentTag += c;
      }
      continue;
    }

    // HTML Entities
    if (c == '&') {
      size_t semi = input.find(';', i);
      if (semi != std::string::npos && semi - i < 12) {
        std::string entity = input.substr(i + 1, semi - i - 1);
        if (entity == "quot") out += '"';
        else if (entity == "amp") out += '&';
        else if (entity == "lt") out += '<';
        else if (entity == "gt") out += '>';
        else if (entity == "apos" || entity == "#39") out += '\'';
        else if (entity == "laquo") out += "«";
        else if (entity == "raquo") out += "»";
        else if (entity == "mdash" || entity == "#8212;") out += "—";
        else if (entity == "ndash" || entity == "#8211;") out += "–";
        else if (entity == "nbsp") out += ' ';
        else if (entity == "hellip" || entity == "#8230;") out += "...";
        else out += ' ';
        i = semi;
        continue;
      }
    }

    out += c;
  }

  // Deduplicate excess blank lines
  std::string cleaned;
  cleaned.reserve(out.size());
  int consecutiveNewlines = 0;
  for (char ch : out) {
    if (ch == '\r') continue;
    if (ch == '\n') {
      consecutiveNewlines++;
      if (consecutiveNewlines <= 2) {
        cleaned += ch;
      }
    } else {
      consecutiveNewlines = 0;
      cleaned += ch;
    }
  }

  return cleaned;
}

std::string extractXmlTag(const std::string& xml, const std::string& tag) {
  std::string openTag = "<" + tag;
  size_t startPos = xml.find(openTag);
  if (startPos == std::string::npos) return "";

  size_t closeTagStart = xml.find('>', startPos);
  if (closeTagStart == std::string::npos) return "";
  closeTagStart++;

  std::string endTag = "</" + tag + ">";
  size_t endPos = xml.find(endTag, closeTagStart);
  if (endPos == std::string::npos) return "";

  std::string val = xml.substr(closeTagStart, endPos - closeTagStart);
  // Check CDATA
  size_t cdataStart = val.find("<![CDATA[");
  if (cdataStart != std::string::npos) {
    size_t cdataEnd = val.find("]]>", cdataStart);
    if (cdataEnd != std::string::npos) {
      val = val.substr(cdataStart + 9, cdataEnd - (cdataStart + 9));
    }
  }

  return cleanHtml(val);
}

uint32_t simpleHash(const std::string& str) {
  uint32_t hash = 5381;
  for (char c : str) {
    hash = ((hash << 5) + hash) + static_cast<uint8_t>(c);
  }
  return hash;
}

}  // namespace

NewsActivity::NewsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("News", renderer, mappedInput) {}

void NewsActivity::onEnter() {
  Activity::onEnter();
  mode = ViewMode::ARTICLES_LIST;
  selectedArticleIndex = 0;
  selectedSourceIndex = 0;
  currentArticlePage = 0;
  statusMessage.clear();
  statusMessageUntil = 0;
  lastSyncMillis = millis();
  isSyncing = false;

  Storage.ensureDirectoryExists("/news");
  Storage.ensureDirectoryExists("/news/cache");

  loadSources();
  loadCachedArticles();

  // If no cached articles and WiFi is up, auto-sync on start;
  // If no cached articles and WiFi is down, auto-connect if saved credentials exist
  if (articles.empty()) {
    if (WiFi.status() == WL_CONNECTED) {
      refreshAllFeeds(false);
    } else if (WIFI_STORE.getCredentialCount() > 0) {
      launchWifiSelection(true);
    }
  }

  needsCleanRefresh = true;
  requestUpdate(true);
}

void NewsActivity::onExit() {
  Activity::onExit();
}

void NewsActivity::setStatus(const std::string& msg, uint32_t durationMs) {
  statusMessage = msg;
  statusMessageUntil = millis() + durationMs;
  requestUpdate(true);
}

void NewsActivity::loadSources() {
  sources.clear();
  const char* configPath = "/news/sources.json";

  if (Storage.exists(configPath)) {
    std::string jsonStr = Storage.readFile(configPath).c_str();
    // Simple line/block parser for source entries
    size_t pos = 0;
    while ((pos = jsonStr.find('{', pos)) != std::string::npos) {
      size_t endObj = jsonStr.find('}', pos);
      if (endObj == std::string::npos) break;
      std::string block = jsonStr.substr(pos, endObj - pos + 1);

      NewsSource src;
      size_t namePos = block.find("\"name\"");
      if (namePos != std::string::npos) {
        size_t q1 = block.find('"', block.find(':', namePos) + 1);
        size_t q2 = block.find('"', q1 + 1);
        if (q1 != std::string::npos && q2 != std::string::npos) {
          src.name = block.substr(q1 + 1, q2 - q1 - 1);
        }
      }
      size_t urlPos = block.find("\"url\"");
      if (urlPos != std::string::npos) {
        size_t q1 = block.find('"', block.find(':', urlPos) + 1);
        size_t q2 = block.find('"', q1 + 1);
        if (q1 != std::string::npos && q2 != std::string::npos) {
          src.url = block.substr(q1 + 1, q2 - q1 - 1);
        }
      }
      size_t enPos = block.find("\"enabled\"");
      if (enPos != std::string::npos) {
        src.enabled = (block.find("true", enPos) != std::string::npos ||
                       block.find("1", enPos) != std::string::npos);
      }

      if (!src.url.empty()) {
        sources.push_back(src);
      }
      pos = endObj + 1;
    }
  }

  // Defaults if none loaded
  if (sources.empty()) {
    sources.push_back({"Код Дурова", "https://kod.ru/rss/", true});
    sources.push_back({"Exploit Media", "https://www.exploit.media/rss/", true});
    saveSources();
  }
}

void NewsActivity::saveSources() {
  std::string json = "[\n";
  for (size_t i = 0; i < sources.size(); ++i) {
    json += "  {\n";
    json += "    \"name\": \"" + sources[i].name + "\",\n";
    json += "    \"url\": \"" + sources[i].url + "\",\n";
    json += "    \"enabled\": " + std::string(sources[i].enabled ? "true" : "false") + "\n";
    json += "  }" + std::string(i + 1 < sources.size() ? ",\n" : "\n");
  }
  json += "]\n";
  Storage.writeFile("/news/sources.json", json.c_str());
}

void NewsActivity::loadCachedArticles() {
  articles.clear();
  auto root = Storage.open("/news/cache");
  if (!root || !root.isDirectory()) {
    return;
  }
  root.rewindDirectory();

  char nameBuf[256];
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    if (file.isDirectory()) continue;
    file.getName(nameBuf, sizeof(nameBuf));
    std::string filename(nameBuf);
    if (filename.find(".txt") == std::string::npos) continue;

    std::string path = "/news/cache/" + filename;
    std::string content;
    if (!Storage.readFileToString("NEWS", path, 1024 * 128, content)) continue;

    NewsArticle art;
    art.filename = filename;

    std::stringstream ss(content);
    std::string line;
    bool readingBody = false;

    while (std::getline(ss, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();

      if (readingBody) {
        art.body += line + "\n";
        continue;
      }

      if (line.rfind("SOURCE: ", 0) == 0) {
        art.sourceName = line.substr(8);
      } else if (line.rfind("DATE: ", 0) == 0) {
        art.pubDate = line.substr(6);
      } else if (line.rfind("LINK: ", 0) == 0) {
        art.link = line.substr(6);
      } else if (line.rfind("TIME: ", 0) == 0) {
        art.timestamp = static_cast<uint32_t>(strtoul(line.substr(6).c_str(), nullptr, 10));
      } else if (line.rfind("TITLE: ", 0) == 0) {
        art.title = line.substr(7);
      } else if (line == "BODY:") {
        readingBody = true;
      }
    }

    if (!art.title.empty()) {
      articles.push_back(art);
    }
  }

  // Sort newest first
  std::sort(articles.begin(), articles.end(), [](const NewsArticle& a, const NewsArticle& b) {
    return a.timestamp > b.timestamp;
  });
}

void NewsActivity::checkAndPruneCache() {
  auto root = Storage.open("/news/cache");
  if (!root || !root.isDirectory()) {
    return;
  }
  root.rewindDirectory();

  struct CachedFileInfo {
    std::string path;
    uint32_t size;
    uint32_t timestamp;
  };

  std::vector<CachedFileInfo> cachedList;
  uint64_t totalBytes = 0;
  char nameBuf[256];

  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    if (file.isDirectory()) continue;
    file.getName(nameBuf, sizeof(nameBuf));
    std::string p = "/news/cache/" + std::string(nameBuf);
    uint32_t sz = static_cast<uint32_t>(file.size());
    totalBytes += sz;
    cachedList.push_back({p, sz, 0});
  }

  if (totalBytes <= MAX_CACHE_BYTES) {
    return;
  }

  // Read timestamps to sort oldest first
  for (auto& item : cachedList) {
    std::string content;
    if (Storage.readFileToString("NEWS", item.path, 1024, content)) {
      size_t pos = content.find("TIME: ");
      if (pos != std::string::npos) {
        item.timestamp = static_cast<uint32_t>(strtoul(content.c_str() + pos + 6, nullptr, 10));
      }
    }
  }

  std::sort(cachedList.begin(), cachedList.end(), [](const CachedFileInfo& a, const CachedFileInfo& b) {
    return a.timestamp < b.timestamp;
  });

  for (const auto& item : cachedList) {
    if (totalBytes <= PRUNE_TARGET_BYTES) break;
    Storage.remove(item.path.c_str());
    if (totalBytes >= item.size) {
      totalBytes -= item.size;
    } else {
      totalBytes = 0;
    }
  }

  loadCachedArticles();
}

void NewsActivity::launchWifiSelection(bool autoRefreshAfter) {
  startActivityForResult(
      std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
      [this, autoRefreshAfter](const ActivityResult& result) {
        onWifiSelectionComplete(!result.isCancelled, autoRefreshAfter);
      });
}

void NewsActivity::onWifiSelectionComplete(bool connected, bool autoRefreshAfter) {
  if (connected && WiFi.status() == WL_CONNECTED) {
    if (autoRefreshAfter) {
      pendingRefresh = true;
      requestUpdate(true);
    } else {
      setStatus(tr(STR_NEWS_REFRESH_OK));
    }
  } else {
    setStatus(tr(STR_WIFI_NOT_CONNECTED));
  }
}

void NewsActivity::refreshAllFeeds(bool manual) {
  if (WiFi.status() != WL_CONNECTED) {
    if (manual) {
      launchWifiSelection(true);
    }
    return;
  }

  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);

  isSyncing = true;
  setStatus(tr(STR_NEWS_REFRESHING), 8000);
  requestUpdateAndWait();

  bool anySuccess = false;
  for (const auto& src : sources) {
    if (!src.enabled) continue;
    if (fetchFeed(src)) {
      anySuccess = true;
    }
  }

  checkAndPruneCache();
  loadCachedArticles();
  isSyncing = false;

  if (anySuccess) {
    setStatus(tr(STR_NEWS_REFRESH_OK));
  } else {
    setStatus(tr(STR_NEWS_REFRESH_FAIL));
  }
}

bool NewsActivity::fetchFeed(const NewsSource& source) {
  const std::string tempFile = "/news/temp_feed.xml";
  Storage.remove(tempFile.c_str());

  LOG_INF("NEWS", "Downloading feed: %s (%s)", source.name.c_str(), source.url.c_str());
  auto err = HttpDownloader::downloadToFile(source.url, tempFile);
  if (err != HttpDownloader::OK) {
    LOG_ERR("NEWS", "Download failed for %s", source.url.c_str());
    Storage.remove(tempFile.c_str());
    return false;
  }

  parseRssFeedFile(tempFile, source.name);
  Storage.remove(tempFile.c_str());
  return true;
}

void NewsActivity::parseRssFeedFile(const std::string& filePath, const std::string& sourceName) {
  auto file = Storage.open(filePath.c_str());
  if (!file) return;

  uint32_t nowTime = static_cast<uint32_t>(time(nullptr));
  int countAdded = 0;

  std::string buffer;
  buffer.reserve(8192);
  char chunk[1024];

  while (file.available() || !buffer.empty()) {
    while (file.available() && buffer.size() < 4096) {
      int bytesRead = file.read(reinterpret_cast<uint8_t*>(chunk), sizeof(chunk));
      if (bytesRead <= 0) break;
      buffer.append(chunk, bytesRead);
    }

    size_t itemStart = buffer.find("<item");
    bool isAtom = false;
    if (itemStart == std::string::npos) {
      itemStart = buffer.find("<entry");
      if (itemStart != std::string::npos) isAtom = true;
    }

    if (itemStart == std::string::npos) {
      if (buffer.size() > 64) {
        buffer.erase(0, buffer.size() - 64);
      } else if (!file.available()) {
        break;
      }
      continue;
    }

    const char* endTag = isAtom ? "</entry>" : "</item>";
    size_t endTagLen = isAtom ? 8 : 7;
    size_t itemEnd = buffer.find(endTag, itemStart);

    while (itemEnd == std::string::npos && file.available()) {
      int bytesRead = file.read(reinterpret_cast<uint8_t*>(chunk), sizeof(chunk));
      if (bytesRead <= 0) break;
      buffer.append(chunk, bytesRead);
      itemEnd = buffer.find(endTag, itemStart);
      if (buffer.size() - itemStart > 32768) break;
    }

    if (itemEnd == std::string::npos) {
      buffer.erase(0, itemStart + 5);
      continue;
    }

    std::string itemXml = buffer.substr(itemStart, itemEnd - itemStart + endTagLen);
    buffer.erase(0, itemEnd + endTagLen);

    std::string title = extractXmlTag(itemXml, "title");
    std::string link = extractXmlTag(itemXml, "link");
    std::string pubDate = extractXmlTag(itemXml, isAtom ? "published" : "pubDate");
    if (pubDate.empty() && isAtom) {
      pubDate = extractXmlTag(itemXml, "updated");
    }

    std::string body = extractXmlTag(itemXml, "content:encoded");
    if (body.empty()) {
      body = extractXmlTag(itemXml, "description");
    }
    if (body.empty() && isAtom) {
      body = extractXmlTag(itemXml, "content");
    }

    if (title.empty()) continue;

    uint32_t hashVal = simpleHash(title + link);
    char fnameBuf[32];
    snprintf(fnameBuf, sizeof(fnameBuf), "%08x.txt", hashVal);
    std::string artPath = "/news/cache/" + std::string(fnameBuf);

    std::string fileData;
    fileData.reserve(title.size() + body.size() + 256);
    fileData += "SOURCE: " + sourceName + "\n";
    fileData += "DATE: " + pubDate + "\n";
    fileData += "LINK: " + link + "\n";
    fileData += "TIME: " + std::to_string(nowTime - countAdded * 60) + "\n";
    fileData += "TITLE: " + title + "\n";
    fileData += "BODY:\n" + body + "\n";

    Storage.writeFile(artPath.c_str(), fileData.c_str());
    countAdded++;
    if (countAdded >= 25) break;
  }

  file.close();
  LOG_INF("NEWS", "Parsed and saved %d articles from %s", countAdded, sourceName.c_str());
}

void NewsActivity::prepareArticlePages(const NewsArticle& article) {
  articlePages.clear();
  currentArticlePage = 0;

  const auto& metrics = UITheme::getInstance().getMetrics();
  int screenW = renderer.getScreenWidth();
  int screenH = renderer.getScreenHeight();
  int contentW = screenW - metrics.contentSidePadding * 2;
  int contentH = screenH - metrics.headerHeight - metrics.buttonHintsHeight - 20;

  int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  int linesPerPage = contentH / (lineHeight > 0 ? lineHeight : 20);
  if (linesPerPage < 6) linesPerPage = 6;

  auto allLines = renderer.wrappedText(UI_10_FONT_ID, article.body.c_str(), contentW, 2000);

  if (allLines.empty()) {
    articlePages.push_back(tr(STR_NEWS_NO_TEXT));
    return;
  }

  std::string curPage;
  int lineCount = 0;
  for (const auto& line : allLines) {
    curPage += line + "\n";
    lineCount++;
    if (lineCount >= linesPerPage) {
      articlePages.push_back(curPage);
      curPage.clear();
      lineCount = 0;
    }
  }

  if (!curPage.empty()) {
    articlePages.push_back(curPage);
  }
}

void NewsActivity::loop() {
  if (pendingRefresh) {
    pendingRefresh = false;
    refreshAllFeeds(true);
    return;
  }

  // Clear expired status messages
  if (statusMessageUntil > 0 && millis() > statusMessageUntil) {
    statusMessage.clear();
    statusMessageUntil = 0;
    requestUpdate();
  }

  // Periodic network sync once an hour
  unsigned long now = millis();
  if (!isSyncing && (now - lastSyncMillis >= 3600000UL)) {
    lastSyncMillis = now;
    if (WiFi.status() == WL_CONNECTED) {
      refreshAllFeeds(false);
    }
  }

  // =========================================================================
  // VIEW MODE: ARTICLES LIST
  // =========================================================================
  if (mode == ViewMode::ARTICLES_LIST) {
    buttonNavigator.onNext([this] {
      if (!articles.empty()) {
        selectedArticleIndex = (selectedArticleIndex + 1) % articles.size();
        requestUpdate();
      }
    });
    buttonNavigator.onPrevious([this] {
      if (!articles.empty()) {
        selectedArticleIndex = (selectedArticleIndex == 0) ? static_cast<int>(articles.size()) - 1 : selectedArticleIndex - 1;
        requestUpdate();
      }
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!articles.empty() && selectedArticleIndex >= 0 && selectedArticleIndex < static_cast<int>(articles.size())) {
        prepareArticlePages(articles[selectedArticleIndex]);
        mode = ViewMode::ARTICLE_VIEW;
        needsCleanRefresh = true;
        requestUpdate(true);
        return;
      } else if (articles.empty()) {
        refreshAllFeeds(true);
        return;
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
      // Toggle to sources manager
      mode = ViewMode::SOURCES_LIST;
      selectedSourceIndex = 0;
      needsCleanRefresh = true;
      requestUpdate(true);
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
      // Refresh feeds now
      refreshAllFeeds(true);
      return;
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      finish();
      return;
    }

    return;
  }

  // =========================================================================
  // VIEW MODE: ARTICLE READER VIEW
  // =========================================================================
  if (mode == ViewMode::ARTICLE_VIEW) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      if (!articlePages.empty() && currentArticlePage + 1 < static_cast<int>(articlePages.size())) {
        currentArticlePage++;
        needsCleanRefresh = true;
        requestUpdate(true);
        return;
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      if (currentArticlePage > 0) {
        currentArticlePage--;
        needsCleanRefresh = true;
        requestUpdate(true);
        return;
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      mode = ViewMode::ARTICLES_LIST;
      needsCleanRefresh = true;
      requestUpdate(true);
      return;
    }

    return;
  }

  // =========================================================================
  // VIEW MODE: SOURCES MANAGER
  // =========================================================================
  if (mode == ViewMode::SOURCES_LIST) {
    int touchX = 0, touchY = 0;
    if (mappedInput.wasScreenTapped(touchX, touchY)) {
      const auto& metrics = UITheme::getInstance().getMetrics();
      int startY = metrics.topPadding + metrics.headerHeight + 8;
      int tileHeight = 56;
      int spacing = 10;
      for (size_t i = 0; i < sources.size(); ++i) {
        int y = startY + static_cast<int>(i) * (tileHeight + spacing);
        if (touchY >= y && touchY <= y + tileHeight) {
          selectedSourceIndex = static_cast<int>(i);
          sources[i].enabled = !sources[i].enabled;
          saveSources();
          requestUpdate(true);
          return;
        }
      }
    }

    buttonNavigator.onNext([this] {
      if (!sources.empty()) {
        selectedSourceIndex = (selectedSourceIndex + 1) % sources.size();
        requestUpdate();
      }
    });
    buttonNavigator.onPrevious([this] {
      if (!sources.empty()) {
        selectedSourceIndex = (selectedSourceIndex == 0) ? static_cast<int>(sources.size()) - 1 : selectedSourceIndex - 1;
        requestUpdate();
      }
    });

    if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
        mappedInput.wasReleased(MappedInputManager::Button::Down)) {
      if (!sources.empty()) {
        selectedSourceIndex = (selectedSourceIndex + 1) % sources.size();
        requestUpdate(true);
        return;
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
        mappedInput.wasReleased(MappedInputManager::Button::Up)) {
      if (!sources.empty()) {
        selectedSourceIndex = (selectedSourceIndex == 0) ? static_cast<int>(sources.size()) - 1 : selectedSourceIndex - 1;
        requestUpdate(true);
        return;
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (selectedSourceIndex >= 0 && selectedSourceIndex < static_cast<int>(sources.size())) {
        sources[selectedSourceIndex].enabled = !sources[selectedSourceIndex].enabled;
        saveSources();
        requestUpdate(true);
        return;
      }
    }

    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
      mode = ViewMode::ARTICLES_LIST;
      needsCleanRefresh = true;
      requestUpdate(true);
      return;
    }

    return;
  }
}

void NewsActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect content = UITheme::getInstance().getContentArea(renderer);
  const int screenW = renderer.getScreenWidth();

  const char* title = (mode == ViewMode::SOURCES_LIST) ? tr(STR_NEWS_SOURCES) : tr(STR_NEWS);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, screenW, metrics.headerHeight}, title, nullptr, true);

  if (mode == ViewMode::ARTICLES_LIST) {
    renderArticlesList(content);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_NEWS_READ), tr(STR_NEWS_SOURCES), tr(STR_NEWS_REFRESH));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (mode == ViewMode::ARTICLE_VIEW) {
    renderArticleView(content);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", tr(STR_PREV_PAGE), tr(STR_NEXT_PAGE));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (mode == ViewMode::SOURCES_LIST) {
    renderSourcesList(content);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_TOGGLE), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  if (!statusMessage.empty()) {
    drawStatusBanner(content.x + 16, content.y + content.height - 76, content.width - 32);
  }

  if (needsCleanRefresh) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    needsCleanRefresh = false;
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}

void NewsActivity::drawStatusBanner(int x, int y, int w) {
  int h = 32;
  renderer.fillRoundedRect(x, y, w, h, 6, Color::White);
  renderer.drawRect(x, y, w, h, true);
  renderer.drawRect(x + 1, y + 1, w - 2, h - 2, true);
  renderer.drawCenteredText(UI_10_FONT_ID, y + 8, statusMessage.c_str(), true, EpdFontFamily::BOLD);
}

void NewsActivity::renderArticlesList(const Rect& content) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int startY = metrics.topPadding + metrics.headerHeight + 8;

  if (articles.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, startY + 80, tr(STR_NEWS_NO_ARTICLES), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, startY + 120, tr(STR_NEWS_PRESS_REFRESH), true);
    return;
  }

  constexpr int VISIBLE_ITEMS = 5;
  int cardH = 76;
  int spacing = 8;

  int scrollOffset = 0;
  if (selectedArticleIndex >= VISIBLE_ITEMS) {
    scrollOffset = selectedArticleIndex - VISIBLE_ITEMS + 1;
  }

  for (int vi = 0; vi < VISIBLE_ITEMS; ++vi) {
    int idx = scrollOffset + vi;
    if (idx >= static_cast<int>(articles.size())) break;

    const auto& art = articles[idx];
    int y = startY + vi * (cardH + spacing);
    int x = content.x + 8;
    int w = content.width - 16;
    bool isSelected = (idx == selectedArticleIndex);

    if (isSelected) {
      renderer.fillRoundedRect(x, y, w, cardH, 6, Color::LightGray);
      renderer.drawRect(x, y, w, cardH, true);
    } else {
      renderer.drawRect(x, y, w, cardH, true);
      renderer.fillRectDither(x + 2, y + cardH, w, 2, Color::LightGray);
    }

    // Source tag pill
    char srcTag[64];
    snprintf(srcTag, sizeof(srcTag), "[%s]", art.sourceName.c_str());
    renderer.drawText(UI_10_FONT_ID, x + 12, y + 8, srcTag, true, EpdFontFamily::BOLD);

    // Title (bold, wrapped or truncated)
    std::string trTitle = renderer.truncatedText(UI_10_FONT_ID, art.title.c_str(), w - 24, EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, x + 12, y + 28, trTitle.c_str(), true, EpdFontFamily::BOLD);

    // Date
    if (!art.pubDate.empty()) {
      renderer.drawText(UI_10_FONT_ID, x + 12, y + 48, art.pubDate.c_str(), true);
    }
  }
}

void NewsActivity::renderArticleView(const Rect& content) {
  if (selectedArticleIndex < 0 || selectedArticleIndex >= static_cast<int>(articles.size())) return;
  const auto& art = articles[selectedArticleIndex];
  const auto& metrics = UITheme::getInstance().getMetrics();

  int startY = metrics.topPadding + metrics.headerHeight + 8;
  int w = content.width - 16;
  int x = content.x + 8;

  // Header Box
  renderer.drawText(UI_10_FONT_ID, x, startY, art.sourceName.c_str(), true, EpdFontFamily::BOLD);
  if (!art.pubDate.empty()) {
    int dw = renderer.getTextWidth(UI_10_FONT_ID, art.pubDate.c_str());
    renderer.drawText(UI_10_FONT_ID, x + w - dw, startY, art.pubDate.c_str(), true);
  }
  startY += 20;

  // Title
  auto titleLines = renderer.wrappedText(UI_12_FONT_ID, art.title.c_str(), w, 3, EpdFontFamily::BOLD);
  for (const auto& tl : titleLines) {
    renderer.drawText(UI_12_FONT_ID, x, startY, tl.c_str(), true, EpdFontFamily::BOLD);
    startY += 24;
  }
  startY += 4;
  renderer.drawLine(x, startY, x + w, startY);
  startY += 8;

  // Body text page
  if (currentArticlePage >= 0 && currentArticlePage < static_cast<int>(articlePages.size())) {
    const std::string& pageText = articlePages[currentArticlePage];
    std::stringstream pss(pageText);
    std::string pLine;
    int curY = startY;
    int lHeight = renderer.getLineHeight(UI_10_FONT_ID);
    if (lHeight < 18) lHeight = 18;

    while (std::getline(pss, pLine) && curY < content.y + content.height - 40) {
      if (!pLine.empty() && pLine.back() == '\r') pLine.pop_back();
      renderer.drawText(UI_10_FONT_ID, x, curY, pLine.c_str(), true);
      curY += lHeight;
    }
  }

  // Page indicator at bottom right
  char pageBuf[32];
  snprintf(pageBuf, sizeof(pageBuf), "%s %d / %d", tr(STR_NEWS_PAGE), currentArticlePage + 1,
           articlePages.empty() ? 1 : static_cast<int>(articlePages.size()));
  int pw = renderer.getTextWidth(UI_10_FONT_ID, pageBuf);
  renderer.drawText(UI_10_FONT_ID, x + w - pw, content.y + content.height - 30, pageBuf, true, EpdFontFamily::BOLD);
}

void NewsActivity::renderSourcesList(const Rect& content) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  int startY = metrics.topPadding + metrics.headerHeight + 8;

  int tileHeight = 56;
  int spacing = 10;

  for (size_t i = 0; i < sources.size(); ++i) {
    int y = startY + i * (tileHeight + spacing);
    int x = content.x + 8;
    int w = content.width - 16;
    bool isSelected = (selectedSourceIndex == static_cast<int>(i));

    if (isSelected) {
      renderer.fillRoundedRect(x, y, w, tileHeight, 6, Color::LightGray);
      renderer.drawRect(x, y, w, tileHeight, true);
    } else {
      renderer.drawRect(x, y, w, tileHeight, true);
      renderer.fillRectDither(x + 2, y + tileHeight, w, 2, Color::LightGray);
    }

    // Checkbox indicator
    const char* checkMark = sources[i].enabled ? "[✓]" : "[  ]";
    renderer.drawText(UI_12_FONT_ID, x + 14, y + 16, checkMark, true, EpdFontFamily::BOLD);

    // Source Name
    renderer.drawText(UI_10_FONT_ID, x + 50, y + 10, sources[i].name.c_str(), true, EpdFontFamily::BOLD);
    // Source URL
    std::string trUrl = renderer.truncatedText(UI_10_FONT_ID, sources[i].url.c_str(), w - 60);
    renderer.drawText(UI_10_FONT_ID, x + 50, y + 30, trUrl.c_str(), true);
  }
}
