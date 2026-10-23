/*
  Joystick-controlled 2.5-DoF game gantry for MKS Base v1.x (ATmega2560)
  Arduino IDE board: "Arduino Mega or Mega 2560", processor ATmega2560

  Joystick wiring:
    GND  -> XS3 GND        +5V -> XS3 5V
    VRx  -> XS3 A11        VRy -> XS3 A12
    SW   -> D19 (S pin on the Z+ endstop header)

  Motors:
    X1 -> X_MOT     X2 -> E0_MOT (driven in sync with X1)
    Y  -> Y_MOT     Z  -> Z_MOT
    Rotating plate -> E1_MOT

  Electromagnet:
    Magnet + / -    -> E0 heater screw terminal (D10 MOSFET output)
    NOTE: the magnet runs at your supply voltage (12V or 24V) - match it.
          Put a flyback diode (1N4007/1N5819) across the magnet:
          stripe (cathode) to the + wire.

  Controls:
    Stick           -> moves X and Y (speed follows how far you push)
    Press stick in once   -> Z goes DOWN, pauses, then comes back UP by itself
    Press stick in twice  -> electromagnet ON / OFF (toggle)
                             (two presses within DOUBLE_CLICK_MS)
    Hold stick in         -> rotating plate game starts:
                               Round 1: PLATE_SPEED    for PLATE_RUN_MS    (1 min)
                               Break:   stopped        for PLATE_BREAK_MS  (3 s)
                               Round 2: PLATE_SPEED_R2 for PLATE_RUN_R2_MS (45 s),
                                        turning the OPPOSITE way
                               Break:   stopped        for PLATE_BREAK_MS  (3 s)
                               Round 3: PLATE_RUN_R3_MS (1 min) split into a
                                        RANDOM 2-4 equal parts. Every part
                                        reverses direction and runs at a RANDOM
                                        speed between PLATE_SPEED and
                                        PLATE_SPEED_R2 x 1.2.
                                        New random values every game.
                             then stops by itself. Hold again to stop early.
                             (held for LONG_PRESS_MS)

  Power on with the stick centred (it calibrates centre at boot)
  and with Z in its UP position.
*/

// ---------- Pins (MKS Base uses the RAMPS 1.4 pin mapping) ----------
const uint8_t X_STEP  = 54, X_DIR  = 55, X_EN  = 38;
const uint8_t X2_STEP = 26, X2_DIR = 28, X2_EN = 24;   // E0 driver slot
const uint8_t Y_STEP  = 60, Y_DIR  = 61, Y_EN  = 56;
const uint8_t Z_STEP  = 46, Z_DIR  = 48, Z_EN  = 62;
const uint8_t E1_STEP = 36, E1_DIR = 34, E1_EN = 30;   // rotating plate
const uint8_t JOY_X = A11, JOY_Y = A12, JOY_SW = 19;
const uint8_t MAGNET_PIN = 10;    // E0 heater output (MOSFET, switches supply voltage)

// ---------- Tuning ----------
const bool  INVERT_X  = false;
const bool  INVERT_X2 = true;     // other rail usually faces the opposite way
const bool  INVERT_Y  = false;
const bool  INVERT_Z  = false;
const float MAX_SPEED_XY = 4000;  // steps/s at full stick (80 steps/mm -> 50 mm/s)
const float ACCEL_XY     = 8000;  // steps/s^2, prevents stalls on sudden stick moves
const float Z_SPEED      = 1600;  // steps/s
const long  Z_TRAVEL     = 4000;  // steps Z goes down (10 mm at 400 steps/mm)
const unsigned long Z_PAUSE_MS = 300;  // time Z waits at the bottom
const bool  LOCK_XY_DURING_DIP = true; // X/Y can't move while Z is going down/up
const int   DEADZONE     = 60;    // ADC counts around centre (range 0-1023)
const unsigned long JOY_PERIOD_MS = 20;   // how often the stick is read
const unsigned long DEBOUNCE_MS   = 40;
const unsigned long DOUBLE_CLICK_MS = 350; // max gap between the two presses of a double click
const unsigned long LONG_PRESS_MS   = 800; // hold this long to start/stop the plate
const bool  INVERT_PLATE = false;          // flip to change rotation direction
const float PLATE_SPEED  = 800;            // steps/s (3200 steps/rev -> 1 turn every 4 s)
const float PLATE_ACCEL  = 1600;           // steps/s^2, smooth start/stop so it doesn't stall
const unsigned long PLATE_RUN_MS = 60000;  // round 1 length: 60 000 ms = 1 minute
const float PLATE_SPEED_R2 = 1600;         // round 2 speed, steps/s (2x faster: 1 turn every 2 s)
const unsigned long PLATE_RUN_R2_MS = 45000; // round 2 length: 45 000 ms = 45 seconds
const unsigned long PLATE_RUN_R3_MS = 60000; // round 3 length: 60 000 ms = 1 minute
const unsigned long PLATE_BREAK_MS  = 3000;  // stop between rounds: 3 000 ms = 3 seconds
const uint8_t R3_MIN_PARTS = 2;              // round 3 random direction changes: min
const uint8_t R3_MAX_PARTS = 4;              //                                    max
const float   R3_MAX_SPEED_FACTOR = 1.2;     // round 3 top speed = PLATE_SPEED_R2 x this
#define DEBUG 0                   // 1 = print stick values (slows stepping)

