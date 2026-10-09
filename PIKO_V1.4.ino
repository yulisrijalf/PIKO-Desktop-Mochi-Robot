/*
 * ============================================================
 * PIKO V1.4 - DESKTOP MOCHI ROBOT
 * ESP32-C3 + SH1106 128x64 + TTP223 + DHT11 + OLED 1.3"
 * ============================================================
 * OLED    : SDA : GPIO 8, SCL : GPIO 9
 * TOUCH 0 : GPIO 0 (Kiri)
 * TOUCH 1 : GPIO 1 (Kanan)
 * DHT11   : GPIO 2
 * ============================================================
 */

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "time.h"
#include <math.h>
#include "DHT.h"

// ========================= PIN ==============================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SDA_PIN 8 
#define SCL_PIN 9
#define TOUCH_0_PIN 0
#define TOUCH_1_PIN 1
#define DHT_PIN 2
#define DHT_TYPE DHT11

// ========================= WIFI =============================
const char* WIFI_SSID = "Nama Wifi Kamu";   
const char* WIFI_PASSWORD = "Pasword Wifi Kamu"; 

// ========================= OLED & DHT =======================
Adafruit_SH1106G display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT dht(DHT_PIN, DHT_TYPE);

// ========================= DHT VARIABLES ====================
float humidity = 0.0;
float temperature = 0.0;
unsigned long lastDHTRead = 0;
const unsigned long DHT_INTERVAL = 5000; // Pembacaan tiap 5 detik saat di menu INFO

// ========================= STATE ============================
enum SystemState { STATE_FACE, STATE_INFO, STATE_SLEEP };
SystemState currentState = STATE_FACE;

enum Mood { MOOD_NORMAL, MOOD_HAPPY, MOOD_PLEASED, MOOD_SLEEPY };
Mood currentMood = MOOD_NORMAL;

enum FaceDirection { LOOK_CENTER, LOOK_LEFT, LOOK_RIGHT };
FaceDirection lookDirection = LOOK_CENTER;

// POSISI BASAL MATA (TENGAH)
const float LEFT_EYE_BASE_X = 24.0;
const float RIGHT_EYE_BASE_X = 72.0;

// ========================= EYE STRUCT =======================
struct Eye {
  float x, y, w, h;
  float targetX, targetY, targetW, targetH;
  float pupilX, pupilY, targetPupilX, targetPupilY;
  float velX, velY, velW, velH, pVelX, pVelY;
  float k = 0.18, d = 0.65, pk = 0.22, pd = 0.60;
  bool blinking = false;
  unsigned long lastBlink = 0, nextBlinkTime = 0;

  void init(float _x, float _y, float _w, float _h) {
    x = targetX = _x; y = targetY = _y;
    w = targetW = _w; h = targetH = _h;
    pupilX = targetPupilX = 0; pupilY = targetPupilY = 0;
    velX = velY = velW = velH = pVelX = pVelY = 0;
    nextBlinkTime = millis() + random(1500, 4000);
  }

  void update() {
    float ax = (targetX - x) * k, ay = (targetY - y) * k;
    float aw = (targetW - w) * k, ah = (targetH - h) * k;
    velX = (velX + ax) * d; velY = (velY + ay) * d;
    velW = (velW + aw) * d; velH = (velH + ah) * d;
    x += velX; y += velY; w += velW; h += velH;

    float pax = (targetPupilX - pupilX) * pk;
    float pay = (targetPupilY - pupilY) * pk;
    pVelX = (pVelX + pax) * pd; pVelY = (pVelY + pay) * pd;
    pupilX += pVelX; pupilY += pVelY;
  }
};

Eye leftEye, rightEye;

// ========================= TIMER & TIMEOUT ==================
const unsigned long FRAME_INTERVAL = 20;
const unsigned long SLEEP_TIMEOUT = 60000;    // 60 Detik tanpa aktivitas -> Sleep
const unsigned long PLEASED_DURATION = 2500;  // Durasi ekspresi keenakan
const unsigned long SWIPE_WINDOW = 1200;      // Jeda batas hitungan elusan

unsigned long lastFrame = 0;
unsigned long lastInteraction = 0;
unsigned long reactionStart = 0;
bool reactionActive = false;

// ========================= GESTURE DETECTOR =================
bool previousTouch0 = false, previousTouch1 = false;
bool touch0State = false, touch1State = false;
bool bothTouchActive = false;
unsigned long bothTouchStart = 0;

