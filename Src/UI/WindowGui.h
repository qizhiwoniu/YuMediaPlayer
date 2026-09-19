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
		bool Initialize(const wchar_t* title, int width, int height);
		void Run();
		void ShowWindowGUI();
		void HideWindowGUI();
	private:
		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		LRESULT EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		bool    InitWindow(const wchar_t* title, int width, int height);
	private:
		// 窗口
		HWND						  m_hwnd;
		//Theme						  m_theme;
	};
}