/*********************************************************************
  LovyanGFX + ILI9341 на ESP32 + Wi-Fi Web Server (v7)
  - Слайдшоу по HTTP (JPG/PNG/BMP)
  - Обёртка ClientWrapper → drawJpg/Png/Bmp
*********************************************************************/
#include <WiFi.h>          // Базовая библиотека для Wi-Fi подключения на ESP32.
#include <HTTPClient.h>    // Библиотека для HTTP-запросов, должна быть перед LovyanGFX для активации обёрток.
#include <MQ135.h>               // Библиотека для MQ135: getPPM(), getRZero(),
// Теперь включаем LovyanGFX — после HTTPClient, чтобы библиотека "увидела" поддержку HTTP
// и определила ClientWrapper в namespace lgfx::v1.
#include <LovyanGFX.hpp>   // Основная графическая библиотека для TFT-дисплеев.

// Другие includes — они не влияют на conditional compilation LovyanGFX.
#include <WebServer.h>     // Библиотека для создания веб-сервера на ESP32.
#include <ArduinoJson.h>   // Библиотека для работы с JSON (парсинг/сериализация).

#include "index_html_gz.h"
// Добавь эти определения в начало кода (после #include <LovyanGFX.hpp>)
#define TFT_GRAY        0x7BEF    // светло-серый
#define TFT_DARKGREY    0x4208    // тёмно-серый
#define TFT_LIGHTGREY   0xC618    // ещё светлее
#define TFT_CYAN        0x07FF    // яркий циан
#define TFT_DARKCYAN    0x03EF    // тёмный циан

const int MQ135_PIN = 34;                    // Аналоговый пин AOUT от MQ-135
bool sensor_mode_active = false;             // Флаг: сейчас показываем данные датчика вместо слайдшоу
unsigned long last_sensor_update = 0;        // Время последнего обновления экрана с данными датчика
const unsigned long sensor_update_interval = 2000;  // Обновлять экран каждые 2 секунды
// ────────────────────── Глобальные переменные для графика (добавь в начало кода) ──────────────────────
// ────────────────────── Глобальные переменные для графика и MQ135 ──────────────────────
#define GRAPH_WIDTH   240                // Количество точек = ширина графика в пикселях (последние 240 измерений)
#define GRAPH_X       40                 // Начало графика по X (с отступом от края экрана)
#define GRAPH_Y       80                 // Верх графика (оставляем место для заголовка и текущих значений)
#define GRAPH_HEIGHT  120                // Высота графика (для отображения 0–2500 ppm)

int ppm_history[GRAPH_WIDTH] = {0};      // Кольцевой буфер: хранит последние 240 значений ppm (история ~8 мин при интервале 2с)
int graph_index = 0;                     // Текущая позиция записи в буфере (циклически 0..239)
bool graph_filled = false;               // Флаг: буфер заполнен полностью? (для плавного старта графика)


const float VOLTAGE_DIVIDER_RATIO = 1.5f; // Коэффициент делителя 10к (от AOUT) + 20к (к GND): (10+20)/20 = 1.5
const float RLOAD = 1000.0f;             // Номинал подтягивающего резистора на модуле MQ135 (обычно 1 кОм — проверьте на плате!)

MQ135 gasSensor = MQ135(MQ135_PIN);      // Объект библиотеки: инициализируем с пином ADC (автоматически использует analogRead)
bool calibrated = false;                 // Флаг: выполнена ли калибровка Ro? (первый раз — в чистом воздухе)
float calibrated_Ro = 0.0f;              // Сохранённое значение Ro (сопротивление в чистом воздухе ~400 ppm)
// ────────────────────── Параметры для коррекции PPM (температура и влажность, по умолчанию из примера библиотеки) ──────────────────────
const float TEMP_C = 20.0f;      // Температура в °C (используйте 20°C для калибровки в помещении; подключите DHT11 для динамики)
const float HUMIDITY_PCT = 65.0f; // Относительная влажность в % (стандарт 65%; влияет на точность, но без датчика — фиксировано)
// === НАСТРОЙКИ Wi-Fi ===
const char* ssid = "pi3da";      // Замените на вашу сеть
const char* password = "igor456258#";  // Замените на пароль
WebServer server(80);
HTTPClient http;
// === Дисплей ===
class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel_instance;
  lgfx::Bus_SPI _bus_instance;
  lgfx::Light_PWM _light_instance;
