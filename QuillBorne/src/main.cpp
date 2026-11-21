#define NOMINMAX                 // чтобы windows.h не определял макросы min/max
#include <windows.h>
#include <gdiplus.h>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <random>
#include <mutex>
#include <algorithm>
#include <shlobj.h>   // для Desktop
#include <sstream>
#include <iomanip>

#pragma comment(lib, "gdiplus.lib")

// ============================================================
//  СИСТЕМА SIN3R — графический шутник
//  Фазы: LOADING (слайдшоу из assets) -> TERMINAL (эмуляция
//  команд) -> LOCKED (арт + neofetch + мемы + ввод кода)
// ============================================================

enum BootPhase { PHASE_LOADING = 0, PHASE_TERMINAL = 1, PHASE_LOCKED = 2 };
static volatile LONG g_phase = PHASE_LOADING;

HHOOK keyboardHook = NULL;
HHOOK mouseHook = NULL;
HWND mainHwnd = NULL;
HWND hEdit = NULL;
HWND hButton = NULL;
HWND hLabel = NULL;

ULONG_PTR g_gdiToken = 0;
Gdiplus::Image* g_artImg = nullptr;      // неофетч-логотип из art.png
Gdiplus::Image* g_loadingImg = nullptr;  // картинка из assets на экране загрузки

// ---------- Ассеты (установим при загрузке) ----------
std::vector<std::wstring> g_assets;
int g_loadIndex = -1;
int g_loadProgress = 0;
std::wstring g_loadText = L"Sin3r OS — злая графическая оболочка";

// ---------- Терминал ----------
struct TermLine { std::wstring text; bool isCmd; };
std::mutex g_termMutex;
std::vector<TermLine> g_termLines;
std::wstring g_termCurrent;

// Глобальный текущий мем (внизу под neofetch)
std::wstring current_meme =
    L"Мышь и клавиатура заблокированы.\nПопробуй нажать Alt+F4... Я подожду.\n\n- siner";

// Мемные фразы для ротации
std::vector<std::wstring> memes = {
    L"Мышь и клавиатура заблокированы.\nПопробуй нажать Alt+F4... Я подожду.\n\n- siner",
    L"Win+M не работает. И Win+Tab тоже.\n\n- siner",
    L"Даже диспетчер задач не вызовешь.\n\n- siner",
    L"Я заблокировал твою мышку. Кликать бесполезно.\n\n- siner",
    L"Чтобы выйти, жми Ctrl+Alt+Del.\nНу или выключи виртуалку.\n\n- siner",
    L"Обнаружено 999+ вирусов. Все хотят обниматься.\n\n- siner",
    L"Твой компьютер теперь мой. Шутка.\n\n- siner"
};

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

// ---------- НЕОФЕТЧ (реальные данные + немного мемов) ----------
struct NeoLine { COLORREF color; std::wstring text; };
std::vector<NeoLine> g_neoLines;

static const COLORREF paletteBlocks[8] = {
    RGB(60, 60, 60), RGB(255, 80, 80), RGB(255, 200, 70), RGB(120, 255, 120),
    RGB(90, 200, 255), RGB(120, 120, 255), RGB(255, 120, 255), RGB(230, 230, 230)
};

// Собираем реальную информацию о системе
static std::wstring GetUserNameStr() {
    wchar_t buf[256] = {};
    DWORD sz = 256;
    if (GetUserNameW(buf, &sz)) return buf;
    return L"user";
}

static std::wstring GetComputerNameStr() {
    wchar_t buf[256] = {};
    DWORD sz = 256;
    if (GetComputerNameW(buf, &sz)) return buf;
    return L"PC";
}

static std::wstring GetOsVersionStr() {
    // RtlGetVersion — самый надёжный способ узнать настоящую версию
    typedef LONG(WINAPI* RtlGetVersionPtr)(OSVERSIONINFOW*);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto fn = (RtlGetVersionPtr)GetProcAddress(ntdll, "RtlGetVersion");
        if (fn) {
            OSVERSIONINFOW rovi = { sizeof(rovi) };
            if (fn(&rovi) == 0) {
                std::wstringstream ss;
                if (rovi.dwMajorVersion == 10 && rovi.dwBuildNumber >= 22000)
                    ss << L"Windows 11";
                else if (rovi.dwMajorVersion == 10)
                    ss << L"Windows 10";
                else
                    ss << L"Windows " << rovi.dwMajorVersion << L"." << rovi.dwMinorVersion;
                ss << L" (build " << rovi.dwBuildNumber << L") x64";
                return ss.str();
            }
        }
    }
    return L"Windows 10/11 x64";
}

