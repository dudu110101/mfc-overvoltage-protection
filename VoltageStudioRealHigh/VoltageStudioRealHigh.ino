// Arduino Uno + A0 voltage sensor + D7 relay test firmware.
// Serial protocol: 115200 bps, ASCII, LF terminated.
#include <Arduino.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

const byte VOLTAGE_PIN = A0;
const byte RELAY_PIN = 7;
const byte GREEN_PIN = 8;
const byte RED_PIN = 9;
const byte BUZZER_PIN = 10;
// Passive buzzer tone output. Use a suitable driver if current requires it.
const unsigned int BUZZER_FREQUENCY_HZ = 3000;

// Most one-channel relay boards are Active-Low. If LOAD ON/OFF is reversed,
// change this one value to false, upload again, and test without 12 V first.
const bool RELAY_ACTIVE_LOW = false;
// Enable ONLY when sensor/pot supply is independent of relay contacts.
// Current wiring loses the sensor supply when relay opens. Keep false.
// After independent sensor power is wired and verified, change to true.
const bool SENSOR_ALWAYS_POWERED = false;
bool acquisitionPaused = false;
bool testStarted = false;
const unsigned long RECOVERY_STABLE_MS = 500;
unsigned long recoverySinceMs = 0;
bool recoveryTiming = false;

// These are starting values only. Confirm the sensor ratio with a multimeter.
const float ADC_REFERENCE_V = 5.0f;
const float SENSOR_DIVIDER_RATIO = 5.0f;

const unsigned long SAMPLE_INTERVAL_MS = 10;
const unsigned long DATA_INTERVAL_MS = 100;

float warningVoltage = 10.0f;
float tripVoltage = 11.0f;
unsigned long configRevision = 1;

float measuredVoltage = 0.0f;
int lastAdcValue = 0;
float sensorOutputVoltage = 0.0f;

bool loadOn = false;
bool tripLatched = false;
const char* stateText = "OFF";
const char* tripCause = "NONE";

unsigned long lastSampleMs = 0;
unsigned long lastDataMs = 0;
unsigned long sequenceNumber = 0;

char receiveBuffer[96] = {};
byte receiveLength = 0;
bool discardLine = false;

void updateIndicators()
{
  const bool normal = loadOn && strcmp(stateText, "NORMAL") == 0;
  const bool warning = strcmp(stateText, "WARNING") == 0;
  const bool danger = strcmp(stateText, "DANGER") == 0;
  const bool alarm = danger || (warning && millis() % 1000UL < 500);
  // Passive buzzer alarm: 180 ms ON / 120 ms OFF, three pulses,
  // then silence until the 1.5-second cycle repeats. No blocking delay.
  static bool alarmWasActive = false;
  static unsigned long alarmStartedAt = 0;
  const unsigned long now = millis();
  if (tripLatched && !alarmWasActive) alarmStartedAt = now;
  alarmWasActive = tripLatched;
  const unsigned long phase = (now - alarmStartedAt) % 1500UL;
  const bool beep = tripLatched && phase < 780UL && phase % 300UL < 180UL;
  digitalWrite(GREEN_PIN, normal ? HIGH : LOW);
  digitalWrite(RED_PIN, alarm ? HIGH : LOW);
  static bool tonePlaying = false;
  if (beep != tonePlaying)
  {
    if (beep) tone(BUZZER_PIN, BUZZER_FREQUENCY_HZ);
    else noTone(BUZZER_PIN);
    tonePlaying = beep;
  }
}

int relayOutputLevel(bool turnOn)
{
  if (RELAY_ACTIVE_LOW)
    return turnOn ? LOW : HIGH;

  return turnOn ? HIGH : LOW;
}

void setLoad(bool turnOn)
{
  loadOn = turnOn;
  digitalWrite(RELAY_PIN, relayOutputLevel(turnOn));

  if (turnOn)
  {
    stateText = "NORMAL";
  }
  else if (!tripLatched)
  {
    stateText = "OFF";
  }
}

void sampleVoltage()
{
  lastAdcValue = analogRead(VOLTAGE_PIN);
  sensorOutputVoltage =
      static_cast<float>(lastAdcValue) * ADC_REFERENCE_V / 1023.0f;
  // Unfiltered reading: no averaging delay in the cutoff path.
  measuredVoltage = sensorOutputVoltage * SENSOR_DIVIDER_RATIO;
}

void sendMeasurement()
{
  Serial.print("MEASURE,"); Serial.print(lastAdcValue); Serial.print(',');
  Serial.print(sensorOutputVoltage, 3); Serial.print(',');
  Serial.print(measuredVoltage, 2); Serial.print(',');
  Serial.println(loadOn ? "ON" : "OFF");
}

