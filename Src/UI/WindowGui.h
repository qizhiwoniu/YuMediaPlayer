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
	private:
		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		LRESULT EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	private:
		// 窗口
		HWND						  m_hwnd;
		//Theme						  m_theme;
		
		int m_hoverButton;	
		bool m_isMaximized;
		RECT m_buttonRects[4];
		int m_cornerRadius;
	};
}