static std::wstring GetUptimeStr() {
    ULONGLONG ms = GetTickCount64();
    ULONGLONG sec = ms / 1000;
    ULONGLONG days = sec / 86400;
    ULONGLONG hours = (sec % 86400) / 3600;
    ULONGLONG mins = (sec % 3600) / 60;
    std::wstringstream ss;
    if (days > 0) ss << days << L"d ";
    ss << hours << L"h " << mins << L"m";
    return ss.str();
}

static std::wstring GetCpuName() {
    HKEY hKey = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t buf[512] = {};
        DWORD sz = sizeof(buf);
        if (RegQueryValueExW(hKey, L"ProcessorNameString", NULL, NULL, (LPBYTE)buf, &sz) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            // убираем лишние пробелы
            std::wstring s = buf;
            while (!s.empty() && s.back() == L' ') s.pop_back();
            return s;
        }
        RegCloseKey(hKey);
    }
    return L"Unknown CPU";
}

static std::wstring GetMemoryStr() {
    MEMORYSTATUSEX ms = { sizeof(ms) };
    if (GlobalMemoryStatusEx(&ms)) {
        auto toMiB = [](DWORDLONG b) { return (b / 1024 / 1024); };
        std::wstringstream ss;
        ss << toMiB(ms.ullTotalPhys - ms.ullAvailPhys) << L"MiB / "
           << toMiB(ms.ullTotalPhys) << L"MiB";
        return ss.str();
    }
    return L"? / ? MiB";
}

static void BuildNeoFetch() {
    g_neoLines.clear();

    std::wstring user = GetUserNameStr();
    std::wstring host = GetComputerNameStr();
    std::wstring title = L"sin3r@" + host;

    auto add = [](COLORREF c, const std::wstring& t) {
        g_neoLines.push_back({ c, t });
    };

    add(RGB(170, 255, 90),  title);
    add(RGB(170, 255, 90),  L"──────────────────────────");
    add(RGB(255, 255, 255), L"OS:         " + GetOsVersionStr());
    add(RGB(170, 255, 90),  L"Host:       " + host + L" (оккупирован)");
    add(RGB(170, 255, 90),  L"Kernel:     10.0 (NT)");
    add(RGB(255, 255, 255), L"Uptime:     " + GetUptimeStr());
    add(RGB(255, 100, 100), L"Packages:   999+ (все мемные)");
    add(RGB(255, 255, 255), L"Shell:      siner-sh 7.7.7");
    add(RGB(255, 100, 100), L"Resolution: " +
        std::to_wstring(GetSystemMetrics(SM_CXSCREEN)) + L"x" +
        std::to_wstring(GetSystemMetrics(SM_CYSCREEN)));
    add(RGB(255, 255, 255), L"DE:         Sin3rDE (захват мыши v2)");
    add(RGB(170, 255, 90),  L"Terminal:   эмулятор внутри эмулятора");
    add(RGB(255, 255, 255), L"CPU:        " + GetCpuName());
    add(RGB(170, 255, 90),  L"GPU:        (смотри в диспетчере)");
    add(RGB(255, 255, 255), L"Memory:     " + GetMemoryStr());
}