void sendEvent(const char* type, const char* detail)
{
  Serial.print("EVENT,");
  Serial.print(millis());
  Serial.print(',');
  Serial.print(type);
  Serial.print(',');
  Serial.println(detail);
}

void updateProtection()
{
  // Preview: live ADC, STOP status, hold the normal supply output.
  if (!testStarted) { stateText = "OFF"; return; }
  if (tripLatched)
  {
    const float recoveryMargin = tripVoltage < 4.0f ? tripVoltage * 0.05f : 0.2f;
    if (SENSOR_ALWAYS_POWERED && !acquisitionPaused &&
        measuredVoltage < tripVoltage - recoveryMargin)
    {
      if (!recoveryTiming) { recoveryTiming = true; recoverySinceMs = millis(); }
      if (millis() - recoverySinceMs >= RECOVERY_STABLE_MS)
      {
        tripLatched = false;
        tripCause = "NONE";
        recoveryTiming = false;
        setLoad(true);
        stateText = measuredVoltage >= warningVoltage ? "WARNING" : "NORMAL";
        sendEvent("RECOVER", "VOLTAGE_SAFE");
        return;
      }
    }
    else recoveryTiming = false;
    stateText = "DANGER";
    setLoad(false);
    return;
  }

  if (!loadOn)
  {
    stateText = "OFF";
    return;
  }

  if (measuredVoltage >= tripVoltage)
  {
    stateText = "DANGER";
    tripLatched = true;
    recoveryTiming = false;
    tripCause = "OVERVOLTAGE";
    setLoad(false); // Switch OFF before writing any serial log.
    sendEvent("TRIP", "OVERVOLTAGE");
    Serial.print("EVENT,"); Serial.print(millis());
    Serial.print(",TRIP_VOLTAGE,"); Serial.print(measuredVoltage, 2);
    Serial.print(','); Serial.println(tripVoltage, 2);
  }
  else
  {
    stateText = measuredVoltage >= warningVoltage ? "WARNING" : "NORMAL";
  }
}

void sendData()
{
  Serial.print("DATA,");
  Serial.print(++sequenceNumber);
  Serial.print(',');
  Serial.print(millis());
  Serial.print(',');
  Serial.print(measuredVoltage, 2);
  Serial.print(',');
  Serial.print(stateText);
  Serial.print(',');
  Serial.print(loadOn ? "ON" : "OFF");
  Serial.print(',');
  Serial.print(tripCause);
  Serial.print(',');
  Serial.println(configRevision);
}

void sendAck(unsigned long requestId, const char* command, const char* result)
{
  Serial.print("ACK,");
  Serial.print(requestId);
  Serial.print(',');
  Serial.print(command);
  Serial.print(',');
  Serial.println(result);
}

void sendError(unsigned long requestId, const char* command, const char* error)
{
  Serial.print("ERR,");
  Serial.print(requestId);
  Serial.print(',');
  Serial.print(command);
  Serial.print(',');
  Serial.println(error);
}

