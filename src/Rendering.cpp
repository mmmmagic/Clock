#include "FocusClockApp.h"

namespace focus_clock {

void FocusClockApp::EnsurePaintFonts(int width) {
    if (!uiFontFamily_ || !monoFontFamily_) {
        return;
    }

    if (cachedPaintFontWidth_ == width && digitalFont_ && dateFont_ && statusFont_ && remainFont_ && focusHintFont_ && idleHintFont_) {
        return;
    }

    cachedPaintFontWidth_ = width;
    REAL digitalSize = static_cast<REAL>(std::max(56, std::min(132, width / 10)));
    digitalFont_ = std::make_unique<Font>(monoFontFamily_.get(), digitalSize, FontStyleRegular, UnitPixel);
    dateFont_ = std::make_unique<Font>(uiFontFamily_.get(), static_cast<REAL>(std::max(20, std::min(34, width / 48))), FontStyleRegular, UnitPixel);
    statusFont_ = std::make_unique<Font>(uiFontFamily_.get(), static_cast<REAL>(std::max(22, std::min(40, width / 42))), FontStyleRegular, UnitPixel);
    remainFont_ = std::make_unique<Font>(monoFontFamily_.get(), static_cast<REAL>(std::max(48, std::min(96, width / 14))), FontStyleRegular, UnitPixel);
    focusHintFont_ = std::make_unique<Font>(uiFontFamily_.get(), 18.0f, FontStyleRegular, UnitPixel);
    idleHintFont_ = std::make_unique<Font>(uiFontFamily_.get(), 20.0f, FontStyleRegular, UnitPixel);
}

void FocusClockApp::ReleaseRenderResources() {
    digitalFont_.reset();
    dateFont_.reset();
    statusFont_.reset();
    remainFont_.reset();
    focusHintFont_.reset();
    idleHintFont_.reset();
    cachedPaintFontWidth_ = 0;
    monoFontFamily_.reset();
    uiFontFamily_.reset();
}

void FocusClockApp::Paint() {
    PAINTSTRUCT ps{};
    HDC hdc = BeginPaint(hwnd_, &ps);
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bitmap = CreateCompatibleBitmap(hdc, width, height);
    HGDIOBJ oldBitmap = SelectObject(mem, bitmap);

    Graphics g(mem);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);

    DrawBackground(g, rc);

    Theme theme{
        darkMode_ ? Color(255, 11, 15, 20) : Color(255, 245, 247, 250),
        darkMode_ ? Color(255, 24, 30, 39) : Color(255, 255, 255, 255),
        darkMode_ ? Color(255, 242, 245, 247) : Color(255, 24, 28, 35),
        darkMode_ ? Color(255, 178, 187, 199) : Color(255, 79, 88, 103),
        darkMode_ ? Color(255, 119, 131, 146) : Color(255, 104, 116, 132),
        darkMode_ ? Color(255, 89, 180, 255) : Color(255, 0, 113, 188),
        darkMode_ ? Color(50, 89, 180, 255) : Color(32, 0, 113, 188),
        darkMode_ ? Color(255, 255, 115, 115) : Color(255, 190, 46, 46),
        darkMode_ ? Color(255, 54, 64, 78) : Color(255, 214, 220, 228)
    };

    SYSTEMTIME now{};
    GetLocalTime(&now);

    if (focusActive_) {
        RectF topProgressRect(0.0f, 0.0f, static_cast<REAL>(width), 4.0f);
        DrawFocusProgressBar(g, topProgressRect, theme);
    }

    int clockSize = std::min(width, height) / 3;
    clockSize = std::max(220, std::min(clockSize, 390));
    RectF clockRect(
        static_cast<REAL>((width - clockSize) / 2),
        static_cast<REAL>(height * 0.12),
        static_cast<REAL>(clockSize),
        static_cast<REAL>(clockSize));
    DrawAnalogClock(g, clockRect, now);

    EnsurePaintFonts(width);
    FontFamily& family = *uiFontFamily_;

    REAL digitalSize = static_cast<REAL>(std::max(56, std::min(132, width / 10)));

    RectF digitalRect(0, static_cast<REAL>(height * 0.52), static_cast<REAL>(width), digitalSize + 18);
    if (focusActive_) {
        REAL clockBottom = clockRect.Y + clockRect.Height;
        REAL progressGap = digitalRect.Y - clockBottom;
        if (progressGap > 18.0f) {
            REAL progressWidth = static_cast<REAL>(std::max(160, width - 96));
            progressWidth = std::min(progressWidth, 560.0f);
            REAL progressHeight = std::min(12.0f, std::max(8.0f, progressGap - 12.0f));
            REAL progressY = clockBottom + (progressGap - progressHeight) / 2.0f;
            RectF progressRect(
                (static_cast<REAL>(width) - progressWidth) / 2.0f,
                progressY,
                progressWidth,
                progressHeight);
            DrawFocusProgressBar(g, progressRect, theme);
        }
    }
    DrawTextBlock(g, FormatTime(now), digitalRect, *digitalFont_, theme.primaryText, StringAlignmentCenter, StringAlignmentCenter);

