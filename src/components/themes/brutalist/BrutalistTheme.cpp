#include "BrutalistTheme.h"

#include <GfxRenderer.h>
#include <algorithm>
#include "components/themes/ThemeIconHelper.h"
#include "fontIds.h"

void BrutalistTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                    const std::function<std::string(int index)>& buttonLabel,
                                    const std::function<UIIcon(int index)>& rowIcon) const {
  const int rowHeight = BrutalistMetrics::values.menuRowHeight;
  const int spacing = BrutalistMetrics::values.menuSpacing;
  const int rowStep = rowHeight + spacing;
  const int maxVisible = std::max(1, (rect.height + spacing) / rowStep);

  int startIndex = 0;
  if (selectedIndex >= maxVisible) {
    startIndex = selectedIndex - maxVisible + 1;
  }
  if (startIndex + maxVisible > buttonCount) {
    startIndex = buttonCount - maxVisible;
  }
  if (startIndex < 0) startIndex = 0;

  const int endIndex = std::min(buttonCount, startIndex + maxVisible);
  const int scrollbarW = buttonCount > maxVisible ? 10 : 0;
  const int tileWidth = rect.width - BrutalistMetrics::values.contentSidePadding * 2 - scrollbarW;

  for (int i = startIndex; i < endIndex; ++i) {
    const int drawRow = i - startIndex;
    Rect tileRect = Rect{rect.x + BrutalistMetrics::values.contentSidePadding,
                         rect.y + drawRow * rowStep, tileWidth, rowHeight};

    const bool selected = selectedIndex == i;

    if (selected) {
      // Solid black box with sharp 0px corners
      renderer.fillRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, true);
    } else {
      // Crisp 1px mechanical rectangle outline
      renderer.drawRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, true);
    }

    std::string labelStr = buttonLabel(i);
    const char* label = labelStr.c_str();
    int textX = tileRect.x + 14;
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int textY = tileRect.y + (rowHeight - lineHeight) / 2;

    if (rowIcon != nullptr) {
      UIIcon icon = rowIcon(i);
      const uint8_t* iconBitmap = themeIconForName(icon);
      if (iconBitmap != nullptr) {
        if (selected) {
          // White badge tile to contrast with solid black background
          renderer.fillRect(textX - 2, textY - 2, 36, 36, false);
          renderer.drawIcon(iconBitmap, textX, textY, 32);
          textX += 32 + 10;
        } else {
          renderer.drawIcon(iconBitmap, textX, textY, 32);
          textX += 32 + 10;
        }
      }
    }

    // High contrast: white text when selected, black text when not
    renderer.drawText(UI_12_FONT_ID, textX, textY, label, !selected, EpdFontFamily::BOLD);
  }

  // Draw industrial brutalist scrollbar (solid thick track & thumb)
  if (buttonCount > maxVisible) {
    const int scrollbarX = rect.x + rect.width - BrutalistMetrics::values.contentSidePadding + 2;
    const int scrollbarTrackH = maxVisible * rowStep - spacing;
    const int scrollbarThumbH = std::max(20, scrollbarTrackH * maxVisible / buttonCount);
    const int maxThumbTravel = scrollbarTrackH - scrollbarThumbH;
    const int maxScroll = buttonCount - maxVisible;
    const int thumbY = rect.y + (maxScroll > 0 ? (startIndex * maxThumbTravel / maxScroll) : 0);

    renderer.drawRect(scrollbarX, rect.y, 6, scrollbarTrackH, true);
    renderer.fillRect(scrollbarX, thumbY, 6, scrollbarThumbH, true);
  }
}
