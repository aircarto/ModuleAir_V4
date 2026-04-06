#include "ble_improv.h"
#include "logger.h"

#include <NimBLEDevice.h>
#include <WiFi.h>

// Improv WiFi BLE UUIDs
#define SERVICE_UUID        "00467768-6228-2272-4663-277478268000"
#define CHAR_CURRENT_STATE  "00467768-6228-2272-4663-277478268001"
#define CHAR_ERROR_STATE    "00467768-6228-2272-4663-277478268002"
#define CHAR_RPC_COMMAND    "00467768-6228-2272-4663-277478268003"
#define CHAR_RPC_RESULT     "00467768-6228-2272-4663-277478268004"
#define CHAR_CAPABILITIES   "00467768-6228-2272-4663-277478268005"

// Custom characteristic: WiFi scan results
#define CHAR_WIFI_SCAN      "00467768-6228-2272-4663-277478268010"

// Improv states
#define STATE_AUTHORIZED     0x02
#define STATE_PROVISIONING   0x03
#define STATE_PROVISIONED    0x04

// Improv errors
#define ERROR_NONE           0x00
#define ERROR_INVALID_RPC    0x01
#define ERROR_UNKNOWN_RPC    0x02
#define ERROR_UNABLE_TO_CONNECT 0x03
#define ERROR_NOT_AUTHORIZED 0x04

// RPC commands
#define CMD_WIFI_SETTINGS    0x01
#define CMD_IDENTIFY         0x02

static NimBLEServer* pServer = nullptr;
static NimBLECharacteristic* charState = nullptr;
static NimBLECharacteristic* charError = nullptr;
static NimBLECharacteristic* charRpcCmd = nullptr;
static NimBLECharacteristic* charRpcResult = nullptr;
static NimBLECharacteristic* charCapabilities = nullptr;
static NimBLECharacteristic* charWifiScan = nullptr;

static BleImprovCredentialsCallback credentialsCb = nullptr;
static bool bleRunning = false;

// Pending WiFi credentials from BLE
static String pendingSSID;
static String pendingPassword;
static bool pendingConnect = false;

// WiFi scan results (stored, sent via notify)
static String wifiScanResults[20];
static int wifiScanCount = 0;
static bool wifiScanReady = false;
static bool wifiScanSent = false;

static void setState(uint8_t state) {
  charState->setValue(&state, 1);
  charState->notify();
}

static void setError(uint8_t error) {
  charError->setValue(&error, 1);
  charError->notify();
}

static void sendRpcResult(uint8_t command, const std::vector<String>& strings) {
  // Result format: length, command, then for each string: length + string bytes
  std::vector<uint8_t> data;
  uint8_t totalLen = 1; // command byte
  for (const auto& s : strings) totalLen += 1 + s.length();

  data.push_back(totalLen);
  data.push_back(command);
  for (const auto& s : strings) {
    data.push_back(s.length());
    for (size_t i = 0; i < s.length(); i++) {
      data.push_back(s[i]);
    }
  }
  charRpcResult->setValue(data.data(), data.size());
  charRpcResult->notify();
}

class RpcCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pChar) override {
    std::string val = pChar->getValue();
    if (val.length() < 3) {
      setError(ERROR_INVALID_RPC);
      return;
    }

    uint8_t dataLen = val[0];
    uint8_t command = val[1];

    if (command == CMD_WIFI_SETTINGS) {
      // Parse: [totalLen, CMD, ssidLen, ssid..., pwLen, pw...]
      if (val.length() < 3) {
        setError(ERROR_INVALID_RPC);
        return;
      }

      size_t pos = 2;
      uint8_t ssidLen = val[pos++];
      if (pos + ssidLen > val.length()) {
        setError(ERROR_INVALID_RPC);
        return;
      }
      String ssid = "";
      for (uint8_t i = 0; i < ssidLen; i++) ssid += (char)val[pos++];

      uint8_t pwLen = (pos < val.length()) ? val[pos++] : 0;
      String password = "";
      for (uint8_t i = 0; i < pwLen && pos < val.length(); i++) password += (char)val[pos++];

      Logger.printf("[BLE Improv] Received credentials - SSID: %s\n", ssid.c_str());

      setError(ERROR_NONE);
      setState(STATE_PROVISIONING);

      pendingSSID = ssid;
      pendingPassword = password;
      pendingConnect = true;

    } else if (command == CMD_IDENTIFY) {
      Logger.println("[BLE Improv] Identify requested");
      // Could flash the LED matrix here
      setError(ERROR_NONE);

    } else {
      setError(ERROR_UNKNOWN_RPC);
    }
  }
};

static RpcCallbacks rpcCallbacks;

void bleImprovOnCredentials(BleImprovCredentialsCallback cb) {
  credentialsCb = cb;
}

