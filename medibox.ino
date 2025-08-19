#include <Wire.h>
#include <iostream>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>
#include <WiFi.h>
#include <time.h>
#include <ESP32Servo.h>
#include <PubSubClient.h>
// NTP Server configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;


// Hardware pin definitions
#define BUZZER 17
#define LED_1 2
#define LED_2 0
#define PB_UP 27
#define PB_DOWN 26
#define PB_OK 25
#define PB_CANCEL 14
#define DHT_PIN 32
#define LDR 35
#define servo 12 

// OLED display setup
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

DHTesp dhtSensor; // DHT sensor instance
Servo servoMotor;

// Time tracking variables
unsigned long timenow = 0, timelast = 0;
int days = 0, hours = 0, minutes = 0, seconds = 0;

// Alarm configuration
#define MAX_ALARMS 2
int alarm_hours[MAX_ALARMS] = {0, 0};
int alarm_minutes[MAX_ALARMS] = {0, 0};
bool alarm_triggered[MAX_ALARMS] = {true, true};
int n_alarms = 0;
bool alarm_enabled = true;

// Menu options for the system
String options[] = {"Set Time (NTP)", "Set Alarm 1", "Set Alarm 2", "View Alarms", "Delete Alarm"};
int current_mode = 0;
const int max_modes = 5;


int notes[] = {262, 294, 330, 349};
const int n_notes = 4;

float SamplingInterval=5.0;
float SendingInterval=120.0;
float temperature =0.0;
float light_intensity =0.0;
float ServoAngle=0.0;
unsigned long sample=0;
unsigned long send=0;
int n=0;
WiFiClient espClient;
PubSubClient mqttClient(espClient);

void setup() {
  Serial.begin(9600);

  // Initialize hardware pins
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  pinMode(PB_UP, INPUT_PULLUP);
  pinMode(PB_DOWN, INPUT_PULLUP);
  pinMode(PB_OK, INPUT_PULLUP);
  pinMode(PB_CANCEL, INPUT_PULLUP);
  std::cout << "connected";

  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED failed!");
    while (1);
  }
  display.clearDisplay();

  // Initialize DHT sensor
  dhtSensor.setup(DHT_PIN, DHTesp::DHT22);
  servoMotor.setPeriodHertz(50);       // Standard servo PWM frequency
  servoMotor.attach(servo, 500, 2400);  // Pin, min/max pulse width in µs
  servoMotor.write(0);

  // Connect to WiFi
  print_line("Connecting WiFi", 2, 0, 0);
  WiFi.begin("Wokwi-GUEST", "", 6);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");

  mqttClient.setServer("test.mosquitto.org", 1883);

  mqttClient.setCallback(receiveCallback);

  connectToBroker();

  // Sync time from NTP server
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  sync_time_from_ntp();

  print_line("Medibox On", 2, 0, 0);
  delay(2000);
}

// Synchronize time from NTP server
void sync_time_from_ntp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    print_line("NTP Failed", 2, 0, 0);
    delay(2000);
    return;
  }
  hours = timeinfo.tm_hour;
  minutes = timeinfo.tm_min;
  seconds = timeinfo.tm_sec;
  days = timeinfo.tm_yday;
  timelast = millis() / 1000;
  display.clearDisplay();
  print_line("Time Synced", 2, 0, 0);
  delay(1000);
}

// Display the current time on the OLED
void print_time_now(void) {
  display.clearDisplay();
  print_line(String(hours), 2, 0, 30);
  print_line(":", 2, 0, 50);
  print_line(String(minutes), 2, 0, 60);
  print_line(":", 2, 0, 80);
  print_line(String(seconds), 2, 0, 90);
  print_line("NTP", 1, 50, 0);
}

// Update the internal clock
void update_time(void) {
  timenow = millis() / 1000;
  seconds = timenow - timelast;
  if (seconds >= 60) {
    timelast += 60;
    minutes++;
    if (minutes >= 60) {
      minutes = 0;
      hours++;
      if (hours >= 24) {
        hours = 0;
        days++;
        for (int i = 0; i < n_alarms; i++) alarm_triggered[i] = false;
      }
    }
  }
}

// Check and trigger alarms
void update_time_with_check_alarm() {
  update_time();
  display.clearDisplay();
  print_time_now();
  if (alarm_enabled) {
    for (int i = 0; i < n_alarms; i++) {
      if (!alarm_triggered[i] && hours == alarm_hours[i] && minutes == alarm_minutes[i] && seconds == 0) {
        ring_alarm();
        alarm_triggered[i] = true;
      }
    }
  }
}

