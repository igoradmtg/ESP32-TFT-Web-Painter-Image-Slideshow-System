# Импорт необходимых модулей из стандартной библиотеки и внешних пакетов
import os  # Для работы с файловой системой: чтение директорий, проверка путей
from typing import List  # Для аннотации типов списков (улучшает читаемость и поддержку IDE)
from fastapi import FastAPI, Response  # FastAPI — основной фреймворк, Response — для отправки бинарных данных (изображений)
# from fastapi.responses import StreamingResponse  # Это не используется, можно убрать
from PIL import Image  # Pillow — библиотека для обработки изображений (открытие, масштабирование, обрезка)
import io  # Для работы с байтовыми потоками в памяти (не сохраняем файлы на диск)

# Создаём экземпляр приложения FastAPI
app = FastAPI(title="Image Slideshow API",  # Название API (отображается в документации Swagger)
              description="Отдаёт изображения из папки c:/img/ по порядку, масштабированные до 240x320 с сохранением пропорций или с обрезкой по центру. Циклический режим.",  # Описание
              version="1.0.0")  # Версия API

# Путь к каталогу с изображениями — фиксированный, как указано в задании
IMG_DIR = "W://001//img_cats_9x16v1"

# Целевой размер изображения: ширина 240, высота 320 пикселей
TARGET_WIDTH = 240
TARGET_HEIGHT = 320

# Глобальная переменная для хранения списка имён файлов (чтобы не читать папку при каждом запросе)
image_files: List[str] = []

# Глобальный индекс текущего изображения в списке (для циклической выдачи)
current_index: int = 0

# Функция для загрузки списка JPG-файлов из директории при старте приложения
def load_image_files() -> None:
    global image_files  # Обращаемся к глобальной переменной
    if not os.path.exists(IMG_DIR):  # Проверяем, существует ли директория
        print(f"Ошибка: директория {IMG_DIR} не найдена!")  # Выводим в консоль (не в HTTP, т.к. это при старте)
        image_files = []  # Пустой список — API будет возвращать 503 или пустой ответ
        return

    # Получаем список всех элементов в папке
    all_files = os.listdir(IMG_DIR)
    
    # Фильтруем только файлы с расширением .jpg или .JPG (регистронезависимо)
    image_files = [
        f for f in all_files 
        if f.lower().endswith('.jpg') and os.path.isfile(os.path.join(IMG_DIR, f))
    ]
    
    # Сортируем имена файлов лексикографически (по алфавиту) — чтобы порядок был предсказуемым
    image_files.sort(key=str.lower)
    
    # Выводим в консоль информацию о количестве найденных файлов
    print(f"Загружено {len(image_files)} изображений из {IMG_DIR}")

# Вызываем функцию загрузки при старте приложения (до первого запроса)
load_image_files()

# Вспомогательная функция: масштабирование и обрезка изображения под размер 240x320
def resize_and_crop_image(image_path: str) -> bytes:
    """
    Открывает изображение, масштабирует с сохранением пропорций,
    если не вписывается — обрезает по центру до 240x320.
    Возвращает байты в формате JPEG.
    """
    try:
        # Открываем изображение с помощью Pillow
        with Image.open(image_path) as img:
            # Конвертируем в RGB (на случай, если изображение в CMYK или с альфа-каналом)
            if img.mode != 'RGB':
                img = img.convert('RGB')
            
            # Получаем оригинальные размеры
            orig_width, orig_height = img.size
            
            # Вычисляем соотношение сторон оригинала и целевого
            target_ratio = TARGET_WIDTH / TARGET_HEIGHT  # 240/320 = 0.75
            orig_ratio = orig_width / orig_height
            
            # Сначала масштабируем изображение так, чтобы оно полностью поместилось в 240x320
            if orig_ratio > target_ratio:
                # Изображение слишком широкое — масштабируем по высоте
                new_height = TARGET_HEIGHT
                new_width = int(TARGET_HEIGHT * orig_ratio)
            else:
                # Изображение слишком высокое или квадратное — масштабируем по ширине
                new_width = TARGET_WIDTH
                new_height = int(TARGET_WIDTH / orig_ratio)
            
            # Масштабируем изображение с высоким качеством (LANCZOS — антиалиасинг)
            img = img.resize((new_width, new_height), Image.Resampling.LANCZOS)
            
            # Теперь обрезаем лишнее по центру, чтобы получить ровно 240x320
            left = (new_width - TARGET_WIDTH) // 2
            top = (new_height - TARGET_HEIGHT) // 2
            right = left + TARGET_WIDTH
            bottom = top + TARGET_HEIGHT
            
            img = img.crop((left, top, right, bottom))
            
            # Создаём байтовый буфер в памяти
            img_byte_arr = io.BytesIO()
            
            # Сохраняем изображение в формат JPEG с качеством 90% (баланс размера и качества)
            img.save(img_byte_arr, format='JPEG', quality=90, optimize=True)
            
            # Возвращаем байты
            return img_byte_arr.getvalue()
    
    except Exception as e:
        # В случае ошибки (повреждённый файл и т.п.) — выводим в консоль и возвращаем None
        print(f"Ошибка обработки изображения {image_path}: {e}")
        return None