// ---------- Фейковые команды терминала ----------
struct FakeCmd {
    const wchar_t* cmd;
    std::vector<const wchar_t*> out;
};
static const FakeCmd fakeCmds[] = {
    { L"whoami",                 { L"sin3r (был: гость, стал: хозяином)" } },
    { L"neofetch",               { L"логотип уже на экране 😎" } },
    { L"taskkill /f /im svoboda.exe",
                                 { L"УСПЕХ: процесс \"svoboda.exe\" (PID 404) завершён.",
                                   L"Восстанавливать не собираюсь." } },
    { L"net user Павел /active:no",
                                 { L"Команда выполнена успешно.",
                                   L"Шучу. Успешно — для меня." } },
    { L"ping -t ya.tut.zhivu",   { L"Обмен пакетами с ya.tut.zhivu [127.0.0.1]:",
                                   L"Ответ: число байт=32 время=<∞> TTL=666",
                                   L"Ответ: число байт=32 время=<∞> TTL=666",
                                   L"(Ctrl+C не поможет, я позаботился)" } },
    { L"type секрет.txt",        { L"Павел, привет! Я тут живу теперь.",
                                   L"Код от двери всё тот же — ищи в исходниках 😉" } },
    { L"exit",                   { L"Выход? Как мило.",
                                   L"Попробуй код. Ну или выключи виртуалку.",
                                   L"- siner" } },
};

const wchar_t* bootSteps[] = {
    L"Проверяю... ага, виртуалка. Опять.",
    L"Загружаю кошечку-горничную...",
    L"Отключаю свободу пользователя...",
    L"Синхронизирую мемы...",
    L"Готово. Меня забыли удалить."
};

// ============================================================
//  helpers
// ============================================================
static std::wstring GetExeDir() {
    wchar_t buf[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring p = buf;
    size_t s = p.find_last_of(L'\\');
    return (s == std::wstring::npos) ? L"." : p.substr(0, s);
}

static Gdiplus::Image* TryLoadImage(const std::wstring& path) {
    Gdiplus::Image* img = new Gdiplus::Image(path.c_str());
    if (img && img->GetLastStatus() == Gdiplus::Ok) return img;
    delete img;
    return nullptr;
}

//Сканируем папку assets рядом с exe
static std::vector<std::wstring> ScanAssets(const std::wstring& dir) {
    std::vector<std::wstring> out;
    const wchar_t* masks[] = { L"*.png", L"*.jpg", L"*.jpeg", L"*.bmp", L"*.gif" };
    for (const wchar_t* m : masks) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((dir + L"\\" + m).c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                    out.push_back(dir + L"\\" + fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

static HFONT MakeMono(int h, int weight) {
    HFONT f = CreateFontW(h, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Consolas");
    if (!f) f = CreateFontW(h, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Courier New");
    return f;
}

static void DrawMonoText(HDC hdc, const std::wstring& text, RECT rc, int size,
                         int weight, COLORREF color, UINT flags = DT_CENTER | DT_TOP | DT_WORDBREAK) {
    HFONT f = MakeMono(size, weight);
    HFONT old = (HFONT)SelectObject(hdc, f);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, color);
    DrawTextW(hdc, text.c_str(), -1, &rc, flags);
    SelectObject(hdc, old);
    DeleteObject(f);
}

// Картинка, вписанная в прямоугольник (Drawn по центру горизонтально)
static void DrawImageFitted(HDC hdc, Gdiplus::Image* img, int x, int y, int maxW, int maxH) {
    if (!img) return;
    int iw = img->GetWidth(), ih = img->GetHeight();
    if (iw <= 0 || ih <= 0 || maxW <= 0 || maxH <= 0) return;
    double s = (std::min)(maxW / (double)iw, maxH / (double)ih);
    int w = (int)(iw * s), h = (int)(ih * s);
    Gdiplus::Graphics g(hdc);
    g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    g.SetSmoothingMode(Gdiplus::SmoothingModeHighQuality);
    g.DrawImage(img, x + (maxW - w) / 2, y + (maxH - h) / 2, w, h);
}

// ============================================================
//  ХУКИ (без изменений логики блокировки)
// ============================================================
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
            bool altPressed = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
            bool ctrlPressed = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
            bool shiftPressed = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;

            // Всегда блокируем опасные комбинации
            if (p->vkCode == VK_LWIN || p->vkCode == VK_RWIN) return 1;
            if (altPressed && p->vkCode == VK_F4) return 1;
            if (ctrlPressed && shiftPressed && p->vkCode == VK_ESCAPE) return 1;
            if (altPressed && p->vkCode == VK_TAB) return 1;
            if (ctrlPressed && p->vkCode == VK_ESCAPE) return 1;

            // Tab и Space блокируем только до фазы LOCKED,
            // чтобы в поле ввода кода можно было нормально работать
            if (g_phase != PHASE_LOCKED) {
                if (p->vkCode == VK_TAB || p->vkCode == VK_SPACE) return 1;
            }

            // Enter — проверка кода (работает даже без мыши)
            if (p->vkCode == VK_RETURN) {
                PostMessage(mainHwnd, WM_USER + 1, 0, 0);
            }
        }
    }
    return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
}

LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        return 1;
    }
    return CallNextHookEx(mouseHook, nCode, wParam, lParam);
}

