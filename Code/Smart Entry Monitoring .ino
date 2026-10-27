
#include <WiFiS3.h>
#include <Arduino_LED_Matrix.h>
#include <Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ============== WIFI SETTINGS ====================
// Change these to your existing router credentials.
const char WIFI_SSID[] = "Yusuf";
const char WIFI_PASSWORD[] = "11110000";

WiFiServer server(80);
bool serverStarted = false;

// ============== PIN CONNECTIONS ==================
const byte SENSOR_A = 2;
const byte SENSOR_B = 3;
const byte GREEN_LED = 5;
const byte RED_LED = 6;
const byte BUZZER = 7;
const byte SERVO_PIN = 9;

// ============== SETTINGS ==========================
const int MAX_OCCUPANCY = 50;

const unsigned long DEBOUNCE_MS = 40;
const unsigned long SEQUENCE_TIMEOUT_MS = 1200;
const unsigned long DOOR_OPEN_MS = 1500;
const unsigned long BEEP_MS = 120;
const unsigned long BEEP_GAP_MS = 100;

const byte SENSOR_ACTIVE = LOW;

// ============== HARDWARE ==========================
ArduinoLEDMatrix matrix;
Servo doorServo;

// 0.96-inch I2C OLED, 128x64. UNO R4 WiFi hardware I2C pins are fixed:
// SDA = A4 / SDA header, SCL = A5 / SCL header. Wire cannot be remapped to arbitrary pins.
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C
Adafruit_SSD1306 oled(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
bool oledReady = false;
unsigned long lastOledUpdate = 0;

// Explicit prototypes prevent Arduino IDE auto-prototype issues.
void updateOLED(bool force = false);
void updateMatrix();
void updateSensors();
void updateDoor();
void updateBuzzer();
bool connectWiFi();
void handleDashboard();

// ============== OCCUPANCY =========================
int occupancy = 0;
unsigned long totalEntries = 0;
unsigned long totalExits = 0;

// ============== DIRECTION =========================
enum DirectionState {
  IDLE,
  ENTRY_A_FIRST,
  EXIT_B_FIRST
};

DirectionState directionState = IDLE;
unsigned long sequenceTime = 0;

// ============== SENSOR DEBOUNCE ==================
bool stableA = false;
bool stableB = false;
bool candidateA = false;
bool candidateB = false;

unsigned long changeTimeA = 0;
unsigned long changeTimeB = 0;

// ============== SERVO ==============================
bool doorOpen = false;
unsigned long doorTime = 0;

// ============== BUZZER =============================
enum BeepState {
  BEEP_IDLE,
  BEEP_ON,
  BEEP_GAP
};

BeepState beepState = BEEP_IDLE;
byte beepsRemaining = 0;
unsigned long beepTime = 0;

// ============== LED MATRIX =========================
uint32_t matrixFrame[3] = {0, 0, 0};

const uint8_t DIGITS[10][8] = {
  {0b011110,0b110011,0b110111,0b111011,
   0b110011,0b110011,0b011110,0},
  {0b001100,0b011100,0b001100,0b001100,
   0b001100,0b001100,0b111111,0},
  {0b011110,0b110011,0b000011,0b001110,
   0b011000,0b110000,0b111111,0},
  {0b111110,0b000011,0b000011,0b011110,
   0b000011,0b000011,0b111110,0},
  {0b000110,0b001110,0b011110,0b110110,
   0b111111,0b000110,0b000110,0},
  {0b111111,0b110000,0b110000,0b111110,
   0b000011,0b000011,0b111110,0},
  {0b011110,0b110000,0b110000,0b111110,
   0b110011,0b110011,0b011110,0},
  {0b111111,0b000011,0b000110,0b001100,
   0b001100,0b001100,0b001100,0},
  {0b011110,0b110011,0b110011,0b011110,
   0b110011,0b110011,0b011110,0},
  {0b011110,0b110011,0b110011,0b011111,
   0b000011,0b000011,0b011110,0}
};

void drawDigit(byte digit, byte offset) {
  if (digit > 9) return;

  for (byte row = 0; row < 8; row++) {
    for (byte col = 0; col < 6; col++) {
      if (DIGITS[digit][row] & (1 << (5 - col))) {
        byte bit = row * 12 + offset + col;

        matrixFrame[bit / 32] |=
          (uint32_t)1 << (31 - (bit % 32));
      }
    }
  }
}

void updateMatrix() {
  matrixFrame[0] = 0;
  matrixFrame[1] = 0;
  matrixFrame[2] = 0;

  drawDigit((occupancy / 10) % 10, 0);
  drawDigit(occupancy % 10, 6);

  matrix.loadFrame(matrixFrame);
}

// ============== BUZZER =============================
void startBeeps(byte count) {
  if (beepState != BEEP_IDLE) return;

  beepsRemaining = count;
  digitalWrite(BUZZER, HIGH);
  beepState = BEEP_ON;
  beepTime = millis();
}

void updateBuzzer() {
  unsigned long now = millis();

  if (beepState == BEEP_ON &&
      now - beepTime >= BEEP_MS) {
    digitalWrite(BUZZER, LOW);
    beepsRemaining--;

    if (beepsRemaining == 0) {
      beepState = BEEP_IDLE;
    } else {
      beepState = BEEP_GAP;
      beepTime = now;
    }
  } else if (beepState == BEEP_GAP &&
             now - beepTime >= BEEP_GAP_MS) {
    digitalWrite(BUZZER, HIGH);
    beepState = BEEP_ON;
    beepTime = now;
  }
}

// ============== SERVO ===============================
void openDoor() {
  doorServo.write(90);
  doorOpen = true;
  doorTime = millis();
}

void updateDoor() {
  if (doorOpen &&
      millis() - doorTime >= DOOR_OPEN_MS) {
    doorServo.write(0);
    doorOpen = false;
  }
}

// ============== ENTRY / EXIT ========================
void entryDetected() {
  if (occupancy >= MAX_OCCUPANCY) {
    Serial.println("CAPACITY FULL");
    startBeeps(3);
    return;
  }

  occupancy++;
  totalEntries++;

  Serial.print("ENTRY: ");
  Serial.println(occupancy);

  startBeeps(1);
  openDoor();
  updateMatrix();
  updateOLED(true);
}

void exitDetected() {
  if (occupancy > 0) {
    occupancy--;
    totalExits++;
  }

  Serial.print("EXIT: ");
  Serial.println(occupancy);

  startBeeps(2);
  openDoor();
  updateMatrix();
  updateOLED(true);
}

// ============== SENSOR PROCESSING ==================
bool readSensor(byte pin) {
  return digitalRead(pin) == SENSOR_ACTIVE;
}

void updateSensors() {
  unsigned long now = millis();

  bool rawA = readSensor(SENSOR_A);
  bool rawB = readSensor(SENSOR_B);

  bool eventA = false;
  bool eventB = false;

  if (rawA != candidateA) {
    candidateA = rawA;
    changeTimeA = now;
  }

  if (rawB != candidateB) {
    candidateB = rawB;
    changeTimeB = now;
  }

  if (now - changeTimeA >= DEBOUNCE_MS &&
      stableA != candidateA) {
    stableA = candidateA;
    if (stableA) eventA = true;
  }

  if (now - changeTimeB >= DEBOUNCE_MS &&
      stableB != candidateB) {
    stableB = candidateB;
    if (stableB) eventB = true;
  }

  if (directionState != IDLE &&
      now - sequenceTime > SEQUENCE_TIMEOUT_MS) {
    directionState = IDLE;
  }

  if (eventA && eventB) {
    directionState = IDLE;
    return;
  }

  if (eventA) {
    if (directionState == EXIT_B_FIRST) {
      exitDetected();
      directionState = IDLE;
    } else {
      directionState = ENTRY_A_FIRST;
      sequenceTime = now;
    }
  }

  if (eventB) {
    if (directionState == ENTRY_A_FIRST) {
      entryDetected();
      directionState = IDLE;
    } else {
      directionState = EXIT_B_FIRST;
      sequenceTime = now;
    }
  }
}

// ============== DASHBOARD HTML ======================
const char DASHBOARD_HTML[] = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#101827">
<title>Smart Entry System| Control Center</title>
<style>
:root{color-scheme:dark;--bg:#0b1120;--panel:#131e31;--line:#293850;--muted:#9aabc3;--text:#f4f7ff;--green:#42dfa0;--red:#ff6f7d;--blue:#7da8ff}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(ellipse at top,#172746 0,#0b1120 58%);color:var(--text);font:15px/1.5 system-ui,-apple-system,Segoe UI,Arial,sans-serif;min-height:100vh;padding:22px}
main{max-width:1050px;margin:auto}.top{display:flex;justify-content:space-between;align-items:center;gap:16px;margin:8px 0 26px}.brand{display:flex;align-items:center;gap:13px}.logo{width:48px;height:48px;border-radius:15px;display:grid;place-items:center;background:#22365a;font-size:24px;border:1px solid #40577b}.eyebrow{font-size:11px;letter-spacing:2px;text-transform:uppercase;color:var(--blue);font-weight:800}.title{font-size:clamp(25px,4vw,35px);font-weight:800;letter-spacing:-1px;margin:2px 0}.sub{color:var(--muted);font-size:13px}.pill{display:flex;align-items:center;gap:8px;border:1px solid var(--line);background:#101b2d;padding:9px 13px;border-radius:99px;color:var(--muted);white-space:nowrap}.dot{width:8px;height:8px;border-radius:50%;background:#f5bd4f}.dot.online{background:var(--green);box-shadow:0 0 12px #42dfa066}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.card{background:linear-gradient(145deg,#17243a,#111b2c);border:1px solid var(--line);border-radius:20px;padding:22px;min-width:0;box-shadow:0 12px 35px #00000012}.hero{grid-column:span 2}.cardhead{display:flex;justify-content:space-between;align-items:center;gap:12px;color:var(--muted);font-size:13px;font-weight:650}.icon{width:34px;height:34px;display:grid;place-items:center;border-radius:11px;background:#243652;font-size:17px}.big{font-size:clamp(35px,6vw,52px);font-weight:800;letter-spacing:-1.8px;line-height:1.2;margin:15px 0 5px}.unit{font-size:17px;color:var(--muted);letter-spacing:0}.hint{color:var(--muted);font-size:13px}.bar{height:11px;border-radius:20px;background:#293850;overflow:hidden;margin:21px 0 9px}.fill{height:100%;width:0;background:linear-gradient(90deg,#32c98e,#74edb8);border-radius:20px;transition:width .3s}.barlabels{display:flex;justify-content:space-between;color:var(--muted);font-size:12px}.mini-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px;margin-top:17px}.mini{padding:14px;background:#0d1728;border:1px solid #263750;border-radius:14px}.mini strong{display:block;font-size:24px;margin-top:3px}.status{font-weight:800;color:var(--green)}.status.full{color:var(--red)}.gate{font-size:27px;font-weight:800;margin:15px 0 4px}.gate.open{color:var(--green)}.gate.closed{color:var(--muted)}.footer{display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap;color:var(--muted);font-size:12px;margin:22px 4px 5px}.connection{color:var(--green);font-weight:750}@media(max-width:600px){body{padding:14px}.top{align-items:flex-start;flex-direction:column}.grid{gap:11px}.card{padding:17px;border-radius:16px}.hero{grid-column:span 2}.mini-grid{grid-template-columns:1fr}.big{font-size:37px}}
</style>
</head>
<body><main>
<header class="top">
 <div class="brand"><div class="logo">⌁</div><div><div class="eyebrow">Live monitoring</div><div class="title">Smart Entry</div><div class="sub">Occupancy & gate dashboard</div></div></div>
 <div class="pill"><span class="dot" id="dot"></span><span id="connection">Connecting to device</span></div>
</header>
<div class="grid">
 <section class="card hero">
  <div class="cardhead"><span>CURRENT OCCUPANCY</span><span class="icon">♙</span></div>
  <div class="big"><span id="occ">--</span><span class="unit"> / <span id="cap">50</span> people</span></div>
  <div class="hint" id="available">Waiting for live data…</div>
  <div class="bar"><div class="fill" id="fill"></div></div>
  <div class="barlabels"><span>0 people</span><span id="percent">0% capacity</span></div>
  <div class="mini-grid">
   <div class="mini"><div class="hint">Total entries</div><strong id="entries">--</strong></div>
   <div class="mini"><div class="hint">Total exits</div><strong id="exits">--</strong></div>
   <div class="mini"><div class="hint">Capacity status</div><strong class="status" id="status">--</strong></div>
  </div>
 </section>
 <section class="card">
  <div class="cardhead"><span>GATE STATUS</span><span class="icon">↔</span></div>
  <div class="gate closed" id="gate">Checking…</div><div class="hint">Servo-controlled entry gate</div>
  <div class="bar"><div class="fill" id="gateFill" style="width:0"></div></div>
  <div class="hint" id="gateHint">Gate closes automatically after opening.</div>
 </section>
 <section class="card">
  <div class="cardhead"><span>DEVICE CONNECTION</span><span class="icon">⌁</span></div>
  <div class="gate" id="networkState">Connecting…</div>
  <div class="hint">Dashboard is hosted directly by your Arduino UNO R4 WiFi on your local network.</div>
  <div class="bar"><div class="fill" id="networkFill" style="width:20%"></div></div>
  <div class="hint" id="updated">Waiting for first update</div>
 </section>
</div>
<footer class="footer"><span>SMART ENTRY SYSTEM · UNO R4 WiFi</span><span>Live refresh every second · No cloud required</span></footer>
</main>
<script>
async function refreshData(){
 try{
  const r=await fetch('/api',{cache:'no-store'});if(!r.ok)throw new Error('HTTP '+r.status);
  const d=await r.json();const occ=Number(d.occupancy)||0;const cap=Math.max(1,Number(d.capacity)||50);const pct=Math.min(100,Math.round(100*occ/cap));const full=occ>=cap;
  document.getElementById('occ').textContent=occ;document.getElementById('cap').textContent=cap;
  document.getElementById('entries').textContent=d.entries;document.getElementById('exits').textContent=d.exits;
  document.getElementById('available').textContent=Math.max(0,cap-occ)+' spaces available';
  document.getElementById('fill').style.width=pct+'%';document.getElementById('percent').textContent=pct+'% capacity';
  const st=document.getElementById('status');st.textContent=full?'FULL':'AVAILABLE';st.className='status'+(full?' full':'');
  const gate=document.getElementById('gate');gate.textContent=d.gateOpen?'OPEN':'CLOSED';gate.className='gate '+(d.gateOpen?'open':'closed');
  document.getElementById('gateFill').style.width=d.gateOpen?'100%':'0%';
  document.getElementById('gateHint').textContent=d.gateOpen?'Gate is currently open.':'Gate closes automatically after opening.';
  document.getElementById('connection').textContent='DEVICE ONLINE';document.getElementById('dot').className='dot online';
  document.getElementById('networkState').textContent='Connected';document.getElementById('networkFill').style.width='100%';
  document.getElementById('updated').textContent='Last updated '+new Date().toLocaleTimeString();
 }catch(e){
  document.getElementById('connection').textContent='RECONNECTING';document.getElementById('dot').className='dot';
  document.getElementById('networkState').textContent='Offline';document.getElementById('networkFill').style.width='15%';
  document.getElementById('updated').textContent='Could not reach Arduino. Retrying…';
 }
}
refreshData();setInterval(refreshData,1000);
</script></body></html>
)HTML";

// ============== HTTP RESPONSE HELPERS ==============
void sendHeaders(WiFiClient &client, const char *contentType) {
  client.println("HTTP/1.1 200 OK");
  client.print("Content-Type: ");
  client.println(contentType);
  client.println("Cache-Control: no-store");
  client.println("Connection: close");
  client.println();
}

void sendJSON(WiFiClient &client) {
  char json[220];

  snprintf(
    json, sizeof(json),
    "{\"occupancy\":%d,\"capacity\":%d,"
    "\"entries\":%lu,\"exits\":%lu,\"gateOpen\":%s}",
    occupancy,
    MAX_OCCUPANCY,
    totalEntries,
    totalExits,
    doorOpen ? "true" : "false"
  );

  sendHeaders(client, "application/json");
  client.print(json);
}

void sendDashboard(WiFiClient &client) {
  sendHeaders(client, "text/html");
  client.print(DASHBOARD_HTML);
}

void handleDashboard() {
  WiFiClient client = server.available();
  if (!client) return;

  client.setTimeout(500);

  unsigned long start = millis();

  while (!client.available() &&
         client.connected() &&
         millis() - start < 500) {
    updateSensors();
    updateDoor();
    updateBuzzer();
  }

  if (!client.available()) {
    client.stop();
    return;
  }

  String requestLine = client.readStringUntil('\n');
  requestLine.trim();

  start = millis();

  while (client.connected() && millis() - start < 500) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      if (line == "\r" || line.length() == 0) break;
    }
  }

  if (requestLine.startsWith("GET /api ")) {
    sendJSON(client);
  } else if (requestLine.startsWith("GET / ") ||
             requestLine.startsWith("GET /index.html ")) {
    sendDashboard(client);
  } else {
    client.println("HTTP/1.1 404 Not Found");
    client.println("Connection: close");
    client.println();
  }

  client.flush();
  client.stop();
}

