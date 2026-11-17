#pragma once

#include "components/themes/lyra/LyraTheme.h"

class GfxRenderer;

namespace MinimalistMetrics {
constexpr ThemeMetrics values = {.batteryWidth = 24,
                                 .batteryHeight = 14,
                                 .topPadding = 10,
                                 .batteryBarHeight = 22,
                                 .headerHeight = 84,
                                 .verticalSpacing = 14,
                                 .previewPadding = 12,
                                 .previewHeightPercent = 30,
                                 .contentSidePadding = 18,
                                 .listRowHeight = 44,
                                 .listWithSubtitleRowHeight = 62,
                                 .listRowGap = 2,
                                 .listRowRadius = 2,
                                 .listInset = 18,
                                 .listSidePadding = 10,
                                 .listSelectionStyle = 1,  // light pill
                                 .listScrollWidth = 2,
                                 .listScrollSide = 0,
                                 .listTitleBold = false,
                                 .headerSidePadding = 18,
                                 .headerUnderlineSize = 1,  // 1px hairline rule
                                 .headerTitleAlign = 0,     // left aligned
                                 .headerBatterySide = 0,
                                 .headerShowsClock = true,
                                 .headerClockCentered = false,
                                 .menuRowHeight = 44,
                                 .menuSpacing = 4,
                                 .tabSpacing = 8,
                                 .tabBarHeight = 46,
                                 .scrollBarWidth = 2,
                                 .scrollBarRightOffset = 4,
                                 .homeTopPadding = 56,
                                 .homeCoverHeight = 226,
                                 .homeCoverTileHeight = 242,
                                 .homeRecentBooksCount = 1,
                                 .homeContinueReadingInMenu = false,
                                 .homeMenuTopOffset = 8,
                                 .buttonHintsHeight = 40,
                                 .sideButtonHintsWidth = 30,
                                 .progressBarHeight = 14,
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
                                 .popupFrameThickness = 1,
                                 .popupCornerRadius = 2,
                                 .popupTextBold = false,
                                 .popupTextInverted = false,
                                 .popupTextBaselineOffsetY = -2,
                                 .popupProgressBarHeight = 2,
                                 .popupProgressDrawOutline = false,
                                 .popupProgressClampPercent = false,
                                 .popupProgressFillInverted = false,
                                 .popupProgressOutlineInverted = false,
                                 .optionPopupItemSpacing = 8,
                                 .optionPopupInnerPadding = 18,
                                 .optionPopupSelectionVPadding = 10,
                                 .optionPopupDialogSideMargin = 20,
                                 .textFieldHorizontalPadding = 6,
                                 .textFieldNormalThickness = 1,
                                 .textFieldCursorThickness = 2,
                                 .textFieldLineEndOffset = 0,
                                 .controlRadius = 2,
                                 .sheetRadius = 2,
                                 .capsuleRadius = 2};
}  // namespace MinimalistMetrics

class MinimalistTheme : public LyraTheme {
 public:
  void drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                      const std::function<std::string(int index)>& buttonLabel,
                      const std::function<UIIcon(int index)>& rowIcon) const override;
};