// ============================================================
//  Своё страшное письмо на рабочий стол
//  (важно: только СОЗДАЁМ файл, существующие файлы не трогаем)
// ============================================================
void EncodeRandomFile() {
    wchar_t desktop[MAX_PATH] = { 0 };
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop))) {
        GetTempPathW(MAX_PATH, desktop);
    }

    std::wstring targetFile = std::wstring(desktop) + L"\\siner_я_был_здесь.txt";

    HANDLE hFile = CreateFileW(targetFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
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
    if (g_phase != PHASE_LOCKED) return;   // во время загрузки и терминала ничего не проверяем

    wchar_t buf[256] = { 0 };
    GetWindowTextW(hEdit, buf, 255);

    if (wcscmp(buf, CORRECT_CODE) == 0) {
        UnhookWindowsHookEx(keyboardHook);
        UnhookWindowsHookEx(mouseHook);
        ShowCursor(TRUE);
        PostQuitMessage(0);
        ExitProcess(0);
    }
    else {
        EncodeRandomFile();
        current_meme = L"НЕВЕРНЫЙ КОД!\n\nОдин файл на рабочем столе\nтеперь на арабском 😈\n\nПопробуй ещё раз...";
        InvalidateRect(mainHwnd, NULL, TRUE);
    }
}

// Поток смены мемов (только в фазе LOCKED)
void meme_updater() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, (int)memes.size() - 1);

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(4));

        if (g_phase == PHASE_LOCKED) {
            // Постоянно возвращаем фокус на поле ввода — чтобы можно было печатать без мыши
            if (hEdit) {
                PostMessage(mainHwnd, WM_USER + 2, 0, 0);
            }

            if (current_meme.find(L"НЕВЕРНЫЙ КОД") == std::wstring::npos) {
                current_meme = memes[dis(gen)];
                InvalidateRect(mainHwnd, NULL, FALSE);
            }
        }
    }
}