// ---------- State ----------
enum ZState { Z_IDLE, Z_GOING_DOWN, Z_PAUSED, Z_GOING_UP };
ZState zState = Z_IDLE;
unsigned long zPauseStart = 0;

float speedX = 0, speedY = 0;     // current speeds, steps/s (signed)
unsigned long lastStepX = 0, lastStepY = 0, lastStepZ = 0, lastJoy = 0;
int  centerX = 512, centerY = 512;
long zPos = 0, zTarget = 0;       // 0 = UP, Z_TRAVEL = DOWN
bool lastBtn = HIGH;
unsigned long lastBtnChange = 0;
bool magnetOn = false;
uint8_t clickCount = 0;           // short presses waiting to be classified as single/double
unsigned long lastReleaseTime = 0;
unsigned long pressStart = 0;
bool longFired = false;           // this press already counted as a long press
enum PlateState { PLATE_OFF, PLATE_R1, PLATE_BREAK1, PLATE_R2, PLATE_BREAK2, PLATE_R3 };
PlateState plateState = PLATE_OFF;
uint8_t r3Parts = 0;              // this game's random number of round-3 parts (2-4)
float   r3Speed[R3_MAX_PARTS];    // this game's random speed for each part
uint8_t r3Part = 0;               // which round-3 part is running now
bool plateWantReverse = false;    // direction the current round wants
bool plateIsReverse   = false;    // direction the motor is actually set to
float plateSpeed = 0;             // current plate speed (ramps toward target)
unsigned long plateStartTime = 0; // when the current round/break started
unsigned long lastStepE1 = 0;

void setupStepper(uint8_t s, uint8_t d, uint8_t e) {
  pinMode(s, OUTPUT); pinMode(d, OUTPUT); pinMode(e, OUTPUT);
  digitalWrite(s, LOW);
  digitalWrite(e, LOW);           // LOW = driver enabled
}

int readCenter(uint8_t pin) {
  long sum = 0;
  for (int i = 0; i < 32; i++) { sum += analogRead(pin); delay(2); }
  return sum / 32;
}

// Stick reading -> signed target speed, with deadzone and proportional control
float stickToSpeed(int raw, int center) {
  int d = raw - center;
  if (abs(d) < DEADZONE) return 0;
  float span = (d > 0 ? 1023 - center : center) - DEADZONE;
  float f = (abs(d) - DEADZONE) / span;
  if (f > 1) f = 1;
  return (d > 0 ? f : -f) * MAX_SPEED_XY;
}

// Move current speed toward target, limited by acceleration
float ramp(float current, float target, float dt) {
  float maxDelta = ACCEL_XY * dt;
  if (target > current + maxDelta) return current + maxDelta;
  if (target < current - maxDelta) return current - maxDelta;
  return target;
}

// True when enough time has passed for the next step at this speed
bool due(float speed, unsigned long &last, unsigned long now) {
  if (fabs(speed) < 1) return false;
  unsigned long interval = (unsigned long)(1000000.0 / fabs(speed));
  if (now - last >= interval) { last = now; return true; }
  return false;
}

inline void pulse(uint8_t pin) {
  digitalWrite(pin, HIGH); delayMicroseconds(2); digitalWrite(pin, LOW);
}

void toggleMagnet() {
  magnetOn = !magnetOn;
  digitalWrite(MAGNET_PIN, magnetOn ? HIGH : LOW);
}

void startDip() {
  if (zState == Z_IDLE) {          // ignored if a dip is already running
    zTarget = Z_TRAVEL;
    zState = Z_GOING_DOWN;
  }
}

// Pick this game's round-3 values: 2-4 parts, each with its own random speed
void newRound3Randoms() {
  randomSeed(micros());                        // human timing makes every game different
  r3Parts = random(R3_MIN_PARTS, R3_MAX_PARTS + 1);
  long lo = (long)PLATE_SPEED;
  long hi = (long)(PLATE_SPEED_R2 * R3_MAX_SPEED_FACTOR);
  for (uint8_t i = 0; i < r3Parts; i++) r3Speed[i] = random(lo, hi + 1);
#if DEBUG
  Serial.print("New game. Round 3 parts="); Serial.print(r3Parts);
  Serial.print(" speeds:");
  for (uint8_t i = 0; i < r3Parts; i++) { Serial.print(" "); Serial.print(r3Speed[i]); }
  Serial.println();
#endif
}

