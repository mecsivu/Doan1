#include <LedControl.h>
#include <IRremote.hpp>  // Sử dụng .hpp cho phiên bản mới
#include <DHT.h>
#include <EEPROM.h>

// Định nghĩa chân kết nối với module MAX7219
const int DATA_IN = 23;    // Chân DIN
const int LOAD = 18;       // Chân LOAD
const int CLK = 15;        // Chân CLK

// Định nghĩa số lượng module LED 7 đoạn
const int NUM_DEVICES = 1;  // Sử dụng một module LED 7 đoạn với 8 LED

// Khởi tạo đối tượng LedControl (DATA_IN, LOAD, CLK, số lượng module)
LedControl lc = LedControl(DATA_IN, CLK, LOAD, NUM_DEVICES);

// Định nghĩa chân cảm biến độ ẩm đất
const int soilMoisturePin = 14; // ESP32 chân ADC (chân D34)

// Định nghĩa chân điều khiển relay
const int relayPin = 13;  // Chân D13 của ESP32 điều khiển relay

// Khai báo IR Receiver
const int recv_pin = 4;  // Chân ESP32 kết nối với IR receiver
IRrecv irrecv(recv_pin);
decode_results results;

// Định nghĩa cảm biến DHT11
#define DHTPIN 5       // Chân của DHT11
#define DHTTYPE DHT11  // Định nghĩa loại cảm biến là DHT11
DHT dht(DHTPIN, DHTTYPE);  // Khởi tạo đối tượng DHT

// Các chế độ
enum Mode { MANUAL, AUTOMATIC, TIMED };
Mode currentMode = MANUAL;  // Mặc định là chế độ thủ công

unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 200;  // Tăng thời gian debounce lên 200ms
bool relayStatus = false; // Trạng thái của relay (máy bơm)

// Định nghĩa chân nút nhấn
const int BUTTON_PIN_1 = 34;  // Nút nhấn bật/tắt relay
const int BUTTON_PIN_2 = 35;  // Nút nhấn đổi chế độ

// Biến theo dõi trạng thái đã thay đổi
Mode previousMode = MANUAL;
bool previousRelayStatus = false;
int previousMoisture = -1;  // Thêm biến theo dõi độ ẩm trước đó
int previousTemperature = -1;  // Biến theo dõi nhiệt độ trước đó
bool previousButton1State = HIGH;
bool previousButton2State = HIGH;

// Biến thời gian để kiểm soát việc in và cập nhật display
unsigned long lastPrintTime = 0;
unsigned long lastDisplayUpdate = 0;
const unsigned long printInterval = 2000;  // In thông tin mỗi 2 giây
const unsigned long displayUpdateInterval = 500; // Cập nhật display mỗi 500ms

// Các biến cho đồng hồ phần mềm
unsigned long previousMillis = 0; // Để theo dõi thời gian đã trôi qua
unsigned long interval = 1000;    // Mỗi giây (1000 ms)
int currentHour = 11;             // Giờ bắt đầu (ví dụ 10h AM)
int currentMinute = 46;            // Phút bắt đầu
int currentSecond = 0;            // Giây bắt đầu

// Thời gian giới hạn 60 phút
unsigned long demoDuration = 3600000; // 60 phút (3600000 ms)
unsigned long demoStartTime = 0;