int swipeCount = 0;
int lastSwipedPin = -1;
unsigned long lastSwipeTime = 0;

// ========================= SLEEP & WAKE STAGES ============================
enum SleepStage { SLEEP_DROWSY, SLEEP_CLOSING, SLEEPING, WAKE_OPENING, WAKE_AWAKENING };
SleepStage sleepStage = SLEEP_DROWSY;
unsigned long sleepStart = 0;
bool wakingUp = false;

// STRUCT UNTUK ANIMASI FLOATING Zzz
struct FloatingZ {
  float offset;
};
FloatingZ zList[3] = { {0.0}, {0.33}, {0.66} };

// ========================= WIFI/TIME ========================
bool wifiConnected = false;
bool timeSynced = false;

// ========================= PROTOTYPES =======================
void setupDisplay();
void setupEyes();
void showBootScreen();
void setupWiFi();
void showGreeting();
void readTouch();
void processSwipeGesture(int pin);
void processSingleTouch(int pin);
void readDHTData();
void updateFaceBehavior();
void updateEyePhysics();
void updateBlink();
void drawCenteredText(const char* text, int y, int textSize);
void drawFace();
void drawNormalEye(Eye& e, bool isLeft);
void drawMouth();
void drawHeart(int16_t x, int16_t y, int size);
void drawInfoPage();
void drawTime();
void drawFloatingZzz();
void setLook(FaceDirection direction);
void setMood(Mood mood);
void enterSleep();
void updateSleep();
void drawSleep();
void wakeFromSleep();
void resetInteractionTimer();

// ============================================================
// SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  delay(200);
  randomSeed(micros());

  pinMode(TOUCH_0_PIN, INPUT);
  pinMode(TOUCH_1_PIN, INPUT);

  dht.begin();

  setupDisplay();
  setupEyes();

  currentState = STATE_FACE;
  setMood(MOOD_NORMAL);
  setLook(LOOK_CENTER);
  resetInteractionTimer();

  showBootScreen();
  setupWiFi();
  showGreeting();

  Serial.println("PIKO V1.4 - READY");
}

// ============================================================
// MAIN LOOP
// ============================================================
void loop() {
  unsigned long now = millis();
  if (now - lastFrame < FRAME_INTERVAL) return;
  lastFrame = now;

  readTouch();

  switch (currentState) {
    case STATE_FACE:
      updateFaceBehavior();
      updateBlink();
      updateEyePhysics();
      drawFace();
      break;

    case STATE_INFO:
      if (now - lastDHTRead >= DHT_INTERVAL) {
        readDHTData();
      }
      updateEyePhysics();
      drawInfoPage();
      break;

    case STATE_SLEEP:
      updateSleep();
      drawSleep();
      break;
  }
}

// ============================================================
// DHT22 READING
// ============================================================
void readDHTData() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (!isnan(h) && !isnan(t)) {
    humidity = h;
    temperature = t;
    Serial.printf("DHT11 Success -> Temp: %.1fC, Hum: %.1f%%\n", temperature, humidity);
  } else {
    Serial.println("Failed to read from DHT sensor!");
  }
  lastDHTRead = millis();
}

// ============================================================
// TOUCH & GESTURE LOGIC
// ============================================================
void readTouch() {
  bool t0 = digitalRead(TOUCH_0_PIN);
  bool t1 = digitalRead(TOUCH_1_PIN);

  bool rising0 = t0 && !previousTouch0;
  bool rising1 = t1 && !previousTouch1;

  touch0State = t0;
  touch1State = t1;

  // 1. STATE SLEEP: Sentuh apa saja untuk bangun
  if (currentState == STATE_SLEEP) {
    if (rising0 || rising1 || t0 || t1) {
      wakeFromSleep();
      previousTouch0 = t0;
      previousTouch1 = t1;
    }
    return;
  }

  // 2. KEDUA TOUCH DITEKAN (Masuk ke Menu INFO)
  if (t0 && t1) {
    if (!bothTouchActive) {
      bothTouchActive = true;
      bothTouchStart = millis();
    }

    if (millis() - bothTouchStart > 700 && currentState == STATE_FACE) {
      currentState = STATE_INFO;
      reactionActive = false;
      swipeCount = 0;
      readDHTData(); // Baca DHT saat masuk menu INFO
      Serial.println("ENTER INFO MODE");
    }
  } else {
    bothTouchActive = false;
  }

  // 3. STATE INFO: Sentuhan keluar kembali ke Wajah
  if (currentState == STATE_INFO) {
    if (rising0 || rising1) {
      currentState = STATE_FACE;
      setMood(MOOD_NORMAL);
      setLook(LOOK_CENTER);
      resetInteractionTimer();
      Serial.println("BACK TO FACE MODE");
    }
    previousTouch0 = t0;
    previousTouch1 = t1;
    return;
  }

  // 4. STATE FACE: Proses Gesture Single / Swipe Elusan (Hanya jika tidak menekan keduanya)
  if (!(t0 && t1)) {
    if (rising0) processSwipeGesture(0);
    if (rising1) processSwipeGesture(1);
  }

  previousTouch0 = t0;
  previousTouch1 = t1;
}

