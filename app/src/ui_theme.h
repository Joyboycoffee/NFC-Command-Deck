#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#include <algorithm>
#include "action_executor.h"

using namespace Gdiplus;

namespace Theme {
    // Executive Dark Palette (Linear / Apple Pro / Obsidian Studio)
    inline Color BgMain()          { return Color(255, 9, 10, 15); }       // #090A0F - Deep Obsidian
    inline Color Surface()         { return Color(255, 15, 19, 29); }      // #0F131D - Matte Titanium Card
    inline Color SurfaceHover()    { return Color(255, 23, 29, 44); }      // #171D2C - Elevated / Hover Card
    inline Color SurfaceInput()    { return Color(255, 20, 26, 40); }      // #141A28 - Input fields
    inline Color Border()          { return Color(255, 30, 38, 56); }      // #1E2638 - Subtle 1px border
    inline Color BorderLight()     { return Color(255, 52, 65, 94); }      // #34415E - Highlight border
    
    // Accents
    inline Color Cyan()            { return Color(255, 14, 165, 233); }    // #0EA5E9 - Electric Sky
    inline Color CyanLight()       { return Color(255, 56, 189, 248); }    // #38BDF8 - Ice Cyan
    inline Color CyanGlow()        { return Color(40, 14, 165, 233); }
    inline Color Emerald()         { return Color(255, 16, 185, 129); }    // #10B981 - Studio Mint
    inline Color EmeraldGlow()     { return Color(40, 16, 185, 129); }
    inline Color Amber()           { return Color(255, 245, 158, 11); }    // #F59E0B - Warm Amber
    inline Color AmberGlow()       { return Color(40, 245, 158, 11); }
    inline Color Purple()          { return Color(255, 139, 92, 246); }    // #8B5CF6 - Indigo
    inline Color Rose()            { return Color(255, 244, 63, 94); }     // #F43F5E - Coral Rose
    inline Color RoseGlow()        { return Color(40, 244, 63, 94); }
    
    // Typography
    inline Color TextMain()        { return Color(255, 248, 250, 252); }   // #F8FAFC - Crisp White
    inline Color TextMuted()       { return Color(255, 148, 163, 184); }   // #94A3B8 - Slate
    inline Color TextDim()         { return Color(255, 100, 116, 139); }   // #64748B - Muted Dim

    inline BYTE ClampByte(int v) {
        if (v < 0) return 0;
        if (v > 255) return 255;
        return (BYTE)v;
    }

    inline void CreateRoundRectangle(GraphicsPath& path, RectF rect, float radius) {
        float d = radius * 2.0f;
        if (d > rect.Width) d = rect.Width;
        if (d > rect.Height) d = rect.Height;
        path.AddArc(rect.X, rect.Y, d, d, 180, 90);
        path.AddArc(rect.X + rect.Width - d, rect.Y, d, d, 270, 90);
        path.AddArc(rect.X + rect.Width - d, rect.Y + rect.Height - d, d, d, 0, 90);
        path.AddArc(rect.X, rect.Y + rect.Height - d, d, d, 90, 90);
        path.CloseFigure();
    }

    // Modern chamfered card with subtle top specular edge
    inline void DrawCard(Graphics& g, int x, int y, int w, int h, int radius, Color bg, Color border) {
        GraphicsPath path;
        CreateRoundRectangle(path, RectF((float)x, (float)y, (float)w, (float)h), (float)radius);

        SolidBrush brush(bg);
        g.FillPath(&brush, &path);

        Pen pen(border, 1.0f);
        g.DrawPath(&pen, &path);

        // Top specular highlight (subtle 1px glass effect)
        Pen topHighlight(Color(25, 255, 255, 255), 1.0f);
        g.DrawLine(&topHighlight, x + radius, y + 1, x + w - radius, y + 1);
    }