    RectF dateRect(0, digitalRect.Y + digitalRect.Height + 4, static_cast<REAL>(width), 48);
    DrawTextBlock(g, FormatDate(now), dateRect, *dateFont_, theme.secondaryText, StringAlignmentCenter, StringAlignmentCenter);

    if (focusActive_) {
        RectF remainRect(0, dateRect.Y + 56, static_cast<REAL>(width), 92);
        DrawTextBlock(g, FormatRemaining(), remainRect, *remainFont_, theme.accent, StringAlignmentCenter, StringAlignmentCenter);

        RectF statusRect(0, remainRect.Y + remainRect.Height + 8, static_cast<REAL>(width), 52);
        DrawTextBlock(g, L"专注进行中", statusRect, *statusFont_, theme.secondaryText, StringAlignmentCenter, StringAlignmentCenter);

        RectF hintRect(0, static_cast<REAL>(height - 70), static_cast<REAL>(width), 28);
        DrawTextBlock(g, L"专注结束前会保持全屏和置顶", hintRect, *focusHintFont_, theme.mutedText, StringAlignmentCenter, StringAlignmentCenter);
    } else {
        RectF hintRect(0, dateRect.Y + 58, static_cast<REAL>(width), 34);
        DrawTextBlock(g, L"选择时长后开始", hintRect, *idleHintFont_, theme.mutedText, StringAlignmentCenter, StringAlignmentCenter);
    }

    auto appButton = std::find_if(buttons_.begin(), buttons_.end(), [](const UiButton& button) {
        return button.id >= kWhitelistButtonBaseId && button.id < kWhitelistButtonLimit;
    });
    if (appButton != buttons_.end()) {
        Font appTitleFont(&family, 18, FontStyleRegular, UnitPixel);
        RectF titleRect(
            static_cast<REAL>(appButton->rect.left),
            static_cast<REAL>(appButton->rect.top - 38),
            static_cast<REAL>(width - appButton->rect.left - 36),
            28.0f);
        DrawTextBlock(g, L"白名单程序", titleRect, appTitleFont, theme.secondaryText, StringAlignmentNear, StringAlignmentCenter);
    }

    for (const auto& button : buttons_) {
        if (panelOpen_ && !focusActive_ && button.id >= kPanelButtonBaseId) {
            continue;
        }
        DrawButton(g, button);
    }

    if (panelOpen_ && !focusActive_) {
        DrawPanel(g, rc);
        RECT viewport = PanelContentViewport();
        auto intersectsViewport = [](const RECT& rect, const RECT& viewport) {
            return rect.right > viewport.left &&
                rect.left < viewport.right &&
                rect.bottom > viewport.top &&
                rect.top < viewport.bottom;
        };

        auto drawClippedToViewport = [&](const UiButton& button) {
            GraphicsState state = g.Save();
            RectF clipRect(
                static_cast<REAL>(viewport.left),
                static_cast<REAL>(viewport.top),
                static_cast<REAL>(viewport.right - viewport.left),
                static_cast<REAL>(viewport.bottom - viewport.top));
            g.SetClip(clipRect);
            DrawButton(g, button);
            g.Restore(state);
        };

        for (const auto& button : buttons_) {
            if (button.id >= kPanelButtonBaseId) {
                bool scrollableScheduleButton = activePanelTab_ == kPanelTabScheduleId &&
                    button.id >= kScheduleStartHourMinusId &&
                    button.id < kScheduleDeleteButtonLimit;
                bool scrollableWhitelistButton = activePanelTab_ == kPanelTabWhitelistId &&
                    button.id >= kWhitelistAddFileId &&
                    button.id < kWhitelistDeleteButtonLimit;

                if (scrollableScheduleButton || scrollableWhitelistButton) {
                    if (!intersectsViewport(button.rect, viewport)) {
                        continue;
                    }
                    drawClippedToViewport(button);
                } else {
                    DrawButton(g, button);
                }
            }
        }
    }