void processSwipeGesture(int pin) {
  unsigned long now = millis();

  if (now - lastSwipeTime > SWIPE_WINDOW) {
    swipeCount = 0;
    lastSwipedPin = -1;
  }

  if (lastSwipedPin != pin) {
    swipeCount++;
    lastSwipedPin = pin;
    lastSwipeTime = now;

    if (swipeCount >= 3) {
      Serial.println("ELUSAN DETECTED -> KEENAKAN!");
      setMood(MOOD_PLEASED);
      setLook(LOOK_CENTER);
      reactionActive = true;
      reactionStart = now;
      swipeCount = 0;
      lastSwipedPin = -1;
    }
  }

  resetInteractionTimer();
}

void processSingleTouch(int pin) {
  if (reactionActive) return;

  if (pin == 0) {
    Serial.println("Touch Kiri -> Nengok Kiri");
    setLook(LOOK_LEFT);
  } else if (pin == 1) {
    Serial.println("Touch Kanan -> Nengok Kanan");
    setLook(LOOK_RIGHT);
  }
}

// ============================================================
// FACE BEHAVIOR
// ============================================================
void updateFaceBehavior() {
  unsigned long now = millis();

  // Eksekusi lirikan single touch jika sentuhan berhenti sebelum 3x swipe
  if (swipeCount > 0 && swipeCount < 3 && (now - lastSwipeTime > 300)) {
    processSingleTouch(lastSwipedPin);
    swipeCount = 0;
  }

  // Selesai durasi kenikmatan
  if (reactionActive && (now - reactionStart > PLEASED_DURATION)) {
    reactionActive = false;
    setMood(MOOD_NORMAL);
    setLook(LOOK_CENTER);
  }

  // Auto Reset lirikan mata ke tengah setelah 1 detik
  if (!reactionActive && lookDirection != LOOK_CENTER) {
    static unsigned long lookStart = 0;
    if (lookStart == 0) lookStart = now;

    if (now - lookStart > 1000) {
      setLook(LOOK_CENTER);
      lookStart = 0;
    }
  }

  // Sleep timeout
  if (now - lastInteraction > SLEEP_TIMEOUT) {
    enterSleep();
  }
}

// ============================================================
// EYES CONTROL & MOOD
// ============================================================
void setupEyes() {
  leftEye.init(LEFT_EYE_BASE_X, 16, 32, 32);
  rightEye.init(RIGHT_EYE_BASE_X, 16, 32, 32);
}

void setLook(FaceDirection direction) {
  lookDirection = direction;
  float pupilTarget = 0;
  float eyeShift = 0;

  if (direction == LOOK_LEFT) {
    pupilTarget = -10;
    eyeShift = -16;
  } 
  else if (direction == LOOK_RIGHT) {
    pupilTarget = 10;
    eyeShift = 16;
  }

  leftEye.targetPupilX = pupilTarget;
  rightEye.targetPupilX = pupilTarget;

  leftEye.targetX = LEFT_EYE_BASE_X + eyeShift;
  rightEye.targetX = RIGHT_EYE_BASE_X + eyeShift;
}

void setMood(Mood mood) {
  currentMood = mood;

  switch (mood) {
    case MOOD_NORMAL:
      leftEye.targetW = rightEye.targetW = 32;
      leftEye.targetH = rightEye.targetH = 32;
      leftEye.targetPupilY = rightEye.targetPupilY = 0;
      break;

    case MOOD_HAPPY:
      leftEye.targetW = rightEye.targetW = 32;
      leftEye.targetH = rightEye.targetH = 28;
      break;

    case MOOD_PLEASED:
      leftEye.targetW = rightEye.targetW = 34;
      leftEye.targetH = rightEye.targetH = 14;
      leftEye.targetPupilY = rightEye.targetPupilY = -2;
      break;

    case MOOD_SLEEPY:
      leftEye.targetW = rightEye.targetW = 32;
      leftEye.targetH = rightEye.targetH = 15;
      break;

    default:
      break;
  }
}