void setup() {
  Serial.begin(115200);
  delay(2000);  // Đợi Serial khởi động
  
  // Khởi tạo EEPROM
  EEPROM.begin(512);  // Kích thước EEPROM, có thể thay đổi tùy theo nhu cầu

  // Đọc thời gian từ EEPROM
  currentHour = EEPROM.read(0);    // Đọc giờ từ EEPROM
  currentMinute = EEPROM.read(1);  // Đọc phút từ EEPROM
  currentSecond = EEPROM.read(2);  // Đọc giây từ EEPROM

  // Nếu thời gian chưa được lưu (ví dụ lần đầu chạy), sử dụng giá trị mặc định
  if (currentHour == 255) currentHour =11;  // Giá trị mặc định 15 (3 PM)
  if (currentMinute == 255) currentMinute = 12;
  if (currentSecond == 255) currentSecond = 0;
  
  Serial.print("Time loaded from EEPROM: ");
  Serial.print(currentHour);
  Serial.print(":");
  Serial.print(currentMinute);
  Serial.print(":");
  Serial.println(currentSecond);

  Serial.println("=== ESP32 Irrigation System Starting ===");

  // Khởi tạo IR receiver
  IrReceiver.begin(recv_pin, false);  // false = tắt LED feedback
  Serial.println("IR Receiver initialized");

  // Khởi tạo các module LedControl
  lc.shutdown(0, false);  // Bật module LED
  lc.setIntensity(0, 8);   // Cài đặt độ sáng (0-15)
  lc.clearDisplay(0);      // Xóa màn hình
  Serial.println("LED Control initialized");

  // Khởi tạo relay
  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, LOW);  // Tắt relay (máy bơm)
  Serial.println("Relay initialized - OFF");

  // Khởi tạo DHT11
  dht.begin();

  // Khởi tạo nút nhấn
  pinMode(BUTTON_PIN_1, INPUT_PULLUP);  // Nút nhấn bật/tắt relay
  pinMode(BUTTON_PIN_2, INPUT_PULLUP);  // Nút nhấn đổi chế độ
  Serial.println("Buttons initialized");

  Serial.println("System ready!");
  Serial.println("=====================================");

  // Lưu thời gian bắt đầu demo
  demoStartTime = millis();

  // Hiển thị ban đầu
  updateDisplay();
}

