@echo off
:: ------------------------------------------------------------
:: Файл: start_image_server.cmd
:: Назначение: Запуск FastAPI-сервера из image_server.py
:: Требования:
::   1. Python 3.8+ (рекомендуется 3.10+)
::   2. Установленные зависимости:
::        pip install fastapi uvicorn pillow
::   3. Файл image_server.py лежит в той же папке
::   4. Папка c:/img/ существует и содержит .jpg файлы
:: ------------------------------------------------------------
chcp 65001 >nul
:: Переходим в директорию, где находится этот .cmd файл
cd /d "%~dp0"

:: Проверяем, установлен ли Python
python --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [ОШИБКА] Python не найден в PATH.
    echo Установите Python с https://python.org и добавьте в PATH.
    pause
    exit /b 1
)

:: Проверяем наличие image_server.py
if not exist "image_server.py" (
    echo [ОШИБКА] Файл image_server.py не найден в текущей папке!
    echo Текущая папка: %cd%
    pause
    exit /b 1
)

:: Проверяем папку с изображениями
if not exist "W:\001\img2\AI_girls_5" (
    echo [ПРЕДУПРЕЖДЕНИЕ] Папка W:\001\img2\AI_girls_5 не найдена!
    echo Создайте её и положите туда .jpg файлы.
    echo.
)

:: Запускаем сервер
echo [INFO] Запуск сервера...
echo Сервер будет доступен по адресу: http://0.0.0.0:8083
echo Документация: http://0.0.0.0:8083/docs
echo Нажмите Ctrl+C для остановки.
echo.
python -m uvicorn image_server:app --host 0.0.0.0 --port 8083 --reload

:: Если сервер упал — показываем сообщение
if %errorlevel% neq 0 (
    echo.
    echo [ОШИБКА] Сервер завершился с ошибкой.
    echo Проверьте вывод выше.
)

echo.
echo Нажмите любую клавишу для выхода...
pause >nul