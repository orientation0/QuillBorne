#!/usr/bin/env python3
"""
Простой шифратор файлов с графическим интерфейсом.
Поддерживает AES-256-GCM и ChaCha20-Poly1305.
Шифрование по паролю (PBKDF2).
"""

import os
import sys
import struct
import secrets
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox, scrolledtext
from pathlib import Path

from cryptography.hazmat.primitives.ciphers.aead import AESGCM, ChaCha20Poly1305
from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.backends import default_backend

# Константы
MAGIC = b"FENC"
VERSION = 1
SALT_SIZE = 16
NONCE_SIZE = 12
KEY_SIZE = 32
PBKDF2_ITERATIONS = 600_000
CHUNK_SIZE = 1024 * 1024  # 1 MB

ALGORITHMS = {
    "AES-256-GCM": 1,
    "ChaCha20-Poly1305": 2,
}
ALGO_NAMES = {v: k for k, v in ALGORITHMS.items()}


def derive_key(password: str, salt: bytes) -> bytes:
    kdf = PBKDF2HMAC(
        algorithm=hashes.SHA256(),
        length=KEY_SIZE,
        salt=salt,
        iterations=PBKDF2_ITERATIONS,
        backend=default_backend(),
    )
    return kdf.derive(password.encode("utf-8"))


def encrypt_file(src: Path, dst: Path, password: str, algo_name: str, progress_callback=None):
    algo_id = ALGORITHMS[algo_name]
    salt = secrets.token_bytes(SALT_SIZE)
    key = derive_key(password, salt)
    nonce = secrets.token_bytes(NONCE_SIZE)

    if algo_id == 1:
        aead = AESGCM(key)
    else:
        aead = ChaCha20Poly1305(key)

    file_size = src.stat().st_size
    processed = 0

    # Читаем весь файл (для простоты и корректной работы AEAD)
    # Для очень больших файлов (>2 ГБ) лучше использовать потоковый режим,
    # но AEAD требует весь plaintext для тега.
    data = src.read_bytes()
    ciphertext = aead.encrypt(nonce, data, None)

    with open(dst, "wb") as f:
        # Заголовок: MAGIC (4) + VERSION (1) + ALGO (1) + SALT (16) + NONCE (12)
        f.write(MAGIC)
        f.write(struct.pack("BB", VERSION, algo_id))
        f.write(salt)
        f.write(nonce)
        f.write(ciphertext)

    if progress_callback:
        progress_callback(100)


def decrypt_file(src: Path, dst: Path, password: str, progress_callback=None):
    with open(src, "rb") as f:
        magic = f.read(4)
        if magic != MAGIC:
            raise ValueError("Это не файл, созданный этим приложением (неверный заголовок).")

        version, algo_id = struct.unpack("BB", f.read(2))
        if version != VERSION:
            raise ValueError(f"Неподдерживаемая версия формата: {version}")

        if algo_id not in ALGO_NAMES:
            raise ValueError(f"Неизвестный алгоритм: {algo_id}")

        salt = f.read(SALT_SIZE)
        nonce = f.read(NONCE_SIZE)
        ciphertext = f.read()

    key = derive_key(password, salt)

    if algo_id == 1:
        aead = AESGCM(key)
    else:
        aead = ChaCha20Poly1305(key)

    try:
        plaintext = aead.decrypt(nonce, ciphertext, None)
    except Exception:
        raise ValueError("Неверный пароль или файл повреждён.")

    dst.write_bytes(plaintext)

    if progress_callback:
        progress_callback(100)


class EncryptorApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("Шифратор файлов")
        self.geometry("640x520")
        self.minsize(560, 480)
        self.configure(bg="#1e1e2e")

        # Стиль
        style = ttk.Style(self)
        style.theme_use("clam")
        style.configure(".", background="#1e1e2e", foreground="#cdd6f4", fieldbackground="#313244")
        style.configure("TLabel", background="#1e1e2e", foreground="#cdd6f4", font=("Segoe UI", 10))
        style.configure("TButton", font=("Segoe UI", 10), padding=6)
        style.configure("TRadiobutton", background="#1e1e2e", foreground="#cdd6f4", font=("Segoe UI", 10))
        style.configure("TCheckbutton", background="#1e1e2e", foreground="#cdd6f4")
        style.configure("TEntry", fieldbackground="#313244", foreground="#cdd6f4")
        style.configure("TCombobox", fieldbackground="#313244", foreground="#cdd6f4")
        style.map("TButton", background=[("active", "#45475a")])
        style.configure("Accent.TButton", background="#89b4fa", foreground="#1e1e2e")
        style.map("Accent.TButton", background=[("active", "#74c7ec")])
        style.configure("Danger.TButton", background="#f38ba8", foreground="#1e1e2e")
        style.map("Danger.TButton", background=[("active", "#eba0ac")])

        self.files = []
        self.mode = tk.StringVar(value="encrypt")
        self.algo = tk.StringVar(value="AES-256-GCM")
        self.delete_original = tk.BooleanVar(value=False)
        self.output_dir = tk.StringVar(value="")

        self._build_ui()

    def _build_ui(self):
        main = ttk.Frame(self, padding=16)
        main.pack(fill=tk.BOTH, expand=True)

        # Заголовок
        title = ttk.Label(main, text="🔒 Шифратор файлов", font=("Segoe UI", 16, "bold"))
        title.pack(pady=(0, 12))

        # Режим
        mode_frame = ttk.LabelFrame(main, text=" Режим ", padding=8)
        mode_frame.pack(fill=tk.X, pady=6)
        ttk.Radiobutton(mode_frame, text="Зашифровать", variable=self.mode, value="encrypt",
                        command=self._update_ui).pack(side=tk.LEFT, padx=12)
        ttk.Radiobutton(mode_frame, text="Расшифровать", variable=self.mode, value="decrypt",
                        command=self._update_ui).pack(side=tk.LEFT, padx=12)

        # Алгоритм
        algo_frame = ttk.LabelFrame(main, text=" Алгоритм ", padding=8)
        algo_frame.pack(fill=tk.X, pady=6)
        ttk.Radiobutton(algo_frame, text="AES-256-GCM (рекомендуется)", variable=self.algo,
                        value="AES-256-GCM").pack(side=tk.LEFT, padx=12)
        ttk.Radiobutton(algo_frame, text="ChaCha20-Poly1305", variable=self.algo,
                        value="ChaCha20-Poly1305").pack(side=tk.LEFT, padx=12)

        # Файлы
        files_frame = ttk.LabelFrame(main, text=" Файлы ", padding=8)
        files_frame.pack(fill=tk.BOTH, expand=True, pady=6)

        btn_row = ttk.Frame(files_frame)
        btn_row.pack(fill=tk.X)
        ttk.Button(btn_row, text="Выбрать файлы…", command=self._select_files).pack(side=tk.LEFT)
        ttk.Button(btn_row, text="Очистить список", command=self._clear_files).pack(side=tk.LEFT, padx=8)

        self.file_list = scrolledtext.ScrolledText(
            files_frame, height=6, bg="#313244", fg="#cdd6f4",
            insertbackground="#cdd6f4", relief=tk.FLAT, font=("Consolas", 9)
        )
        self.file_list.pack(fill=tk.BOTH, expand=True, pady=(8, 0))

        # Пароль
        pass_frame = ttk.LabelFrame(main, text=" Пароль ", padding=8)
        pass_frame.pack(fill=tk.X, pady=6)

        ttk.Label(pass_frame, text="Пароль:").grid(row=0, column=0, sticky=tk.W, padx=4)
        self.pass_entry = ttk.Entry(pass_frame, show="•", width=36)
        self.pass_entry.grid(row=0, column=1, sticky=tk.EW, padx=4, pady=2)

        self.confirm_label = ttk.Label(pass_frame, text="Подтверждение:")
        self.confirm_label.grid(row=1, column=0, sticky=tk.W, padx=4)
        self.confirm_entry = ttk.Entry(pass_frame, show="•", width=36)
        self.confirm_entry.grid(row=1, column=1, sticky=tk.EW, padx=4, pady=2)

        pass_frame.columnconfigure(1, weight=1)

        # Опции
        opt_frame = ttk.Frame(main)
        opt_frame.pack(fill=tk.X, pady=6)
        ttk.Checkbutton(opt_frame, text="Удалить исходные файлы после успешной операции",
                        variable=self.delete_original).pack(side=tk.LEFT)

        # Кнопка запуска
        self.run_btn = ttk.Button(main, text="Зашифровать", style="Accent.TButton",
                                  command=self._start)
        self.run_btn.pack(fill=tk.X, pady=10)

        # Прогресс и лог
        self.progress = ttk.Progressbar(main, mode="indeterminate")
        self.progress.pack(fill=tk.X, pady=(0, 6))

        self.log = scrolledtext.ScrolledText(
            main, height=5, bg="#181825", fg="#a6e3a1",
            insertbackground="#a6e3a1", relief=tk.FLAT, font=("Consolas", 9), state=tk.DISABLED
        )
        self.log.pack(fill=tk.BOTH, expand=False)

        self._update_ui()

    def _update_ui(self):
        if self.mode.get() == "encrypt":
            self.run_btn.configure(text="🔒 Зашифровать выбранные файлы")
            self.confirm_label.grid()
            self.confirm_entry.grid()
        else:
            self.run_btn.configure(text="🔓 Расшифровать выбранные файлы")
            self.confirm_label.grid_remove()
            self.confirm_entry.grid_remove()

    def _select_files(self):
        paths = filedialog.askopenfilenames(title="Выберите файлы")
        if paths:
            for p in paths:
                if p not in self.files:
                    self.files.append(p)
            self._refresh_list()

    def _clear_files(self):
        self.files.clear()
        self._refresh_list()

    def _refresh_list(self):
        self.file_list.delete("1.0", tk.END)
        for p in self.files:
            self.file_list.insert(tk.END, p + "\n")

    def _log(self, msg: str, error=False):
        self.log.configure(state=tk.NORMAL)
        tag = "err" if error else "ok"
        self.log.insert(tk.END, msg + "\n", tag)
        self.log.tag_config("err", foreground="#f38ba8")
        self.log.tag_config("ok", foreground="#a6e3a1")
        self.log.see(tk.END)
        self.log.configure(state=tk.DISABLED)

    def _start(self):
        if not self.files:
            messagebox.showwarning("Нет файлов", "Сначала выберите файлы.")
            return

        password = self.pass_entry.get()
        if not password:
            messagebox.showwarning("Пароль", "Введите пароль.")
            return

        if self.mode.get() == "encrypt":
            if password != self.confirm_entry.get():
                messagebox.showerror("Пароль", "Пароли не совпадают.")
                return
            if len(password) < 8:
                if not messagebox.askyesno("Слабый пароль",
                                           "Пароль короче 8 символов. Продолжить?"):
                    return

        self.run_btn.configure(state=tk.DISABLED)
        self.progress.start(12)
        self._log("Начинаю…")

        thread = threading.Thread(target=self._worker, args=(password,), daemon=True)
        thread.start()

    def _worker(self, password: str):
        mode = self.mode.get()
        algo = self.algo.get()
        delete = self.delete_original.get()
        success = 0
        errors = 0

        for path_str in self.files:
            src = Path(path_str)
            try:
                if mode == "encrypt":
                    dst = src.with_suffix(src.suffix + ".enc")
                    self.after(0, lambda s=src.name: self._log(f"Шифрую: {s}"))
                    encrypt_file(src, dst, password, algo)
                    self.after(0, lambda d=dst.name: self._log(f"  → {d}"))
                else:
                    if not src.name.endswith(".enc"):
                        raise ValueError("Файл должен иметь расширение .enc")
                    dst = src.with_suffix("")  # убираем .enc
                    # если осталось двойное расширение типа .txt.enc → .txt
                    if dst.suffix == "" and "." in src.stem:
                        # уже обработано with_suffix
                        pass
                    self.after(0, lambda s=src.name: self._log(f"Расшифровываю: {s}"))
                    decrypt_file(src, dst, password)
                    self.after(0, lambda d=dst.name: self._log(f"  → {d}"))

                if delete:
                    src.unlink()
                    self.after(0, lambda: self._log("  (исходный файл удалён)"))

                success += 1
            except Exception as e:
                errors += 1
                self.after(0, lambda m=str(e): self._log(f"Ошибка: {m}", error=True))

        def finish():
            self.progress.stop()
            self.run_btn.configure(state=tk.NORMAL)
            self._log(f"Готово. Успешно: {success}, ошибок: {errors}")
            if errors == 0 and success > 0:
                messagebox.showinfo("Готово", f"Обработано файлов: {success}")
            elif errors:
                messagebox.showwarning("Завершено с ошибками",
                                       f"Успешно: {success}\nОшибок: {errors}")

        self.after(0, finish)


if __name__ == "__main__":
    app = EncryptorApp()
    app.mainloop()