void bleImprovInit(const String& deviceName) {
  Logger.printf("[BLE Improv] Starting as '%s'\n", deviceName.c_str());

  NimBLEDevice::init(deviceName.c_str());
  NimBLEDevice::setPower(ESP_PWR_LVL_P6);
  NimBLEDevice::setMTU(517);  // Allow large characteristic reads (WiFi scan list)

  pServer = NimBLEDevice::createServer();

  NimBLEService* pService = pServer->createService(SERVICE_UUID);

  charState = pService->createCharacteristic(
    CHAR_CURRENT_STATE,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  uint8_t initState = STATE_AUTHORIZED;
  charState->setValue(&initState, 1);

  charError = pService->createCharacteristic(
    CHAR_ERROR_STATE,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  uint8_t initError = ERROR_NONE;
  charError->setValue(&initError, 1);

  charRpcCmd = pService->createCharacteristic(
    CHAR_RPC_COMMAND,
    NIMBLE_PROPERTY::WRITE
  );
  charRpcCmd->setCallbacks(&rpcCallbacks);

  charRpcResult = pService->createCharacteristic(
    CHAR_RPC_RESULT,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );

  charCapabilities = pService->createCharacteristic(
    CHAR_CAPABILITIES,
    NIMBLE_PROPERTY::READ
  );
  // Capabilities: bit 0 = identify supported
  uint8_t caps = 0x01;
  charCapabilities->setValue(&caps, 1);

  // WiFi scan results: sent as notifications, one per network
  charWifiScan = pService->createCharacteristic(
    CHAR_WIFI_SCAN,
    NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
  );
  charWifiScan->setValue("pending");

  pService->start();

  // Run WiFi scan and store results (sent when client subscribes)
  Logger.println("[BLE Improv] Scanning WiFi networks...");
  wifiScanCount = WiFi.scanNetworks();
  if (wifiScanCount < 0) wifiScanCount = 0;
  if (wifiScanCount > 20) wifiScanCount = 20;
  Logger.printf("[BLE Improv] Found %d networks\n", wifiScanCount);
  for (int i = 0; i < wifiScanCount; i++) {
    wifiScanResults[i] = WiFi.SSID(i) + "\t" + String(WiFi.RSSI(i)) + "\t" + String(WiFi.encryptionType(i) != WIFI_AUTH_OPEN ? 1 : 0);
    Logger.printf("[BLE Improv]   %s\n", wifiScanResults[i].c_str());
  }
  WiFi.scanDelete();
  wifiScanReady = true;

  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->addServiceUUID(SERVICE_UUID);
  pAdv->setScanResponse(true);
  pAdv->start();

  bleRunning = true;
  Logger.println("[BLE Improv] Ready, waiting for connections");
}

void bleImprovLoop() {
  if (!bleRunning) return;

  // Send WiFi scan results once a client subscribes to notifications
  if (wifiScanReady && !wifiScanSent && pServer->getConnectedCount() > 0 && charWifiScan->getSubscribedCount() > 0) {
    wifiScanSent = true;
    delay(200);  // let client settle after subscribing
    Logger.printf("[BLE Improv] Sending %d scan results via notify\n", wifiScanCount);
    for (int i = 0; i < wifiScanCount; i++) {
      const String& s = wifiScanResults[i];
      Logger.printf("[BLE Improv]   Notify[%d] (%d bytes): %s\n", i, s.length(), s.c_str());
      charWifiScan->setValue((const uint8_t*)s.c_str(), s.length());
      charWifiScan->notify();
      delay(150);
    }
    // End marker
    charWifiScan->setValue((const uint8_t*)"END", 3);
    charWifiScan->notify();
    Logger.println("[BLE Improv] Scan results sent");
  }

  if (!pendingConnect) return;
  pendingConnect = false;

  Logger.printf("[BLE Improv] Attempting WiFi connection to '%s'...\n", pendingSSID.c_str());

  // Try connecting to the WiFi
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(pendingSSID.c_str(), pendingPassword.c_str());

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Logger.print(".");
  }
  Logger.println();

  if (WiFi.status() == WL_CONNECTED) {
    Logger.printf("[BLE Improv] Connected! IP: %s\n", WiFi.localIP().toString().c_str());

    setState(STATE_PROVISIONED);

    // Send the redirect URL as RPC result
    String url = "http://" + WiFi.localIP().toString();
    std::vector<String> result = { url };
    sendRpcResult(CMD_WIFI_SETTINGS, result);

    delay(1000);

    // Save credentials and restart via callback
    if (credentialsCb) {
      credentialsCb(pendingSSID, pendingPassword);
    }
  } else {
    Logger.println("[BLE Improv] Connection failed");
    setError(ERROR_UNABLE_TO_CONNECT);
    setState(STATE_AUTHORIZED);
    // Go back to AP only
    WiFi.mode(WIFI_AP);
  }

  pendingSSID = "";
  pendingPassword = "";
}

void bleImprovStop() {
  if (!bleRunning) return;
  Logger.println("[BLE Improv] Stopping");
  NimBLEDevice::deinit(true);
  bleRunning = false;
}