    BitBlt(hdc, 0, 0, width, height, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(mem);
    EndPaint(hwnd_, &ps);
}

void FocusClockApp::DrawBackground(Graphics& g, const RECT& rc) {
    Color top = darkMode_ ? Color(255, 9, 13, 18) : Color(255, 241, 245, 249);
    Color bottom = darkMode_ ? Color(255, 18, 24, 31) : Color(255, 255, 255, 255);
    LinearGradientBrush brush(
        Point(0, rc.top),
        Point(0, rc.bottom),
        top,
        bottom);
    g.FillRectangle(
        &brush,
        static_cast<INT>(rc.left),
        static_cast<INT>(rc.top),
        static_cast<INT>(rc.right - rc.left),
        static_cast<INT>(rc.bottom - rc.top));
}

void FocusClockApp::DrawAnalogClock(Graphics& g, const RectF& bounds, const SYSTEMTIME& now) {
    Theme theme{
        darkMode_ ? Color(255, 11, 15, 20) : Color(255, 245, 247, 250),
        darkMode_ ? Color(255, 24, 30, 39) : Color(255, 255, 255, 255),
        darkMode_ ? Color(255, 242, 245, 247) : Color(255, 24, 28, 35),
        darkMode_ ? Color(255, 178, 187, 199) : Color(255, 79, 88, 103),
        darkMode_ ? Color(255, 119, 131, 146) : Color(255, 104, 116, 132),
        darkMode_ ? Color(255, 89, 180, 255) : Color(255, 0, 113, 188),
        darkMode_ ? Color(50, 89, 180, 255) : Color(32, 0, 113, 188),
        darkMode_ ? Color(255, 255, 115, 115) : Color(255, 190, 46, 46),
        darkMode_ ? Color(255, 54, 64, 78) : Color(255, 214, 220, 228)
    };

    PointF center(bounds.X + bounds.Width / 2.0f, bounds.Y + bounds.Height / 2.0f);
    float radius = bounds.Width / 2.0f;

    SolidBrush faceBrush(darkMode_ ? Color(235, 18, 24, 32) : Color(242, 255, 255, 255));
    Pen rimPen(theme.line, 2.0f);
    g.FillEllipse(&faceBrush, bounds);
    g.DrawEllipse(&rimPen, bounds);

    Pen tickPen(theme.secondaryText, 2.0f);
    Pen hourTickPen(theme.primaryText, 4.0f);
    for (int i = 0; i < 60; ++i) {
        double angle = (i / 60.0) * 2.0 * kPi - kPi / 2.0;
        float outer = radius - 16.0f;
        float inner = outer - ((i % 5 == 0) ? 16.0f : 8.0f);
        PointF p1(center.X + std::cos(angle) * inner, center.Y + std::sin(angle) * inner);
        PointF p2(center.X + std::cos(angle) * outer, center.Y + std::sin(angle) * outer);
        g.DrawLine(i % 5 == 0 ? &hourTickPen : &tickPen, p1, p2);
    }

    double second = now.wSecond + now.wMilliseconds / 1000.0;
    double minute = now.wMinute + second / 60.0;
    double hour = (now.wHour % 12) + minute / 60.0;

    DrawHand(g, center, radius * 0.50f, hour / 12.0 * 2.0 * kPi - kPi / 2.0, 7.0f, theme.primaryText);
    DrawHand(g, center, radius * 0.70f, minute / 60.0 * 2.0 * kPi - kPi / 2.0, 5.0f, theme.primaryText);
    DrawHand(g, center, radius * 0.78f, second / 60.0 * 2.0 * kPi - kPi / 2.0, 2.0f, theme.danger);

    SolidBrush centerBrush(theme.accent);
    g.FillEllipse(&centerBrush, center.X - 6.0f, center.Y - 6.0f, 12.0f, 12.0f);
}

void FocusClockApp::DrawHand(Graphics& g, const PointF& center, float radius, double angleRadians, float width, const Color& color) {
    Pen pen(color, width);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    PointF end(center.X + std::cos(angleRadians) * radius, center.Y + std::sin(angleRadians) * radius);
    g.DrawLine(&pen, center, end);
}

void FocusClockApp::DrawFocusProgressBar(Graphics& g, const RectF& bounds, const Theme& theme) const {
    if (bounds.Width <= 0.0f || bounds.Height <= 0.0f) {
        return;
    }

    auto addRoundedPath = [](GraphicsPath& path, const RectF& rect) {
        REAL radius = std::min(rect.Height / 2.0f, rect.Width / 2.0f);
        if (radius <= 0.0f) {
            return;
        }

        REAL diameter = radius * 2.0f;
        path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
        path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270, 90);
        path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0, 90);
        path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90, 90);
        path.CloseFigure();
    };

    long long total = std::max(1LL, focusTotalSeconds_);
    long long remaining = std::clamp(remainingSeconds_, 0LL, total);
    REAL progress = static_cast<REAL>(total - remaining) / static_cast<REAL>(total);

    GraphicsPath trackPath;
    addRoundedPath(trackPath, bounds);
    SolidBrush trackBrush(darkMode_ ? Color(120, 54, 64, 78) : Color(150, 214, 220, 228));
    Pen borderPen(darkMode_ ? Color(120, 89, 180, 255) : Color(95, 0, 113, 188), 1.0f);
    g.FillPath(&trackBrush, &trackPath);

    REAL fillWidth = bounds.Width * progress;
    if (fillWidth > 0.5f) {
        RectF fillRect(bounds.X, bounds.Y, fillWidth, bounds.Height);
        GraphicsPath fillPath;
        addRoundedPath(fillPath, fillRect);
        SolidBrush fillBrush(theme.accent);
        g.FillPath(&fillBrush, &fillPath);
    }

    g.DrawPath(&borderPen, &trackPath);
}

