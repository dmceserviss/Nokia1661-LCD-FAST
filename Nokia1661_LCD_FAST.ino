#include "nokia1661_lcd_driver.h"
#include "lcd_font5x7.h"

// Переменные времени (начинаем с 12:45:00)
uint8_t hours = 12;
uint8_t minutes = 45;
uint8_t seconds = 0;

unsigned long prevMillis = 0;
char timeStr[12]; // Буфер для строки времени

// Вспомогательный макрос для обмена значений
#define SWAP_INT16(a, b) { int16_t t = a; a = b; b = t; }

// Настройки измерения напряжения
unsigned long prevVoltMillis = 0;
#define PIN_VOLT       A0    // Аналоговый пин для измерения напряжения
const float ADC_VREF   = 5.0;  // Опорное напряжение Arduino (5.0 В)
const float DIVIDER    = 6.1;  // Коэффициент делителя напряжения (1.0 без делителя, 6.1 до 30 В)

uint32_t detectedLcdID = 0;    // Сюда запишется ID подключенного экрана

// --- БЫСТРЫЙ ПАКЕТНЫЙ ВЫВОД КИРИЛЛИЦЫ И ЛАТИНИЦЫ (UTF-8) ---
void drawTextRus(uint8_t x, uint8_t y, uint16_t color, uint16_t bg, uint8_t scale, const char* str) {
  uint8_t cursorX = x;
  uint8_t cursorY = y;

  uint8_t fgHi = color >> 8, fgLo = color & 0xFF;
  uint8_t bgHi = bg >> 8,    bgLo = bg & 0xFF;

  while (*str) {
    uint8_t b1 = (uint8_t)*str++;

    if (b1 == '\n') {
      cursorX = x;
      cursorY += 8 * scale;
      continue;
    }

    // Декодируем 2-байтный символ кириллицы UTF-8
    int8_t rusIndex = -1;
    if ((b1 == 0xD0 || b1 == 0xD1) && *str) {
      uint8_t b2 = (uint8_t)*str++;
      if (b1 == 0xD0) {
        if (b2 == 0x81) rusIndex = 64;                               // 'Ё'
        else if (b2 >= 0x90 && b2 <= 0xBF) rusIndex = b2 - 0x90;     // 'А'..'Я', 'а'..'п'
      } else if (b1 == 0xD1) {
        if (b2 == 0x91) rusIndex = 65;                               // 'ё'
        else if (b2 >= 0x80 && b2 <= 0x8F) rusIndex = b2 - 0x80 + 48;// 'р'..'я'
      }
    }

    // Автоперенос строки при достижении правого края
    if (cursorX + 6 * scale > nlcdGetWidth()) {
      cursorX = x;
      cursorY += 8 * scale;
    }

    // Читаем 5 столбцов глифа из Flash-памяти
    uint8_t cols[6] = {0, 0, 0, 0, 0, 0};
    for (uint8_t c = 0; c < 5; c++) {
      if (rusIndex >= 0) {
        cols[c] = pgm_read_byte(&font5x7rus[rusIndex * 5 + c]);
      } else if (b1 >= 0x20 && b1 <= 0x7F) {
        cols[c] = pgm_read_byte(&font5x7lat[7 + (b1 - 0x20) * 5 + c]);
      }
    }
    // Открываем ОДНО окно на весь символ с учетом смещения драйвера
    uint8_t x0 = cursorX + LCD_CTRL_X_OFFSET;
    uint8_t y0 = cursorY + LCD_CTRL_Y_OFFSET;
    uint8_t x1 = x0 + (6 * scale) - 1;
    uint8_t y1 = y0 + (8 * scale) - 1;

    _nlcdSendCmd(0x2A);
    _nlcdSendData(0x00); _nlcdSendData(x0);
    _nlcdSendData(0x00); _nlcdSendData(x1);

    _nlcdSendCmd(0x2B);
    _nlcdSendData(0x00); _nlcdSendData(y0);
    _nlcdSendData(0x00); _nlcdSendData(y1);

    _nlcdSendCmd(0x2C);

    // Шлем пиксели непрерывным потоком
    for (uint8_t row = 0; row < 8; row++) {
      for (uint8_t sy = 0; sy < scale; sy++) {
        for (uint8_t col = 0; col < 6; col++) {
          bool isPixelSet = (cols[col] & (1 << row));
          uint8_t hi = isPixelSet ? fgHi : bgHi;
          uint8_t lo = isPixelSet ? fgLo : bgLo;
          for (uint8_t sx = 0; sx < scale; sx++) {
            _nlcdSendData(hi);
            _nlcdSendData(lo);
          }
        }
      }
    }

    cursorX += 6 * scale;
  }
}

