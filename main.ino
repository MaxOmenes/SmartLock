#include <EncButton.h>
#include <WiFi.h>
#include <WebServer.h>

// Пины подключения
#define BUZZER_PIN 22
#define BUZZER_FREQ_HIGH 2000
#define BUZZER_FREQ_LOW 500
#define BUZZER_FREQ 1000
#define PWM_RESOLUTION 8   // 8 бит (0–255)
#define PWM_CHANNEL 0

#define ENCODER_KEY 33
#define ENCODER_S1 35
#define ENCODER_S2 32

#define YELLOW_LIGHT 18
#define RED_LIGHT 19

#define SOLENOID 16

// WiFi настройки
const char* ssid = "RT-GPON-0958";
const char* password = "Fd3ahai2ah";

// Глобальные переменные
EncButton enc(ENCODER_S1, ENCODER_S2, ENCODER_KEY);
WebServer server(80);

// Код замка (по умолчанию 1,2,3,4,5)
int code[5] = {1, 2, 3, 4, 5};

// Переменные для ввода кода
int current_code[10];  // Максимум 10 цифр
int current_code_length = 0;
int current_number = 0;  // Текущее набираемое число
unsigned long last_rotation_time = 0;
bool entering_code = false;
bool last_direction_right = true;  // true = right, false = left
const unsigned long TIMEOUT_MS = 5000;  // 5 секунд таймаут

void setup() {
  Serial.begin(115200);
  
  // Настройка PWM для динамика
  ledcAttach(BUZZER_PIN, BUZZER_FREQ, PWM_RESOLUTION);
  
  // Настройка пинов
  pinMode(SOLENOID, OUTPUT);
  pinMode(YELLOW_LIGHT, OUTPUT);
  pinMode(RED_LIGHT, OUTPUT);
  digitalWrite(SOLENOID, LOW);
  digitalWrite(YELLOW_LIGHT, LOW);
  digitalWrite(RED_LIGHT, LOW);
  
  // Подключение к WiFi
  connectToWiFi();
  
  // Настройка веб-сервера
  setupWebServer();
  
  Serial.println("Smart Lock ready!");
  printCurrentCode();
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());
}

void setupWebServer() {
  // Эндпоинт для смены кода
  server.on("/change", HTTP_GET, []() {
    if (server.hasArg("code")) {
      String newCode = server.arg("code");
      
      if (validateNewCode(newCode)) {
        updateCode(newCode);
        server.send(200, "text/plain", "Code updated successfully");
        Serial.println("Code updated via web interface");
        printCurrentCode();
      } else {
        server.send(401, "text/plain", "Invalid code format. Must be 5 digits from 1 to 9");
      }
    } else {
      server.send(400, "text/plain", "Missing code parameter");
    }
  });
  
  // Эндпоинт для открытия замка
  server.on("/unlock", HTTP_GET, []() {
    Serial.println("Unlock requested via web interface");
    openLock();
    server.send(200, "text/plain", "Lock opened");
  });
  
  server.begin();
  Serial.println("Web server started");
}

bool validateNewCode(String codeStr) {
  if (codeStr.length() != 5) {
    return false;
  }
  
  for (int i = 0; i < 5; i++) {
    char digit = codeStr.charAt(i);
    if (digit < '1' || digit > '9') {
      return false;
    }
  }
  
  return true;
}

void updateCode(String codeStr) {
  for (int i = 0; i < 5; i++) {
    code[i] = codeStr.charAt(i) - '0';
  }
}

void printCurrentCode() {
  Serial.print("Current code: ");
  for (int i = 0; i < 5; i++) {
    Serial.print(code[i]);
  }
  Serial.println();
}

void rotateBeep() {
  ledcWriteChannel(PWM_CHANNEL, 32);
  digitalWrite(YELLOW_LIGHT, HIGH);
  delay(100);
  ledcWriteChannel(PWM_CHANNEL, 0);
  digitalWrite(YELLOW_LIGHT, LOW);
}

void beepLow() {
  ledcChangeFrequency(BUZZER_PIN, BUZZER_FREQ_LOW, PWM_RESOLUTION);
  rotateBeep();
}

void beepHigh() {
  ledcChangeFrequency(BUZZER_PIN, BUZZER_FREQ_HIGH, PWM_RESOLUTION);
  rotateBeep();
}

void resetCurrentCode() {
  current_code_length = 0;
  current_number = 0;
  entering_code = false;
  Serial.println("Code input reset");
}

void showError() {
  digitalWrite(RED_LIGHT, HIGH);
  delay(2000);
  digitalWrite(RED_LIGHT, LOW);
}

void openLock() {
  Serial.println("Lock opened!");
  digitalWrite(SOLENOID, HIGH);
  delay(10000);  // 10 секунд
  digitalWrite(SOLENOID, LOW);
  Serial.println("Lock closed");
}

void addNumberToCode() {
  if (current_number > 0 && current_number <= 9 && current_code_length < 10) {
    current_code[current_code_length] = current_number;
    current_code_length++;
    Serial.print("Added number: ");
    Serial.println(current_number);
    current_number = 0;
  }
}

bool compareCode() {
  if (current_code_length != 5) {
    return false;
  }
  
  for (int i = 0; i < 5; i++) {
    if (current_code[i] != code[i]) {
      return false;
    }
  }
  
  return true;
}

void printCurrentInput() {
  Serial.print("Current input: ");
  for (int i = 0; i < current_code_length; i++) {
    Serial.print(current_code[i]);
  }
  if (current_number > 0) {
    Serial.print(" (entering: ");
    Serial.print(current_number);
    Serial.print(")");
  }
  Serial.println();
}

void handleTimeout() {
  if (entering_code && millis() - last_rotation_time > TIMEOUT_MS) {
    Serial.println("Timeout! Code reset");
    resetCurrentCode();
    showError();
  }
}

void handleRotation(bool is_right) {
  unsigned long current_time = millis();
  
  // Если это первое вращение или смена направления
  if (!entering_code) {
    entering_code = true;
    current_number = 1;
    last_direction_right = is_right;
    Serial.println("Started entering code");
  } else if (last_direction_right != is_right) {
    // Смена направления - добавляем текущее число в код
    addNumberToCode();
    current_number = 1;
    last_direction_right = is_right;
  } else {
    // Продолжение в том же направлении
    if (current_number < 9) {
      current_number++;
    }
  }
  
  last_rotation_time = current_time;
  
  // Звук в зависимости от направления
  if (is_right) {
    beepLow();
  } else {
    beepHigh();
  }
  
  printCurrentInput();
}

void handleClick() {
  if (entering_code) {
    // Добавляем последнее число
    addNumberToCode();
    
    // Проверяем код
    Serial.print("Checking code... ");
    if (compareCode()) {
      Serial.println("Correct!");
      openLock();
    } else {
      Serial.println("Incorrect!");
      showError();
    }
    
    resetCurrentCode();
  } else {
    Serial.println("Click without code entry");
  }
}

void loop() {
  // Обработка веб-сервера
  server.handleClient();
  
  // Обработка энкодера
  enc.tick();
  
  // Проверка таймаута
  handleTimeout();
  
  // Проверка вращения энкодера
  if (enc.right()) {
    Serial.println("Rotate Right");
    handleRotation(true);
  }
  
  if (enc.left()) {
    Serial.println("Rotate Left");
    handleRotation(false);
  }
  
  // Проверка нажатия кнопки
  if (enc.click()) {
    Serial.println("Click!");
    handleClick();
  }
}