# Основной эндпоинт: GET /next-image — возвращает следующее изображение
@app.get("/next-image/", 
          response_class=Response,  # Указываем, что ответ — сырые байты
          summary="Получить следующее изображение", 
          description="Возвращает масштабированное изображение 240x320. Циклически перебирает все JPG в c:/img/")

async def get_next_image():
    global current_index
    
    if not image_files:
        # Если изображений нет, возвращаем ошибку 503 (Сервис недоступен)
        return Response(content="Нет изображений в папке", status_code=503)
    
    current_file = image_files[current_index]
    image_path = os.path.join(IMG_DIR, current_file)
    
    # 1. Обрабатываем изображение, получаем байты
    image_bytes = resize_and_crop_image(image_path)
    
    if image_bytes is None:
        # Если произошла ошибка при обработке, переходим к следующему файлу и возвращаем ошибку
        current_index = (current_index + 1) % len(image_files)
        return Response(content="Ошибка обработки изображения", status_code=500)
    
    # 2. Вычисляем размер байтов, которые будут отправлены
    image_size_bytes = len(image_bytes)
    
    # 3. Выводим информацию в консоль
    # Используем f-строку для удобного форматирования
    print(f"Отправлено: {current_file} | Размер: {image_size_bytes} байт ({image_size_bytes/1024:.2f} КБ)")
    
    # 4. Обновляем индекс для циклической выдачи
    current_index = (current_index + 1) % len(image_files)
    
    # 5. Возвращаем HTTP-ответ с байтами изображения
    return Response(
        content=image_bytes,
        media_type="image/jpeg",
        headers={
            "Content-Length": str(image_size_bytes),  # Отлично, что этот заголовок уже был!
            "Connection": "close",  
            "Cache-Control": "no-cache" 
        }
    )

# Дополнительный эндпоинт: GET / — корневая страница с инструкцией
@app.get("/", 
          summary="Главная страница", 
          description="Простая HTML-страница с инструкцией по использованию")
async def root():
    """
    Возвращает HTML-страницу с динамическими значениями:
    - Путь к папке с изображениями (IMG_DIR)
    - Размеры целевого изображения (TARGET_WIDTH x TARGET_HEIGHT)
    """
    # Используем f-строку для подстановки актуальных значений переменных
    html_content = f"""
    <html>
        <head><title>Image Slideshow API</title></head>
        <body>
            <h1>API для показа изображений</h1>
            <p>Используйте: <code>GET /next-image</code></p>
            <p>Каждый запрос возвращает следующее изображение из <code>{IMG_DIR}</code>, масштабированное до {TARGET_WIDTH}x{TARGET_HEIGHT}.</p>
            <p>Циклический режим. Поддерживаются только файлы <code>.jpg</code> (регистронезависимо).</p>
            <hr>
            <img src="/next-image/" alt="Текущее изображение" style="width:{TARGET_WIDTH}px; height:{TARGET_HEIGHT}px; object-fit: contain;">
        </body>
    </html>
    """
    # Возвращаем HTML с медиа-типом text/html
    return Response(content=html_content, media_type="text/html")

# Точка входа: запускаем сервер при прямом запуске скрипта (python script.py)
if __name__ == "__main__":
    import uvicorn  # Uvicorn — ASGI-сервер для запуска FastAPI
    # Запускаем на локальном хосте, порт 8083, с автоматической перезагрузкой при изменении кода
    uvicorn.run("image_server:app", host="0.0.0.0", port=8083, reload=True)