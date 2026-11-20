#QuillBorne

QuillBorne — экспериментальный Windows Locker, написанный на C++ с использованием WinAPI. Проект демонстрирует механизмы системной блокировки пользовательского ввода, создания полноэкранного интерфейса и взаимодействия с файловой системой Windows.

Features

- Fullscreen lock screen — полноэкранное окно поверх остальных приложений.
- Keyboard interception — перехват и блокировка определённых клавиш и сочетаний.
- Mouse interception — блокировка взаимодействия с мышью.
- Unlock system — проверка кода разблокировки.
- Dynamic messages — автоматическая смена сообщений каждые 5 секунд.
- ASCII Art — встроенная ASCII-графика.
- Custom Win32 GUI — собственный интерфейс на базе Windows API.
- File interaction — демонстрация работы с файлами через Windows File API.

Technologies

- Language: C++
- Platform: Windows
- API: WinAPI
- Interface: Win32 GUI
- Build: Windows-compatible C++ compiler

How it works

После запуска приложение создаёт полноэкранное окно, устанавливает низкоуровневые перехватчики клавиатуры и мыши и отображает интерфейс с запросом кода разблокировки.

При правильном вводе программа снимает перехватчики и завершает работу. При неправильном вводе активируется предусмотренная в исходном коде функция перезаписи TXT-файла.

Disclaimer

This project is intended for educational and experimental purposes.

WARNING: Siner Locker contains functionality that can interfere with normal computer operation, including keyboard and mouse input interception and file overwriting.

Run only in an isolated virtual machine or disposable test environment. Do not use on personal or production systems.

Author

orientation0
