#include "Core/CircularAvatar.h"
#include <algorithm>
#include <vector>

using namespace Gdiplus;

namespace
{
    // 取当前 exe 所在目录（末尾带 \），用于把相对路径解析成"exe 旁边"的路径，
    // 而不是依赖不确定的"当前工作目录"（尤其是 IDE 调试启动时工作目录经常不是 exe 目录）。
    std::wstring GetExeDir()
    {
        wchar_t buf[MAX_PATH] = {};
        DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
        if (len == 0 || len == MAX_PATH)
            return L"";

        std::wstring exePath(buf, len);
        size_t pos = exePath.find_last_of(L"\\/");
        if (pos == std::wstring::npos)
            return L"";

        return exePath.substr(0, pos + 1);
    
    }

    // 判断是否已经是绝对路径（"C:\..."、"\\server\share\..."、"\folder..."），
    // 绝对路径就不用再拼 exe 目录了。
    bool IsAbsoluteOrRootedPath(const std::wstring& path)
    {
        if (path.size() >= 2 && path[1] == L':')
            return true;
        if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
            return true;
        if (!path.empty() && path[0] == L'\\')
            return true;
        return false;
    }
    // 没有手动设置封面时，默认从 Assets\disk\ 下加载的图片文件名。
    // 想换成别的文件名/格式，改这一个常量就行。
    const wchar_t* const DEFAULT_COVER_FILENAME = L"周杰伦-七里香.png";
}

ULONG_PTR CircularAvatar::s_gdiplusToken = 0;

CircularAvatar::CircularAvatar()
{
    // 启动时尝试加载默认封面（Assets\disk\default.png）。
    // 加载失败（文件不存在等）也没关系，LoadImageFromFile 会保持 m_pImage 为空，
    // Draw() 那边该怎么画占位图（黑胶/纯色）还是怎么画，不受影响。
    LoadImageFromFile(DEFAULT_COVER_FILENAME);
}

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

    // 依次尝试：
    // 1. 调用者传进来的原始路径（绝对路径，或相对于"当前工作目录"的相对路径）
    // 2. exe 所在目录 + 传入路径（比如只传了 "cover.png"，去 exe 旁边找）
    // 3. exe 所在目录 + "Assets\disk\" + 传入路径（去 exe 旁边的 Assets\disk 子目录里找）
    // 只要有一个能成功加载（GDI+ 原生支持 png/jpg/bmp/gif 等），就用它。
    std::vector<std::wstring> candidates;
    candidates.push_back(path);

    if (!IsAbsoluteOrRootedPath(path))
    {
        std::wstring exeDir = GetExeDir();
        if (!exeDir.empty())
        {
            candidates.push_back(exeDir + path);
            candidates.push_back(exeDir + L"disk\\" + path);
        }
    }

    for (const std::wstring& candidate : candidates)
    {
        Bitmap* bmp = new Bitmap(candidate.c_str());
        if (bmp->GetLastStatus() == Ok)
        {
            m_pImage = bmp;
            return true;
        }
        delete bmp;
    }

    return false;
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
