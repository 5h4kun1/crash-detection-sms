#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <EEPROM.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Create an ADXL345 object
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

// Pin definitions
#define BUZZER_PIN 13

// WiFi credentials
const char* ssid = "Airtel_LuffyThePirateKing";
const char* password = "sweety457";

// CircuitDigest API credentials
const char* circuitDigestApiKey = "FBk86qDn2CjL"; // Replace with your API key
const char* deviceName = "FallSensor"; // Variable 1 for template
const char* location = "LivingRoom"; // Variable 2 for template
String phoneNumber = "+918555853408"; // Must be OTP-verified in CircuitDigest
const int templateId = 103; // Motion Detected template

// Web server on port 80
WebServer server(80);

// System variables
const float fallThreshold = 15.0;
bool alertSent = false;
bool systemArmed = true;
String lastSMSStatus = "Not sent";
String systemLogs = "";
float currentAcceleration = 0.0;
float accelX = 0.0, accelY = 0.0, accelZ = 0.0;
unsigned long lastSensorRead = 0;
unsigned long lastLogUpdate = 0;

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512);
  
  // Initialize buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Load saved phone number from EEPROM
  loadPhoneNumber();
  
  // Initialize accelerometer
  if (!accel.begin()) {
    addLog("ERROR: ADXL345 not detected!");
    while (1);
  }
  
  accel.setRange(ADXL345_RANGE_16_G);
  addLog("System initialized - ADXL345 ready");
  
  // Connect to WiFi
  addLog("Connecting to WiFi: " + String(ssid));
  WiFi.begin(ssid, password);
  int wifiAttempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifiAttempts < 20) {
    delay(500);
    Serial.print(".");
    wifiAttempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    addLog("WiFi connected, IP address: " + WiFi.localIP().toString());
  } else {
    addLog("ERROR: Failed to connect to WiFi");
    while (1);
  }
  
  // Setup web server routes
  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/config", HTTP_POST, handleConfig);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/arm", HTTP_POST, handleArm);
  server.on("/api/logs", handleLogs);
  server.on("/api/custom-sms", HTTP_POST, handleCustomSMS);
  server.begin();
  
  addLog("Web server started");
  delay(2000);
  sendSMS("Fall Detection System Online - IP: " + WiFi.localIP().toString());
}

void loop() {
  server.handleClient();
  
  // Read sensor data every 500ms
  if (millis() - lastSensorRead > 500) {
    readSensorData();
    checkFallDetection();
    lastSensorRead = millis();
  }
  
  // Update logs every 5 seconds
  if (millis() - lastLogUpdate > 5000) {
    addLog("Status: " + String(systemArmed ? "Armed" : "Disarmed") + 
           " | Accel: " + String(currentAcceleration, 2) + " m/s²");
    lastLogUpdate = millis();
  }
}

bool sendSMS(String message) {
  if (WiFi.status() != WL_CONNECTED) {
    addLog("ERROR: WiFi not connected, cannot send SMS");
    return false;
  }
  
  addLog("Attempting to send SMS: " + message.substring(0, 30) + "...");
  
  HTTPClient http;
  String url = "https://www.circuitdigest.cloud/send_sms?ID=" + String(templateId);
  http.begin(url);
  
  // Set headers
  http.addHeader("Authorization", circuitDigestApiKey);
  http.addHeader("Content-Type", "application/json");
  
  // Prepare JSON payload
  DynamicJsonDocument doc(512);
  doc["mobiles"] = phoneNumber; // Use "mobiles" as per API requirement
  doc["var1"] = deviceName;
  doc["var2"] = location;
  
  String payload;
  serializeJson(doc, payload);
  
  // Send POST request
  int httpCode = http.POST(payload);
  
  if (httpCode == 200) {
    String response = http.getString();
    DynamicJsonDocument respDoc(512);
    DeserializationError error = deserializeJson(respDoc, response);
    if (error) {
      addLog("ERROR: Failed to parse JSON response: " + String(error.c_str()));
      http.end();
      return false;
    }
    String status = respDoc["status"] | "unknown";
    if (status == "success") {
      addLog("SMS sent successfully");
      http.end();
      return true;
    } else {
      addLog("ERROR: SMS failed, Response: " + response);
      http.end();
      return false;
    }
  } else {
    String response = http.getString();
    addLog("ERROR: SMS send failed, HTTP " + String(httpCode) + ", Response: " + response);
    http.end();
    return false;
  }
}

