#pragma once 
#include <memory>  
#include "NotifyIcon/TrayIcon.h"
#include "Core/Theme.h"
namespace YuMediaPlayer
{
	class MainWindow
	{

	public:
		MainWindow();
		~MainWindow();
		bool Initialize(const wchar_t* title, int width, int height);
		void Run();

	private:
		static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		LRESULT EventProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		bool    InitWindow(const wchar_t* title, int width, int height);
		/*bool    InitD3D11(int width, int height);
		bool    InitDocument(int width, int height);*/

		//void DocumentInput();
		//// ── 渲染 ──────────────────────────────────────────────
		//void RenderFrame();

	private:
		// 窗口
		HWND						  m_hwnd;
		Theme						  m_theme;
		//// ── D3D11 层（硬件资源）──────────────────────────────
		//std::unique_ptr<Device>       m_device;
		//std::unique_ptr<SwapChain>    m_swapChain;
		//std::unique_ptr<IRenderer>    m_renderer;

		//DocumentManager               m_docManager;
		//ViewportInputAdapter          m_viewportInputAdapter;
		//UIManager                     m_uiManager;

		TrayIcon                      m_trayIcon;
	};
}
