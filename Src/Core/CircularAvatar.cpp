#include "Core/CircularAvatar.h"
#include <algorithm>

using namespace Gdiplus;

ULONG_PTR CircularAvatar::s_gdiplusToken = 0;

CircularAvatar::CircularAvatar() {}

CircularAvatar::~CircularAvatar()
{
    if (m_pImage)
    {
        delete m_pImage;
        m_pImage = nullptr;
    }
}

void CircularAvatar::InitGdiplus()
{
    GdiplusStartupInput gdiplusStartupInput;
    GdiplusStartup(&s_gdiplusToken, &gdiplusStartupInput, nullptr);
}

void CircularAvatar::ShutdownGdiplus()
{
    GdiplusShutdown(s_gdiplusToken);
}

bool CircularAvatar::LoadImageFromFile(const std::wstring& path)
{
    if (m_pImage)
    {
        delete m_pImage;
        m_pImage = nullptr;
    }

    Bitmap* bmp = new Bitmap(path.c_str());
    if (bmp->GetLastStatus() != Ok)
    {
        delete bmp;
        return false;
    }

    m_pImage = bmp;
    return true;
}

void CircularAvatar::ClearImage()
{
    if (m_pImage)
    {
        delete m_pImage;
        m_pImage = nullptr;
    }
}

void CircularAvatar::SetProgress(float progress01)
{
    m_progress = std::clamp(progress01, 0.0f, 1.0f);
}

void CircularAvatar::SetSkin(const Skin& skin)
{
    m_skin = skin;
}

void CircularAvatar::SetOnClick(ClickCallback cb, void* userData)
{
    m_onClick = cb;
    m_onClickUserData = userData;
}

bool CircularAvatar::HitTest(const Gdiplus::RectF& rect, POINT pt) const
{
    float cx = rect.X + rect.Width / 2.0f;
    float cy = rect.Y + rect.Height / 2.0f;
    float r = rect.Width / 2.0f;
    float dx = (float)pt.x - cx;
    float dy = (float)pt.y - cy;
    return (dx * dx + dy * dy) <= (r * r);
}

void CircularAvatar::Draw(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect) const
{
    float w = rect.Width;
    float h = rect.Height;
    if (w <= 0 || h <= 0) return;

    GraphicsContainer container = graphics.BeginContainer();
    graphics.TranslateTransform(rect.X, rect.Y);

    const float ringThickness = m_skin.ringThickness;
    const float ringGap = m_skin.ringGap;

    // 1
    {
        float inset = ringThickness / 2.0f;
        RectF arcRect(inset, inset, w - ringThickness, h - ringThickness);

        Pen trackPen(m_skin.trackColor, ringThickness);
        trackPen.SetStartCap(LineCapRound);
        trackPen.SetEndCap(LineCapRound);
        graphics.DrawArc(&trackPen, arcRect, 0.0f, 360.0f);

        if (m_progress > 0.0f)
        {
            Pen progressPen(m_skin.progressColor, ringThickness);
            progressPen.SetStartCap(LineCapRound);
            progressPen.SetEndCap(LineCapRound);
            float sweep = 360.0f * m_progress;
            graphics.DrawArc(&progressPen, arcRect, -90.0f, sweep);
        }
    }

    //  2
    {
        float inset = ringThickness + ringGap;
        float innerW = w - inset * 2.0f;
        float innerH = h - inset * 2.0f;
        if (innerW > 0 && innerH > 0)
        {
            GraphicsPath path;
            path.AddEllipse(inset, inset, innerW, innerH);
            Region clipRegion(&path);

            Region oldClip;
            graphics.GetClip(&oldClip);
            graphics.SetClip(&clipRegion);

            if (m_pImage)
            {
                UINT imgW = m_pImage->GetWidth();
                UINT imgH = m_pImage->GetHeight();
                if (imgW > 0 && imgH > 0)
                {
                    double scale = (std::max)((double)innerW / imgW, (double)innerH / imgH);
                    double drawW = imgW * scale;
                    double drawH = imgH * scale;
                    double drawX = inset + (innerW - drawW) / 2.0;
                    double drawY = inset + (innerH - drawH) / 2.0;
                    graphics.DrawImage(m_pImage, (REAL)drawX, (REAL)drawY, (REAL)drawW, (REAL)drawH);
                }
            }
            else if (m_skin.useVinylPlaceholder)
            {
                SolidBrush discBrush(m_skin.vinylDiscColor);
                graphics.FillEllipse(&discBrush, inset, inset, innerW, innerH);

                float cx = inset + innerW / 2.0f;
                float cy = inset + innerH / 2.0f;
                float maxR = (std::min)(innerW, innerH) / 2.0f;

                Pen groovePen(m_skin.vinylGrooveColor, 1.0f);
                const float grooveRatios[] = { 0.55f, 0.72f, 0.88f };
                for (float ratio : grooveRatios)
                {
                    float r = maxR * ratio;
                    graphics.DrawEllipse(&groovePen, cx - r, cy - r, r * 2.0f, r * 2.0f);
                }

                float ringR = maxR * 0.20f;
                float dotR = maxR * 0.11f;
                SolidBrush ringBrush(m_skin.vinylDotRing);
                graphics.FillEllipse(&ringBrush, cx - ringR, cy - ringR, ringR * 2.0f, ringR * 2.0f);
                SolidBrush dotBrush(m_skin.vinylDotColor);
                graphics.FillEllipse(&dotBrush, cx - dotR, cy - dotR, dotR * 2.0f, dotR * 2.0f);
            }
            else
            {
                SolidBrush placeholder(m_skin.placeholderColor);
                graphics.FillEllipse(&placeholder, inset, inset, innerW, innerH);
            }

            if (m_skin.borderColor.GetA() > 0)
            {
                Pen borderPen(m_skin.borderColor, 1.0f);
                graphics.SetClip(&oldClip);
                graphics.DrawEllipse(&borderPen, inset, inset, innerW, innerH);
            }

            graphics.SetClip(&oldClip);
        }
    }

    graphics.EndContainer(container);
}