// ============================================================
//  СКРИПТ ЗАГРУЗКИ: слайдшоу assets -> эмулированный терминал
// ============================================================
void boot_thread() {
    auto invalidate = []() { InvalidateRect(mainHwnd, NULL, FALSE); };

    // -------- ФАЗА 1: LOADING (покажешь картинки из assets) --------
    int total = (int)g_assets.size();
    if (total == 0) {
        // Нет картинок — красиво прогоняем текстовые шаги с плавным прогрессом
        int n = (int)(sizeof(bootSteps) / sizeof(bootSteps[0]));
        for (int i = 0; i < n; i++) {
            g_loadText = bootSteps[i];
            int from = i * 100 / n;
            int to   = (i + 1) * 100 / n;
            for (int p = from; p <= to; p += 2) {
                g_loadProgress = p;
                invalidate();
                std::this_thread::sleep_for(std::chrono::milliseconds(35));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(280));
        }
        g_loadProgress = 100;
    }
    else {
        for (int i = 0; i < total; i++) {
            Gdiplus::Image* img = TryLoadImage(g_assets[i]);
            Gdiplus::Image* old = g_loadingImg;
            g_loadingImg = img;
            // удаляем старую после небольшой задержки, чтобы Paint не ловил dangling pointer
            if (old) {
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                delete old;
            }

            g_loadIndex = i;
            int from = i * 100 / total;
            int to   = (i + 1) * 100 / total;
            g_loadText = L"Синхронизирую мем-базы (" + std::to_wstring(i + 1) +
                         L"/" + std::to_wstring(total) + L")...";
            for (int p = from; p <= to; p += 2) {
                g_loadProgress = p;
                invalidate();
                std::this_thread::sleep_for(std::chrono::milliseconds(28));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(450));
        }
        g_loadProgress = 100;
    }
    g_loadText = L"Загрузка завершена. Поднимаю терминал...";
    invalidate();
    std::this_thread::sleep_for(std::chrono::milliseconds(600));

    // -------- ФАЗА 2: TERMINAL --------
    InterlockedExchange(&g_phase, PHASE_TERMINAL);
    invalidate();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));

    auto addLine = [&](const std::wstring& s, bool isCmd) {
        std::lock_guard<std::mutex> lk(g_termMutex);
        g_termLines.push_back({ s, isCmd });
    };

    addLine(L"Sin3r Terminal v6.6.6 — проснись, Павел", true);
    addLine(L"", true);

    for (const FakeCmd& c : fakeCmds) {
        const std::wstring prompt = L"C:\\Users\\Павел> ";
        // Печатаем команду посимвольно (чуть медленнее и живее)
        for (size_t i = 0; i <= wcslen(c.cmd); i++) {
            {
                std::lock_guard<std::mutex> lk(g_termMutex);
                g_termCurrent = prompt + std::wstring(c.cmd).substr(0, i);
            }
            invalidate();
            // небольшая рандомизация скорости набора
            static thread_local std::mt19937 rng{ std::random_device{}() };
            std::uniform_int_distribution<int> dist(16, 32);
            std::this_thread::sleep_for(std::chrono::milliseconds(dist(rng)));
        }
        addLine(prompt + c.cmd, true);
        { std::lock_guard<std::mutex> lk(g_termMutex); g_termCurrent.clear(); }
        invalidate();
        std::this_thread::sleep_for(std::chrono::milliseconds(90));

        // Печатаем вывод
        for (const wchar_t* o : c.out) {
            addLine(o, false);
            invalidate();
            std::this_thread::sleep_for(std::chrono::milliseconds(140));
        }
        addLine(L"", true);
        invalidate();
        std::this_thread::sleep_for(std::chrono::milliseconds(180));
    }

    // -------- ФАЗА 3: LOCKED --------
    InterlockedExchange(&g_phase, PHASE_LOCKED);
    invalidate();
    ShowWindow(hLabel, SW_SHOW);
    ShowWindow(hEdit, SW_SHOW);
    ShowWindow(hButton, SW_SHOW);

    // Несколько раз жёстко ставим фокус — topmost-окно иногда его теряет
    for (int i = 0; i < 5; i++) {
        PostMessage(mainHwnd, WM_USER + 2, 0, 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
    }

    // держим поток
    for (;;) std::this_thread::sleep_for(std::chrono::minutes(1));
}

// ============================================================
//  ОТРИСОВКА ФАЗ
// ============================================================
static void PaintLoading(HDC hdc, int W, int H) {
    DrawMonoText(hdc, L"З А Г Р У З К А   S I N 3 R   O S", { 0, 40, W, 80 }, 30, FW_BOLD, RGB(255, 60, 60));

    // Картинка из assets по центру
    int areaTop = 110, areaBottom = H - 300;
    if (g_loadingImg && areaBottom > areaTop + 50) {
        DrawImageFitted(hdc, g_loadingImg, 40, areaTop, W - 80, areaBottom - areaTop);
    }
    else if (areaBottom > areaTop + 50) {
        // fallback, когда картинок в assets нет
        DrawMonoText(hdc, L"[ assets пусты — кинь png/jpg сюда ]",
                     { 40, (areaTop + areaBottom) / 2 - 20, W - 40, (areaTop + areaBottom) / 2 + 40 },
                     18, FW_NORMAL, RGB(90, 90, 90));
    }

    // Статус
    DrawMonoText(hdc, g_loadText,
                 { 0, H - 280, W, H - 235 }, 20, FW_NORMAL, RGB(170, 255, 90));

    // Прогресс-бар
    int bw = 560, bh = 24, bx = (W - bw) / 2, by = H - 215;
    RECT frame = { bx, by, bx + bw, by + bh };
    SetBkMode(hdc, TRANSPARENT);
    FrameRect(hdc, &frame, (HBRUSH)GetStockObject(GRAY_BRUSH));
    int fillW = bw * g_loadProgress / 100;
    if (fillW > 0) {
        RECT fill = { bx + 2, by + 2, bx + 2 + fillW - (fillW > 4 ? 4 : 0), by + bh - 2 };
        HBRUSH lime = CreateSolidBrush(RGB(170, 255, 90));
        FillRect(hdc, &fill, lime);
        DeleteObject(lime);
    }
    DrawMonoText(hdc, std::to_wstring(g_loadProgress) + L" %",
                 { 0, by + bh + 10, W, by + bh + 45 }, 18, FW_NORMAL, RGB(200, 200, 200));

    DrawMonoText(hdc, L"не выключай. я пошутил. наверное.",
                 { 0, H - 60, W, H - 20 }, 15, FW_NORMAL, RGB(120, 120, 120));
}