// --- ГРАФИЧЕСКИЕ ПРИМИТИВЫ ---

void drawRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color) {
  for (uint8_t i = x; i < x + w; i++) {
    nlcdPixel(i, y, color);
    nlcdPixel(i, y + h - 1, color);
  }
  for (uint8_t j = y; j < y + h; j++) {
    nlcdPixel(x, j, color);
    nlcdPixel(x + w - 1, j, color);
  }
}

void fillRect(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color) {
  nlcdFillRect(x, y, w, h, color); // Используем быструю заливку драйвера
}

void drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
  int16_t f = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x = 0;
  int16_t y = r;

  nlcdPixel(x0, y0 + r, color);
  nlcdPixel(x0, y0 - r, color);
  nlcdPixel(x0 + r, y0, color);
  nlcdPixel(x0 - r, y0, color);

  while (x < y) {
    if (f >= 0) {
      y--;
      ddF_y += 2;
      f += ddF_y;
    }
    x++;
    ddF_x += 2;
    f += ddF_x;

    nlcdPixel(x0 + x, y0 + y, color);
    nlcdPixel(x0 - x, y0 + y, color);
    nlcdPixel(x0 + x, y0 - y, color);
    nlcdPixel(x0 - x, y0 - y, color);
    nlcdPixel(x0 + y, y0 + x, color);
    nlcdPixel(x0 - y, y0 + x, color);
    nlcdPixel(x0 + y, y0 - x, color);
    nlcdPixel(x0 - y, y0 - x, color);
  }
}

void fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
  nlcdFillRect(x0, y0 - r, 1, 2 * r + 1, color);

  int16_t f = 1 - r;
  int16_t ddF_x = 1;
  int16_t ddF_y = -2 * r;
  int16_t x = 0;
  int16_t y = r;

  while (x < y) {
    if (f >= 0) {
      y--;
      ddF_y += 2;
      f += ddF_y;
    }
    x++;
    ddF_x += 2;
    f += ddF_x;

    nlcdFillRect(x0 + x, y0 - y, 1, 2 * y + 1, color);
    nlcdFillRect(x0 - x, y0 - y, 1, 2 * y + 1, color);
    nlcdFillRect(x0 + y, y0 - x, 1, 2 * x + 1, color);
    nlcdFillRect(x0 - y, y0 - x, 1, 2 * x + 1, color);
  }
}

void drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
  int16_t steep = abs(y1 - y0) > abs(x1 - x0);
  if (steep) { SWAP_INT16(x0, y0); SWAP_INT16(x1, y1); }
  if (x0 > x1) { SWAP_INT16(x0, x1); SWAP_INT16(y0, y1); }

  int16_t dx = x1 - x0;
  int16_t dy = abs(y1 - y0);
  int16_t err = dx / 2;
  int16_t ystep = (y0 < y1) ? 1 : -1;

  for (; x0 <= x1; x0++) {
    if (steep) nlcdPixel(y0, x0, color);
    else nlcdPixel(x0, y0, color);
    err -= dy;
    if (err < 0) { y0 += ystep; err += dx; }
  }
}

void drawTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
  drawLine(x0, y0, x1, y1, color);
  drawLine(x1, y1, x2, y2, color);
  drawLine(x2, y2, x0, y0, color);
}