void handleCommand(char* line)
{
  char* savePosition = nullptr;
  char* frameType = strtok_r(line, ",", &savePosition);
  char* requestText = strtok_r(nullptr, ",", &savePosition);
  char* command = strtok_r(nullptr, ",", &savePosition);

  if (frameType == nullptr || requestText == nullptr || command == nullptr ||
      strcmp(frameType, "CMD") != 0)
  {
    sendError(0, "FORMAT", "INVALID_FRAME");
    return;
  }

  const unsigned long requestId = strtoul(requestText, nullptr, 10);

  if (strcmp(command, "HELLO") == 0)
  {
    Serial.print("HELLO,1,UNO,config_rev=");
    Serial.print(configRevision);
    Serial.print(",ready=1,mode=REAL_12V,behavior=PAUSE_HOLD,auto_recover=");
    Serial.println(SENSOR_ALWAYS_POWERED ? 1 : 0);
    sendEvent("ACQUISITION", acquisitionPaused ? "PAUSED" : testStarted ? "RUNNING" : "PREVIEW");
    Serial.print("CONFIG,");
    Serial.print(warningVoltage, 2);
    Serial.print(',');
    Serial.print(tripVoltage, 2);
    Serial.print(',');
    Serial.print(configRevision);
    Serial.print(",relay_active_low=");
    Serial.println(RELAY_ACTIVE_LOW ? 1 : 0);
    return;
  }

  if (strcmp(command, "LOAD") == 0)
  {
    char* value = strtok_r(nullptr, ",", &savePosition);
    if (value == nullptr)
    {
      sendError(requestId, "LOAD", "MISSING_VALUE");
      return;
    }

    if (strcmp(value, "ON") == 0)
    {
      acquisitionPaused = false;
      testStarted = true;
      sendEvent("ACQUISITION", "RUNNING");
      if (tripLatched)
      {
        sendError(requestId, "LOAD", "TRIP_LATCHED");
        return;
      }

      if (!loadOn) setLoad(true);
      sampleVoltage();
      updateProtection();
      if (tripLatched) sendError(requestId, "LOAD", "TRIP_LATCHED");
      else sendAck(requestId, "LOAD", "ON");
      if (!tripLatched)
        sendEvent("RELAY_OUTPUT", relayOutputLevel(true) == LOW ? "ON_LOW" : "ON_HIGH");
      updateIndicators();
      sendMeasurement();
      return;
    }

    if (strcmp(value, "OFF") == 0)
    {
      // UI pause: hold current relay output, continue hardware protection.
      acquisitionPaused = true;
      recoveryTiming = false;
      sendAck(requestId, "LOAD", "PAUSED");
      sendEvent("ACQUISITION", "PAUSED");
      return;
    }

    if (strcmp(value, "DISABLE") == 0)
    {
      acquisitionPaused = false;
      testStarted = false;
      setLoad(false);
      tripLatched = false;
      recoveryTiming = false;
      tripCause = "NONE";
      stateText = "OFF";
      sendAck(requestId, "LOAD", "OFF");
      sendEvent("ACQUISITION", "DISABLED");
      updateIndicators();
      return;
    }

    sendError(requestId, "LOAD", "INVALID_VALUE");
    return;
  }

  if (strcmp(command, "RESET") == 0)
  {
    acquisitionPaused = false;
    testStarted = false;
    setLoad(false);
    tripLatched = false;
    recoveryTiming = false;
    tripCause = "NONE";
    stateText = "OFF";
    sendAck(requestId, "RESET", "OK");
    sendEvent("RESET", "MANUAL");
    sendEvent("ACQUISITION", "DISABLED");
    return;
  }

  if (strcmp(command, "SET") == 0)
  {
    char* warningText = strtok_r(nullptr, ",", &savePosition);
    char* tripText = strtok_r(nullptr, ",", &savePosition);

    if (warningText == nullptr || tripText == nullptr)
    {
      sendError(requestId, "SET", "MISSING_VALUE");
      return;
    }

    char* warningEnd = nullptr;
    char* tripEnd = nullptr;
    const float newWarning = strtod(warningText, &warningEnd);
    const float newTrip = strtod(tripText, &tripEnd);

    if (warningEnd == warningText || tripEnd == tripText ||
        *warningEnd || *tripEnd || !isfinite(newWarning) || !isfinite(newTrip) ||
        newWarning <= 0.0f || newTrip <= newWarning || newTrip > 24.0f)
    {
      sendError(requestId, "SET", "INVALID_RANGE");
      return;
    }

    warningVoltage = newWarning;
    tripVoltage = newTrip;
    ++configRevision;
    sampleVoltage();
    updateProtection();
    sendAck(requestId, "SET", "OK");
    return;
  }

  sendError(requestId, command, "UNKNOWN_COMMAND");
}

void receiveSerialCommands()
{
  while (Serial.available() > 0)
  {
    const char received = static_cast<char>(Serial.read());

    if (received == '\r')
      continue;

    if (received == '\n')
    {
      receiveBuffer[receiveLength] = '\0';
      if (!discardLine && receiveLength > 0)
        handleCommand(receiveBuffer);
      if (discardLine) sendError(0, "FORMAT", "LINE_TOO_LONG");
      receiveLength = 0;
      discardLine = false;
      continue;
    }

    if (discardLine) continue;
    if (receiveLength < sizeof(receiveBuffer) - 1)
      receiveBuffer[receiveLength++] = received;
    else
    {
      receiveLength = 0;
      discardLine = true;
    }
  }
}

void setup()
{
  digitalWrite(RELAY_PIN, relayOutputLevel(false));
  pinMode(RELAY_PIN, OUTPUT);
  setLoad(false);
  digitalWrite(GREEN_PIN, LOW);
  digitalWrite(RED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(RED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(VOLTAGE_PIN, INPUT);

  Serial.begin(115200);
  // Requested normal state: ON at boot, with protection enabled immediately.
  setLoad(true);
  sampleVoltage();
  updateProtection();
  updateIndicators();
  Serial.print("HELLO,1,UNO,config_rev=1,ready=1,mode=REAL_12V,behavior=PAUSE_HOLD,auto_recover=");
  Serial.println(SENSOR_ALWAYS_POWERED ? 1 : 0);
  sendEvent("ACQUISITION", "PREVIEW");
}

void loop()
{
  receiveSerialCommands();
  const unsigned long now = millis();

  if (now - lastSampleMs >= SAMPLE_INTERVAL_MS)
  {
    lastSampleMs = now;
    sampleVoltage();
    updateProtection();
  }
  updateIndicators();

  if (now - lastDataMs >= DATA_INTERVAL_MS)
  {
    lastDataMs = now;
    sendData();
  }
}

