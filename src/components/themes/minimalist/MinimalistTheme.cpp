#include "MinimalistTheme.h"

#include <GfxRenderer.h>
#include <algorithm>
#include "components/themes/ThemeIconHelper.h"
#include "fontIds.h"

void MinimalistTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                                     const std::function<std::string(int index)>& buttonLabel,
                                     const std::function<UIIcon(int index)>& rowIcon) const {
  const int rowHeight = MinimalistMetrics::values.menuRowHeight;
  const int spacing = MinimalistMetrics::values.menuSpacing;
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
  const int scrollbarW = buttonCount > maxVisible ? 6 : 0;
  const int tileWidth = rect.width - MinimalistMetrics::values.contentSidePadding * 2 - scrollbarW;

  for (int i = startIndex; i < endIndex; ++i) {
    const int drawRow = i - startIndex;
    Rect tileRect = Rect{rect.x + MinimalistMetrics::values.contentSidePadding,
                         rect.y + drawRow * rowStep, tileWidth, rowHeight};

    const bool selected = selectedIndex == i;

    if (selected) {
      // Subtle light pill with 2px corner radius and a crisp hairline left accent
      renderer.fillRoundedRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, 2, Color::LightGray);
      renderer.fillRect(tileRect.x, tileRect.y + 4, 3, tileRect.height - 8, true);
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
        renderer.drawIcon(iconBitmap, textX, textY, 32);
        textX += 32 + 8;
      }
    }

    renderer.drawText(UI_12_FONT_ID, textX, textY, label, true);
  }

  // Draw minimalist hairline scrollbar
  if (buttonCount > maxVisible) {
    const int scrollbarX = rect.x + rect.width - MinimalistMetrics::values.contentSidePadding + 2;
    const int scrollbarTrackH = maxVisible * rowStep - spacing;
    const int scrollbarThumbH = std::max(16, scrollbarTrackH * maxVisible / buttonCount);
    const int maxThumbTravel = scrollbarTrackH - scrollbarThumbH;
    const int maxScroll = buttonCount - maxVisible;
    const int thumbY = rect.y + (maxScroll > 0 ? (startIndex * maxThumbTravel / maxScroll) : 0);

    renderer.drawLine(scrollbarX, rect.y, scrollbarX, rect.y + scrollbarTrackH, true);
    renderer.fillRect(scrollbarX - 1, thumbY, 3, scrollbarThumbH, true);
  }
}