public:
LGFX(void) {
    // === НАСТРОЙКА SPI ===
    {
      auto cfg = _bus_instance.config();  // Получаем конфигурацию шины
      cfg.freq_write = 27000000;          // Частота записи (27 МГц)
      cfg.freq_read = 16000000;           // Частота чтения
      cfg.spi_mode = 0;                   // Режим SPI
      cfg.pin_sclk = 18;                  // Пин SCK
      cfg.pin_mosi = 23;                  // Пин MOSI
      cfg.pin_miso = -1;                  // MISO не используется
      cfg.pin_dc = 27;                    // Пин DC (Data/Command)
      cfg.spi_host = VSPI_HOST;           // Используем VSPI
      cfg.dma_channel = 1;                // Канал DMA
      _bus_instance.config(cfg);          // Применяем конфигурацию
      _panel_instance.setBus(&_bus_instance); // Привязываем шину к панели
    }
    // === НАСТРОЙКА ПАНЕЛИ ===
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 5;                     // Пин CS
      cfg.pin_rst = 14;                   // Пин RST
      cfg.panel_width = 320;              // Ширина экрана
      cfg.panel_height = 320;             // Высота экрана
      cfg.offset_x = 0;                   // Смещение по X
      cfg.offset_y = 0;                   // Смещение по Y
      cfg.offset_rotation = 0;            // Поворот
      cfg.dummy_read_pixel = 8;           // Байт-заполнитель при чтении
      cfg.dummy_read_bits = 1;            // Бит-заполнитель
      cfg.readable = false;               // Чтение не требуется
      cfg.invert = false;                 // Инверсия цветов
      cfg.rgb_order = true;              // Порядок RGB
      cfg.dlen_16bit = false;             // 8-битный режим
      cfg.bus_shared = true;             // Шина не общая
      _panel_instance.config(cfg);        // Применяем конфигурацию
    }
    // === ПОДСВЕТКА НА GPIO 21 ===
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = 21;                    // Пин подсветки
      cfg.invert = false;                 // false — HIGH = включено
      cfg.freq = 1200;                    // Частота ШИМ
      cfg.pwm_channel = 7;                // Свободный канал ШИМ
      _light_instance.config(cfg);        // Применяем
      _panel_instance.setLight(&_light_instance); // Привязываем подсветку
    }
    setPanel(&_panel_instance);          // Устанавливаем панель как основную
  }
};
// Кастомная обертка для WiFiClient, чтобы использовать его как источник данных для drawJpg/drawPng/drawBmp.
// Наследует от lgfx::v1::DataWrapper - базового класса LovyanGFX для источников данных (streams).
// Назначение: Адаптирует WiFiClient* (поток от HTTPClient) для чтения данных изображений по сети.
// Параметры: Нет конструктора с параметрами, используется метод set для привязки клиента.
// Описание: Реализует необходимые методы read (чтение буфера), skip (пропуск байт), available (доступные байты),
// close (закрытие, опционально), seek (позиционирование, не поддерживается для потоков).
// Примеры использования:
// ClientWrapper wrapper;
// wrapper.set(http.getStreamPtr());
// tft.drawJpg(&wrapper, 0, 0, 240, 320);
struct ClientWrapper : public lgfx::v1::DataWrapper {
  WiFiClient* client = nullptr;  // Указатель на WiFiClient (поток от HTTP).