// ============== OLED STATUS DISPLAY =================
void updateOLED(bool force) {
  if (!oledReady) return;
  if (!force && millis() - lastOledUpdate < 500) return;
  lastOledUpdate = millis();

  oled.clearDisplay();
  oled.setTextColor(SSD1306_WHITE);
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println(F("SMART ENTRY SYSTEM"));
  oled.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  oled.setCursor(0, 14);
  oled.print(F("People: "));
  oled.print(occupancy);
  oled.print(F(" / "));
  oled.println(MAX_OCCUPANCY);

  oled.setCursor(0, 25);
  oled.print(F("In: "));
  oled.print(totalEntries);
  oled.print(F("  Out: "));
  oled.println(totalExits);

  oled.setCursor(0, 36);
  oled.print(F("Gate: "));
  oled.println(doorOpen ? F("OPEN") : F("CLOSED"));

  oled.setCursor(0, 47);
  if (occupancy >= MAX_OCCUPANCY) oled.println(F("STATUS: FULL"));
  else oled.println(F("STATUS: AVAILABLE"));

  oled.setCursor(0, 57);
  if (WiFi.status() == WL_CONNECTED) {
    oled.print(F("WiFi OK "));
    oled.print(WiFi.localIP());
  } else {
    oled.print(F("WiFi: Connecting..."));
  }
  oled.display();
}

