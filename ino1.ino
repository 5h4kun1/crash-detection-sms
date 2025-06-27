#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <EEPROM.h>

// Create an ADXL345 object
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(12345);

// SIM900A on Serial2 (GPIO 16 = RX2, GPIO 17 = TX2)
#define SIM900A Serial2
#define BUZZER_PIN 13

// WiFi credentials for SoftAP
const char* ssid = "FallDetector";
const char* password = "fall123456";

// Web server on port 80
WebServer server(80);

// System variables
const float fallThreshold = 15.0;
bool alertSent = false;
bool systemArmed = true;
String phoneNumber = "+918555853408"; // Default number
String lastSMSStatus = "Not sent";
String systemLogs = "";
float currentAcceleration = 0.0;
float accelX = 0.0, accelY = 0.0, accelZ = 0.0;
unsigned long lastSensorRead = 0;
unsigned long lastLogUpdate = 0;

void setup() {
  Serial.begin(115200);  // ESP32 <-> Computer communication
  SIM900A.begin(9600);   // ESP32 <-> SIM900A communication
  EEPROM.begin(512);
  
  // Initialize buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  
  // Clear EEPROM on first boot (uncomment if needed to reset)
  // clearEEPROM();
  
  // Load saved phone number from EEPROM
  loadPhoneNumber();
  
  // Initialize accelerometer
  if (!accel.begin()) {
    addLog("ERROR: ADXL345 not detected!");
    while (1);
  }
  
  accel.setRange(ADXL345_RANGE_16_G);
  addLog("System initialized - ADXL345 ready");
  
  // Initialize SIM900A
  addLog("Initializing SIM900A...");
  initializeSIM900A();
  
  // Setup WiFi SoftAP
  WiFi.softAP(ssid, password);
  IPAddress IP = WiFi.softAPIP();
  addLog("WiFi AP started: " + String(ssid));
  addLog("IP address: " + IP.toString());
  
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
  sendSMS("Fall Detection System Online - IP: " + IP.toString());
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

void initializeSIM900A() {
  // Clear serial buffer
  while(SIM900A.available()) {
    SIM900A.read();
  }
  
  delay(5000); // Wait for SIM900A to start
  
  // Send AT command with retries
  for(int i = 0; i < 3; i++) {
    SIM900A.println("AT");
    addLog("Sent AT command, attempt " + String(i + 1));
    delay(2000);
    if (waitForResponse("OK", 3000)) {
      addLog("SIM900A responding to AT commands");
      break;
    }
  }
  
  // Disable echo
  SIM900A.println("ATE0");
  delay(1000);
  waitForResponse("OK", 3000);
  
  // Check signal quality
  SIM900A.println("AT+CSQ");
  delay(1000);
  String response = getResponse(3000);
  if(response.indexOf("+CSQ:") != -1) {
    addLog("Signal quality: " + response);
  }
  
  // Check network registration
  SIM900A.println("AT+CREG?");
  delay(1000);
  response = getResponse(3000);
  if(response.indexOf("+CREG:") != -1) {
    addLog("Network registration: " + response);
  }
  
  // Set SMS format to text mode
  SIM900A.println("AT+CMGF=1");
  delay(1000);
  if (waitForResponse("OK", 3000)) {
    addLog("SMS text mode enabled");
  }
  
  addLog("SIM900A initialization complete");
}

String getResponse(unsigned long timeout) {
  String response = "";
  unsigned long startTime = millis();
  
  while (millis() - startTime < timeout) {
    while (SIM900A.available()) {
      char c = SIM900A.read();
      response += c;
    }
    delay(10);
  }
  
  return response;
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
    
    String alertMsg = "⚠ FALL ALERT: Detected at " + getTimeString() + 
                     " | Acceleration: " + String(currentAcceleration, 2) + " m/s²";
    
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

bool sendSMS(String message) {
  addLog("Attempting to send SMS: " + message.substring(0, 30) + "...");
  
  // Reinitialize SIM900A to ensure stable state
  addLog("Reinitializing SIM900A before SMS...");
  initializeSIM900A();
  
  // Clear serial buffer
  while(SIM900A.available()) {
    SIM900A.read();
    addLog("Cleared serial buffer data");
  }
  
  // Test AT command with retries
  bool atSuccess = false;
  for (int i = 0; i < 3; i++) {
    SIM900A.println("AT");
    addLog("Sent AT command, attempt " + String(i + 1));
    delay(2000);
    String atResponse = getResponse(3000);
    addLog("AT response: " + atResponse);
    if (waitForResponse("OK", 3000)) {
      atSuccess = true;
      break;
    }
    addLog("Retrying AT command...");
    delay(1000);
  }
  if (!atSuccess) {
    addLog("ERROR: SIM900A not responding to AT");
    return false;
  }
  
  // Check network registration with retries
  bool networkRegistered = false;
  for (int i = 0; i < 3; i++) {
    SIM900A.println("AT+CREG?");
    delay(2000);
    String regResponse = getResponse(3000);
    addLog("Network status: " + regResponse);
    if (regResponse.indexOf("+CREG: 0,1") != -1 || regResponse.indexOf("+CREG: 0,5") != -1) {
      networkRegistered = true;
      break;
    }
    addLog("Not registered, retrying...");
    SIM900A.println("AT+COPS=0");
    delay(15000); // Wait longer for network registration
  }
  if (!networkRegistered) {
    addLog("ERROR: Failed to register to network");
    return false;
  }
  
  // Check signal strength
  SIM900A.println("AT+CSQ");
  delay(2000);
  String signalResponse = getResponse(3000);
  addLog("Signal strength: " + signalResponse);
  
  // Set SMS text mode
  SIM900A.println("AT+CMGF=1");
  delay(2000);
  String cmgfResponse = getResponse(3000);
  addLog("CMGF response: " + cmgfResponse);
  if (!waitForResponse("OK", 3000)) {
    addLog("ERROR: Failed to set SMS text mode");
    return false;
  }
  
  // Set character set
  SIM900A.println("AT+CSCS=\"GSM\"");
  delay(2000);
  String cscsResponse = getResponse(3000);
  addLog("CSCS response: " + cscsResponse);
  if (!waitForResponse("OK", 3000)) {
    addLog("ERROR: Failed to set character set");
    return false;
  }
  
  // Send SMS command with retries
  bool smsPromptReceived = false;
  for (int i = 0; i < 2; i++) {
    addLog("Sending SMS to: " + phoneNumber);
    SIM900A.println("AT+CMGS=\"" + phoneNumber + "\"");
    delay(3000);
    String cmgsResponse = getResponse(10000);
    addLog("CMGS response: " + cmgsResponse);
    if (waitForResponse(">", 10000)) {
      smsPromptReceived = true;
      break;
    }
    addLog("ERROR: Failed to get SMS prompt (>), retrying...");
    SIM900A.println((char)27); // Send ESC to cancel
    delay(2000);
  }
  if (!smsPromptReceived) {
    addLog("ERROR: Failed to get SMS prompt after retries");
    return false;
  }
  
  // Send message content and Ctrl+Z
  SIM900A.print(message);
  delay(1000); // Increased delay before Ctrl+Z
  SIM900A.write(26); // Send Ctrl+Z (ASCII 26)
  addLog("Sent message and Ctrl+Z");
  delay(2000); // Increased delay after Ctrl+Z
  
  // Wait for send confirmation
  String sendResponse = getResponse(30000);
  addLog("SMS Response: " + sendResponse);
  
  if (sendResponse.indexOf("+CMGS:") != -1 || sendResponse.indexOf("OK") != -1) {
    addLog("SMS sent successfully");
    return true;
  } else {
    addLog("ERROR: SMS send failed - " + sendResponse);
    return false;
  }
}

bool waitForResponse(String expectedResponse, unsigned long timeout) {
  String response = "";
  unsigned long startTime = millis();
  
  // Clear buffer before reading
  while (SIM900A.available()) {
    SIM900A.read();
  }
  
  while (millis() - startTime < timeout) {
    while (SIM900A.available()) {
      char c = SIM900A.read();
      response += c;
    }
    
    if (response.indexOf(expectedResponse) != -1) {
      addLog("Received expected response: " + response);
      return true;
    }
    
    if (response.indexOf("ERROR") != -1) {
      addLog("SIM900A Error: " + response);
      return false;
    }
    
    delay(100);
  }
  
  addLog("Timeout waiting for: " + expectedResponse + ", got: " + response);
  return false;
}

// EEPROM phone number management
void loadPhoneNumber() {
  char storedPhone[20];
  for (int i = 0; i < 20; i++) {
    storedPhone[i] = EEPROM.read(i);
    Serial.print("EEPROM["); Serial.print(i); Serial.print("]="); Serial.println(storedPhone[i], HEX);
    if (storedPhone[i] == '\0') break;
  }
  storedPhone[19] = '\0'; // Ensure null termination
  
  if (isValidPhoneNumber(String(storedPhone))) {
    phoneNumber = String(storedPhone);
    addLog("Loaded phone number: " + phoneNumber);
  } else {
    phoneNumber = "+918574339999"; // Fallback to default
    addLog("Invalid phone number in EEPROM, using default: " + phoneNumber);
    savePhoneNumber(); // Save default to EEPROM
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
  if (phone.length() < 6 || phone.length() > 15) return false;
  if (!phone.startsWith("+")) return false;
  for (int i = 1; i < phone.length(); i++) {
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
  
  // Keep last 2000 characters
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
  String testMsg = "🧪 TEST MESSAGE: Fall detection system is working. Time: " + getTimeString();
  
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
    if (sendSMS(customMsg)) {
      lastSMSStatus = "Custom message sent at " + getTimeString();
      server.send(200, "text/plain", "Custom SMS sent successfully!");
    } else {
      lastSMSStatus = "Custom message failed at " + getTimeString();
      server.send(500, "text/plain", "Failed to send custom SMS");
    }
  } else {
    server.send(400, "text/plain", "Missing message parameter");
  }
}

void handleArm() {
  systemArmed = !systemArmed;
  String status = systemArmed ? "Armed" : "Disarmed";
  addLog("System " + status + " via web interface");
  
  // Reset alert flag when arming/disarming
  alertSent = false;
  
  server.send(200, "text/plain", "System " + status);
}

void handleLogs() {
  server.send(200, "text/plain", systemLogs);
}

void handleRoot() {
  // Build HTML in parts to avoid string length issues
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Fall Detection System</title><style>";
  
  // CSS Styles
  html += "* { margin: 0; padding: 0; box-sizing: border-box; }";
  html += "body { font-family: Arial, sans-serif; background: #f0f2f5; color: #333; }";
  html += ".container { max-width: 800px; margin: 0 auto; padding: 20px; }";
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
  html += "<div class='header'><h1>🛡️ Fall Detection System</h1><p>Real-time monitoring and alert system</p></div>";
  
  // Status Section
  html += "<div class='card'><h2>System Status</h2><div class='status-grid'>";
  html += "<div id='systemStatus' class='status-item'>Loading...</div>";
  html += "<div id='smsStatus' class='status-item status-normal'>SMS: Not sent</div>";
  html += "<div id='phoneStatus' class='status-item status-normal'>Phone: Loading...</div>";
  html += "</div></div>";
  
  // Sensor Data Section
  html += "<div class='card'><h2>Sensor Data</h2><div class='sensor-data'>";
  html += "<div class='sensor-value'><strong>X-Axis</strong><br><span id='accelX'>0.00</span> m/s²</div>";
  html += "<div class='sensor-value'><strong>Y-Axis</strong><br><span id='accelY'>0.00</span> m/s²</div>";
  html += "<div class='sensor-value'><strong>Z-Axis</strong><br><span id='accelZ'>0.00</span> m/s²</div>";
  html += "<div class='sensor-value'><strong>Magnitude</strong><br><span id='accelMag'>0.00</span> m/s²</div>";
  html += "</div></div>";
  
  // Controls Section
  html += "<div class='card'><h2>Controls</h2>";
  html += "<button id='armBtn' class='btn btn-success' onclick='toggleArm()'>Arm System</button>";
  html += "<button class='btn btn-warning' onclick='testSMS()'>Test SMS</button>";
  html += "<button class='btn btn-info' onclick='openCustomSMS()'>Send Custom SMS</button>";
  html += "<button class='btn btn-primary' onclick='refreshData()'>Refresh</button>";
  html += "<div class='input-group'><label for='phoneInput'>Phone Number:</label>";
  html += "<input type='tel' id='phoneInput' placeholder='+1234567890' value=''>";
  html += "<button class='btn btn-primary' onclick='savePhone()' style='margin-top: 10px;'>Save Number</button>";
  html += "</div></div>";
  
  // Logs Section
  html += "<div class='card'><h2>System Logs</h2>";
  html += "<div id='logs' class='logs'>Loading logs...</div>";
  html += "<button class='btn btn-primary' onclick='refreshLogs()'>Refresh Logs</button>";
  html += "</div></div>";
  
  // Custom SMS Modal
  html += "<div id='customSMSModal' class='modal'>";
  html += "<div class='modal-content'>";
  html += "<span class='close' onclick='closeCustomSMS()'>&times;</span>";
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
  js += "  console.log('Sending custom SMS:', message);"; // Log for debugging
  js += "  if (!message.trim()) { alert('Please enter a message'); return; }";
  js += "  fetch('/api/custom-sms', {";
  js += "    method: 'POST',";
  js += "    headers: {'Content-Type': 'application/x-www-form-urlencoded'},";
  js += "    body: 'message=' + encodeURIComponent(message)";
  js += "  }).then(response => response.text()).then(result => {";
  js += "    alert(result); updateStatus(); closeCustomSMS();";
  js += "    document.getElementById('customMessage').value = '';";
  js += "  });";
  js += "}";
  
  js += "function savePhone() {";
  js += "  const phone = document.getElementById('phoneInput').value;";
  js += "  console.log('Saving phone:', phone);"; // Log for debugging
  js += "  if (!phone) { alert('Please enter a phone number'); return; }";
  js += "  fetch('/api/config', {";
  js += "    method: 'POST',";
  js += "    headers: {'Content-Type': 'application/x-www-form-urlencoded'},";
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