  // Метод для привязки клиента (вызывается перед drawJpg).
  // Параметры: WiFiClient* s - указатель на поток.
  // Описание: Устанавливает клиент для чтения.
  // Пример: clientWrapper.set(http.getStreamPtr());
  void set(WiFiClient* s) { client = s; }

  // Обязательный метод: Чтение данных в буфер.
  // Параметры: uint8_t* buf - буфер для данных; uint32_t len - длина для чтения.
  // Описание: Читает len байт из потока в buf. Возвращает количество прочитанных байт или -1 при ошибке.
  // Пример: Используется внутренне декодером Jpeg/Png.
  int read(uint8_t* buf, uint32_t len) override {
    if (!client) return -1;  // Если клиент не установлен, ошибка.
    return client->read(buf, len);  // Чтение из WiFiClient.
  }

  // Метод пропуска байт (нужен для некоторых декодеров, напр. PNG для пропуска чанков).
  // Параметры: int32_t offset - количество байт для пропуска (положительное).
  // Описание: Пропускает offset байт в потоке, читая их по частям в временный буфер.
  // Пример: Декодер вызывает для игнора ненужных данных.
  void skip(int32_t offset) override {
    if (offset <= 0 || !client) return;  // Если offset <=0 или нет клиента, ничего не делать.
    uint8_t tmp[256];  // Временный буфер для пропуска (размер можно увеличить для оптимизации).
    while (offset > 0) {
      uint32_t len = (offset > sizeof(tmp)) ? sizeof(tmp) : offset;  // Читаем по частям.
      int read_len = read(tmp, len);  // Чтение в tmp.
      if (read_len <= 0) break;  // Если ошибка чтения, остановка.
      offset -= read_len;  // Уменьшаем offset.
    }
  }

  // Метод проверки доступных байт (опционально, но полезно для эффективности).
  // Параметры: Нет.
  // Описание: Возвращает количество доступных байт в потоке или 1 (если неизвестно).
  // Пример: Декодер проверяет, есть ли данные.
  int available() {
    if (!client) return 0;
    return client->available() ? client->available() : 1;  // Если 0, декодер может ждать.
  }

  // Метод закрытия (опционально, вызывается после рисования).
  // Параметры: Нет.
  // Описание: Здесь ничего не делает, так как HTTPClient управляет закрытием.
  // Пример: tft.drawJpg вызывает после завершения.
  void close() override {
    // Можно добавить client->stop(), но лучше оставить на HTTPClient.
  }

  // Метод позиционирования (seek, не поддерживается для последовательных потоков как WiFiClient).
  // Параметры: uint32_t offset - позиция.
  // Описание: Возвращает false, так как поток не поддерживает seek (только вперед).
  // Пример: Если декодер требует seek, это вызовет ошибку (редко для JPG/PNG).
  bool seek(uint32_t offset) override {
    return false;  // Не поддерживается.
  }
  // Метод получения текущей позиции в потоке (tell).
  // Параметры: Нет.
  // Описание: Эта функция переопределяет pure virtual метод из lgfx::v1::DataWrapper. Поскольку поток WiFiClient является последовательным и не поддерживает произвольное позиционирование (seek), возвращаем -1, указывая на отсутствие поддержки tell. Это позволяет классу ClientWrapper стать конкретным (не абстрактным) и создать экземпляр. Без реализации tell() компилятор считает класс абстрактным, что приводит к ошибке при объявлении переменной clientWrapper.
  // Примеры использования: Вызывается внутренне декодерами изображений (Jpg/Png/Bmp) для проверки или корректировки позиции в данных. В нашем случае, поскольку seek не поддерживается, декодеры должны обрабатывать поток последовательно без reposition.
  int32_t tell(void) override {
  return -1;  // Позиция неизвестна или не поддерживается для потокового чтения.
  }

};