void fillTriangle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint16_t color) {
  int16_t a, b, y, last;

  if (y0 > y1) { SWAP_INT16(y0, y1); SWAP_INT16(x0, x1); }
  if (y1 > y2) { SWAP_INT16(y1, y2); SWAP_INT16(x1, x2); }
  if (y0 > y1) { SWAP_INT16(y0, y1); SWAP_INT16(x0, x1); }

  if (y0 == y2) {
    a = b = x0;
    if (x1 < a) a = x1; else if (x1 > b) b = x1;
    if (x2 < a) a = x2; else if (x2 > b) b = x2;
    nlcdFillRect(a, y0, b - a + 1, 1, color);
    return;
  }

  int16_t dx01 = x1 - x0, dy01 = y1 - y0;
  int16_t dx02 = x2 - x0, dy02 = y2 - y0;
  int16_t dx12 = x2 - x1, dy12 = y2 - y1;
  int32_t sa = 0, sb = 0;

  last = (y1 == y2) ? y1 : y1 - 1;

  for (y = y0; y <= last; y++) {
    a = x0 + sa / dy01;
    b = x0 + sb / dy02;
    sa += dx01;
    sb += dx02;
    if (a > b) SWAP_INT16(a, b);
    nlcdFillRect(a, y, b - a + 1, 1, color);
  }

  sa = (int32_t)dx12 * (y - y1);
  sb = (int32_t)dx02 * (y - y0);
  for (; y <= y2; y++) {
    a = x1 + sa / dy12;
    b = x0 + sb / dy02;
    sa += dx12;
    sb += dx02;
    if (a > b) SWAP_INT16(a, b);
    nlcdFillRect(a, y, b - a + 1, 1, color);
  }
}

void setup() {
  nlcdInit();
  nlcdSetOrientation(LCD_ORIENTATION_NORMAL);
 //nlcdSetOrientation(LCD_ORIENTATION_90);
 //nlcdSetOrientation(LCD_ORIENTATION_180);
 //nlcdSetOrientation(LCD_ORIENTATION_270);
  
  // --- 1. ТЕСТ ЗАЛИВКИ ЭКРАНА РАЗНЫМИ ЦВЕТАМИ ---
  uint16_t testColors[] = {RED, GREEN, BLUE, YELLOW, CYAN, MAGENTA, WHITE, BLACK};
  for (uint8_t i = 0; i < 8; i++) {
    nlcdSetBackgroundColor(testColors[i]);
    nlcdClear();
    delay(300); // Пауза 300 мс на каждый цвет (можно уменьшить или увеличить)
}

 // Вложенные контурные квадраты (уменьшаются к центру)
  drawRect(14, 25, 100, 100, RED);
  delay(300);
  drawRect(24, 35, 80, 80, GREEN);
  delay(300);
  drawRect(34, 45, 60, 60, CYAN);
  delay(300);

  // Закрашенные квадраты разных размеров в центре
  fillRect(44, 55, 40, 40, YELLOW);
  delay(300);
  fillRect(54, 65, 20, 20, MAGENTA);
  delay(300);
  fillRect(59, 70, 10, 10, BLUE);
  delay(1000); // Пауза 1 сек, чтобы рассмотреть геометрию

  nlcdClear();

  // --- 2. ВЫВОД ОСНОВНОГО ИНТЕРФЕЙСА ---
  nlcdSetBackgroundColor(BLACK);
  nlcdSetFont(font5x7lat);

 // Внешняя рамка вокруг всего видимого поля (с учетом сдвигов 3 px слева и 2 px сверху)
  uint8_t maxW = nlcdGetWidth() - 4;
  uint8_t maxH = nlcdGetHeight() - 3;
  drawRect(3, 2, maxW, maxH, WHITE);

  drawRect(20, 15, 35, 25, GREEN);// квадрат без заливки
  nlcdFillRect(80, 15, 35, 25, RED);// квадрат с заливкой

  fillCircle(35 + 3, 70 + 2, 20, RED); // Рисуем красный круг радиусом 20 с заливкой
  drawCircle(95 + 3, 70 + 2, 20, BLUE); // Рисуем сирий круг радиусом 20 без заливки

// 1. Большой красный закрашенный треугольник (40x40) вверху
    fillTriangle(64 + 3, 10 + 2, 44 + 3, 50 + 2, 84 + 3, 50 + 2, MAGENTA);

    // 2. Средний зелёный закрашенный треугольник-стрелка в центре
    fillTriangle(55 + 3, 65 + 2, 55 + 3, 95 + 2, 75 + 3, 80 + 2, GREEN);

    // 3. Жёлтый закрашенный треугольник 20x20 внизу
    uint8_t cx = (nlcdGetWidth() / 2) + 3;
    uint8_t bottomY = nlcdGetHeight() - 12;
    fillTriangle(cx, bottomY - 20, cx - 10, bottomY, cx + 10, bottomY, YELLOW);

    // 4. ОДИН БОЛЬШОЙ ТРЕУГОЛЬНИК БЕЗ ЗАЛИВКИ (бирюзовый контур почти на весь экран)
    // Вершина сверху (67, 4), левый нижний угол (6, 154), правый нижний угол (126, 154)
    drawTriangle(64 + 3, 2 + 2, 3 + 3, 152 + 2, 123 + 3, 152 + 2, CYAN);
  delay(2000);

  nlcdClear();
  // Возвращаем черный фон и мелкий шрифт
  nlcdSetBackgroundColor(BLACK);
  nlcdSetTextScale(1);
  nlcdGotoPixelXY(45, 145);
  nlcdString(MAGENTA, "Test OK!");
}