// Single press -> Z dip.  Double press -> toggle magnet.  Long hold -> plate on/off.
// Short presses are counted on RELEASE, so a long hold is never mistaken for a click.
void handleButton(unsigned long nowMs) {
  bool btn = digitalRead(JOY_SW);
  if (btn != lastBtn && nowMs - lastBtnChange > DEBOUNCE_MS) {
    lastBtnChange = nowMs;
    lastBtn = btn;
    if (btn == LOW) {                          // pressed
      pressStart = nowMs;
      longFired = false;
    } else if (!longFired) {                   // released after a short press
      if (clickCount == 0) {
        clickCount = 1;
        lastReleaseTime = nowMs;
      } else {                                 // second short press in time -> double click
        clickCount = 0;
        toggleMagnet();
      }
    }
  }

  // Still holding past LONG_PRESS_MS -> toggle the plate (once per hold)
  if (lastBtn == LOW && !longFired && nowMs - pressStart >= LONG_PRESS_MS) {
    longFired = true;
    clickCount = 0;                            // a hold cancels any pending click
    if (plateState == PLATE_OFF) {             // start a new game at round 1
      newRound3Randoms();
      plateState = PLATE_R1;
      plateWantReverse = false;
      plateStartTime = nowMs;
    } else {                                   // stop early
      plateState = PLATE_OFF;
    }
  }

  // Button up and no second press arrived in time -> it was a single click
  if (clickCount == 1 && lastBtn == HIGH && nowMs - lastReleaseTime > DOUBLE_CLICK_MS) {
    clickCount = 0;
    startDip();
  }
}

// Runs the dip sequence: down -> pause -> up -> idle
void updateZSequence(unsigned long nowMs) {
  switch (zState) {
    case Z_GOING_DOWN:
      if (zPos == zTarget) { zState = Z_PAUSED; zPauseStart = nowMs; }
      break;
    case Z_PAUSED:
      if (nowMs - zPauseStart >= Z_PAUSE_MS) { zTarget = 0; zState = Z_GOING_UP; }
      break;
    case Z_GOING_UP:
      if (zPos == zTarget) zState = Z_IDLE;
      break;
    default:
      break;
  }
}

void setup() {
  setupStepper(X_STEP,  X_DIR,  X_EN);
  setupStepper(X2_STEP, X2_DIR, X2_EN);
  setupStepper(Y_STEP,  Y_DIR,  Y_EN);
  setupStepper(Z_STEP,  Z_DIR,  Z_EN);
  setupStepper(E1_STEP, E1_DIR, E1_EN);
  digitalWrite(E1_DIR, INVERT_PLATE ? LOW : HIGH);   // plate always turns one way
  pinMode(JOY_SW, INPUT_PULLUP);              // SW shorts to GND when pressed
  pinMode(MAGNET_PIN, OUTPUT);
  digitalWrite(MAGNET_PIN, LOW);              // magnet starts OFF

  delay(200);
  centerX = readCenter(JOY_X);
  centerY = readCenter(JOY_Y);

#if DEBUG
  Serial.begin(115200);
  Serial.print("Centre X="); Serial.print(centerX);
  Serial.print(" Y="); Serial.println(centerY);
#endif
  lastJoy = millis();
}

