#pragma once

#include <cstdint>
#include "components/themes/BaseTheme.h"
#include "components/icons/blocks.h"
#include "components/icons/book.h"
#include "components/icons/bookmark.h"
#include "components/icons/folder.h"
#include "components/icons/hotspot.h"
#include "components/icons/library.h"
#include "components/icons/recent.h"
#include "components/icons/settings2.h"
#include "components/icons/transfer.h"
#include "components/icons/wifi.h"
#include "components/icons/anki.h"
#include "components/icons/kanji.h"
#include "components/icons/calendar.h"
#include "components/icons/news.h"

inline const uint8_t* themeIconForName(UIIcon icon) {
  switch (icon) {
    case UIIcon::Folder:
      return FolderIcon;
    case UIIcon::Book:
      return BookIcon;
    case UIIcon::Recent:
      return RecentIcon;
    case UIIcon::Settings:
      return Settings2Icon;
    case UIIcon::Transfer:
      return TransferIcon;
    case UIIcon::Library:
      return LibraryIcon;
    case UIIcon::Plugins:
      return BlocksIcon;
    case UIIcon::Wifi:
      return WifiIcon;
    case UIIcon::Hotspot:
      return HotspotIcon;
    case UIIcon::Bookmark:
      return BookmarkIcon;
    case UIIcon::Blocks:
      return BlocksIcon;
    case UIIcon::Anki:
      return AnkiIcon;
    case UIIcon::Kanji:
      return KanjiIcon;
    case UIIcon::Calendar:
      return CalendarIcon;
    case UIIcon::News:
      return NewsIcon;
    default:
      return nullptr;
  }
}
