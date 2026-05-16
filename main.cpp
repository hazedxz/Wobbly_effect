#include <windows.h>
#include <shellapi.h>
#include <vector>


#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif


#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_SHOW   2001
#define ID_TRAY_TOGGLE 2002
#define ID_TRAY_EXIT   2003
#define ID_BTN_TOGGLE  3001


const int NUM_SLICES = 20;       
const int PADDING = 150;         

struct Slice {
    float x, y;       
    float vx, vy;     
};


HHOOK hMouseHook = NULL;
HWND draggedWindow = NULL;
HWND fakeWindow = NULL;
HWND hMainWindow = NULL;
HBITMAP hCapturedBitmap = NULL;
std::vector<Slice> slices;
int imageWidth, imageHeight;
float sliceHeight;
POINT lastMousePos;
HBRUSH bgBrush = NULL; 
bool isEffectActive = false;
NOTIFYICONDATA nid = {0};


HBITMAP CaptureWindow(HWND hwnd) {
    RECT rc;
    GetWindowRect(hwnd, &rc);
    imageWidth = rc.right - rc.left;
    imageHeight = rc.bottom - rc.top;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbm = CreateCompatibleBitmap(hdcScreen, imageWidth, imageHeight);
    SelectObject(hdcMem, hbm);

    PrintWindow(hwnd, hdcMem, PW_RENDERFULLCONTENT);

    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return hbm;
}

LRESULT CALLBACK FakeWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_TIMER) {
        for (int i = 0; i < NUM_SLICES; i++) {
            float targetX = PADDING; 
            float targetY = PADDING + (i * sliceHeight);

            float lagFactor = (float)i / (NUM_SLICES - 1);
            float stiffness = 0.35f - (0.20f * lagFactor); 
            float damping = 0.65f; 

            float forceX = (targetX - slices[i].x) * stiffness;
            float forceY = (targetY - slices[i].y) * stiffness;

            slices[i].vx = (slices[i].vx + forceX) * damping;
            slices[i].vy = (slices[i].vy + forceY) * damping;
            slices[i].x += slices[i].vx;
            slices[i].y += slices[i].vy;
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    else if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc; GetClientRect(hwnd, &rc);
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbmMem = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
        HGDIOBJ oldBm = SelectObject(hdcMem, hbmMem);

        FillRect(hdcMem, &rc, bgBrush);

        if (hCapturedBitmap) {
            HDC hdcImage = CreateCompatibleDC(hdc);
            SelectObject(hdcImage, hCapturedBitmap);

            for (int i = 0; i < NUM_SLICES; i++) {
                int srcY = (int)(i * sliceHeight);
                int srcH = (i == NUM_SLICES - 1) ? (imageHeight - srcY) : (int)sliceHeight;
                
                int currentY = (int)slices[i].y;
                int nextY = (i == NUM_SLICES - 1) ? (currentY + srcH) : (int)slices[i+1].y;
                
                int currentX = (int)slices[i].x;
                int nextX = (i == NUM_SLICES - 1) ? currentX : (int)slices[i+1].x;

                POINT pts[3];
                pts[0].x = currentX;             pts[0].y = currentY;
                pts[1].x = currentX + imageWidth; pts[1].y = currentY;
                pts[2].x = nextX;                pts[2].y = nextY;

                PlgBlt(hdcMem, pts, hdcImage, 0, srcY, imageWidth, srcH, NULL, 0, 0);
            }
            DeleteDC(hdcImage);
        }

        BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, oldBm);
        DeleteObject(hbmMem);
        DeleteDC(hdcMem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        MSLLHOOKSTRUCT* pMouseStruct = (MSLLHOOKSTRUCT*)lParam;

        if (wParam == WM_LBUTTONDOWN) {
            POINT pt = pMouseStruct->pt;
            HWND hwnd = WindowFromPoint(pt);
            HWND hTopLevel = GetAncestor(hwnd, GA_ROOT);

            if (hTopLevel != NULL && hTopLevel != fakeWindow) {
                char className[256];
                GetClassNameA(hTopLevel, className, sizeof(className));
                if (strcmp(className, "Shell_TrayWnd") == 0 || 
                    strcmp(className, "Progman") == 0 || 
                    strcmp(className, "WorkerW") == 0) {
                    return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
                }

                LRESULT hitTest = SendMessage(hTopLevel, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));

                if (hitTest == HTCAPTION) {
                    draggedWindow = hTopLevel;
                    lastMousePos = pt;

                    hCapturedBitmap = CaptureWindow(draggedWindow);
                    sliceHeight = (float)imageHeight / NUM_SLICES;

                    slices.clear();
                    slices.resize(NUM_SLICES);
                    for (int i = 0; i < NUM_SLICES; i++) {
                        slices[i] = { (float)PADDING, (float)PADDING + (i * sliceHeight), 0, 0 };
                    }

                    RECT originalRect; GetWindowRect(draggedWindow, &originalRect);
                    HINSTANCE hInstance = GetModuleHandle(NULL);
                    
                    fakeWindow = CreateWindowEx(
                        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT,
                        "JellyWindowClass", "", WS_POPUP,
                        originalRect.left - PADDING, originalRect.top - PADDING, 
                        imageWidth + (PADDING * 2), imageHeight + (PADDING * 2),
                        NULL, NULL, hInstance, NULL
                    );

                    SetLayeredWindowAttributes(fakeWindow, RGB(255, 0, 255), 255, LWA_COLORKEY);
                    ShowWindow(fakeWindow, SW_SHOW);
                    ShowWindow(draggedWindow, SW_HIDE);
                    
                    SetTimer(fakeWindow, 1, 16, NULL); 
                    return 1;
                }
            }
        }
        else if (wParam == WM_MOUSEMOVE && draggedWindow != NULL) {
            POINT pt = pMouseStruct->pt;
            int dx = pt.x - lastMousePos.x;
            int dy = pt.y - lastMousePos.y;
            lastMousePos = pt;

            RECT fakeRect; GetWindowRect(fakeWindow, &fakeRect);
            SetWindowPos(fakeWindow, NULL, fakeRect.left + dx, fakeRect.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

            for (int i = 0; i < NUM_SLICES; i++) {
                float lagFactor = (float)i / (NUM_SLICES - 1); 
                slices[i].x -= dx * lagFactor;
                slices[i].y -= dy * lagFactor;
            }
        }
        else if (wParam == WM_LBUTTONUP && draggedWindow != NULL) {
            KillTimer(fakeWindow, 1);
            
            RECT fakeRect; GetWindowRect(fakeWindow, &fakeRect);
            SetWindowPos(draggedWindow, NULL, fakeRect.left + PADDING, fakeRect.top + PADDING, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

            ShowWindow(draggedWindow, SW_SHOW);
            DestroyWindow(fakeWindow);
            fakeWindow = NULL;
            if (hCapturedBitmap) { DeleteObject(hCapturedBitmap); hCapturedBitmap = NULL; }
            draggedWindow = NULL;
        }
    }
    return CallNextHookEx(hMouseHook, nCode, wParam, lParam);
}



void ToggleHook() {
    if (isEffectActive) {
        if (hMouseHook) {
            UnhookWindowsHookEx(hMouseHook);
            hMouseHook = NULL;
        }
        isEffectActive = false;
        SetWindowText(GetDlgItem(hMainWindow, ID_BTN_TOGGLE), "Activar Efecto Wobbly");
    } else {
        hMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, NULL, 0);
        isEffectActive = true;
        SetWindowText(GetDlgItem(hMainWindow, ID_BTN_TOGGLE), "Desactivar Efecto");
    }
}

