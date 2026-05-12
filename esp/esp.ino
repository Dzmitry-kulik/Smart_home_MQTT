#include <SoftwareSerial.h>

// Настройка пинов для связи с STM32:
// Если у вас ESP8266 (NodeMCU / Wemos D1 Mini): Подключите TX от STM32 к пину D1, а RX к D2
// Если у вас ESP32: Подключите TX от STM32 к пину GPIO4, а RX к GPIO5
#if defined(ESP8266)
  const int RX_PIN = 5; // Пин D1 на плате ESP8266
  const int TX_PIN = 4; // Пин D2 на плате ESP8266
#else
  const int RX_PIN = 4; // GPIO4 на ESP32
  const int TX_PIN = 5; // GPIO5 на ESP32
#endif

// Создаем программный порт для общения с STM32
SoftwareSerial stmSerial(RX_PIN, TX_PIN);

void setup() {
  // Порт для вывода логов на компьютер
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n--- ТЕСТ СВЯЗИ С STM32 ЗАПУЩЕН ---");
  Serial.print("Подключите TX от STM32 к пину GPIO ");
  Serial.println(RX_PIN);

  // Порт для приема данных от STM32 (скорость совпадает с MX_USART1_UART_Init)
  stmSerial.begin(115200);
}

void loop() {
  // Проверяем, пришли ли данные от STM32
  if (stmSerial.available()) {
    // Читаем входящую строку до символа переноса строки '\n'
    String data_from_stm = stmSerial.readStringUntil('\n');
    
    // Удаляем случайные пробелы или символы перевода каретки '\r'
    data_from_stm.trim();
    
    // Выводим принятое сообщение в Монитор порта компьютера
    Serial.print("[Данные из STM32]: ");
    Serial.println(data_from_stm);

    // Простая проверка формата JSON
    if (data_from_stm.startsWith("{") && data_from_stm.endsWith("}")) {
      Serial.println(" -> Статус: Успешно! Строка является валидным JSON пакетом.");
    } else {
      Serial.println(" -> Статус: Предупреждение! Строка повреждена или не является JSON.");
    }
    Serial.println("----------------------------------------");
  }
}
