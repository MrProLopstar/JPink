#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

namespace BrutalistMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 24,
                                 .batteryHeight = 14,
                                 .topPadding = 10,
                                 .batteryBarHeight = 22,
                                 .headerHeight = 84,
                                 .verticalSpacing = 14,
                                 .previewPadding = 12,
                                 .previewHeightPercent = 30,
                                 .contentSidePadding = 16,
                                 .listRowHeight = 46,
                                 .listWithSubtitleRowHeight = 64,
                                 .listRowGap = 4,
                                 .listRowRadius = 0,       // Sharp 0px corners
                                 .listInset = 16,
                                 .listSidePadding = 12,
                                 .listSelectionStyle = 0,  // Invert fill (black block)
                                 .listScrollWidth = 6,     // Heavy industrial scrollbar
                                 .listScrollSide = 0,
                                 .listTitleBold = true,    // Bold Swiss typography
                                 .headerSidePadding = 16,
                                 .headerUnderlineSize = 3, // Heavy 3px underline
                                 .headerTitleAlign = 0,    // Left aligned
                                 .headerBatterySide = 0,
                                 .headerShowsClock = true,
                                 .headerClockCentered = false,
                                 .menuRowHeight = 46,
                                 .menuSpacing = 4,
                                 .tabSpacing = 8,
                                 .tabBarHeight = 48,
                                 .scrollBarWidth = 6,
                                 .scrollBarRightOffset = 4,
                                 .homeTopPadding = 56,
                                 .homeCoverHeight = 226,
                                 .homeCoverTileHeight = 242,
                                 .homeRecentBooksCount = 1,
                                 .homeContinueReadingInMenu = false,
                                 .homeMenuTopOffset = 8,
                                 .buttonHintsHeight = 40,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 16,
                                 .progressBarMarginTop = 1,
                                 .statusBarHorizontalMargin = 6,
                                 .statusBarVerticalMargin = 18,
                                 .keyboardKeyHeight = 56,
                                 .keyboardKeySpacing = 0,
                                 .keyboardCenteredText = false,
                                 .keyboardVerticalOffset = -7,
                                 .keyboardTextFieldWidthPercent = 85,
                                 .keyboardWidthPercent = 94,
                                 .popupTopOffsetRatio = 0.165f,
                                 .popupMarginX = 16,
                                 .popupMarginY = 12,
                                 .popupFrameThickness = 3, // Bold 3px border
                                 .popupCornerRadius = 0,   // Sharp corners
                                 .popupTextBold = true,
                                 .popupTextInverted = false,
                                 .popupTextBaselineOffsetY = -2,
                                 .popupProgressBarHeight = 6,
                                 .popupProgressDrawOutline = true,
                                 .popupProgressClampPercent = false,
                                 .popupProgressFillInverted = false,
                                 .popupProgressOutlineInverted = false,
                                 .optionPopupItemSpacing = 8,
                                 .optionPopupInnerPadding = 18,
                                 .optionPopupSelectionVPadding = 10,
                                 .optionPopupDialogSideMargin = 20,
                                 .textFieldHorizontalPadding = 6,
                                 .textFieldNormalThickness = 2,
                                 .textFieldCursorThickness = 3,
                                 .textFieldLineEndOffset = 0,
                                 .controlRadius = 0,
                                 .sheetRadius = 0,
                                 .capsuleRadius = 0};
}  // namespace BrutalistMetrics

class BrutalistTheme : public LyraTheme {
 public:
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
};
