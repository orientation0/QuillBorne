# Project1 — System Locker (Prank)

Простой Windows-приложение на C++ (WinAPI), которое блокирует клавиатуру и мышь, показывает полноэкранное окно с мемными сообщениями и требует ввода специального кода для разблокировки.

> **Внимание:** Это шуточный / образовательный проект.  
> Запускайте **только на виртуальной машине** или в изолированной среде.  
> Программа запрашивает права администратора и устанавливает низкоуровневые хуки.

## Возможности

- Полноэкранное окно поверх всех окон (`WS_EX_TOPMOST`)
- Блокировка большинства горячих клавиш (Win, Alt+F4, Ctrl+Shift+Esc, Alt+Tab и т.д.)
- Блокировка мыши
- Смена мемных фраз каждые 5 секунд
- При неправильном коде — «кодирование» случайного `.txt` файла на рабочем столе арабским текстом
- Разблокировка по SHA-256 хешу пустой строки (`e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`)

## Требования

- **Windows 10 / 11** (x64 или x86)
- **Visual Studio 2022** (Community / Professional / Enterprise)
  - Workload: **Desktop development with C++**
  - Platform Toolset: **v143**
  - Windows SDK: **10.0** (любая актуальная версия)

## Структура репозитория

```
Project1/
├── Project1.sln                 # Solution
├── .gitignore
├── README.md
└── Project1/
    ├── main.cpp                 # Весь исходный код
    ├── Project1.vcxproj         # Проект MSBuild
    ├── Project1.vcxproj.filters
    └── app.manifest             # requireAdministrator
```

## Сборка

### Способ 1 — Visual Studio (рекомендуется)

1. Откройте `Project1.sln` в Visual Studio 2022.
2. Выберите конфигурацию:
   - **Release | x64** — основная (рекомендуется)
   - Debug | x64 — для отладки
3. Нажмите **Build → Build Solution** (или `Ctrl+Shift+B`).
4. Готовый `.exe` появится в:
   ```
   x64\Release\Project1.exe
   ```
   или
   ```
   x64\Debug\Project1.exe
   ```

### Способ 2 — Developer Command Prompt / MSBuild

```bat
cd путь\к\Project1
msbuild Project1.sln /p:Configuration=Release /p:Platform=x64
```

Или только проект:

```bat
msbuild Project1\Project1.vcxproj /p:Configuration=Release /p:Platform=x64
```

## Запуск

1. Запустите `Project1.exe` **от имени администратора** (манифест это требует).
2. Появится чёрный полноэкранный экран.
3. Чтобы разблокировать — введите код:
   ```
   e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
   ```
   и нажмите кнопку.

### Аварийный выход

- `Ctrl + Alt + Del` → Диспетчер задач (если система позволяет)
- Перезагрузка / выключение виртуальной машины
- Принудительное завершение процесса через другой компьютер / RDP

## Настройки проекта (.vcxproj)

| Параметр                      | Значение              | Комментарий                          |
|-------------------------------|-----------------------|--------------------------------------|
| Platform Toolset              | v143                  | Visual Studio 2022                   |
| WindowsTargetPlatformVersion  | 10.0                  | Windows 10/11 SDK                    |
| CharacterSet                  | Unicode               | `wchar_t` / `std::wstring`           |
| SubSystem                     | Windows               | `WinMain`, без консоли               |
| LanguageStandard              | stdcpp17              | C++17                                |
| Manifest                      | app.manifest          | `requireAdministrator`               |
| ConfigurationType             | Application           | `.exe`                               |

Манифест применяется во **всех** конфигурациях (Debug/Release × Win32/x64).

## Что **не** нужно коммитить в GitHub

- Папка `.vs/`
- Папки `x64/`, `Debug/`, `Release/`, `Win32/`
- `*.user`, `*.suo`, `*.pdb`, `*.obj`, `*.idb`, `ipch/`
- Любые артефакты сборки

Всё это уже покрыто `.gitignore`.

## Лицензия / Дисклеймер

Проект создан в образовательных целях.  
Автор не несёт ответственности за использование программы во вред.  
Используйте только на собственных машинах / виртуалках и с согласия владельца.

---

**Код разблокировки (для удобства):**
```
e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855
```
(это SHA-256 от пустой строки)