void loop() {
  unsigned long currentMillis = millis();  // Lấy thời gian hiện tại (milisecond từ khi khởi động)

  // Kiểm tra nếu đã vượt quá 60 phút
  if (currentMillis - demoStartTime >= demoDuration) {
    Serial.println("Demo time is over (60 minutes)");
    // Bạn có thể dừng hệ thống hoặc thực hiện hành động khác khi hết thời gian demo
    return; // Dừng lại sau khi 60 phút
  }

  // Kiểm tra tín hiệu từ remote IR
  if (IrReceiver.decode()) {
    uint32_t decCode = IrReceiver.decodedIRData.decodedRawData;
    
    // Chỉ xử lý nếu không phải tín hiệu repeat
    if (!(IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT)) {
      Serial.print("IR Code received: 0x");
      Serial.println(decCode, HEX);
      
      // Kiểm tra mã của các nút từ remote để bật/tắt relay và đổi chế độ
      if (decCode == 0xBA45FF00) { // Mã nút để bật/tắt relay
        toggleRelay();
        Serial.println("IR: Toggle Relay");
      } 
      else if (decCode == 0xB946FF00) { // Mã nút để đổi chế độ
        toggleMode();
        Serial.println("IR: Toggle Mode");
      }
    }
    IrReceiver.resume();
  }

  // Kiểm tra nút nhấn với debounce
  checkButtons();
  
  // Đọc giá trị từ cảm biến DHT11 (Nhiệt độ và độ ẩm)
  float temperature = dht.readTemperature();  // Đọc nhiệt độ
  // Đọc giá trị từ cảm biến độ ẩm đất
  int sensorValue = analogRead(soilMoisturePin);
  float moisturePercentage = map(sensorValue, 0, 4095, 0, 100);
  float actualMoisture = 100.0 - moisturePercentage;
  int moistureInt = round(actualMoisture);
  int tempInt = round(temperature);

  // Cập nhật đồng hồ phần mềm mỗi giây
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;  // Đặt lại thời gian bắt đầu cho lần sau

    // Cập nhật giây
    currentSecond++;
    if (currentSecond >= 60) {
      currentSecond = 0;
      currentMinute++;  // Tăng phút
    }
    if (currentMinute >= 60) {
      currentMinute = 0;
      currentHour++;  // Tăng giờ
    }
    if (currentHour >= 24) {
      currentHour = 0;  // Quay lại 0 giờ (đồng hồ 24h)
    }
    
    // In thời gian ra Serial Monitor
    Serial.print("Time: ");
    if (currentHour < 10) Serial.print("0");  // Hiển thị 2 chữ số
    Serial.print(currentHour);
    Serial.print(":");
    if (currentMinute < 10) Serial.print("0");  // Hiển thị 2 chữ số
    Serial.print(currentMinute);
    Serial.print(":");
    if (currentSecond < 10) Serial.print("0");  // Hiển thị 2 chữ số
    Serial.println(currentSecond);
  }

  // Ghi thời gian vào EEPROM mỗi phút (giảm tần suất ghi)
  if (currentMillis - previousMillis >= 60000) {  // Mỗi phút ghi lại
    EEPROM.write(0, currentHour);    // Ghi giờ vào EEPROM
    EEPROM.write(1, currentMinute);  // Ghi phút vào EEPROM
    EEPROM.write(2, currentSecond);  // Ghi giây vào EEPROM
    EEPROM.commit();  // Lưu vào EEPROM
    Serial.println("Time saved to EEPROM.");
  }

  // Kiểm tra chế độ và điều khiển relay
  if (currentMode == AUTOMATIC) {
    bool shouldTurnOn = (actualMoisture < 20 || temperature > 40);  // Nếu độ ẩm dưới ngưỡng 30% hoặc nhiệt độ trên 40°C
    
    if (shouldTurnOn && !relayStatus) {
      digitalWrite(relayPin, HIGH);  // Bật relay (máy bơm)
      relayStatus = true;
      Serial.println("AUTO: Relay ON - Soil too dry or Temperature high");
    } 
    else if (!shouldTurnOn && relayStatus) {
      digitalWrite(relayPin, LOW);  // Tắt relay (máy bơm)
      relayStatus = false;
      Serial.println("AUTO: Relay OFF - Soil moisture OK or Temperature OK");
    }
  }

  // Kiểm tra chế độ MODE 3 (Tưới cây theo lịch - từ 16h đến 17h)
  if (currentMode == TIMED) {
    checkScheduledWatering();  // Kiểm tra thời gian và điều khiển relay
  }

  // Cập nhật display chỉ khi cần thiết (để tránh nhấp nháy)
  if (millis() - lastDisplayUpdate > displayUpdateInterval || 
      currentMode != previousMode || 
      moistureInt != previousMoisture || 
      tempInt != previousTemperature ||
      relayStatus != previousRelayStatus) {
    
    updateDisplay(moistureInt, tempInt);
    
    // Lưu trạng thái hiện tại
    previousMode = currentMode;
    previousMoisture = moistureInt;
    previousTemperature = tempInt;
    previousRelayStatus = relayStatus;
    lastDisplayUpdate = millis();
  }

  // In thông tin tổng quan định kỳ
  if (millis() - lastPrintTime > printInterval) {
    printStatus(moistureInt);
    lastPrintTime = millis();
  }

  delay(100);  // Đợi một chút trước khi tiếp tục vòng lặp
}

// Hàm kiểm tra nút nhấn với debounce cải tiến
void checkButtons() {
  static unsigned long lastButton1Press = 0;
  static unsigned long lastButton2Press = 0;
  static bool button1Processed = false;
  static bool button2Processed = false;
  
  bool button1State = digitalRead(BUTTON_PIN_1);
  bool button2State = digitalRead(BUTTON_PIN_2);
  
  // Xử lý nút 1 (Toggle Relay)
  if (button1State == LOW && !button1Processed) {
    if (millis() - lastButton1Press > debounceDelay) {
      toggleRelay();
      button1Processed = true;
      lastButton1Press = millis();
    }
  } else if (button1State == HIGH) {
    button1Processed = false;
  }
  
  // Xử lý nút 2 (Toggle Mode)
  if (button2State == LOW && !button2Processed) {
    if (millis() - lastButton2Press > debounceDelay) {
      toggleMode();
      button2Processed = true;
      lastButton2Press = millis();
    }
  } else if (button2State == HIGH) {
    button2Processed = false;
  }
}

// Hàm điều khiển relay
void toggleRelay() {
  relayStatus = !relayStatus;
  digitalWrite(relayPin, relayStatus ? HIGH : LOW);
  Serial.print("Relay ");
  Serial.println(relayStatus ? "ON" : "OFF");
}