void ShowContextMenu(HWND hwnd, POINT pt) {
    HMENU hMenu = CreatePopupMenu();
    InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_SHOW, "Abrir Panel");
    InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_TOGGLE, isEffectActive ? "Pausar Efecto" : "Reanudar Efecto");
    InsertMenu(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenu(hMenu, -1, MF_BYPOSITION | MF_STRING, ID_TRAY_EXIT, "Salir Completamente");

    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            
            HFONT hFont = CreateFont(18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, 
                                     DEFAULT_PITCH | FF_SWISS, "Segoe UI");

            HWND hBtn = CreateWindow("BUTTON", "Activar Efecto Wobbly",
                         WS_TABSTOP | WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
                         40, 40, 200, 40, hwnd, (HMENU)ID_BTN_TOGGLE, GetModuleHandle(NULL), NULL);
            SendMessage(hBtn, WM_SETFONT, (WPARAM)hFont, TRUE);

            
            ToggleHook();
            break;
        }
        case WM_COMMAND:
            if (LOWORD(wParam) == ID_BTN_TOGGLE || LOWORD(wParam) == ID_TRAY_TOGGLE) {
                ToggleHook();
            }
            else if (LOWORD(wParam) == ID_TRAY_SHOW) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            }
            else if (LOWORD(wParam) == ID_TRAY_EXIT) {
                PostQuitMessage(0);
            }
            break;
        case WM_TRAYICON:
            if (lParam == WM_RBUTTONUP) {
                POINT pt; GetCursorPos(&pt);
                ShowContextMenu(hwnd, pt);
            } else if (lParam == WM_LBUTTONDBLCLK) {
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
            }
            break;
        case WM_CLOSE:
            
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            if (isEffectActive && hMouseHook) UnhookWindowsHookEx(hMouseHook);
            Shell_NotifyIcon(NIM_DELETE, &nid);
            PostQuitMessage(0);
            break;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORDLG:
            
            return (LRESULT)GetStockObject(WHITE_BRUSH);
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}


int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    bgBrush = CreateSolidBrush(RGB(255, 0, 255));

    
    WNDCLASS wcFake = {0};
    wcFake.lpfnWndProc = FakeWindowProc;
    wcFake.hInstance = hInstance;
    wcFake.lpszClassName = "JellyWindowClass";
    RegisterClass(&wcFake);

    
    WNDCLASS wcMain = {0};
    wcMain.lpfnWndProc = MainWindowProc;
    wcMain.hInstance = hInstance;
    wcMain.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wcMain.lpszClassName = "WobblyMainClass";
    wcMain.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClass(&wcMain);

    
    hMainWindow = CreateWindowEx(
        0, "WobblyMainClass", "Wobbly Effect",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 160,
        NULL, NULL, hInstance, NULL
    );

    
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hMainWindow;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION); 
    lstrcpy(nid.szTip, "Wobbly Effect");
    Shell_NotifyIcon(NIM_ADD, &nid);

    ShowWindow(hMainWindow, nCmdShow);
    UpdateWindow(hMainWindow);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    
    DeleteObject(bgBrush);
    return (int)msg.wParam;
}