void set_alarm(int alarm) {
  int temp_hour = alarm_hours[alarm];
  while (true) {
    display.clearDisplay();
    print_line("Enter hour: " + String(temp_hour), 2, 0, 0);
    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      temp_hour = (temp_hour + 1) % 24;
    }
    else if (pressed == PB_DOWN) {
      temp_hour = (temp_hour - 1 + 24) % 24;
    }
    else if (pressed == PB_OK) {
      alarm_hours[alarm] = temp_hour;
      break;
    }
    else if (pressed == PB_CANCEL) {
      break;
    }
  }
  int temp_minute = alarm_minutes[alarm];
  while (true) {
    display.clearDisplay();
    print_line("Enter minute: " + String(temp_minute), 2, 0, 0);
    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      temp_minute = (temp_minute + 1) % 60;
    }
    else if (pressed == PB_DOWN) {
      temp_minute = (temp_minute - 1 + 60) % 60;
    }
    else if (pressed == PB_OK) {
      alarm_minutes[alarm] = temp_minute;
      alarm_triggered[alarm] = false;
      if (alarm >= n_alarms) n_alarms = alarm + 1;
      break;
    }
    else if (pressed == PB_CANCEL) {
      break;
    }
  }
  display.clearDisplay();
  print_line("Alarm is set", 2, 0, 0);
  delay(200);
}
void set_time() {
  display.clearDisplay();
  print_line("Syncing NTP...", 2, 0, 0);
  sync_time_from_ntp();
}

// Activate the alarm buzzer and LED
void ring_alarm() {
  display.clearDisplay();
  print_line("Medicine Time", 2, 0, 0);
  digitalWrite(LED_1, HIGH);
  while (digitalRead(PB_CANCEL) == HIGH) {
    for (int i = 0; i < n_notes; i++) {
      if (digitalRead(PB_CANCEL) == LOW) break;
      tone(BUZZER, notes[i]);
      delay(500);
      noTone(BUZZER);
    }
  }
  digitalWrite(LED_1, LOW);
}

// Check temperature and humidity
void check_temp(void) {
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  bool all_good = true;
  temperature = data.temperature;
  if (data.temperature > 32 || data.temperature < 24 || data.humidity > 80 || data.humidity < 65) {
    all_good = false;
    digitalWrite(LED_2, HIGH);
    if (data.temperature > 32) print_line("TEMP HIGH", 1, 40, 0);
    if (data.temperature < 24) print_line("TEMP LOW", 1, 40, 0);
    if (data.humidity > 80) print_line("HUMD HIGH", 1, 50, 0);
    if (data.humidity < 65) print_line("HUMD LOW", 1, 50, 0);
  }
  if (all_good) digitalWrite(LED_2, LOW);
}
void print_line(String text, int text_size, int row, int column) {
  display.setTextSize(text_size);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(column, row);
  display.println(text);
  display.display();
}

void go_to_menu(void) {
  while (digitalRead(PB_CANCEL) == HIGH) {
    display.clearDisplay();
    print_line(options[current_mode], 2, 0, 0);
    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      current_mode = (current_mode + 1) % max_modes;
    }
    else if (pressed == PB_DOWN) {
      current_mode = (current_mode - 1 + max_modes) % max_modes;
    }
    else if (pressed == PB_OK) {
      run_mode(current_mode);
    }
  }
}
void run_mode(int mode) {
  switch (mode) {
    case 0: set_time(); break;
    case 1: set_alarm(0); break;
    case 2: set_alarm(1); break;
    case 3: view_alarms(); break;
    case 4: delete_alarm(); break;
  }
}
int wait_for_button_press() {
  while (true) {
    if (digitalRead(PB_UP) == LOW) {
      delay(200);
      return PB_UP;
    }
    else if (digitalRead(PB_DOWN) == LOW) {
      delay(200);
      return PB_DOWN;
    }
    else if (digitalRead(PB_OK) == LOW) {
      delay(200);
      return PB_OK;
    }
    else if (digitalRead(PB_CANCEL) == LOW) {
      delay(200);
      return PB_CANCEL;
    }
    update_time(); // Keep time updated while waiting
  }
}
void view_alarms() {
  display.clearDisplay();
  for (int i = 0; i < n_alarms; i++) {
    String alarm_str = "A" + String(i + 1) + ": " + String(alarm_hours[i]) + ":" + String(alarm_minutes[i]);
    print_line(alarm_str, 1, i * 10, 0);
  }
  delay(2000);
}