// Hàm thay đổi chế độ theo thứ tự: MANUAL -> AUTOMATIC -> TIMED -> MANUAL
void toggleMode() {
  // Đổi chế độ theo chu trình MANUAL -> AUTO -> TIMED
  if (currentMode == MANUAL) {
    currentMode = AUTOMATIC;
  } 
  else if (currentMode == AUTOMATIC) {
    currentMode = TIMED;
  } 
  else if (currentMode == TIMED) {
    currentMode = MANUAL;
  }

  Serial.print("Mode changed to: ");
  Serial.println(currentMode == MANUAL ? "MANUAL" : (currentMode == AUTOMATIC ? "AUTOMATIC" : "TIMED"));
}

// Hàm kiểm tra lịch tưới cây từ API
void checkScheduledWatering() {
  // Kiểm tra thời gian hiện tại và bật/tắt relay tương ứng
  if ((currentHour == 11 && currentMinute >= 20) || (currentHour == 12 && currentMinute < 20)) {  // Nếu trong khoảng thời gian từ 16h đến 17h
    if (!relayStatus) {
      digitalWrite(relayPin, HIGH);  // Bật relay (máy bơm)
      relayStatus = true;
      Serial.println("TIMED: Relay ON - Scheduled watering");
    }
  } else {
    if (relayStatus) {
      digitalWrite(relayPin, LOW);  // Tắt relay (máy bơm)
      relayStatus = false;
      Serial.println("TIMED: Relay OFF - Outside scheduled watering time");
    }
  }
}

// Hàm cập nhật toàn bộ display - GỌI DUY NHẤT MỘT LẦN
void updateDisplay() {
  float temperature = dht.readTemperature();
  int sensorValue = analogRead(soilMoisturePin);
  float moisturePercentage = map(sensorValue, 0, 4095, 0, 100);
  float actualMoisture = 100.0 - moisturePercentage;
  int moistureInt = round(actualMoisture);
  int tempInt = round(temperature);
  
  updateDisplay(moistureInt, tempInt);
}

void updateDisplay(int moistureInt, int tempInt) {
  // Xóa display trước khi cập nhật
  lc.clearDisplay(0);
  
  // Hiển thị chế độ (vị trí 4,3)
  displayMode(currentMode);
  
  // Hiển thị độ ẩm (vị trí 1,0) 
  displayMoisture(moistureInt);
  
  // Hiển thị nhiệt độ (vị trí 7,6)
  displayTemperature(tempInt);
  
  // Không hiển thị trạng thái relay trên LED
}

// Hàm hiển thị chế độ lên module LED 7 đoạn
void displayMode(Mode mode) {
  if (mode == AUTOMATIC) {
    lc.setChar(0, 4, 'A', false);
    lc.setRow(0, 3, B00111110); // Hiển thị chữ "U"
  } else if (mode == TIMED) {
    lc.setChar(0, 4, '5', false);
    lc.setRow(0, 3, B01001110); // Chữ "C" - segments a,d,e,f (dp=0)
  } else if (mode == MANUAL) {
    lc.setChar(0, 4, 'H', false);
    lc.setChar(0, 3, 'A', false);
  }
}

// Hàm hiển thị độ ẩm lên module LED 7 đoạn
void displayMoisture(int moistureInt) {
  lc.setDigit(0, 1, moistureInt / 10, false);
  lc.setDigit(0, 0, moistureInt % 10, false);
}

// Hàm hiển thị nhiệt độ lên module LED 7 đoạn
void displayTemperature(int tempInt) {
  lc.setDigit(0, 7, tempInt / 10, false);
  lc.setDigit(0, 6, tempInt % 10, false);
}

// Hàm in trạng thái tổng quan
void printStatus(int moisture) {
  Serial.println("--- STATUS ---");
  Serial.print("Mode: ");
  Serial.println(currentMode == AUTOMATIC ? "AUTO" : (currentMode == MANUAL ? "MANUAL" : "TIMED"));
  Serial.print("Moisture: ");
  Serial.print(moisture);
  Serial.println("%");
  Serial.print("Relay: ");
  Serial.println(relayStatus ? "ON" : "OFF");
  Serial.println("-------------");
}