static void PaintTerminal(HDC hdc, int W, int H) {
    DrawMonoText(hdc, L"[ SIN3R TERMINAL — полностью безопасен, честно ]",
                 { 30, 25, W - 30, 60 }, 16, FW_BOLD, RGB(255, 200, 70), DT_LEFT | DT_TOP);

    const int lineH = 24;
    int visible = (H - 90) / lineH;

    std::vector<TermLine> lines;
    std::wstring current;
    {
        std::lock_guard<std::mutex> lk(g_termMutex);
        lines = g_termLines;
        current = g_termCurrent;
    }

    int start = (int)lines.size() - visible;
    if (start < 0) start = 0;

    int y = 60;
    for (int i = start; i < (int)lines.size(); i++) {
        COLORREF c = lines[i].isCmd ? RGB(170, 255, 90) : RGB(200, 200, 200);
        DrawMonoText(hdc, lines[i].text, { 30, y, W - 30, y + lineH + 20 }, 17,
                     lines[i].isCmd ? FW_BOLD : FW_NORMAL, c, DT_LEFT | DT_TOP);
        y += lineH;
    }

    // текущая строка + мигающий курсор
    bool cursor = ((GetTickCount() / 400) % 2) == 0;
    std::wstring cur = current + (cursor ? L"█" : L" ");
    DrawMonoText(hdc, cur, { 30, y, W - 30, y + lineH + 20 }, 17, FW_BOLD, RGB(170, 255, 90), DT_LEFT | DT_TOP);
}

static void PaintLocked(HDC hdc, int W, int H) {
    // -------- Логотип: арт из art.png --------
    int logoMaxH = (H > 800) ? (H - 330) : (H - 120);
    int logoMaxW = W / 2 - 80;
    DrawImageFitted(hdc, g_artImg, 40, 80, logoMaxW, logoMaxH);

    // -------- neofetch справа (реальные данные) --------
    int nx = W / 2 + 40;
    int y = 90;
    const int lh = 26;
    for (const auto& l : g_neoLines) {
        DrawMonoText(hdc, l.text, { nx, y, W - 20, y + lh + 20 }, 18, FW_NORMAL, l.color, DT_LEFT | DT_TOP);
        y += lh;
    }

    // Палитра (как в neofetch)
    int bx = nx, by = y + 8;
    for (int i = 0; i < 8; i++) {
        RECT r = { bx, by, bx + 22, by + 22 };
        HBRUSH b = CreateSolidBrush(paletteBlocks[i]);
        FillRect(hdc, &r, b);
        DeleteObject(b);
        bx += 30;
    }

    // -------- Слоган и мем внизу --------
    DrawMonoText(hdc, L"СИСТЕМА ЗАБЛОКИРОВАНА",
                 { 0, H - 330, W, H - 280 }, 34, FW_BOLD, RGB(255, 40, 40));
    DrawMonoText(hdc, L"Павел, привет! Я тут живу теперь.  - siner",
                 { 0, H - 278, W, H - 250 }, 17, FW_NORMAL, RGB(230, 230, 230));

    RECT memeRect = { 100, H - 235, W - 100, H - 165 };
    DrawMonoText(hdc, current_meme, memeRect, 15, FW_NORMAL, RGB(180, 180, 180));
}