void delete_alarm() {
  int selected = 0;
  while (true) {
    display.clearDisplay();
    for (int i = 0; i < n_alarms; i++) {
      String alarm_str = (i == selected ? "> " : "  ") + String("A") + String(i + 1) + ": " + 
                         String(alarm_hours[i]) + ":" + String(alarm_minutes[i]);
      print_line(alarm_str, 1, i * 10, 0);
    }
    int pressed = wait_for_button_press();
    if (pressed == PB_UP) selected = (selected - 1 + n_alarms) % n_alarms;
    else if (pressed == PB_DOWN) selected = (selected + 1) % n_alarms;
    else if (pressed == PB_OK) {
      for (int i = selected; i < n_alarms - 1; i++) {
        alarm_hours[i] = alarm_hours[i + 1];
        alarm_minutes[i] = alarm_minutes[i + 1];
        alarm_triggered[i] = alarm_triggered[i + 1];
      }
      n_alarms--;
      display.clearDisplay();
      print_line("Alarm Deleted", 2, 0, 0);
      delay(1000);
      break;
    }
    else if (pressed == PB_CANCEL) break;
  }
}

void connectToBroker() {
  const char* clientId = "ESP32-7777777";

  while (!mqttClient.connected()) {
    Serial.println("Attempting MQTT connection...");

    display.clearDisplay();
    print_line("Attempting MQTT", 2, 0, 0);
    delay(3000);

    bool connected = mqttClient.connect(clientId);

    if (connected) {
      Serial.println("Connected to MQTT broker");
      display.clearDisplay();
      print_line("Connected to MQTT", 2, 0, 0);
      delay(3000);

      display.clearDisplay();
      print_line("Subscribing...", 2, 0, 0);

      mqttClient.subscribe("sampling interval");
      mqttClient.subscribe("sending interval");
      mqttClient.subscribe("minimum servo angle");
    } else {
      Serial.print("Connection failed, state: ");
      Serial.println(mqttClient.state());

      display.clearDisplay();
      print_line("MQTT failed", 2, 0, 0);
      delay(5000);
    }
  }
}

void receiveCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  char payloadCharAr[length + 1];  // +1 for null terminator

  for (int i = 0; i < length; i++) {
    payloadCharAr[i] = (char)payload[i];
    Serial.println((char)payload[i]);
    Serial.print("Message arrived [");
  }
  payloadCharAr[length] = '\0';  // Null-terminate the char array

  if (strcmp(topic, "sampling interval") == 0) {
    SamplingInterval = atof(payloadCharAr);
  } 
  else if (strcmp(topic, "sending interval") == 0) {
    SendingInterval = atof(payloadCharAr);
  } 
  else if (strcmp(topic, "minimum servo angle") == 0) {
    ServoAngle = atof(payloadCharAr);
  }
}

void lightIntensity(){
  float I = analogRead(LDR)*1.00;
  light_intensity += 1-(I/4096.00);
  n+=1;

}




// Main loop
void loop() {
  Serial.println(F("Starting loop cycle"));
  Serial.println(temperature);
  if (!mqttClient.connected()) {
    Serial.println(F("Reconnect MQTT..."));
    connectToBroker();
  }

  check_temp();
  mqttClient.loop();
  //mqttClient.setCallback(receiveCallback);
  servoMotor.write(ServoAngle);
  Serial.println(ServoAngle);
  mqttClient.publish("Mediboxtemperature", String(temperature).c_str());
  

  unsigned long currentSeconds = millis() / 1000;

  if (SamplingInterval <= currentSeconds - sample) {
    sample = currentSeconds;
    lightIntensity();
  }

  if (SendingInterval <= currentSeconds - send) {
    send = currentSeconds;

    if (n != 0) {
      light_intensity /= n;
      mqttClient.publish("Light intensity", String(light_intensity).c_str());
    }
    n = 0;

    Serial.println(F("Light Intensity:"));
    Serial.println(light_intensity);

    
    
  }

    update_time_with_check_alarm();
    check_temp();
    if (digitalRead(PB_OK) == LOW) {
      delay(200);
      go_to_menu();
    }
    static unsigned long lastSync = 0;
    if (millis() - lastSync >= 3600000) {
      sync_time_from_ntp();
      lastSync = millis();
    }
    delay(100);
}