    // Professional Studio Button
    inline void DrawButton(Graphics& g, int x, int y, int w, int h, const std::wstring& text, 
                           Color bg, Color border, Color textCol, bool hovered, bool isPrimary = false) {
        GraphicsPath path;
        CreateRoundRectangle(path, RectF((float)x, (float)y, (float)w, (float)h), 6.0f);

        Color actualBg;
        if (isPrimary) {
            actualBg = hovered ? Color(255, 56, 189, 248) : Cyan();
        } else {
            actualBg = hovered ? Color(bg.GetA(), ClampByte(bg.GetR() + 20), ClampByte(bg.GetG() + 24), ClampByte(bg.GetB() + 32)) : bg;
        }

        SolidBrush brush(actualBg);
        g.FillPath(&brush, &path);

        Pen pen(hovered ? (isPrimary ? CyanLight() : BorderLight()) : border, 1.0f);
        g.DrawPath(&pen, &path);

        FontFamily fontFamily(L"Segoe UI");
        Font font(&fontFamily, 9.0f, FontStyleBold, UnitPoint);
        SolidBrush textBrush(isPrimary ? Color(255, 9, 10, 15) : textCol);

        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);

        RectF layoutRect((float)x, (float)y, (float)w, (float)h);
        g.DrawString(text.c_str(), -1, &font, layoutRect, &format, &textBrush);
    }

    // Modern Hardware Status Badge with Glowing Micro-Dot
    inline int DrawStatusBadge(Graphics& g, int x, int y, const std::wstring& label, Color color) {
        FontFamily fontFamily(L"Segoe UI");
        Font font(&fontFamily, 8.5f, FontStyleBold, UnitPoint);
        
        RectF layoutRect(0, 0, 1000, 50);
        RectF bounds;
        g.MeasureString(label.c_str(), -1, &font, layoutRect, &bounds);

        float badgeW = bounds.Width + 28.0f;
        float badgeH = 24.0f;

        GraphicsPath path;
        CreateRoundRectangle(path, RectF((float)x, (float)y, badgeW, badgeH), 12.0f);

        SolidBrush bgBrush(Color(25, color.GetR(), color.GetG(), color.GetB()));
        g.FillPath(&bgBrush, &path);

        Pen borderPen(Color(120, color.GetR(), color.GetG(), color.GetB()), 1.0f);
        g.DrawPath(&borderPen, &path);

        // Glowing LED Micro-Dot
        SolidBrush glowBrush(Color(60, color.GetR(), color.GetG(), color.GetB()));
        g.FillEllipse(&glowBrush, x + 7, y + 6, 11, 11);

        SolidBrush dotBrush(color);
        g.FillEllipse(&dotBrush, x + 9, y + 8, 7, 7);

        SolidBrush textBrush(color);
        RectF textRect((float)(x + 21), (float)y, bounds.Width + 4, badgeH);
        StringFormat format;
        format.SetAlignment(StringAlignmentNear);
        format.SetLineAlignment(StringAlignmentCenter);
        g.DrawString(label.c_str(), -1, &font, textRect, &format, &textBrush);

        return (int)badgeW;
    }

    // Sleek Toggle Switch (iOS / Modern UI pill with sliding knob)
    inline void DrawToggleSwitch(Graphics& g, int x, int y, bool enabled) {
        float swW = 38.0f;
        float swH = 20.0f;

        GraphicsPath path;
        CreateRoundRectangle(path, RectF((float)x, (float)y, swW, swH), 10.0f);

        Color pillBg = enabled ? Emerald() : Color(255, 30, 38, 56);
        SolidBrush bgBrush(pillBg);
        g.FillPath(&bgBrush, &path);

        Pen borderPen(enabled ? Emerald() : BorderLight(), 1.0f);
        g.DrawPath(&borderPen, &path);

        // Knob circle
        float knobX = enabled ? (x + swW - 18.0f) : (x + 2.0f);
        float knobY = y + 2.0f;
        SolidBrush knobBrush(Color(255, 255, 255, 255));
        g.FillEllipse(&knobBrush, knobX, knobY, 16.0f, 16.0f);
    }

    // Professional Vector NFC Wave Emblem (Anti-aliased)
    inline void DrawNfcEmblem(Graphics& g, int x, int y, int size) {
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        float s = (float)size;
        float cx = x + s * 0.55f;
        float cy = y + s * 0.50f;

        Pen pen1(Cyan(), s * 0.08f);
        Pen pen2(CyanLight(), s * 0.08f);
        Pen pen3(Color(255, 224, 242, 254), s * 0.08f);

        float r1 = s * 0.38f;
        float r2 = s * 0.25f;
        float r3 = s * 0.12f;

        g.DrawArc(&pen1, cx - r1, cy - r1, r1 * 2, r1 * 2, 130, 100);
        g.DrawArc(&pen2, cx - r2, cy - r2, r2 * 2, r2 * 2, 130, 100);
        g.DrawArc(&pen3, cx - r3, cy - r3, r3 * 2, r3 * 2, 130, 100);

        SolidBrush dotBrush(Color(255, 255, 255, 255));
        float dotR = s * 0.05f;
        g.FillEllipse(&dotBrush, cx - r3 - dotR, cy - dotR, dotR * 2, dotR * 2);
    }

    // Precision Action Badge with Icon
    inline void DrawActionBadge(Graphics& g, int x, int y, ActionType type) {
        int w = 58;
        int h = 22;
        GraphicsPath path;
        CreateRoundRectangle(path, RectF((float)x, (float)y, (float)w, (float)h), 4.0f);

        Color badgeColor;
        std::wstring label;

        switch (type) {
            case ACTION_APP:     badgeColor = Cyan();    label = L"APP";    break;
            case ACTION_STEAM:   badgeColor = Purple();  label = L"STEAM";  break;
            case ACTION_SPOTIFY: badgeColor = Emerald(); label = L"SPOTIFY"; break;
            case ACTION_MEDIA:   badgeColor = Emerald(); label = L"MEDIA";  break;
            case ACTION_HOTKEY:  badgeColor = Amber();   label = L"KEY";    break;
            case ACTION_URL:     badgeColor = CyanLight(); label = L"URL";  break;
            case ACTION_SYSTEM:  badgeColor = Rose();    label = L"LOCK";   break;
            case ACTION_COMMAND: badgeColor = Purple();  label = L"CMD";    break;
            default:             badgeColor = TextMuted(); label = L"ACT";   break;
        }

        SolidBrush bgBrush(Color(30, badgeColor.GetR(), badgeColor.GetG(), badgeColor.GetB()));
        g.FillPath(&bgBrush, &path);

        Pen borderPen(badgeColor, 1.0f);
        g.DrawPath(&borderPen, &path);

        FontFamily fontFamily(L"Segoe UI");
        Font font(&fontFamily, 7.5f, FontStyleBold, UnitPoint);
        SolidBrush textBrush(badgeColor);

        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);

        RectF layoutRect((float)x, (float)y, (float)w, (float)h);
        g.DrawString(label.c_str(), -1, &font, layoutRect, &format, &textBrush);
    }

    inline HICON CreateAppLogoIcon() {
        // High resolution 48x48 dynamically generated icon
        int size = 48;
        Bitmap bmp(size, size, PixelFormat32bppARGB);
        Graphics g(&bmp);
        g.SetSmoothingMode(SmoothingModeAntiAlias);

        GraphicsPath path;
        CreateRoundRectangle(path, RectF(2.0f, 2.0f, 44.0f, 44.0f), 10.0f);
        SolidBrush bg(Color(255, 15, 19, 29));
        g.FillPath(&bg, &path);
        Pen border(Cyan(), 1.5f);
        g.DrawPath(&border, &path);

        DrawNfcEmblem(g, 6, 6, 36);

        HICON hIcon = NULL;
        bmp.GetHICON(&hIcon);
        return hIcon;
    }
}