LGFX tft;
ClientWrapper clientWrapper;   // ← Глобально, кастомная обертка
// Текущий уровень подсветки (0-255)
uint8_t backlight_level = 255;        
// если ошибка – подождём 3 сек
unsigned long slideshow_retry_delay = 3000;  

// === Состояние слайдшоу ===
bool slideshow_active = false;
String slideshow_url = "http://192.168.0.70:8083/next-image/";
unsigned long last_slideshow_time = 0;
const unsigned long slideshow_interval = 8500;  // 6 секунд
// === Случайные цвета для звёзд (16-бит) ===
const uint16_t star_colors[] = {
  0xFFFF, 0xF81F, 0xFFE0, 0x7FFF, 0x07FF, 0xAFE5, 0xFBCF,
  0xFD20, 0x7BE0, 0xF800, 0x07E0, 0x001F, 0xA254, 0xC618
};
const int num_star_colors = sizeof(star_colors) / sizeof(star_colors[0]);

// === Главная страница ===
void handleRoot() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "max-age=86400");
  server.send_P(200, "text/html", (const char*)index_html_gz, index_html_gz_size);
}

// === Рисование ===
void handleDraw() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }

  String body = server.arg("plain");
  DynamicJsonDocument doc(256);
  DeserializationError err = deserializeJson(doc, body);
  if (err) { server.send(400, "text/plain", "Invalid JSON"); return; }

  String shape = doc["shape"] | "";
  int x1 = doc["x1"] | 0;
  int y1 = doc["y1"] | 0;
  int x2 = doc["x2"] | 0;
  int y2 = doc["y2"] | 0;
  uint16_t color = doc["color"] | TFT_WHITE;

  if (shape == "line") tft.drawLine(x1, y1, x2, y2, color);
  else if (shape == "rect") tft.drawRect(x1, y1, x2 - x1, y2 - y1, color);
  else if (shape == "fillrect") tft.fillRect(x1, y1, x2 - x1, y2 - y1, color);
  else if (shape == "circle") tft.drawCircle(x1, y1, x2, color);
  else if (shape == "fillcircle") tft.fillCircle(x1, y1, x2, color);

  server.send(200, "text/plain", "OK");
}

// === Очистка ===
void handleClear() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }

  String body = server.arg("plain");
  DynamicJsonDocument doc(128);
  deserializeJson(doc, body);
  uint16_t color = doc["color"] | TFT_BLACK;

  tft.fillScreen(color);
  server.send(200, "text/plain", "Cleared");
}

