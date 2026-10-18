#include "CobLedController.h"
#include "driver/gpio.h"

// Khởi tạo 2 đối tượng điều khiển độc lập cho 2 thanh LED mặt lưng
CobLedController cobLedRight(PIN_COB_LED_R); // GPIO 18 (Thanh Phải / Xanh dương)
CobLedController cobLedLeft(PIN_COB_LED_L);  // GPIO 38 (Thanh Trái / Đỏ)

// Alias tương thích ngược với code phác thảo ban đầu của người dùng
CobLedController& ledCobBlue = cobLedRight;
CobLedController& ledCobRed  = cobLedLeft;

CobLedController::CobLedController(uint8_t targetPin) {
  pin = targetPin;
  isPoweredOn = false;
  isTriggering = false;
  pulseStartTime = 0;
}

void CobLedController::begin(bool powerOn) {
  pinMode(pin, OUTPUT);
  // Thiết lập khả năng cấp dòng tối đa của chân GPIO S3 (Drive Cap 3 = ~40mA)
  // để nuôi thanh LED COB 3V (< 20mA) ổn định nhất, chống sụt áp
  gpio_set_drive_capability((gpio_num_t)pin, GPIO_DRIVE_CAP_3);

  if (powerOn) {
    turnOn();
  } else {
    turnOff();
  }
}

void CobLedController::turnOn() {
  isTriggering = false;
  isPoweredOn = true;
  // Xuất mức HIGH số thuần túy (Không dùng PWM)
  digitalWrite(pin, HIGH);
}

void CobLedController::turnOff() {
  isTriggering = false;
  isPoweredOn = false;
  digitalWrite(pin, LOW);
}

void CobLedController::triggerNextMode() {
  if (!isPoweredOn) {
    turnOn();
    return;
  }
  if (!isTriggering) {
    // Ngắt nguồn tạm thời xuống LOW để IC trên thanh LED nhận cờ chuyển mode
    digitalWrite(pin, LOW);
    pulseStartTime = millis();
    isTriggering = true;
  }
}

void CobLedController::update() {
  if (isTriggering) {
    // Sau khi ngắt đủ thời gian COB_PULSE_LOW_MS (300ms), cấp điện HIGH trở lại
    if (millis() - pulseStartTime >= COB_PULSE_LOW_MS) {
      digitalWrite(pin, HIGH);
      isTriggering = false;
    }
  }
}

// Hàm tương thích ngược với bản phác thảo
void triggerBothCobLeds() {
  CobLeds::triggerNextMode();
}

// ============================================================================
//                  NAMESPACE QUẢN LÝ TẬP TRUNG HỆ THỐNG COB LED
// ============================================================================
namespace CobLeds {

  // Danh mục 4 chế độ chuẩn của thanh LED COB mưa sao băng 58mm/70mm (theo catalog nhà sản xuất):
  // 0: Tắt nguồn
  // 1: Sao băng (Nước chảy quét tự động)
  // 2: Ánh sáng không đổi và luôn sáng
  // 3: Nhấp nháy bùng nổ (Strobe / Flash)
  // 4: Ngọn lửa (Flame / Thở bập bùng)
  static const char* MODE_NAMES[5] = {
    "Đang tắt",
    "1. Luôn Sáng",
    "2. Sao Băng",
    "3. Ngọn Lửa",
    "4. Nhấp Nháy"
  };

  static uint8_t s_currentMode = 1; // Mặc định Mode 1 (Sao băng) khi bật
  static bool s_isPoweredOn = true;

  void init(bool powerOn) {
    s_isPoweredOn = powerOn;
    if (!powerOn) {
      s_currentMode = 0;
    } else if (s_currentMode == 0) {
      s_currentMode = 1;
    }

    cobLedRight.begin(powerOn);
    cobLedLeft.begin(powerOn);

    Serial.println("✨ [COB LED] Đã khởi tạo 2 thanh LED COB Sao Băng (GPIO 18 & 38) ở chế độ Digital HIGH thuần túy!");
  }

  void update() {
    cobLedRight.update();
    cobLedLeft.update();
  }

  void turnOn() {
    s_isPoweredOn = true;
    if (s_currentMode == 0) s_currentMode = 1;
    cobLedRight.turnOn();
    cobLedLeft.turnOn();
  }

  void turnOff() {
    s_isPoweredOn = false;
    s_currentMode = 0;
    cobLedRight.turnOff();
    cobLedLeft.turnOff();
  }

  void togglePower() {
    if (s_isPoweredOn) {
      turnOff();
    } else {
      turnOn();
    }
  }

  bool isOn() {
    return s_isPoweredOn;
  }

  void triggerNextMode() {
    if (!s_isPoweredOn) {
      turnOn();
      return;
    }

    // Nhảy sang mode kế tiếp trên IC
    s_currentMode++;
    if (s_currentMode > 4) {
      s_currentMode = 1; // Vòng lại chế độ 1 (Sao băng)
    }

    cobLedRight.triggerNextMode();
    cobLedLeft.triggerNextMode();
  }

  void nextMode() {
    if (!s_isPoweredOn) {
      turnOn();
      return;
    }
    triggerNextMode();
  }

  void prevMode() {
    // Với IC đếm vòng tròn một chiều, lùi mode tương đương đi tới 3 bước
    triggerNextMode();
  }

  uint8_t getCurrentMode() {
    return s_isPoweredOn ? s_currentMode : 0;
  }

  void setCurrentMode(uint8_t mode) {
    if (mode == 0) {
      turnOff();
      return;
    }
    if (!s_isPoweredOn) {
      turnOn();
    }
    s_currentMode = constrain(mode, 1, 4);
  }

  const char* getModeName() {
    if (!s_isPoweredOn) return MODE_NAMES[0];
    return MODE_NAMES[constrain(s_currentMode, 1, 4)];
  }

} // namespace CobLeds