#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>
#pragma comment(lib, "gdiplus.lib")

// 圆形封面控件：中间显示专辑封面/头像，外圈是可自定义颜色的播放进度环。
//
// 注意：这个类不再是独立子窗口了。为了实现"头像可以探出卡片顶部、
// 探出的部分背后完全透明（露出桌面）"这种效果，整个 MainWindow 改成了
// 分层窗口（WS_EX_LAYERED + UpdateLayeredWindow），所有内容——卡片、
// 头像、文字——必须画在同一张带 Alpha 通道的位图上才能正确合成透明区域。
// 所以这里砍掉了原来的 Create()/WndProc/子窗口消息处理，只留下"画到
// 调用者给的 Graphics + 矩形区域里"这一个核心能力。
class CircularAvatar
{
public:
    // 皮肤配置：颜色都可以自定义，方便做换肤功能。
    // 相比之前去掉了 bgColor —— 现在头像直接画在透明或卡片背景上，
    // 不再需要用一个方形背景色去"补"子窗口四角了。
    struct Skin
    {
        Gdiplus::Color trackColor       = Gdiplus::Color(70, 70, 70);   // 进度环"未播放"部分的颜色（底色）
        Gdiplus::Color progressColor    = Gdiplus::Color(255, 205, 60); // 进度环"已播放"部分的颜色
        Gdiplus::Color placeholderColor = Gdiplus::Color(60, 60, 60);   // 没有封面图且不用黑胶样式时的纯色占位
        Gdiplus::Color borderColor      = Gdiplus::Color(255, 255, 255, 255); // 封面图外沿细描边颜色（alpha 可调，0=不画）
        float ringThickness = 3.0f; // 进度环粗细
        float ringGap       = 2.0f; // 进度环与封面图之间的间隙

        // ── 无封面时的默认占位样式：黑胶唱片 ───────────────────
        bool useVinylPlaceholder        = true;
        Gdiplus::Color vinylDiscColor   = Gdiplus::Color(18, 18, 18);
        Gdiplus::Color vinylGrooveColor = Gdiplus::Color(48, 48, 48);
        Gdiplus::Color vinylDotRing     = Gdiplus::Color(230, 230, 230);
        Gdiplus::Color vinylDotColor    = Gdiplus::Color(140, 170, 255);
    };

    CircularAvatar();
    ~CircularAvatar();

    // 从文件加载封面图片（支持 jpg/png/bmp/gif 等 GDI+ 原生支持的格式）
    bool LoadImageFromFile(const std::wstring& path);
    // 清空封面图，恢复占位圆
    void ClearImage();

    // 设置播放进度，范围 [0, 1]
    void SetProgress(float progress01);
    float GetProgress() const { return m_progress; }

    // 设置/获取皮肤
    void SetSkin(const Skin& skin);
    const Skin& GetSkin() const { return m_skin; }

    // 把头像画到 graphics 上的 rect 区域内（rect 应为正方形，直径=rect.Width）。
    // 只画圆形范围内的内容（进度环+封面/占位圆），rect 之外、圆形之外都不touch，
    // 调用者负责提前把背景（卡片/透明）画好。
    void Draw(Gdiplus::Graphics& graphics, const Gdiplus::RectF& rect) const;

    // 判断某个点（跟 rect 同一坐标系，通常是窗口客户区坐标）是否落在头像圆内，
    // 用于点击检测（MainWindow 在 WM_LBUTTONUP 里调用）
    bool HitTest(const Gdiplus::RectF& rect, POINT pt) const;

    typedef void (*ClickCallback)(void* userData);
    void SetOnClick(ClickCallback cb, void* userData = nullptr);
    ClickCallback GetOnClick() const { return m_onClick; }
    void* GetOnClickUserData() const { return m_onClickUserData; }

    // 整个程序生命周期内只需调用一次，建议在 WinMain 最开始调用
    static void InitGdiplus();
    // 建议在所有 CircularAvatar 对象都已销毁之后，程序退出前调用
    static void ShutdownGdiplus();

private:
    Gdiplus::Bitmap* m_pImage = nullptr;
    float m_progress = 0.0f;
    Skin  m_skin;

    ClickCallback m_onClick = nullptr;
    void* m_onClickUserData = nullptr;

    static ULONG_PTR s_gdiplusToken;
};