// === ОБРАБОТЧИК ДЛЯ УПРАВЛЕНИЯ ПОДСВЕТКОЙ ===
void handleBacklight() {
  if (server.method() != HTTP_POST) {                 // Проверяем, что запрос POST
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  String body = server.arg("plain");                  // Получаем тело запроса
  DynamicJsonDocument doc(128);                       // Небольшой JSON-документ
  DeserializationError err = deserializeJson(doc, body);

  if (err || !doc.containsKey("level")) {             // Если JSON некорректный или нет поля level
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  int level = doc["level"];                           // Получаем значение уровня
  level = constrain(level, 0, 255);                   // Ограничиваем диапазон 0-255

  tft.setBrightness(level);                           // Устанавливаем яркость через LovyanGFX
  backlight_level = level;                            // Сохраняем текущее значение (на всякий случай)

  server.send(200, "text/plain", "OK");               // Успешный ответ
}
// === Звёздное небо ===
void handleStars() {
  if (server.method() != HTTP_POST) { server.send(405, "text/plain", "Method Not Allowed"); return; }

  String body = server.arg("plain");
  DynamicJsonDocument doc(64);
  deserializeJson(doc, body);
  int count = doc["count"] | 150;  // По умолчанию 150 звёзд

  randomSeed(millis());  // Инициализация ГСЧ
  for (int i = 0; i < count; i++) {
    int x = random(0, 240);
    int y = random(0, 320);
    int size = 1;   
    uint16_t color = star_colors[random(0, num_star_colors)];

    tft.drawPixel(x, y, color);
  }

  server.send(200, "text/plain", "Stars drawn");
}
// ────────────────────── Слайдшоу вкл/выкл ──────────────────────
void handleSlideshow() {
  if (server.method() != HTTP_POST) { 
    server.send(405, "text/plain", "Method Not Allowed"); 
    return; 
  }
  String body = server.arg("plain");
  DynamicJsonDocument doc(256);
  if (deserializeJson(doc, body)) { server.send(400, "text/plain", "Invalid JSON"); return; }

  slideshow_active = doc["active"] | false;
  if (doc.containsKey("url")) {
    slideshow_url = doc["url"].as<String>();
  }

  if (slideshow_active) {
    sensor_mode_active = false;
    last_slideshow_time = millis() - slideshow_interval;   // сразу загрузить
    server.send(200, "text/plain", "Slideshow started");
  } else {
    server.send(200, "text/plain", "Slideshow stopped");
  }
}

// ────────────────────── Режим отображения данных с MQ-135 ──────────────────────
void handleSensorMode() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }

  String body = server.arg("plain");
  DynamicJsonDocument doc(128);
  if (deserializeJson(doc, body)) {
    server.send(400, "text/plain", "Invalid JSON");
    return;
  }

  bool enable = doc["active"] | false;

  if (enable) {
    slideshow_active = false;        // Отключаем слайдшоу
    sensor_mode_active = true;       // Включаем режим датчика
    last_sensor_update = 0;          // Сразу обновим экран
    server.send(200, "text/plain", "Sensor mode ON");
  } else {
    sensor_mode_active = false;
    server.send(200, "text/plain", "Sensor mode OFF");
  }
}
void updateSensorDisplay() {
  if (!sensor_mode_active) return;                                      // Если режим датчика выключен — выходим
  if (millis() - last_sensor_update < sensor_update_interval) return;  // Обновляем не чаще чем раз в 2 секунды

  last_sensor_update = millis();                                        // Запоминаем время последнего обновления

  int raw_adc = analogRead(MQ135_PIN);                                  // Считываем сырое значение с пина (0–4095)

  // === КАЛИБРОВКА (один раз при первом запуске режима датчика) ===
  if (!calibrated) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(70, 60);
    tft.println("MQ-135");

    tft.setCursor(50, 100);
    tft.println("Calibrating...");

    tft.setTextSize(1);
    tft.setCursor(30, 150);
    tft.println("60 sec warm-up");
    tft.setCursor(30, 170);
    tft.println("Keep in clean air!");
    tft.setCursor(30, 190);
    tft.println("(open window)");

    // Обратный отсчёт 60 секунд
    for (int i = 60; i > 0; i--) {
      tft.fillRect(130, 220, 70, 20, TFT_BLACK);                       // Стираем предыдущее значение
      tft.setTextColor(TFT_YELLOW);
      tft.setCursor(140, 225);
      tft.printf("%02d sec", i);                                       // Выводим секунды с ведущим нулём
      delay(1000);
    }

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN);
    tft.setTextSize(2);
    tft.setCursor(60, 100);
    tft.println("Measuring Ro...");

    calibrated_Ro = gasSensor.getRZero();                             // Калибруем Ro (без параметров!)
    calibrated = true;                                                // Помечаем как откалиброванный

    // Результат калибровки
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN);
    tft.setTextSize(2);
    tft.setCursor(70, 70);
    tft.println("Done!");

    tft.setTextColor(TFT_WHITE);
    tft.setCursor(40, 120);
    tft.print("Ro = ");
    tft.setTextColor(TFT_YELLOW);
    tft.printf("%.1f Ohm", calibrated_Ro);

    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(1);
    tft.setCursor(40, 170);
    tft.println("Sensor calibrated");
    tft.setCursor(40, 190);
    tft.println("and ready to use");

    delay(4000);                                                      // Даём пользователю прочитать

    Serial.printf("[MQ135] Calibration complete: Ro = %.2f Ohm\n", calibrated_Ro);
  }

  // === ЧТЕНИЕ PPM ===
  float ppm_float = gasSensor.getCorrectedPPM(20.0f, 65.0f);            // Температура 20°C, влажность 65%
  int current_ppm = (int)(ppm_float * VOLTAGE_DIVIDER_RATIO);          // Корректируем за счёт делителя 10к+20к (×1.5)
  current_ppm = constrain(current_ppm, 0, 5000);                       // Ограничиваем диапазон

  // === Запись в буфер истории графика ===
  ppm_history[graph_index] = current_ppm;
  graph_index = (graph_index + 1) % GRAPH_WIDTH;
  if (graph_index == 0) graph_filled = true;

  // === Отрисовка экрана ===
  tft.fillScreen(TFT_BLACK);

  // Заголовок
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(2);
  tft.setCursor(50, 15);
  tft.println("MQ-135 Live Graph");

  // Текущее значение CO2
  tft.setTextSize(3);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(80, 40);
  tft.printf("%d ppm", current_ppm);

  // Напряжение на датчике (реальное, 0–5В)
  float real_voltage = raw_adc * (5.0f / 4095.0f);
  tft.setTextSize(2);
  tft.setTextColor(TFT_YELLOW);
  tft.setCursor(20, 70);
  tft.printf("%.2fV", real_voltage);

  // Оси графика
  tft.drawRect(GRAPH_X - 1, GRAPH_Y - 1, GRAPH_WIDTH + 2, GRAPH_HEIGHT + 2, TFT_WHITE);
  tft.drawFastHLine(GRAPH_X, GRAPH_Y + GRAPH_HEIGHT / 2, GRAPH_WIDTH, TFT_DARKGREY);
  tft.drawFastVLine(GRAPH_X + GRAPH_WIDTH / 2, GRAPH_Y, GRAPH_HEIGHT, TFT_DARKGREY);

  // Подписи шкалы
  tft.setTextSize(1);
  tft.setTextColor(TFT_GRAY);
  tft.setCursor(GRAPH_X - 35, GRAPH_Y - 5);           tft.print("2000");
  tft.setCursor(GRAPH_X - 35, GRAPH_Y + GRAPH_HEIGHT / 2 - 5); tft.print("1000");
  tft.setCursor(GRAPH_X - 35, GRAPH_Y + GRAPH_HEIGHT - 5);   tft.print("0");

  // Рисуем график (240 точек)
  int start_idx = graph_filled ? graph_index : 0;
  int points = graph_filled ? GRAPH_WIDTH : graph_index;

  for (int i = 1; i < points; i++) {
    int idx1 = (start_idx + i - 1) % GRAPH_WIDTH;
    int idx2 = (start_idx + i) % GRAPH_WIDTH;

    int y1 = GRAPH_Y + GRAPH_HEIGHT - map(ppm_history[idx1], 0, 2000, 0, GRAPH_HEIGHT);
    int y2 = GRAPH_Y + GRAPH_HEIGHT - map(ppm_history[idx2], 0, 2000, 0, GRAPH_HEIGHT);

    int x1 = GRAPH_X + i - 1;
    int x2 = GRAPH_X + i;

    uint16_t line_color = (ppm_history[idx2] > 1200) ? TFT_RED :
                          (ppm_history[idx2] > 800)  ? TFT_YELLOW : TFT_GREEN;

    tft.drawLine(x1, y1, x2, y2, line_color);
  }

  // Последняя точка — белый круг
  if (points > 0) {
    int last_y = GRAPH_Y + GRAPH_HEIGHT - map(current_ppm, 0, 2000, 0, GRAPH_HEIGHT);
    tft.fillCircle(GRAPH_X + points - 1, last_y, 3, TFT_WHITE);
  }

  // Нижняя подпись
  tft.setTextColor(TFT_DARKGREY);
  tft.setTextSize(1);
  tft.setCursor(30, 310);
  tft.print("240 points | Ro=");
  tft.print((int)calibrated_Ro);
  tft.print(" | 10k+20k divider");
}
void updateSlideshow() {
  if (!slideshow_active) return;
  if (millis() - last_slideshow_time < 8500) return;  // ← МИНИМУМ 8.5 секунд!

  last_slideshow_time = millis();

  Serial.println("\n[SLIDE] Requesting next image...");

  HTTPClient http;  // ← ЛОКАЛЬНАЯ переменная! Это важно!
  http.setTimeout(20000);
  http.setConnectTimeout(10000);
  http.setReuse(false);
  http.begin(slideshow_url);
  http.addHeader("Connection", "close");

  int httpCode = http.GET();

  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[SLIDE] HTTP ERROR %d: %s\n", httpCode, http.errorToString(httpCode).c_str());
    http.end();
    delay(3000);
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  if (!stream || !stream->connected()) {
    Serial.println("[SLIDE] No stream or disconnected");
    http.end();
    delay(3000);
    return;
  }

  // Ждём полный приём данных
  uint32_t contentLength = http.getSize();
  uint32_t received = 0;
  uint32_t lastAvail = 0;
  unsigned long timeout = millis();

  while (millis() - timeout < 15000) {
    size_t avail = stream->available();
    if (avail > lastAvail) {
      received += (avail - lastAvail);
      lastAvail = avail;
      timeout = millis();  // новые данные — сбрасываем таймаут
    }

    if (contentLength > 0 && received >= contentLength) break;
    if (avail == 0) delay(10);
    yield();
  }

  Serial.printf("[SLIDE] Received %lu bytes → drawing...\n", received ? received : lastAvail);

  clientWrapper.set(stream);  // ← это глобальный объект, он у тебя уже объявлен выше

  bool ok = tft.drawJpg(&clientWrapper, 0, 0, 320, 320, 0, 0, 1.0f);

  http.end();  // безопасно закрываем

  if (ok) {
    Serial.println("[SLIDE] SUCCESS — perfect frame!");
    delay(1500);  // ← ВОТ ОНА, ТВОЯ ПОДУШКА БЕЗОПАСНОСТИ 1.5 сек
  } else {
    Serial.println("[SLIDE] FAILED — decode error");
    delay(5000);
  }
}
// === setup() ===
void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 TFT Web Painter v5 + Stars");

  tft.init();
  tft.setRotation(5);
  tft.setBrightness(255);
  tft.fillScreen(TFT_BLACK);
  WiFi.setSleep(false);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  
  Serial.println("\nWi-Fi OK");
  Serial.print("IP: "); Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/draw", HTTP_POST, handleDraw);
  server.on("/clear", HTTP_POST, handleClear);
  server.on("/stars", HTTP_POST, handleStars);  
  server.on("/slideshow", HTTP_POST, handleSlideshow);
  server.on("/backlight", HTTP_POST, handleBacklight);
  server.on("/sensor", HTTP_POST, handleSensorMode);   

  server.begin();
  Serial.println("Server started");

  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(20, 80);
  tft.println("Web Painter");
  tft.setTextSize(1);
  tft.setCursor(20, 110);
  tft.print("IP: "); tft.println(WiFi.localIP());
}

void loop() {
  static uint32_t lastHeap = 0;
  if (millis() - lastHeap > 30000) {
    Serial.printf("[MEM] Free heap: %u bytes\n", ESP.getFreeHeap());
    lastHeap = millis();
  }  
  server.handleClient();
  updateSlideshow();  // Проверяем каждые 5 сек
  updateSensorDisplay();  // Новая функция — работает, если включён режим датчика
}