// ============================================================
//  ОКОННАЯ ПРОЦЕДУРА (ввод кода и события окна)
// ============================================================
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
    case WM_ERASEBKGND:
        return 1; // рисуем всё сами, без мерцания

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);
        int W = rc.right, H = rc.bottom;

        // Двойная буферизация
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
        HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);
        RECT full = rc;
        FillRect(mem, &full, (HBRUSH)GetStockObject(BLACK_BRUSH));

        switch (g_phase) {
        case PHASE_LOADING:  PaintLoading(mem, W, H);  break;
        case PHASE_TERMINAL: PaintTerminal(mem, W, H); break;
        case PHASE_LOCKED:   PaintLocked(mem, W, H);   break;
        }

        BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_CTLCOLORSTATIC: { // красный текст у лейбла
        HDC hdcStatic = (HDC)wParam;
        SetTextColor(hdcStatic, RGB(255, 60, 60));
        SetBkMode(hdcStatic, TRANSPARENT);
        return (LRESULT)GetStockObject(BLACK_BRUSH);
    }

    case WM_COMMAND:
        if (LOWORD(wParam) == 1001) CheckCode();
        return 0;

    case WM_USER + 1: // Enter из хука
        CheckCode();
        return 0;

    case WM_USER + 2: // показать UI в фазе LOCKED — жёстко вернуть фокус на поле кода
        if (g_phase == PHASE_LOCKED && hEdit) {
            SetForegroundWindow(hwnd);
            SetActiveWindow(hwnd);
            BringWindowToTop(hwnd);
            SetFocus(hEdit);
            // выделяем весь текст, чтобы сразу было видно, что фокус есть
            SendMessageW(hEdit, EM_SETSEL, 0, -1);
        }
        return 0;

    case WM_CLOSE:
        return 0; // Не даём закрыть
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    // GDI+ — чтобы рисовать PNG
    Gdiplus::GdiplusStartupInput gdiStartup;
    if (Gdiplus::GdiplusStartup(&g_gdiToken, &gdiStartup, NULL) != Gdiplus::Ok) {
        return 1;
    }

    // Собираем реальные данные для neofetch
    BuildNeoFetch();

    // Логотип: art.png рядом с exe (в PrjDir это "..\art.png")
    std::wstring exeDir = GetExeDir();
    g_artImg = TryLoadImage(exeDir + L"\\art.png");
    if (!g_artImg) g_artImg = TryLoadImage(exeDir + L"\\..\\art.png");
    if (!g_artImg) g_artImg = TryLoadImage(exeDir + L"\\assets\\art.png");

    // Ассеты для слайдшоу на экране загрузки
    g_assets = ScanAssets(exeDir + L"\\assets");
    if (g_assets.empty()) g_assets = ScanAssets(exeDir + L"\\..\\assets");

    keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(NULL), 0);
    mouseHook = SetWindowsHookEx(WH_MOUSE_LL, MouseProc, GetModuleHandle(NULL), 0);

    if (keyboardHook == NULL || mouseHook == NULL) {
        Gdiplus::GdiplusShutdown(g_gdiToken);
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

    // --- UI элементы внизу (создаём СПРЯТАННЫМИ, покажем после загрузки) ---
    int uiY = screenH - 120;
    int centerX = screenW / 2;

    hLabel = CreateWindowW(
        L"STATIC", L"Павел, введи код для разблокировки:",
        WS_CHILD | SS_CENTER,
        centerX - 250, uiY, 500, 25,
        mainHwnd, NULL, hInstance, NULL
    );

    hEdit = CreateWindowW(
        L"EDIT", L"",
        WS_CHILD | WS_BORDER | WS_TABSTOP | ES_AUTOHSCROLL | ES_CENTER,
        centerX - 220, uiY + 30, 300, 28,
        mainHwnd, (HMENU)1002, hInstance, NULL
    );

    hButton = CreateWindowW(
        L"BUTTON", L"нажать",
        WS_CHILD | BS_DEFPUSHBUTTON,
        centerX + 90, uiY + 28, 100, 32,
        mainHwnd, (HMENU)1001, hInstance, NULL
    );

    ShowCursor(FALSE);
    ShowWindow(mainHwnd, SW_SHOW);
    UpdateWindow(mainHwnd);

    std::thread booter(boot_thread);
    booter.detach();

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
    Gdiplus::GdiplusShutdown(g_gdiToken);
    return 0;
}