void FocusClockApp::DrawTextBlock(Graphics& g, const std::wstring& text, const RectF& rect, Font& font, const Color& color, StringAlignment align, StringAlignment lineAlign) {
    StringFormat format;
    format.SetAlignment(align);
    format.SetLineAlignment(lineAlign);
    format.SetTrimming(StringTrimmingEllipsisCharacter);
    SolidBrush brush(color);
    g.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, rect, &format, &brush);
}

void FocusClockApp::DrawPanel(Graphics& g, const RECT& rc) {
    Theme theme{
        darkMode_ ? Color(255, 11, 15, 20) : Color(255, 245, 247, 250),
        darkMode_ ? Color(255, 24, 30, 39) : Color(255, 255, 255, 255),
        darkMode_ ? Color(255, 242, 245, 247) : Color(255, 24, 28, 35),
        darkMode_ ? Color(255, 178, 187, 199) : Color(255, 79, 88, 103),
        darkMode_ ? Color(255, 119, 131, 146) : Color(255, 104, 116, 132),
        darkMode_ ? Color(255, 89, 180, 255) : Color(255, 0, 113, 188),
        darkMode_ ? Color(50, 89, 180, 255) : Color(32, 0, 113, 188),
        darkMode_ ? Color(255, 255, 115, 115) : Color(255, 190, 46, 46),
        darkMode_ ? Color(255, 54, 64, 78) : Color(255, 214, 220, 228)
    };

    SolidBrush scrim(darkMode_ ? Color(170, 0, 0, 0) : Color(125, 22, 29, 38));
    g.FillRectangle(
        &scrim,
        static_cast<INT>(rc.left),
        static_cast<INT>(rc.top),
        static_cast<INT>(rc.right - rc.left),
        static_cast<INT>(rc.bottom - rc.top));

    RectF panel(
        static_cast<REAL>(panelBounds_.left),
        static_cast<REAL>(panelBounds_.top),
        static_cast<REAL>(panelBounds_.right - panelBounds_.left),
        static_cast<REAL>(panelBounds_.bottom - panelBounds_.top));

    GraphicsPath path;
    const REAL radius = 10.0f;
    path.AddArc(panel.X, panel.Y, radius * 2, radius * 2, 180, 90);
    path.AddArc(panel.X + panel.Width - radius * 2, panel.Y, radius * 2, radius * 2, 270, 90);
    path.AddArc(panel.X + panel.Width - radius * 2, panel.Y + panel.Height - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(panel.X, panel.Y + panel.Height - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();

    SolidBrush panelBrush(theme.panel);
    Pen borderPen(theme.line, 1.5f);
    g.FillPath(&panelBrush, &path);
    g.DrawPath(&borderPen, &path);

    FontFamily& family = *uiFontFamily_;
    Font titleFont(&family, 26, FontStyleRegular, UnitPixel);
    Font hintFont(&family, 15, FontStyleRegular, UnitPixel);
    RectF titleRect(panel.X + 32.0f, panel.Y + 24.0f, panel.Width - 112.0f, 34.0f);
    DrawTextBlock(g, L"功能面板", titleRect, titleFont, theme.primaryText, StringAlignmentNear, StringAlignmentCenter);

    RectF hintRect(panel.X + 32.0f, panel.Y + 56.0f, panel.Width - 112.0f, 22.0f);
    DrawTextBlock(g, L"按 F6 或 Esc 关闭，专注时段不可打开", hintRect, hintFont, theme.mutedText, StringAlignmentNear, StringAlignmentCenter);

    Pen divider(theme.line, 1.0f);
    g.DrawLine(&divider, PointF(panel.X + 184.0f, panel.Y + 84.0f), PointF(panel.X + 184.0f, panel.Y + panel.Height - 32.0f));

    RectF contentRect(panel.X + 216.0f, panel.Y + 92.0f, panel.Width - 248.0f, panel.Height - 126.0f);
    if (activePanelTab_ == kPanelTabOverviewId) {
        DrawOverviewTab(g, contentRect, theme, family);
    } else if (activePanelTab_ == kPanelTabScheduleId) {
        GraphicsState state = g.Save();
        g.SetClip(contentRect);
        DrawScheduleTab(g, contentRect, theme, family);
        g.Restore(state);
    } else if (activePanelTab_ == kPanelTabWhitelistId) {
        GraphicsState state = g.Save();
        g.SetClip(contentRect);
        DrawWhitelistTab(g, contentRect, theme, family);
        g.Restore(state);
    } else if (activePanelTab_ == kPanelTabSettingsId) {
        DrawSettingsTab(g, contentRect, theme, family);
    } else if (activePanelTab_ == kPanelTabRerunId) {
        DrawRerunTab(g, contentRect, theme, family);
    } else if (activePanelTab_ == kPanelTabUpdateId) {
        DrawUpdateTab(g, contentRect, theme, family);
    }
}

void FocusClockApp::DrawOverviewTab(Graphics& g, const RectF& contentRect, const Theme& theme, FontFamily& family) {
    Font headingFont(&family, 24, FontStyleRegular, UnitPixel);
    Font bodyFont(&family, 17, FontStyleRegular, UnitPixel);
    Font smallFont(&family, 15, FontStyleRegular, UnitPixel);

    DrawTextBlock(g, L"概览", RectF(contentRect.X, contentRect.Y, contentRect.Width, 34.0f), headingFont, theme.primaryText, StringAlignmentNear, StringAlignmentCenter);
    DrawTextBlock(g, L"这里是可扩展标签页框架的占位页，后续功能可以通过新增标签定义接入。", RectF(contentRect.X, contentRect.Y + 48.0f, contentRect.Width, 60.0f), bodyFont, theme.secondaryText, StringAlignmentNear, StringAlignmentNear);

    std::wstring summary = L"当前默认时长 " + std::to_wstring(defaultFocusMinutes_) + L" 分钟，最大时长 " + std::to_wstring(maxFocusMinutes_) + L" 分钟。";
    DrawTextBlock(g, summary, RectF(contentRect.X, contentRect.Y + 132.0f, contentRect.Width, 28.0f), smallFont, theme.mutedText, StringAlignmentNear, StringAlignmentCenter);
}

void FocusClockApp::DrawSettingsTab(Graphics& g, const RectF& contentRect, const Theme& theme, FontFamily& family) {
    Font headingFont(&family, 24, FontStyleRegular, UnitPixel);
    Font labelFont(&family, 18, FontStyleRegular, UnitPixel);
    Font helpFont(&family, 15, FontStyleRegular, UnitPixel);

    DrawTextBlock(g, L"设置", RectF(contentRect.X, contentRect.Y, contentRect.Width, 34.0f), headingFont, theme.primaryText, StringAlignmentNear, StringAlignmentCenter);

    DrawTextBlock(g, L"最大专注时长", RectF(contentRect.X, contentRect.Y + 58.0f, contentRect.Width, 28.0f), labelFont, theme.primaryText, StringAlignmentNear, StringAlignmentCenter);
    DrawTextBlock(g, L"控制自定义时长可调到的上限，保存到 FocusClock.ini。", RectF(contentRect.X, contentRect.Y + 88.0f, contentRect.Width, 26.0f), helpFont, theme.mutedText, StringAlignmentNear, StringAlignmentCenter);

    DrawTextBlock(g, L"默认专注时长", RectF(contentRect.X, contentRect.Y + 202.0f, contentRect.Width, 28.0f), labelFont, theme.primaryText, StringAlignmentNear, StringAlignmentCenter);
    DrawTextBlock(g, L"启动时和恢复默认时使用的时长，会自动受最大时长限制。", RectF(contentRect.X, contentRect.Y + 232.0f, contentRect.Width, 26.0f), helpFont, theme.mutedText, StringAlignmentNear, StringAlignmentCenter);
}

void FocusClockApp::DrawScheduleTab(Graphics& g, const RectF& contentRect, const Theme& theme, FontFamily& family) {
    Font headingFont(&family, 24, FontStyleRegular, UnitPixel);
    Font labelFont(&family, 18, FontStyleRegular, UnitPixel);
    Font helpFont(&family, 15, FontStyleRegular, UnitPixel);
    Font rowFont(&family, 16, FontStyleRegular, UnitPixel);
    REAL y = -static_cast<REAL>(scheduleScrollOffset_);

    DrawTextBlock(g, L"计划专注", RectF(contentRect.X, contentRect.Y + y, contentRect.Width, 34.0f), headingFont, theme.primaryText, StringAlignmentNear, StringAlignmentCenter);
    DrawTextBlock(g, L"添加每天执行的计划，时间段不能重叠。到达计划时间后会自动开始专注。", RectF(contentRect.X, contentRect.Y + 38.0f + y, contentRect.Width, 42.0f), helpFont, theme.mutedText, StringAlignmentNear, StringAlignmentNear);

    DrawTextBlock(g, L"开始时间", RectF(contentRect.X, contentRect.Y + 96.0f + y, contentRect.Width, 28.0f), labelFont, theme.primaryText, StringAlignmentNear, StringAlignmentCenter);
    DrawTextBlock(g, L"结束时间", RectF(contentRect.X, contentRect.Y + 198.0f + y, contentRect.Width, 28.0f), labelFont, theme.primaryText, StringAlignmentNear, StringAlignmentCenter);

    Color messageColor = scheduleMessageIsError_ ? theme.danger : theme.secondaryText;
    std::wstring message = scheduleMessage_;
    if (message.empty()) {
        int duration = ScheduleDurationMinutes(ScheduleDraftStartMinute(), ScheduleDraftEndMinute());
        if (duration > 0 && duration <= maxFocusMinutes_) {
            message = L"当前计划时长 " + std::to_wstring(duration) + L" 分钟。";
        } else {
            message = L"结束时间必须晚于开始时间。";
            messageColor = theme.danger;
        }
    }
    DrawTextBlock(g, message, RectF(contentRect.X + 170.0f, contentRect.Y + 292.0f + y, contentRect.Width - 170.0f, 44.0f), helpFont, messageColor, StringAlignmentNear, StringAlignmentCenter);

    DrawTextBlock(g, L"已添加计划", RectF(contentRect.X, contentRect.Y + 374.0f + y, contentRect.Width, 28.0f), labelFont, theme.primaryText, StringAlignmentNear, StringAlignmentCenter);
    if (scheduledTasks_.empty()) {
        DrawTextBlock(g, L"暂无计划。", RectF(contentRect.X, contentRect.Y + 408.0f + y, contentRect.Width, 28.0f), helpFont, theme.mutedText, StringAlignmentNear, StringAlignmentCenter);
        return;
    }

    int listTop = static_cast<int>(contentRect.Y + 414.0f + y);
    int rowHeight = 34;
    for (int i = 0; i < static_cast<int>(scheduledTasks_.size()); ++i) {
        int rowTop = listTop + i * rowHeight;
        if (rowTop + 26 < contentRect.Y || rowTop > contentRect.Y + contentRect.Height) {
            continue;
        }
        const auto& task = scheduledTasks_[i];
        int duration = ScheduleDurationMinutes(task.startMinute, task.endMinute);
        std::wstring line = FormatMinuteOfDay(task.startMinute) + L" - " + FormatMinuteOfDay(task.endMinute) +
            L"  (" + std::to_wstring(duration) + L" 分钟)";
        DrawTextBlock(g, line, RectF(contentRect.X, static_cast<REAL>(rowTop), contentRect.Width - 90.0f, 26.0f), rowFont, theme.secondaryText, StringAlignmentNear, StringAlignmentCenter);
    }

    int maxScroll = ScheduleContentMaxScroll();
    if (maxScroll > 0) {
        REAL trackX = contentRect.X + contentRect.Width - 8.0f;
        REAL trackY = contentRect.Y;
        REAL trackHeight = contentRect.Height;
        SolidBrush trackBrush(darkMode_ ? Color(90, 68, 78, 94) : Color(90, 184, 194, 207));
        SolidBrush thumbBrush(theme.accent);
        g.FillRectangle(&trackBrush, trackX, trackY, 4.0f, trackHeight);

        REAL thumbHeight = std::max(40.0f, trackHeight * (trackHeight / (trackHeight + maxScroll)));
        REAL thumbY = trackY + (trackHeight - thumbHeight) * (static_cast<REAL>(scheduleScrollOffset_) / maxScroll);
        g.FillRectangle(&thumbBrush, trackX, thumbY, 4.0f, thumbHeight);
    }
}

void FocusClockApp::DrawRerunTab(Graphics& g, const RectF& contentRect, const Theme& theme, FontFamily& family) {
    Font headingFont(&family, 24, FontStyleRegular, UnitPixel);
    Font bodyFont(&family, 17, FontStyleRegular, UnitPixel);
    Font helpFont(&family, 15, FontStyleRegular, UnitPixel);

    DrawTextBlock(g, L"防重启", RectF(contentRect.X, contentRect.Y, contentRect.Width, 34.0f), headingFont, theme.primaryText, StringAlignmentNear, StringAlignmentCenter);
    std::wstring status = rerunTaskMessage_.empty() ? L"状态：等待添加" : rerunTaskMessage_;
    Color statusColor = rerunTaskMessageIsError_ ? theme.danger : theme.secondaryText;
    DrawTextBlock(g, status, RectF(contentRect.X, contentRect.Y + 164.0f, contentRect.Width, 44.0f), helpFont, statusColor, StringAlignmentNear, StringAlignmentCenter);
    return;

    DrawTextBlock(g, L"防重启", RectF(contentRect.X, contentRect.Y, contentRect.Width, 34.0f), headingFont, theme.primaryText, StringAlignmentNear, StringAlignmentCenter);
    DrawTextBlock(
        g,
        L"添加一个当前用户登录时运行的计划任务。它会用 -rerun 参数启动 FocusClock，并只在未完成的专注时段内恢复窗口。",
        RectF(contentRect.X, contentRect.Y + 48.0f, contentRect.Width, 72.0f),
        bodyFont,
        theme.secondaryText,
        StringAlignmentNear,
        StringAlignmentNear);
    DrawTextBlock(
        g,
        L"如果电脑在专注期间重启，登录后会根据 FocusClock.ini 的 FocusSession 记录继续剩余时间；不在时段内则自动退出。",
        RectF(contentRect.X, contentRect.Y + 132.0f, contentRect.Width, 64.0f),
        helpFont,
        theme.mutedText,
        StringAlignmentNear,
        StringAlignmentNear);

    if (!rerunTaskMessage_.empty()) {
        Color messageColor = rerunTaskMessageIsError_ ? theme.danger : theme.secondaryText;
        DrawTextBlock(g, rerunTaskMessage_, RectF(contentRect.X, contentRect.Y + 286.0f, contentRect.Width, 44.0f), helpFont, messageColor, StringAlignmentNear, StringAlignmentCenter);
    }
}

void FocusClockApp::DrawWhitelistTab(Graphics& g, const RectF& contentRect, const Theme& theme, FontFamily& family) {
    Font headingFont(&family, 24, FontStyleRegular, UnitPixel);
    Font bodyFont(&family, 16, FontStyleRegular, UnitPixel);
    Font helpFont(&family, 15, FontStyleRegular, UnitPixel);
    Font rowFont(&family, 15, FontStyleRegular, UnitPixel);
    REAL y = -static_cast<REAL>(whitelistScrollOffset_);

    DrawTextBlock(g, L"白名单管理", RectF(contentRect.X, contentRect.Y + y, contentRect.Width, 34.0f), headingFont, theme.primaryText, StringAlignmentNear, StringAlignmentCenter);

    Color messageColor = whitelistMessageIsError_ ? theme.danger : theme.secondaryText;
    std::wstring message = whitelistMessage_.empty()
        ? (L"当前 " + std::to_wstring(whitelistEntries_.size()) + L" 个白名单程序。")
        : whitelistMessage_;
    DrawTextBlock(g, message, RectF(contentRect.X, contentRect.Y + 158.0f + y, contentRect.Width, 26.0f), helpFont, messageColor, StringAlignmentNear, StringAlignmentCenter);

    DrawTextBlock(g, L"已添加程序", RectF(contentRect.X, contentRect.Y + 198.0f + y, contentRect.Width, 28.0f), bodyFont, theme.primaryText, StringAlignmentNear, StringAlignmentCenter);
    if (whitelistEntries_.empty()) {
        DrawTextBlock(g, L"暂无白名单程序。", RectF(contentRect.X, contentRect.Y + 238.0f + y, contentRect.Width, 28.0f), helpFont, theme.mutedText, StringAlignmentNear, StringAlignmentCenter);
        return;
    }

    int listTop = static_cast<int>(contentRect.Y + 244.0f + y);
    int rowHeight = 36;
    RECT viewport = PanelContentViewport();
    int count = static_cast<int>(whitelistEntries_.size());
    for (int i = 0; i < count; ++i) {
        int top = listTop + i * rowHeight;
        if (top + 26 < viewport.top || top > viewport.bottom) {
            continue;
        }
        const auto& entry = whitelistEntries_[i];
        std::wstring line = entry.label;
        if (!entry.exeName.empty() && entry.exeName != ToLower(entry.label)) {
            line += L"  -  " + entry.exeName;
        }
        DrawTextBlock(g, line, RectF(contentRect.X, static_cast<REAL>(top), contentRect.Width - 100.0f, 26.0f), rowFont, theme.secondaryText, StringAlignmentNear, StringAlignmentCenter);
    }

    if (static_cast<int>(whitelistEntries_.size()) > count) {
        DrawTextBlock(
            g,
            L"还有 " + std::to_wstring(static_cast<int>(whitelistEntries_.size()) - count) + L" 项未显示，可直接编辑 Whitelist.txt 管理更多项目。",
            RectF(contentRect.X, contentRect.Y + contentRect.Height - 26.0f + y, contentRect.Width, 24.0f),
            helpFont,
            theme.mutedText,
            StringAlignmentNear,
            StringAlignmentCenter);
    }

    int maxScroll = WhitelistContentMaxScroll();
    if (maxScroll > 0) {
        REAL trackX = contentRect.X + contentRect.Width - 8.0f;
        REAL trackY = contentRect.Y;
        REAL trackHeight = contentRect.Height;
        SolidBrush trackBrush(darkMode_ ? Color(90, 68, 78, 94) : Color(90, 184, 194, 207));
        SolidBrush thumbBrush(theme.accent);
        g.FillRectangle(&trackBrush, trackX, trackY, 4.0f, trackHeight);

        REAL thumbHeight = std::max(40.0f, trackHeight * (trackHeight / (trackHeight + maxScroll)));
        REAL thumbY = trackY + (trackHeight - thumbHeight) * (static_cast<REAL>(whitelistScrollOffset_) / maxScroll);
        g.FillRectangle(&thumbBrush, trackX, thumbY, 4.0f, thumbHeight);
    }
}

void FocusClockApp::DrawUpdateTab(Graphics& g, const RectF& contentRect, const Theme& theme, FontFamily& family) {
    Font headingFont(&family, 24, FontStyleRegular, UnitPixel);
    Font helpFont(&family, 15, FontStyleRegular, UnitPixel);

    DrawTextBlock(g, L"更新", RectF(contentRect.X, contentRect.Y, contentRect.Width, 34.0f), headingFont, theme.primaryText, StringAlignmentNear, StringAlignmentCenter);

    RectF messageRect(contentRect.X, contentRect.Y + 134.0f, contentRect.Width, contentRect.Height - 146.0f);
    SolidBrush boxBrush(darkMode_ ? Color(120, 18, 24, 32) : Color(135, 248, 250, 252));
    Pen boxPen(theme.line, 1.0f);
    g.FillRectangle(&boxBrush, messageRect);
    g.DrawRectangle(&boxPen, messageRect);

    Color messageColor = updateMessageIsError_ ? theme.danger : theme.secondaryText;
    DrawTextBlock(
        g,
        updateMessage_,
        RectF(messageRect.X + 14.0f, messageRect.Y + 12.0f, messageRect.Width - 28.0f, messageRect.Height - 24.0f),
        helpFont,
        messageColor,
        StringAlignmentNear,
        StringAlignmentNear);
}

void FocusClockApp::DrawButton(Graphics& g, const UiButton& button) {
    Theme theme{
        darkMode_ ? Color(255, 11, 15, 20) : Color(255, 245, 247, 250),
        darkMode_ ? Color(255, 24, 30, 39) : Color(255, 255, 255, 255),
        darkMode_ ? Color(255, 242, 245, 247) : Color(255, 24, 28, 35),
        darkMode_ ? Color(255, 178, 187, 199) : Color(255, 79, 88, 103),
        darkMode_ ? Color(255, 119, 131, 146) : Color(255, 104, 116, 132),
        darkMode_ ? Color(255, 89, 180, 255) : Color(255, 0, 113, 188),
        darkMode_ ? Color(50, 89, 180, 255) : Color(32, 0, 113, 188),
        darkMode_ ? Color(255, 255, 115, 115) : Color(255, 190, 46, 46),
        darkMode_ ? Color(255, 54, 64, 78) : Color(255, 214, 220, 228)
    };

    RectF r(
        static_cast<REAL>(button.rect.left),
        static_cast<REAL>(button.rect.top),
        static_cast<REAL>(button.rect.right - button.rect.left),
        static_cast<REAL>(button.rect.bottom - button.rect.top));

    Color fill = theme.panel;
    Color border = theme.line;
    Color text = theme.primaryText;

    if (button.primary) {
        fill = theme.accent;
        border = theme.accent;
        text = Color(255, 255, 255, 255);
    } else if (button.selected) {
        fill = theme.accentSoft;
        border = theme.accent;
        text = theme.primaryText;
    } else if (button.danger) {
        text = theme.danger;
        border = Color(120, theme.danger.GetR(), theme.danger.GetG(), theme.danger.GetB());
    }

    if (!button.enabled) {
        text = theme.secondaryText;
        border = theme.line;
        if (!button.selected) {
            fill = darkMode_ ? Color(160, 24, 30, 39) : Color(170, 245, 247, 250);
        }
    }

    GraphicsPath path;
    const REAL radius = 8.0f;
    path.AddArc(r.X, r.Y, radius * 2, radius * 2, 180, 90);
    path.AddArc(r.X + r.Width - radius * 2, r.Y, radius * 2, radius * 2, 270, 90);
    path.AddArc(r.X + r.Width - radius * 2, r.Y + r.Height - radius * 2, radius * 2, radius * 2, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - radius * 2, radius * 2, radius * 2, 90, 90);
    path.CloseFigure();

    SolidBrush fillBrush(fill);
    Pen borderPen(border, 1.5f);
    g.FillPath(&fillBrush, &path);
    g.DrawPath(&borderPen, &path);

    if (button.iconOnly) {
        DrawButtonIcon(g, button, r);
        return;
    }

    FontFamily& family = *uiFontFamily_;
    int buttonHeight = static_cast<int>(button.rect.bottom - button.rect.top);
    REAL fontSize = static_cast<REAL>(std::max(18, std::min(24, buttonHeight / 3)));
    if (button.id >= kScheduleDeleteButtonBaseId && button.id < kScheduleDeleteButtonLimit) {
        fontSize = 14.0f;
    } else if (button.id >= kWhitelistDeleteButtonBaseId && button.id < kWhitelistDeleteButtonLimit) {
        fontSize = 12.5f;
    } else if (button.id == kUpdateNowButtonId) {
        fontSize = 17.0f;
    }
    Font font(&family, fontSize, FontStyleRegular, UnitPixel);
    DrawTextBlock(g, button.label, r, font, text, StringAlignmentCenter, StringAlignmentCenter);
}

void FocusClockApp::DrawButtonIcon(Graphics& g, const UiButton& button, const RectF& bounds) {
    if (!button.icon) {
        return;
    }

    int iconSize = static_cast<int>(std::min(bounds.Width, bounds.Height) * 0.62f);
    iconSize = std::max(28, std::min(iconSize, 48));
    int x = static_cast<int>(bounds.X + (bounds.Width - iconSize) / 2.0f);
    int y = static_cast<int>(bounds.Y + (bounds.Height - iconSize) / 2.0f);

    g.Flush();
    HDC hdc = g.GetHDC();
    DrawIconEx(hdc, x, y, button.icon, iconSize, iconSize, 0, nullptr, DI_NORMAL);
    g.ReleaseHDC(hdc);
}

} // namespace focus_clock