// ============== WIFI CONNECTION ====================
bool connectWiFi() {
  Serial.println("Connecting to Wi-Fi router...");

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Wi-Fi module not detected.");
    return false;
  }


  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();

  while (WiFi.status() != WL_CONNECTED &&
         millis() - start < 20000) {
    updateSensors();
    updateDoor();
    updateBuzzer();
    delay(10);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi connection failed.");
    return false;
  }

  Serial.println("Wi-Fi connected!");
  Serial.print("SSID: ");
  Serial.println(WIFI_SSID);
  Serial.print("Dashboard IP: http://");
  Serial.println(WiFi.localIP());

  if (!serverStarted) {
    server.begin();
    serverStarted = true;
  }

  return true;
}

// ============== SETUP ==============================
void setup() {
  Serial.begin(115200);

  pinMode(SENSOR_A, INPUT_PULLUP);
  pinMode(SENSOR_B, INPUT_PULLUP);
  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(BUZZER, LOW);

  doorServo.attach(SERVO_PIN);
  doorServo.write(0);

  matrix.begin();
  updateMatrix();

  // UNO R4 WiFi uses its fixed hardware I2C pins (A4=SDA, A5=SCL).
  Wire.begin();
  Wire.setClock(100000);

  // Scan I2C bus to help diagnose wiring/address problems.
  Serial.println(F("Scanning I2C bus (SDA=A4, SCL=A5)..."));
  bool foundOLEDAddress = false;
  for (byte address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();
    if (error == 0) {
      Serial.print(F("I2C device found at 0x"));
      if (address < 16) Serial.print('0');
      Serial.println(address, HEX);
      if (address == 0x3C || address == 0x3D) foundOLEDAddress = true;
    }
  }

  oledReady = oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS);
  if (!oledReady && foundOLEDAddress) {
    // Some SSD1306 OLED modules use address 0x3D instead of 0x3C.
    oledReady = oled.begin(SSD1306_SWITCHCAPVCC, 0x3D);
  }
  if (oledReady) {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE);
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println(F("SMART ENTRY SYSTEM"));
    oled.println(F("Starting WiFi..."));
    oled.display();
  } else {
    Serial.println(F("OLED not detected. Check SDA=A4, SCL=A5, VCC/GND and address 0x3C/0x3D."));
  }

  stableA = candidateA = readSensor(SENSOR_A);
  stableB = candidateB = readSensor(SENSOR_B);
  changeTimeA = changeTimeB = millis();

  connectWiFi();
  updateOLED(true);

  Serial.println("Offline Occupancy System Ready");
}

// ============== MAIN LOOP ==========================
void loop() {
  updateSensors();
  updateDoor();
  updateBuzzer();
  updateOLED();

  bool full = occupancy >= MAX_OCCUPANCY;
  digitalWrite(GREEN_LED, full ? LOW : HIGH);
  digitalWrite(RED_LED, full ? HIGH : LOW);

  static unsigned long lastReconnectAttempt = 0;

  if (WiFi.status() == WL_CONNECTED) {
    handleDashboard();
  } else if (millis() - lastReconnectAttempt >= 10000) {
    lastReconnectAttempt = millis();
    connectWiFi();
  }
}