void updateEyePhysics() {
  leftEye.update();
  rightEye.update();
}

void updateBlink() {
  unsigned long now = millis();

  if (currentMood == MOOD_PLEASED) return;

  if (!leftEye.blinking && now > leftEye.nextBlinkTime) {
    leftEye.blinking = rightEye.blinking = true;
    leftEye.lastBlink = now;
    leftEye.nextBlinkTime = now + random(2000, 5000);
  }

  if (leftEye.blinking) {
    leftEye.targetH = rightEye.targetH = 8;

    if (now - leftEye.lastBlink > 90) {
      leftEye.blinking = rightEye.blinking = false;
      setMood(currentMood);
    }
  }
}

// ============================================================
// OLED & DISPLAY SCREENS
// ============================================================
void setupDisplay() {
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(100);

  if (!display.begin(0x3C, true)) {
    while (1) delay(1000);
  }

  display.clearDisplay();
  display.setTextColor(SH110X_WHITE);
  display.display();
}

void showBootScreen() {
  display.clearDisplay();
  
//  drawCenteredText("PIKO", 7, 2);
//  drawCenteredText("Starting up...", 38, 1);

  display.display();
//  delay(700);
}

void setupWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  int dots = 0;

  Serial.println("Connecting WiFi...");

  while (WiFi.status() != WL_CONNECTED && millis() - start < 5000) {
    display.clearDisplay();
    
    drawCenteredText("PIKO V1.4", 7, 1);
    drawCenteredText("Connecting WiFi", 26, 1);

    // Animasi titik-titik (Size 1)
    char dotBuf[5] = "";
    for (int i = 0; i < dots; i++) strcat(dotBuf, ".");
    drawCenteredText(dotBuf, 44, 1);

    display.display();
    dots = (dots + 1) % 4;
    delay(250);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("WiFi connected!");

    display.clearDisplay();
    drawCenteredText("PIKO V1.4", 7, 1);
    drawCenteredText("WiFi Connected", 26, 1);
    drawCenteredText("Online :)", 44, 1);
    display.display();
    delay(700);

    configTime(7 * 3600, 0, "pool.ntp.org", "time.nist.gov");

    struct tm timeinfo;
    unsigned long ntpStart = millis();

    while (!getLocalTime(&timeinfo) && millis() - ntpStart < 3000) {
      delay(100);
    }

    if (getLocalTime(&timeinfo)) {
      timeSynced = true;
      Serial.println("Time synchronized!");
    }
  } else {
    wifiConnected = false;
    Serial.println("WiFi Timeout - Offline mode");

    display.clearDisplay();
    drawCenteredText("PIKO V1.4", 7, 1);
    drawCenteredText("Offline Mode", 31, 1);
    //drawCenteredText("No problem :)", 45, 1);
    display.display();
    delay(700);
  }
}

void showGreeting() {
  display.clearDisplay();

  drawCenteredText("SALAM KENAL!", 7, 1);
  drawCenteredText("AKU PIKO", 30, 2);
  //drawCenteredText("Senang bertemu Kamu!", 48, 1);

  display.display();
  delay(1800);
}

// ============================================================
// HELPER FUNCTION: AUTO CENTER TEXT
// ============================================================
void drawCenteredText(const char* text, int y, int textSize) {
  int16_t x1, y1;
  uint16_t w, h;

  display.setTextSize(textSize);
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  
  int x = (SCREEN_WIDTH - w) / 2;
  display.setCursor(x, y);
  display.print(text);
}

// ============================================================
// DRAWING FACE & ELEMENTS
// ============================================================
void drawFace() {
  display.clearDisplay();
  drawNormalEye(leftEye, true);
  drawNormalEye(rightEye, false);
  drawMouth();
  display.display();
}