void readSensorData() {
  sensors_event_t event;
  accel.getEvent(&event);
  
  accelX = event.acceleration.x;
  accelY = event.acceleration.y;
  accelZ = event.acceleration.z;
  
  currentAcceleration = sqrt(accelX * accelX + accelY * accelY + accelZ * accelZ);
}

void checkFallDetection() {
  if (!systemArmed) return;
  
  if (currentAcceleration > fallThreshold && !alertSent) {
    addLog("FALL DETECTED! Acceleration: " + String(currentAcceleration, 2) + " m/s²");
    
    String alertMsg = "Fall detected at " + getTimeString();
    
    if (sendSMS(alertMsg)) {
      lastSMSStatus = "Sent successfully at " + getTimeString();
      addLog("Alert SMS sent successfully");
    } else {
      lastSMSStatus = "Failed to send at " + getTimeString();
      addLog("ERROR: Failed to send alert SMS");
    }
    
    // Activate buzzer
    for (int i = 0; i < 3; i++) {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(200);
      digitalWrite(BUZZER_PIN, LOW);
      delay(100);
    }
    
    alertSent = true;
    delay(2000);
  }
  
  // Reset alert flag if motion stabilizes
  if (currentAcceleration < 12.0) {
    alertSent = false;
  }
}

// URL encode function for HTTP requests
String urlEncode(String str) {
  String encoded = "";
  for (int i = 0; i < str.length(); i++) {
    char c = str[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else {
      char hex[4];
      sprintf(hex, "%%%02X", c);
      encoded += hex;
    }
  }
  return encoded;
}

// EEPROM phone number management
void loadPhoneNumber() {
  char storedPhone[20];
  for (int i = 0; i < 20; i++) {
    storedPhone[i] = EEPROM.read(i);
    if (storedPhone[i] == '\0') break;
  }
  storedPhone[19] = '\0';
  
  if (isValidPhoneNumber(String(storedPhone))) {
    phoneNumber = String(storedPhone);
    addLog("Loaded phone number: " + phoneNumber);
  } else {
    phoneNumber = "+918555853408";
    addLog("Invalid phone number in EEPROM, using default: " + phoneNumber);
    savePhoneNumber();
  }
}

void savePhoneNumber() {
  for (int i = 0; i < phoneNumber.length() && i < 19; i++) {
    EEPROM.write(i, phoneNumber[i]);
  }
  EEPROM.write(phoneNumber.length(), '\0');
  EEPROM.commit();
  addLog("Saved phone number to EEPROM: " + phoneNumber);
}

bool isValidPhoneNumber(String phone) {
  if (phone.length() < 12 || phone.length() > 15) return false; // Adjusted for +91xxxxxxxxxx
  if (!phone.startsWith("+91")) return false;
  for (int i = 3; i < phone.length(); i++) {
    if (!isDigit(phone[i])) return false;
  }
  return true;
}

void clearEEPROM() {
  for (int i = 0; i < 20; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  addLog("EEPROM cleared");
}

// Logging function
void addLog(String message) {
  String timestamp = getTimeString();
  String logEntry = "[" + timestamp + "] " + message + "\n";
  
  systemLogs += logEntry;
  Serial.println(logEntry);
  
  if (systemLogs.length() > 2000) {
    systemLogs = systemLogs.substring(systemLogs.length() - 1500);
  }
}

// Time string function (uptime-based)
String getTimeString() {
  unsigned long uptime = millis();
  unsigned long seconds = (uptime / 1000) % 60;
  unsigned long minutes = (uptime / (1000 * 60)) % 60;
  unsigned long hours = (uptime / (1000 * 60 * 60)) % 24;
  
  String timeStr = "";
  if (hours < 10) timeStr += "0";
  timeStr += String(hours) + ":";
  if (minutes < 10) timeStr += "0";
  timeStr += String(minutes) + ":";
  if (seconds < 10) timeStr += "0";
  timeStr += String(seconds);
  
  return timeStr;
}

// Web server handler functions
void handleStatus() {
  String json = "{";
  json += "\"armed\":" + String(systemArmed ? "true" : "false") + ",";
  json += "\"phone\":\"" + phoneNumber + "\",";
  json += "\"sms_status\":\"" + lastSMSStatus + "\",";
  json += "\"accel_x\":" + String(accelX, 2) + ",";
  json += "\"accel_y\":" + String(accelY, 2) + ",";
  json += "\"accel_z\":" + String(accelZ, 2) + ",";
  json += "\"accel_mag\":" + String(currentAcceleration, 2);
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleConfig() {
  if (server.hasArg("phone")) {
    String newPhone = server.arg("phone");
    if (isValidPhoneNumber(newPhone)) {
      phoneNumber = newPhone;
      savePhoneNumber();
      addLog("Phone number updated to: " + phoneNumber);
      server.send(200, "text/plain", "Phone number saved successfully");
    } else {
      addLog("Invalid phone number: " + newPhone);
      server.send(400, "text/plain", "Invalid phone number format");
    }
  } else {
    server.send(400, "text/plain", "Missing phone parameter");
  }
}

void handleTest() {
  String testMsg = "Test message from Fall Detection System";
  
  if (sendSMS(testMsg)) {
    lastSMSStatus = "Test sent successfully at " + getTimeString();
    server.send(200, "text/plain", "Test SMS sent successfully!");
  } else {
    lastSMSStatus = "Test failed at " + getTimeString();
    server.send(500, "text/plain", "Failed to send test SMS");
  }
}

void handleCustomSMS() {
  if (server.hasArg("message")) {
    String customMsg = server.arg("message");
    addLog("Received custom message: " + customMsg);
    if (customMsg.length() == 0) {
      server.send(400, "text/plain", "Message cannot be empty");
      return;
    }
    // Use custom message as var1, truncate to 30 chars
    String var1 = customMsg.substring(0, min(30, customMsg.length()));
    var1.replaceAll("[^a-zA-Z0-9]", ""); // Ensure alphanumeric
    DynamicJsonDocument doc(512);
    doc["mobiles"] = phoneNumber;
    doc["var1"] = var1;
    doc["var2"] = getTimeString();
    String payload;
    serializeJson(doc, payload);
    
    HTTPClient http;
    String url = "https://www.circuitdigest.cloud/send_sms?ID=" + String(templateId);
    http.begin(url);
    http.addHeader("Authorization", circuitDigestApiKey);
    http.addHeader("Content-Type", "application/json");
    int httpCode = http.POST(payload);
    
    if (httpCode == 200 && http.getString().indexOf("\"status\":\"success\"") >= 0) {
      lastSMSStatus = "Custom message sent at " + getTimeString();
      server.send(200, "text/plain", "Custom SMS sent successfully!");
    } else {
      lastSMSStatus = "Custom message failed at " + getTimeString();
      server.send(500, "text/plain", "Failed to send custom SMS");
    }
    http.end();
  } else {
    server.send(400, "text/plain", "Missing message parameter");
  }
}

void handleArm() {
  systemArmed = !systemArmed;
  String status = systemArmed ? "Armed" : "Disarmed";
  addLog("System " + status + " via web interface");
  
  alertSent = false;
  
  server.send(200, "text/plain", "System " + status);
}

void handleLogs() {
  server.send(200, "text/plain", systemLogs);
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Fall Detection System</title><style>";
  
  html += "* { margin: 0; padding: 0; box-sizing: border-box; }";
  html += "body { font-family: Arial, sans-serif; background: #f0f2f5; color: #333; }";
  html += ".container { max-width: 800px; margin: 20px auto; padding: 20px; }";
  html += ".header { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: white; padding: 20px; border-radius: 10px; margin-bottom: 20px; }";
  html += ".card { background: white; border-radius: 10px; padding: 20px; margin-bottom: 20px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  html += ".status-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; margin-bottom: 20px; }";
  html += ".status-item { padding: 15px; border-radius: 8px; text-align: center; }";
  html += ".status-armed { background: #d4edda; color: #155724; }";
  html += ".status-disarmed { background: #f8d7da; color: #721c24; }";
  html += ".status-normal { background: #e7f3ff; color: #0c5460; }";
  html += ".btn { padding: 12px 24px; border: none; border-radius: 6px; cursor: pointer; font-size: 16px; margin: 5px; }";
  html += ".btn-primary { background: #007bff; color: white; }";
  html += ".btn-success { background: #28a745; color: white; }";
  html += ".btn-danger { background: #dc3545; color: white; }";
  html += ".btn-warning { background: #ffc107; color: #212529; }";
  html += ".btn-info { background: #17a2b8; color: white; }";
  html += ".input-group { margin: 15px 0; }";
  html += ".input-group label { display: block; margin-bottom: 5px; font-weight: bold; }";
  html += ".input-group input, .input-group textarea { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 4px; font-size: 16px; }";
  html += ".input-group textarea { height: 80px; resize: vertical; }";
  html += ".logs { background: #f8f9fa; border: 1px solid #dee2e6; border-radius: 4px; padding: 15px; height: 200px; overflow-y: auto; font-family: monospace; font-size: 12px; }";
  html += ".sensor-data { display: grid; grid-template-columns: repeat(4, 1fr); gap: 10px; }";
  html += ".sensor-value { text-align: center; padding: 10px; background: #f8f9fa; border-radius: 4px; }";
  html += ".modal { display: none; position: fixed; z-index: 1000; left: 0; top: 0; width: 100%; height: 100%; background-color: rgba(0,0,0,0.5); }";
  html += ".modal-content { background-color: white; margin: 15% auto; padding: 20px; border-radius: 10px; width: 80%; max-width: 500px; }";
  html += ".close { color: #aaa; float: right; font-size: 28px; font-weight: bold; cursor: pointer; }";
  html += ".close:hover { color: black; }";
  html += "@media (max-width: 600px) { .container { padding: 10px; } .sensor-data { grid-template-columns: repeat(2, 1fr); } }";
  html += "</style></head><body>";
  
  server.send(200, "text/html", html + getBodyHTML());
}

String getBodyHTML() {
  String html = "<div class='container'>";
  html += "<div class='header'><h1>🛡️ Fall Detection System</h1><p>Real-time monitoring with CircuitDigest SMS alerts</p></div>";
  
  html += "<div class='card'><h2>System Status</h2><div class='status-grid'>";
  html += "<div id='systemStatus' class='status-item'>Loading...</div>";
  html += "<div id='smsStatus' class='status-item status-normal'>SMS: Not sent</div>";
  html += "<div id='phoneStatus' class='status-item status-normal'>Phone: Loading...</div>";
  html += "</div></div>";
  
  html += "<div class='card'><h2>Sensor Data</h2><div class='sensor-data'>";
  html += "<div class='sensor-value'><strong>X-Axis</strong><br><span id='accelX'>0.00</span> m/s²</div>";
  html += "<div class='sensor-value'><strong>Y-Axis</strong><br><span id='accelY'>0.00</span> m/s²</div>";
  html += "<div class='sensor-value'><strong>Z-Axis</strong><br><span id='accelZ'>0.00</span> m/s²</div>";
  html += "<div class='sensor-value'><strong>Magnitude</strong><br><span id='accelMag'>0.00</span> m/s²</div>";
  html += "</div></div>";
  
  html += "<div class='card'><h2>Controls</h2>";
  html += "<button id='armBtn' class='btn btn-success' onclick='toggleArm()'>Arm System</button>";
  html += "<button class='btn btn-warning' onclick='testSMS()'>Test SMS</button>";
  html += "<button class='btn btn-info' onclick='openCustomSMS()'>Send Custom SMS</button>";
  html += "<button class='btn btn-primary' onclick='refreshData()'>Refresh</button>";
  html += "<div class='input-group'><label for='phoneInput'>Phone Number:</label>";
  html += "<input type='tel' id='phoneInput' placeholder='+91xxxxxxxxxx' value=''>";
  html += "<button class='btn btn-primary' onclick='savePhone()' style='margin-top: 10px;'>Save Number</button>";
  html += "</div></div>";
  
  html += "<div class='card'><h2>System Logs</h2>";
  html += "<div id='logs' class='logs'>Loading logs...</div>";
  html += "<button class='btn btn-primary' onclick='refreshLogs()'>Refresh Logs</button>";
  html += "</div></div>";
  
  html += "<div id='customSMSModal' class='modal'>";
  html += "<div class='modal-content'>";
  html += "<span class='close' onclick='closeCustomSMS()'>×</span>";
  html += "<h2>Send Custom SMS</h2>";
  html += "<div class='input-group'>";
  html += "<label for='customMessage'>Message:</label>";
  html += "<textarea id='customMessage' placeholder='Enter your custom message here...'></textarea>";
  html += "</div>";
  html += "<button class='btn btn-primary' onclick='sendCustomSMS()'>Send SMS</button>";
  html += "<button class='btn btn-secondary' onclick='closeCustomSMS()'>Cancel</button>";
  html += "</div></div>";
  
  html += getJavaScript();
  html += "</body></html>";
  
  return html;
}

String getJavaScript() {
  String js = "<script>";
  js += "function updateStatus() {";
  js += "  fetch('/api/status').then(response => response.json()).then(data => {";
  js += "    document.getElementById('systemStatus').className = 'status-item ' + (data.armed ? 'status-armed' : 'status-disarmed');";
  js += "    document.getElementById('systemStatus').textContent = 'System: ' + (data.armed ? 'Armed' : 'Disarmed');";
  js += "    document.getElementById('smsStatus').textContent = 'SMS: ' + data.sms_status;";
  js += "    document.getElementById('phoneStatus').textContent = 'Phone: ' + data.phone;";
  js += "    document.getElementById('phoneInput').value = data.phone;";
  js += "    document.getElementById('accelX').textContent = data.accel_x.toFixed(2);";
  js += "    document.getElementById('accelY').textContent = data.accel_y.toFixed(2);";
  js += "    document.getElementById('accelZ').textContent = data.accel_z.toFixed(2);";
  js += "    document.getElementById('accelMag').textContent = data.accel_mag.toFixed(2);";
  js += "    document.getElementById('armBtn').textContent = data.armed ? 'Disarm System' : 'Arm System';";
  js += "    document.getElementById('armBtn').className = 'btn ' + (data.armed ? 'btn-danger' : 'btn-success');";
  js += "  });";
  js += "}";
  
  js += "function toggleArm() { fetch('/api/arm', {method: 'POST'}).then(() => updateStatus()); }";
  
  js += "function testSMS() {";
  js += "  fetch('/api/test', {method: 'POST'}).then(response => response.text()).then(result => {";
  js += "    alert(result); updateStatus();";
  js += "  });";
  js += "}";
  
  js += "function openCustomSMS() {";
  js += "  document.getElementById('customSMSModal').style.display = 'block';";
  js += "}";
  
  js += "function closeCustomSMS() {";
  js += "  document.getElementById('customSMSModal').style.display = 'none';";
  js += "}";
  
  js += "function sendCustomSMS() {";
  js += "  const message = document.getElementById('customMessage').value;";
  js += "  if (!message.trim()) { alert('Please enter a message'); return; }";
  js += "  fetch('/api/custom-sms', {";
  js += "    method: 'POST',";
  js += "    headers: {'Content-Type': 'application/x-www-form-urlencoded'}, ";
  js += "    body: 'message=' + encodeURIComponent(message)";
  js += "  }).then(response => response.text()).then(result => {";
  js += "    alert(result); updateStatus(); closeCustomSMS();";
  js += "    document.getElementById('customMessage').value = '';";
  js += "  });";
  js += "}";
  
  js += "function savePhone() {";
  js += "  const phone = document.getElementById('phoneInput').value;";
  js += "  if (!phone) { alert('Please enter a phone number'); return; }";
  js += "  fetch('/api/config', {";
  js += "    method: 'POST',";
  js += "    headers: {'Content-Type': 'application/x-www-form-urlencoded'}, ";
  js += "    body: 'phone=' + encodeURIComponent(phone)";
  js += "  }).then(response => response.text()).then(result => {";
  js += "    alert(result); updateStatus();";
  js += "  });";
  js += "}";
  
  js += "function refreshData() { updateStatus(); refreshLogs(); }";
  
  js += "function refreshLogs() {";
  js += "  fetch('/api/logs').then(response => response.text()).then(logs => {";
  js += "    document.getElementById('logs').textContent = logs;";
  js += "    document.getElementById('logs').scrollTop = document.getElementById('logs').scrollHeight;";
  js += "  });";
  js += "}";
  
  js += "setInterval(updateStatus, 3000);";
  js += "setInterval(refreshLogs, 8000);";
  js += "updateStatus(); refreshLogs();";
  js += "</script>";
  
  return js;
}
