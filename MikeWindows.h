#include <Windows.h>
#include <WindowsX.h>
#include "WindowsUndefines.h"
#include <stdexcept>
#include <string>
#include <sstream>

namespace MikeWindows
{
    // Inherit from noncopyable to prevent a class instance from being copied.  Same as boost::noncopyable.
    class noncopyable
    {
    protected:

        noncopyable() {}

    private:

        noncopyable(const noncopyable&);
        void operator= (const noncopyable&);
    };


    // A win32_error formats it's message using the Windows error string for the give error code
    class win32_error : public std::runtime_error
    {
    public:

        win32_error(const char * message, DWORD error_code = GetLastError())
            : std::runtime_error(format(message, error_code))
        {}

    private:

        std::string format(const char * message, DWORD error_code)
        {
            std::stringstream stream;
            stream << message << ": error " << error_code;

            char * description;
            if (FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                error_code,
                0,
                reinterpret_cast<LPSTR>(&description),
                0,
                nullptr))
            {
                size_t length = strlen(description);
                if (length >= 2 && 0 == strcmp(description + length - 2, "\r\n"))
                    description[length - 2] = '\0';

                stream << ": " << description;

                LocalFree(description);
            }

            return stream.str();
        }
    };


    // A Window.  The root of all evil.
    class Window : noncopyable
    {
    public:

        operator HWND ()
        {
            return m_window;
        }

    protected:

        Window()
            : m_window(nullptr)
        {
        }

        HWND m_window;
    };

    
    // An AppWindow is a window that is owned by the application.  It's main purpose is to dispatch window messages
    // to application-derived types
    template <typename T>
    class AppWindow : public Window
    {
    public:

#pragma warning(push)
#pragma warning(disable: 4458)  // declaration of 'foo' hides class member

        class WindowCreationParams
        {
        public:

            WindowCreationParams & with_caption(std::wstring caption)
            {
                this->caption = caption;
                return *this;
            }

        private:

            template<typename T> friend class AppWindow;

            std::wstring caption;
        };

#pragma warning(pop)

        AppWindow() : AppWindow(WindowCreationParams()) {}

        AppWindow(WindowCreationParams creation_params)
        {
            static_cast<T *>(this)->register_class(creation_params);
            static_cast<T *>(this)->create(creation_params);
        }

    protected:

        void register_class(WindowCreationParams creation_params)
        {
            std::wstring class_name = static_cast<T *>(this)->get_class_name().c_str();

            WNDCLASSEX wndclass = { 0 };
            wndclass.cbSize = sizeof(wndclass);
            wndclass.style = CS_DBLCLKS,
            wndclass.lpfnWndProc = WndProc;
            wndclass.hInstance = static_cast<T *>(this)->get_current_module();
            wndclass.hIcon = LoadIcon(NULL, IDI_APPLICATION);
            wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
            wndclass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            wndclass.lpszClassName = class_name.c_str();

            if (!RegisterClassEx(&wndclass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
                throw win32_error("RegisterClassEx");
        }

        void create(WindowCreationParams creation_params)
        {
            std::wstring class_name = static_cast<T *>(this)->get_class_name().c_str();

            m_window = CreateWindow(
                class_name.c_str(),
                creation_params.caption.c_str(),
                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_VISIBLE,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                CW_USEDEFAULT,
                NULL,
                NULL,
                static_cast<T *>(this)->get_current_module(),
                static_cast<T *>(this));

            if (!m_window)
                throw win32_error("CreateWindow");
        }

        std::wstring get_class_name()
        {
            return std::to_wstring(reinterpret_cast<uintptr_t>(&WndProc));
        }

        HMODULE get_current_module()
        {
            HMODULE module;
            ::GetModuleHandleEx(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCWSTR)&WndProc,
                &module);
            return module;
        }

        static LRESULT CALLBACK WndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == WM_NCCREATE)
            {
                void * param = reinterpret_cast<CREATESTRUCT *>(lParam)->lpCreateParams;
                reinterpret_cast<AppWindow *>(param)->m_window = window;
                ::SetWindowLongPtr(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(param));
            }

            T * this_ = reinterpret_cast<T *>(::GetWindowLongPtr(window, GWLP_USERDATA));

            if (this_)
            {
                switch (message)
                {
                    HANDLE_MSG(window, WM_DESTROY, this_->on_destroy);
                    HANDLE_MSG(window, WM_LBUTTONUP, this_->on_left_button_up);
                    HANDLE_MSG(window, WM_MOUSEMOVE, this_->on_mouse_move);
                    HANDLE_MSG(window, WM_NCHITTEST, this_->on_hittest);
                    HANDLE_MSG(window, WM_PAINT, this_->on_paint);
                    HANDLE_MSG(window, WM_SETCURSOR, this_->on_set_cursor);
                }
            }

            return this_->on_message(window, message, wParam, lParam);
        }

        //
        // Default message handler implementations
        //

        void on_destroy(HWND) {}
        UINT on_hittest(HWND hwnd, int x, int y) { return FORWARD_WM_NCHITTEST(hwnd, x, y, ::DefWindowProc); }
        void on_left_button_up(HWND, int x, int y, UINT flags) {}
        void on_mouse_move(HWND hwnd, int x, int y, UINT flags) {}
        void on_paint(HWND) {}
        BOOL on_set_cursor(HWND hwnd, HWND hwndCursor, UINT codeHitTest, UINT msg) { return FORWARD_WM_SETCURSOR(hwnd, hwndCursor, codeHitTest, msg, ::DefWindowProc); }

        // on_message can be overridden to handle arbitrary messages that don't already have more specific handlers
        LRESULT on_message(HWND window, int message, WPARAM wParam, LPARAM lParam)
        {
            return ::DefWindowProc(window, message, wParam, lParam);
        }
    };


    // A MainWindow is an AppWindow that has default processing during window destruction to signal any message pump
    // to exit.
    template <class T>
    class MainWindow : public AppWindow<T>
    {
    public:

        using AppWindow::AppWindow;

        void on_destroy(HWND)
        {
            PostQuitMessage(1);
        }
    };
   
} // namespace MikeWindows