void drawNormalEye(Eye& e, bool isLeft) {
  int iw = max(2, (int)e.w);
  int ih = max(2, (int)e.h);

  int ix = (int)e.x;
  int iy = (int)(e.y + (32 - ih) / 2.0);

  // FX KEENAKAN
  if (currentMood == MOOD_PLEASED) {
    float pulse = sin(millis() / 120.0) * 1.5;
    int hPleased = max(4, (int)(ih + pulse));

    display.fillRoundRect(ix, iy + 4, iw, hPleased, 5, SH110X_WHITE);
    display.fillRoundRect(ix + 3, iy + 8, iw - 6, hPleased, 4, SH110X_BLACK);

    if (!isLeft) {
      float heartPulse = (sin(millis() / 150.0) + 1.0) / 2.0; 
      int heartSize = 4 + (int)(heartPulse * 3);
      drawHeart(ix + iw + 5, iy - 2, heartSize);
    }
    return;
  }

  // BENTUK MATA
  int r = min(8, ih / 3);
  display.fillRoundRect(ix, iy, iw, ih, r, SH110X_WHITE);

  if (ih > 6) {
    int pupilW = 11;
    int pupilH = min(18, ih - 4);
    int px = ix + iw / 2 - pupilW / 2 + (int)e.pupilX;
    int py = iy + ih / 2 - pupilH / 2 + (int)e.pupilY;

    px = constrain(px, ix + 2, ix + iw - pupilW - 2);
    py = constrain(py, iy + 2, iy + ih - pupilH - 2);

    if (pupilH > 2) {
      display.fillRoundRect(px, py, pupilW, pupilH, min(4, pupilH / 2), SH110X_BLACK);
    }
    
    if (ih > 14 && pupilH > 8) {
      display.fillCircle(px + 3, py + 4, 2, SH110X_WHITE);
    }
  }
}

void drawMouth() {
  int y = 52;

  float eyeCenterX = (leftEye.x + (leftEye.w / 2.0) + rightEye.x + (rightEye.w / 2.0)) / 2.0;
  int cx = (int)eyeCenterX;

  if (currentMood == MOOD_PLEASED) {
    display.drawCircle(cx - 3, y - 1, 3, SH110X_WHITE);
    display.drawCircle(cx + 3, y - 1, 3, SH110X_WHITE);
    display.fillRect(cx - 7, y - 4, 15, 3, SH110X_BLACK);
  } 
  else if (lookDirection != LOOK_CENTER) {
    display.fillCircle(cx, y + 1, 3, SH110X_WHITE);
    display.fillCircle(cx, y + 1, 2, SH110X_BLACK); // Lubang di tengah mulut
  } 
  else {
    // Ekspresi Biasa (Garis Datar)
    display.drawLine(cx - 5, y, cx + 5, y, SH110X_WHITE);
  }
}

void drawHeart(int16_t x, int16_t y, int size) {
  if (size < 2) return;
  int r = size / 2;
  display.fillCircle(x - r + 1, y - r / 2, r, SH110X_WHITE);
  display.fillCircle(x + r - 1, y - r / 2, r, SH110X_WHITE);
  display.fillTriangle(x - size + 1, y - r / 2 + 1, 
                       x + size - 1, y - r / 2 + 1, 
                       x, y + size, SH110X_WHITE);
}

// ============================================================
// INFO PAGE & TIME
// ============================================================
void drawInfoPage() {
  display.clearDisplay();
  
  // Tampilkan Jam dan Tanggal (Otomatis Center)
  drawTime();

  // Tampilkan Suhu dan Kelembapan dari DHT22
  char envBuffer[25];
  snprintf(envBuffer, sizeof(envBuffer), "%.1fC  |  %.0f%%", temperature, humidity);
  drawCenteredText(envBuffer, 40, 1);

  // Navigasi bawah (Otomatis Center)
  drawCenteredText("[Touch to Back]", 54, 1);

  display.display();
}

void drawTime() {
  struct tm timeinfo;

  if (timeSynced && getLocalTime(&timeinfo)) {
    char timeString[10];
    strftime(timeString, sizeof(timeString), "%H:%M:%S", &timeinfo);
    drawCenteredText(timeString, 4, 2);

    char dateString[20];
    strftime(dateString, sizeof(dateString), "%d/%m/%Y", &timeinfo);
    drawCenteredText(dateString, 25, 1);
  } else {
    drawCenteredText("--:--:--", 4, 2);
    drawCenteredText(wifiConnected ? "SYNC..." : "OFFLINE", 25, 1);
  }
}