void loop() {
  // ---- Read joystick + button every JOY_PERIOD_MS ----
  unsigned long nowMs = millis();
  if (nowMs - lastJoy >= JOY_PERIOD_MS) {
    float dt = (nowMs - lastJoy) / 1000.0;
    lastJoy = nowMs;

    int rawX = analogRead(JOY_X);
    int rawY = analogRead(JOY_Y);
    float targetX = stickToSpeed(rawX, centerX);
    float targetY = stickToSpeed(rawY, centerY);

    if (LOCK_XY_DURING_DIP && zState != Z_IDLE) {  // freeze X/Y during the dip
      targetX = 0;
      targetY = 0;
    }

    speedX = ramp(speedX, targetX, dt);
    speedY = ramp(speedY, targetY, dt);

    digitalWrite(X_DIR,  (speedX > 0) ^ INVERT_X);
    digitalWrite(X2_DIR, (speedX > 0) ^ INVERT_X2);
    digitalWrite(Y_DIR,  (speedY > 0) ^ INVERT_Y);

    handleButton(nowMs);

    // Plate timer: R1 -> break -> R2 (reversed) -> break -> R3 (random parts) -> stop
    unsigned long elapsed = nowMs - plateStartTime;
    unsigned long r3PartMs = r3Parts ? PLATE_RUN_R3_MS / r3Parts : PLATE_RUN_R3_MS;
    if (plateState == PLATE_R1 && elapsed >= PLATE_RUN_MS) {
      plateState = PLATE_BREAK1;
      plateStartTime = nowMs;
    } else if (plateState == PLATE_BREAK1 && elapsed >= PLATE_BREAK_MS) {
      plateState = PLATE_R2;
      plateWantReverse = true;                 // round 2 turns the other way
      plateStartTime = nowMs;
    } else if (plateState == PLATE_R2 && elapsed >= PLATE_RUN_R2_MS) {
      plateState = PLATE_BREAK2;
      plateStartTime = nowMs;
    } else if (plateState == PLATE_BREAK2 && elapsed >= PLATE_BREAK_MS) {
      plateState = PLATE_R3;
      r3Part = 0;
      plateWantReverse = !plateWantReverse;    // part 1 reverses from round 2
      plateStartTime = nowMs;
    } else if (plateState == PLATE_R3 && elapsed >= r3PartMs) {
      if (r3Part + 1 < r3Parts) {              // next part: reverse again, new speed
        r3Part++;
        plateWantReverse = !plateWantReverse;
        plateStartTime = nowMs;
      } else {
        plateState = PLATE_OFF;                // game over
      }
    }

    // Speed for the current state (0 during breaks and when off)
    float plateTarget = plateState == PLATE_R1 ? PLATE_SPEED
                      : plateState == PLATE_R2 ? PLATE_SPEED_R2
                      : plateState == PLATE_R3 ? r3Speed[r3Part]
                      : 0;

    // Only flip direction once the plate has fully stopped; until then, slow down
    if (plateWantReverse != plateIsReverse) {
      if (plateSpeed == 0) {
        plateIsReverse = plateWantReverse;
        digitalWrite(E1_DIR, plateIsReverse ^ INVERT_PLATE ? LOW : HIGH);
      } else {
        plateTarget = 0;
      }
    }
    float plateDelta = PLATE_ACCEL * dt;
    if (plateSpeed < plateTarget) plateSpeed = min(plateSpeed + plateDelta, plateTarget);
    else                          plateSpeed = max(plateSpeed - plateDelta, plateTarget);

#if DEBUG
    static uint8_t n = 0;
    if (++n >= 10) {                          // print ~5x per second
      n = 0;
      Serial.print("X="); Serial.print(rawX);
      Serial.print(" Y="); Serial.print(rawY);
      Serial.print(" SW="); Serial.print(digitalRead(JOY_SW));
      Serial.print(" Zstate="); Serial.print(zState);
      Serial.print(" Magnet="); Serial.print(magnetOn ? "ON" : "OFF");
      Serial.print(" Plate=");
      if (plateState != PLATE_OFF) {
        unsigned long len = plateState == PLATE_R1 ? PLATE_RUN_MS
                          : plateState == PLATE_R2 ? PLATE_RUN_R2_MS
                          : plateState == PLATE_R3 ? r3PartMs
                          : PLATE_BREAK_MS;
        if (plateState == PLATE_R3) {
          Serial.print("ROUND 3 part "); Serial.print(r3Part + 1);
          Serial.print("/"); Serial.print(r3Parts);
          Serial.print(" @"); Serial.print(r3Speed[r3Part]); Serial.print(" ");
        } else {
          Serial.print(plateState == PLATE_R1 ? "ROUND 1 "
                     : plateState == PLATE_R2 ? "ROUND 2 "
                     : "BREAK ");
        }
        Serial.print((len - (nowMs - plateStartTime)) / 1000);
        Serial.println("s left");
      } else {
        Serial.println("OFF");
      }
    }
#endif
  }

  updateZSequence(nowMs);

  // ---- Step the motors ----
  unsigned long now = micros();

  if (due(speedX, lastStepX, now)) {          // X1 and X2 step together
    digitalWrite(X_STEP, HIGH); digitalWrite(X2_STEP, HIGH);
    delayMicroseconds(2);
    digitalWrite(X_STEP, LOW);  digitalWrite(X2_STEP, LOW);
  }

  if (due(speedY, lastStepY, now)) pulse(Y_STEP);

  if (zPos != zTarget && due(Z_SPEED, lastStepZ, now)) {
    bool down = zTarget > zPos;
    digitalWrite(Z_DIR, down ^ INVERT_Z);
    pulse(Z_STEP);
    zPos += down ? 1 : -1;
  }

  if (due(plateSpeed, lastStepE1, now)) pulse(E1_STEP);   // rotating plate
}