void loop() {

// Мелкий шрифт (масштаб 1) — чуть сдвинут внутрь рамки (Y = 6)
  nlcdSetTextScale(1);
 // nlcdGotoPixelXY(6, 6);
 // nlcdString(WHITE, "Scale 1 (3px, 2px)");
  nlcdGotoPixelXY(3, 1);
nlcdString(CYAN,  "0123456789 ABCDEFGEt");
// ------------------Примеры вызова кириллицы:---------------------------------------
/*
2 (первый) — координата X в пикселях (отступ от левого края экрана).
11 (второй) — координата Y в пикселях (отступ от верхнего края экрана).
WHITE — цвет самих букв (белый).
BLACK — цвет фона под буквами (черный; закрашивает старый текст при обновлении цифр без мерцания).
1 — масштаб (размер) шрифта (1 = мелкий 5×7 пикселей, 2 = средний 10×14 пикселей, 3 = крупный 15×21 пикселей).
*/
drawTextRus(2, 11, WHITE,  BLACK, 2, "БЬЮйЖэЯяЪФ");

  // Инверсия фона (белый текст на синем фоне)
  nlcdSetTextScale(2);
  nlcdSetBackgroundColor(BLUE);
  nlcdGotoPixelXY(5, 30);
  nlcdString(WHITE, " BLUE  BG ");

  // Крупный шрифт (масштаб 2)
  nlcdSetBackgroundColor(BLACK);
  nlcdSetTextScale(2);
  nlcdStringXY(0, 3, YELLOW, "Size-2:BIG");

  // -------------------измерения напряжения-------------------------
  nlcdSetTextScale(1);
  nlcdGotoPixelXY(35 + 3, 105 + 2);
  nlcdString(WHITE, "VOLTMETER:");
  // Объявляем currentMillis
  unsigned long currentMillis = millis();
  // --- 2. БЛОК ВОЛЬТМЕТРА (обновляется раз в 250 мс) ---
  if (currentMillis - prevVoltMillis >= 250) {
    prevVoltMillis = currentMillis;

    uint32_t sum = 0;
    for (uint8_t i = 0; i < 16; i++) {
      sum += analogRead(PIN_VOLT);
    }
    float rawAdc = (float)sum / 16.0;
    float voltage = ((rawAdc * ADC_VREF) / 1023.0) * DIVIDER;
    if (voltage < 0.05) voltage = 0.0;

    char voltStr[8];
    char outStr[12];
    dtostrf(voltage, 5, 2, voltStr);
    snprintf(outStr, sizeof(outStr), "%s V", voltStr);

    nlcdSetBackgroundColor(BLACK);
    nlcdSetTextScale(2); // Масштаб 2, чтобы помещалось вместе с часами
    nlcdGotoPixelXY(16 + 3, 120 + 2); // Координаты ниже часов
    nlcdString(GREEN, outStr);
  }
  // -------------------измерения напряжения-------------------------
  
  //unsigned long currentMillis = millis();
  // Обновление времени раз в 1000 мс (1 секунду)
  if (currentMillis - prevMillis >= 1000) {
    prevMillis = currentMillis;

    seconds++;
    if (seconds >= 60) {
      seconds = 0;
      minutes++;
      if (minutes >= 60) {
        minutes = 0;
        hours++;
        if (hours >= 24) {
          hours = 0;
        }
      }
    }

    // Мигающее двоеточие раз в секунду для формата ЧЧ:ММ (5 символов, идеально для масштаба 3)
    if (seconds % 2 == 0) {
      sprintf(timeStr, "%02d:%02d", hours, minutes);
    } else {
      sprintf(timeStr, "%02d %02d", hours, minutes);
    }

    // Выводим крупным шрифтом (масштаб 3)
    nlcdSetBackgroundColor(BLACK);
    nlcdSetTextScale(3);
    nlcdGotoPixelXY(20, 74);
    nlcdString(RED, timeStr);

  }
}