// ============================================================
// SLEEP SYSTEM
// ============================================================
void drawFloatingZzz() {
  unsigned long t = millis();
  
  for (int i = 0; i < 3; i++) {
    float cycle = fmod((t / 2400.0) + zList[i].offset, 1.0);
    
    int startX = 96; 
    int startY = 32;
    int currX = startX + (int)(sin(cycle * 6.28) * 4.0) + (int)(cycle * 12.0);
    int currY = startY - (int)(cycle * 26.0);
    
    if (cycle < 0.8) {
      if (i == 0) {
        display.setTextSize(1);
      } else if (i == 1) {
        display.setTextSize(cycle > 0.4 ? 2 : 1);
      } else {
        display.setTextSize(cycle > 0.3 ? 2 : 1);
      }
      
      display.setCursor(currX, currY);
      display.print("Z");
    }
  }
}

void enterSleep() {
  if (currentState == STATE_SLEEP) return;

  Serial.println("PIKO Entering Sleep Mode...");
  currentState = STATE_SLEEP;
  sleepStage = SLEEP_DROWSY;
  sleepStart = millis();
  wakingUp = false;
  setMood(MOOD_SLEEPY);
}

void updateSleep() {
  unsigned long elapsed = millis() - sleepStart;

  if (!wakingUp) {
    if (sleepStage == SLEEP_DROWSY) {
      setMood(MOOD_SLEEPY);
      if (elapsed > 1500) {
        sleepStage = SLEEP_CLOSING;
        sleepStart = millis();
      }
    } else if (sleepStage == SLEEP_CLOSING) {
      leftEye.targetH = rightEye.targetH = 2;
      if (elapsed > 800) {
        sleepStage = SLEEPING;
        sleepStart = millis();
      }
    } else if (sleepStage == SLEEPING) {
      leftEye.targetH = rightEye.targetH = 2;
    }
  } 
  else {
    if (sleepStage == WAKE_OPENING) {
      float progress = constrain(elapsed / 250.0, 0.0, 1.0);
      leftEye.targetH = rightEye.targetH = 2.0 + (13.0 * progress);

      if (elapsed > 250) {
        sleepStage = WAKE_AWAKENING;
        sleepStart = millis();
      }
    } else if (sleepStage == WAKE_AWAKENING) {
      float progress = constrain(elapsed / 400.0, 0.0, 1.0);
      leftEye.targetH = rightEye.targetH = 15.0 + (17.0 * progress);

      if (elapsed > 400) {
        wakingUp = false;
        currentState = STATE_FACE;
        setMood(MOOD_NORMAL);
        setLook(LOOK_CENTER);
        resetInteractionTimer();
        Serial.println("PIKO Awake Completely!");
      }
    }
  }

  updateEyePhysics();
}

void drawSleep() {
  display.clearDisplay();

  if (sleepStage == SLEEP_DROWSY || sleepStage == WAKE_OPENING || sleepStage == WAKE_AWAKENING) {
    drawNormalEye(leftEye, true);
    drawNormalEye(rightEye, false);
    
    if (sleepStage == WAKE_OPENING || sleepStage == WAKE_AWAKENING) {
      drawMouth();
    }
  } else {
    float breath = (sin(millis() / 400.0) + 1.0) * 0.5;
    int eyeOffsetY = (int)(breath * 1.5);
    
    display.fillRoundRect((int)leftEye.x, 31 + eyeOffsetY, 32, 2, 1, SH110X_WHITE);
    display.fillRoundRect((int)rightEye.x, 31 + eyeOffsetY, 32, 2, 1, SH110X_WHITE);
  }

  if (sleepStage == SLEEPING) {
    drawFloatingZzz();

    float breathPhase = millis() / 400.0;
    int mouthWidth = 3 + (int)((sin(breathPhase) + 1.0) * 1.5);
    display.drawLine(64 - mouthWidth, 50, 64 + mouthWidth, 50, SH110X_WHITE);
  }

  display.display();
}

void wakeFromSleep() {
  Serial.println("Waking up fast!");
  wakingUp = true;
  sleepStage = WAKE_OPENING;
  sleepStart = millis();

  leftEye.targetW = rightEye.targetW = 32;
  leftEye.targetH = rightEye.targetH = 2;
  leftEye.x = leftEye.targetX = LEFT_EYE_BASE_X;
  leftEye.y = leftEye.targetY = 16;
  rightEye.x = rightEye.targetX = RIGHT_EYE_BASE_X;
  rightEye.y = rightEye.targetY = 16;
  leftEye.targetPupilX = rightEye.targetPupilX = 0;
  leftEye.targetPupilY = rightEye.targetPupilY = 0;

  resetInteractionTimer();
}

void resetInteractionTimer() {
  lastInteraction = millis();
}