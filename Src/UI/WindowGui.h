#pragma once 
#include <memory>  
#include <windows.h>
#include <windowsx.h>

namespace YuMediaPlayer
{
	class WindowGUI
	{

	public:
		WindowGUI();
		~WindowGUI();
		bool InitWindow(const wchar_t* title, int width, int height);
		void Run();
		void ShowWindowGUI();
		void HideWindowGUI();
		HWND GetHWND() const;
		void DrawButton(HDC hdc, const RECT& rect, bool hovered, int btnType);
		void UpdateButtonRects(int width);
		void UpdateWindowRegion();
		void SetCornerRadius(int radius);
		int GetCornerRadius() const; 
		void ApplyLegacyCornerRadius();
		// 告诉主窗口"迷你窗口是哪个"，关闭主窗口时用它把迷你窗口恢复显示
		void SetMiniWindow(HWND miniHwnd);
		// 关闭主窗口：隐藏主窗口，并把迷你窗口重新显示出来
		void CloseToMini();
	private:
		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		LRESULT EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	private:
		// 窗口
		HWND						  m_hwnd;
		//Theme						  m_theme;
		
		int m_hoverButton;	
		bool m_isMaximized;
		// 按钮数量是 5（Close/Maximize/Minimize/Settings/Updata），WindowGUI.CPP 里所有循环都按 5 个访问。
		// 之前这里写成 [4]，构造函数和 UpdateButtonRects 写 m_buttonRects[4] 时会越界覆盖
		// m_cornerRadius 并写到对象外面，造成堆损坏（一显示主窗口就可能崩溃/卡死）。
		RECT m_buttonRects[5];
		int m_cornerRadius;
		HWND m_miniHwnd = nullptr;   // 迷你窗口（MainWindow）的句柄，不拥有它
	};
}
