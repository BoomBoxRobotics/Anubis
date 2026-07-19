#include <RAK14014_FT6336U.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <math.h>
#include <ctype.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_ota_ops.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <esp32-hal-rgb-led.h>
#include <Preferences.h>
#include <AudioBoard.h>
#include <AudioTools.h>
#include <AudioTools/AudioLibs/AudioBoardStream.h>

using namespace audio_driver;
using namespace audio_tools;

#include "anubis_rgb565.h"
#include "menu_icons_rgb565.h"
#include "cave_johnson_startup_audio.h"
#include "dungeon_run_assets_rgb565.h"

/*
  Anubis / Hosyond ESP32-S3 transmitter firmware.

  This sketch is intentionally organized as a single Arduino .ino so it stays
  easy to open in the Arduino IDE. Major regions are marked with section
  comments: storage/config, UI state, model/protocol helpers, setup/loop,
  hardware bring-up, input processing, battery safety, drawing, and touch
  dispatch.

  Drawing convention used throughout:
    * drawXStatic() paints the full screen or long-lived chrome.
    * drawXDynamic() updates values that change while the screen is visible.
    * fullRedraw/uiNeedsRedraw/topBarNeedsRedraw keep the small TFT responsive
      by avoiding full-screen redraws unless something actually changed.
*/

  // Persistent storage layout and schema versions. These addresses are packed
  // from the end of EEPROM downward so older model data can survive additions
  // when the version checks below know how to repair the changed block.
  #define EEPROM_SIZE 1024
  #define MIX_STORAGE_VERSION 3
  #define ESPNOW_BIND_STORAGE_VERSION 1
  #define FAILSAFE_STORAGE_VERSION 2
  #define RATE_STORAGE_VERSION 2
  #define EXPO_STORAGE_VERSION 3
  #define ENDPOINT_STORAGE_VERSION 2
  #define ESC_MAP_STORAGE_VERSION 1
  #define ACTIVE_MODEL_STORAGE_VERSION 1
  #define ESPNOW_OUTPUT_MODE_STORAGE_VERSION 1
  #define EEPROM_MIX_VERSION_ADDR (EEPROM_SIZE - 1)
  #define EEPROM_BIND_VERSION_ADDR (EEPROM_SIZE - 2)
  #define EEPROM_FAILSAFE_VERSION_ADDR (EEPROM_SIZE - 3)
  #define EEPROM_RATE_VERSION_ADDR (EEPROM_SIZE - 4)
  #define EEPROM_EXPO_VERSION_ADDR (EEPROM_SIZE - 5)
  #define EEPROM_ENDPOINT_VERSION_ADDR (EEPROM_SIZE - 6)
  #define EEPROM_ESC_MAP_VERSION_ADDR (EEPROM_ENDPOINT_VERSION_ADDR - 1)
  #define EEPROM_ESC_MAP_DATA_ADDR (EEPROM_ESC_MAP_VERSION_ADDR - 2)
  #define EEPROM_ACTIVE_MODEL_VERSION_ADDR (EEPROM_ESC_MAP_DATA_ADDR - 1)
  #define EEPROM_ACTIVE_MODEL_INDEX_ADDR (EEPROM_ACTIVE_MODEL_VERSION_ADDR - 1)
  #define EEPROM_ESPNOW_OUTPUT_MODE_VERSION_ADDR (EEPROM_ACTIVE_MODEL_INDEX_ADDR - 1)
  #define EEPROM_ESPNOW_OUTPUT_MODE_DATA_ADDR (EEPROM_ESPNOW_OUTPUT_MODE_VERSION_ADDR - MAX_MODELS)
  #define MIX_CHANNEL_MASK 0x0F
  #define MIX_REVERSE_SEPARATE_FLAG 0x80

  // Hardware driver instances shared by the UI, touch, and input pipeline.
  TFT_eSPI tft = TFT_eSPI();
  TFT_eSprite stickBaseSprite = TFT_eSprite(&tft);
  FT6336U touchPanel;

  // Screen and button IDs are the backbone of the UI state machine. Touch and
  // D-pad handlers both resolve user input to these enum values.
  enum Screen {
  SCREEN_SPLASH,
  SCREEN_MAIN,
  SCREEN_MENU,
  SCREEN_CONTROLLER_SETTINGS,
  SCREEN_EXPO,
  SCREEN_RATES,
  SCREEN_PROTOCOL,
  SCREEN_TX_UPDATE,
  SCREEN_STICK_CALIBRATION,
  SCREEN_OTA_SETTINGS,
  SCREEN_ELRS,
  SCREEN_ELRS_RX_CONFIG,
  SCREEN_MODEL_SETTINGS,
  SCREEN_REVERSE,
  SCREEN_TRIM,
  SCREEN_ENDPOINTS,
  SCREEN_FAILSAFE,
  SCREEN_MODEL_NAME,
  SCREEN_DRIVE_TYPE,
  SCREEN_TANK_MODE,
  SCREEN_ESC_CHANNEL_SETUP,
  SCREEN_MIXING,
  SCREEN_DISPLAY_SETTINGS,
  SCREEN_TIMER,
  SCREEN_GAMES_MENU,
  SCREEN_20Q_GAME,
  SCREEN_ASTEROIDS_GAME,
  SCREEN_STAR_SWARM_GAME,
  SCREEN_DUNGEON_RUN,
  SCREEN_SPACE_GAME
  };

  enum ButtonID {
  BTN_NONE,
  BTN_MENU,
  BTN_BACK,
  BTN_CTRL,
  BTN_MODEL,
  BTN_REVERSE,
  BTN_TRIM,
  BTN_ENDPOINTS,
  BTN_FAILSAFE,
  BTN_EXPO,
  BTN_RATES,
  BTN_PROTOCOL,
  BTN_STICK_CAL,
  BTN_PAGE_NAV,
  BTN_PROTOCOL_ELRS,
  BTN_PROTOCOL_ESPNOW,
  BTN_PROTOCOL_BIND,
  BTN_PROTOCOL_OTA,
  BTN_PROTOCOL_OTA_CFG,
  BTN_TX_UPDATE,
  BTN_ELRS_BIND,
  BTN_ELRS_RX_CONFIG,
  BTN_ELRS_TX_POWER_SLIDER,
  BTN_ELRS_TX_POWER_VALUE,
  BTN_ELRS_RX_MODEL_ID,
  BTN_ELRS_RX_FAILSAFE,
  BTN_ELRS_RX_PWM1,
  BTN_ELRS_RX_PWM2,
  BTN_ELRS_RX_PWM3,
  BTN_ELRS_RX_PWM4,
  BTN_ELRS_RX_PUSH_FAILSAFE,
  BTN_ELRS_RX_SAVE,
  BTN_DRIVE_TYPE,
  BTN_DRIVE_TANK,
  BTN_DRIVE_CAR,
  BTN_DRIVE_OMNI,
  BTN_DRIVE_X_DRONE,
  BTN_TANK_SINGLE,
  BTN_TANK_DUAL,
  BTN_ESC_CH1,
  BTN_ESC_CH2,
  BTN_ESC_CH3,
  BTN_ESC_CH4,
  BTN_MODEL_NAME,
  BTN_MIXING,
  BTN_DISPLAY_SETTINGS,
  BTN_DISPLAY_BRIGHTNESS_DEC,
  BTN_DISPLAY_BRIGHTNESS_INC,
  BTN_DISPLAY_TIMEOUT_DEC,
  BTN_DISPLAY_TIMEOUT_INC,
  BTN_DISPLAY_OFF_TIMEOUT_DEC,
  BTN_DISPLAY_OFF_TIMEOUT_INC,
  BTN_DISPLAY_SLEEP_DEC,
  BTN_DISPLAY_SLEEP_INC,
  BTN_DISPLAY_SLEEP_NOW,
  BTN_DISPLAY_THEME_TOGGLE,
  BTN_TIMER_COUNT_DOWN,
  BTN_TIMER_COUNT_UP,
  BTN_TIMER_START_PAUSE,
  BTN_TIMER_RESET,
  BTN_TIMER_KEY,
  BTN_DRONE_ARM,
  BTN_GAME,
  BTN_GAME_BATTLEZONE,
  BTN_GAME_20Q,
  BTN_GAME_ASTEROIDS,
  BTN_GAME_INVADERS,
  BTN_GAME_SNAKE,
  BTN_GAME_MAZE_CHASE,
  BTN_GAME_STAR_SWARM,
  BTN_GAME_MISSILE_DEFENSE,
  BTN_GAME_DUNGEON_RUN,
  BTN_GAME_SCROLL_SHOOTER,
  BTN_20Q_YES,
  BTN_20Q_NO,
  BTN_20Q_UNSURE,
  BTN_20Q_RESTART,
  BTN_OPTION_2,
  BTN_OPTION_3,
  BTN_OPTION_4,
  BTN_OPTION_5
  };

  enum DriveType {
  DRIVE_TANK,
  DRIVE_CAR,
  DRIVE_OMNI,
  DRIVE_X_DRONE,
  DRIVE_QUAD_X = DRIVE_X_DRONE
  };

  enum TankControlMode {
  TANK_MODE_DUAL_STICK,
  TANK_MODE_RIGHT_STICK
  };

  enum ProtocolType {
  PROTOCOL_ELRS,
  PROTOCOL_ESPNOW
  };

  #define MAX_MODELS 4
  #define STICK_AXIS_COUNT 4
  #define CHANNEL_COUNT 6
  #define DRONE_ROLL_CHANNEL 0
  #define DRONE_PITCH_CHANNEL 1
  #define DRONE_THROTTLE_CHANNEL 2
  #define DRONE_YAW_CHANNEL 3
  #define DRONE_ARM_CHANNEL 4
  #define DRONE_ARM_DISARMED_VALUE -1.0f
  #define DRONE_ARM_ARMED_VALUE 1.0f
  #define PRESET_MIX_COUNT 4
  #define USER_MIX_COUNT 4
  #define MIXES_PER_PAGE 4
  #define MIX_PAGE_COUNT 2
  #define MIX_PAGE_PRESET 0
  #define MIX_PAGE_USER 1
  #define MIX_COUNT (PRESET_MIX_COUNT + USER_MIX_COUNT)
  #define PROTOCOL_MASK 0x60
  #define PROTOCOL_SHIFT 5
  #define EEPROM_BIND_DATA_ADDR (EEPROM_RATE_VERSION_ADDR - (MAX_MODELS * ESP_NOW_ETH_ALEN))
  #define EEPROM_FAILSAFE_DATA_ADDR (EEPROM_BIND_DATA_ADDR - (MAX_MODELS * CHANNEL_COUNT))
  #define EEPROM_RATE_DATA_ADDR (EEPROM_FAILSAFE_DATA_ADDR - (MAX_MODELS * CHANNEL_COUNT))
  #define EEPROM_EXPO_DATA_ADDR (EEPROM_RATE_DATA_ADDR - (MAX_MODELS * CHANNEL_COUNT))
  #define EEPROM_ENDPOINT_LOW_DATA_ADDR (EEPROM_EXPO_DATA_ADDR - (MAX_MODELS * CHANNEL_COUNT))
  #define EEPROM_ENDPOINT_HIGH_DATA_ADDR (EEPROM_ENDPOINT_LOW_DATA_ADDR - (MAX_MODELS * CHANNEL_COUNT))
  #define OTA_SETTINGS_STORAGE_VERSION 1
  #define OTA_STA_SSID_STORAGE_BYTES 32
  #define OTA_STA_PASSWORD_STORAGE_BYTES 64
  #define OTA_AP_SSID_STORAGE_BYTES 32
  #define EEPROM_OTA_VERSION_ADDR (EEPROM_ENDPOINT_HIGH_DATA_ADDR - 1)
  #define EEPROM_OTA_STA_SSID_ADDR (EEPROM_OTA_VERSION_ADDR - OTA_STA_SSID_STORAGE_BYTES)
  #define EEPROM_OTA_STA_PASSWORD_ADDR (EEPROM_OTA_STA_SSID_ADDR - OTA_STA_PASSWORD_STORAGE_BYTES)
  #define EEPROM_OTA_AP_SSID_ADDR (EEPROM_OTA_STA_PASSWORD_ADDR - OTA_AP_SSID_STORAGE_BYTES)
  #define DISPLAY_SETTINGS_STORAGE_VERSION 3
  #define EEPROM_DISPLAY_VERSION_ADDR (EEPROM_OTA_AP_SSID_ADDR - 1)
  #define EEPROM_DISPLAY_BRIGHTNESS_ADDR (EEPROM_DISPLAY_VERSION_ADDR - 1)
  #define EEPROM_DISPLAY_SLEEP_BRIGHTNESS_ADDR (EEPROM_DISPLAY_BRIGHTNESS_ADDR - 1)
  #define EEPROM_DISPLAY_TIMEOUT_INDEX_ADDR (EEPROM_DISPLAY_SLEEP_BRIGHTNESS_ADDR - 1)
  #define EEPROM_DISPLAY_OFF_TIMEOUT_INDEX_ADDR (EEPROM_DISPLAY_TIMEOUT_INDEX_ADDR - 1)
  #define EEPROM_DISPLAY_THEME_ADDR (EEPROM_DISPLAY_OFF_TIMEOUT_INDEX_ADDR - 1)
  #define STICK_CAL_STORAGE_VERSION 1
  // Calibration remains compiled in for future service use, but hidden from
  // the user-facing Controller Settings menu while hardcoded values are final.
  #define STICK_CAL_SCREEN_ENABLED false
  #define EEPROM_STICK_CAL_VERSION_ADDR (EEPROM_DISPLAY_THEME_ADDR - 1)
  #define EEPROM_STICK_CAL_MIN_DATA_ADDR (EEPROM_STICK_CAL_VERSION_ADDR - (STICK_AXIS_COUNT * 2))
  #define EEPROM_STICK_CAL_CENTER_DATA_ADDR (EEPROM_STICK_CAL_MIN_DATA_ADDR - (STICK_AXIS_COUNT * 2))
  #define EEPROM_STICK_CAL_MAX_DATA_ADDR (EEPROM_STICK_CAL_CENTER_DATA_ADDR - (STICK_AXIS_COUNT * 2))
  #define ESPNOW_LINK_MAGIC 0xA614
  #define ESPNOW_LINK_VERSION 2
  #define ESPNOW_WIFI_CHANNEL 6
  #define ESPNOW_SEND_INTERVAL_MS 20
  #define ESPNOW_PING_INTERVAL_MS 100
  #define ESPNOW_TELEMETRY_INTERVAL_MS 100
  #define ESPNOW_TELEMETRY_TIMEOUT_MS 1000
  #define ESPNOW_HEADER_SIGNAL_TIMEOUT_MS 2000
  #define ESPNOW_BIND_TIMEOUT_MS 15000
  #define ESPNOW_BIND_COMMIT_RETRY_MS 300
  #define ESPNOW_UNBIND_BURST_COUNT 5
  #define ESPNOW_SEND_SERIAL_DEBUG false
  // ELRS module profile:
  //   ELRS_MODULE_PROFILE_INTERNAL     = RadioMaster Pocket/internal-style full-duplex module
  //   ELRS_MODULE_PROFILE_RANGER_NANO  = RadioMaster Ranger Nano external one-wire module bay
  #define ELRS_MODULE_PROFILE_INTERNAL 0
  #define ELRS_MODULE_PROFILE_RANGER_NANO 1
  #define ELRS_MODULE_PROFILE_AUTO 2
  #define ELRS_MODULE_PROFILE ELRS_MODULE_PROFILE_AUTO
  #define ELRS_PROFILE_DETECT_SWITCH_MS 1500UL
  #define ELRS_PROFILE_DETECT_ACTIVE_MS 3500UL
  // Same-pin Ranger probing can wedge the UART when no module is physically
  // present. Leave this off for the internal-module transmitter build; enable
  // it only on units where the Ranger Nano is expected.
  #define ELRS_AUTO_PROBE_RANGER_ON_SILENT_INTERNAL 0

#if ELRS_MODULE_PROFILE == ELRS_MODULE_PROFILE_RANGER_NANO || ELRS_MODULE_PROFILE == ELRS_MODULE_PROFILE_AUTO
  #define ELRS_HALF_DUPLEX_MODE 1
#else
  #define ELRS_HALF_DUPLEX_MODE 0
#endif

#if ELRS_MODULE_PROFILE == ELRS_MODULE_PROFILE_RANGER_NANO
  #define ELRS_UART_DATA_PIN 43
  #define ELRS_FORCE_PIN_PAIR_INDEX 0
  #define ELRS_FORCE_INVERT_MODE 2
  #define ELRS_BIND_FALLBACK_FIELD_INDEX 29
  #define ELRS_SNIFF_SINGLE_WIRE_ONLY 1
  #define ELRS_PASSIVE_SNIFF_RX_INVERT 1
#elif ELRS_MODULE_PROFILE == ELRS_MODULE_PROFILE_AUTO
  #define ELRS_UART_DATA_PIN 43
  #define ELRS_FORCE_PIN_PAIR_INDEX -1
  #define ELRS_FORCE_INVERT_MODE -1
  #define ELRS_BIND_FALLBACK_FIELD_INDEX 28
  #define ELRS_SNIFF_SINGLE_WIRE_ONLY 1
  #define ELRS_PASSIVE_SNIFF_RX_INVERT 1
#else
  #define ELRS_UART_DATA_PIN 44
  #define ELRS_FORCE_PIN_PAIR_INDEX 1
  #define ELRS_FORCE_INVERT_MODE 0
  #define ELRS_BIND_FALLBACK_FIELD_INDEX 28
  #define ELRS_SNIFF_SINGLE_WIRE_ONLY 0
  #define ELRS_PASSIVE_SNIFF_RX_INVERT 0
#endif

  #define ELRS_UART_TX_PIN_DEFAULT 44
  #define ELRS_UART_RX_PIN_DEFAULT 43
  #define ELRS_CRSF_TX_INTERVAL_MS 10
  #define ELRS_DEVICE_PING_INTERVAL_MS 500
  #define ELRS_MODULE_TIMEOUT_MS 2000
  #define ELRS_LINK_TIMEOUT_MS 1200
  #define ELRS_BAUD_RETRY_MS 1500
  #define ELRS_TX_ONLY_ACTIVE_WINDOW_MS 500
  #define ELRS_PARAM_READ_INTERVAL_MS 120
  #define ELRS_PARAM_SCAN_FALLBACK_MAX 64
  #define ELRS_BIND_BURST_INTERVAL_MS 150UL
  #define ELRS_BIND_COMMAND_WINDOW_MS 10000UL
  #define ELRS_BIND_SERIAL_DEBUG false
  #define ELRS_LOW_POWER_NO_LINK_TIMEOUT_MS 0UL
  #define ELRS_LOW_POWER_FALLBACK_MW 10
  #define ELRS_LINK_DEFAULT_POWER_MW 100
  #define ELRS_RX_WIFI_SSID "ExpressLRS RX"
  #define ELRS_RX_WIFI_PASSWORD "expresslrs"
  #define ELRS_RX_WIFI_CONNECT_TIMEOUT_MS 150000UL
  #define ELRS_RX_HTTP_TIMEOUT_MS 1800UL
  #define ELRS_TX_SYNC_PRIMARY 0xC8
  #define ELRS_TX_MODULE_ADDR 0xEE
  #define ELRS_FORCE_BAUD 5250000UL
  #define RADIO_PROTOCOL_SERIAL_DEBUG true
  #define ELRS_VERBOSE_SERIAL_DEBUG false
  #define DRIVE_OUTPUT_SERIAL_DEBUG true
  // For internal RadioMaster ELRS modules, actively drive CRSF traffic so the
  // module has a reason to answer.
  #define ELRS_PROBE_UNTIL_MODULE_FRAMES false
  #define ELRS_RX_ONLY_DIAGNOSTIC false
  #define ELRS_RX_READ_BUDGET 256
  // Diagnostic only: true listens passively and sends no ELRS traffic.
  #define ELRS_PASSIVE_SNIFF_MODE false
  #define ELRS_PASSIVE_SNIFF_BAUD ELRS_FORCE_BAUD
  #define ELRS_SNIFF_LOG_WRITES_IMMEDIATE true
  #define ELRS_SNIFF_LOG_RC_SAMPLES true
  #define ELRS_SNIFF_RC_SAMPLE_MS 250
  #define ELRS_SNIFF_HOST_TO_MODULE_PIN 43
  #define ELRS_SNIFF_MODULE_TO_HOST_PIN 44

  enum EspNowMessageType : uint8_t {
  ESPNOW_MSG_CONTROL = 0x43,
  ESPNOW_MSG_TELEMETRY = 0x54,
  ESPNOW_MSG_PING = 0x50,
  ESPNOW_MSG_PING_ACK = 0x51,
  ESPNOW_MSG_BIND_BEACON = 0x42,
  ESPNOW_MSG_BIND_COMMIT = 0x62,
  ESPNOW_MSG_BIND_ACK = 0x41,
  ESPNOW_MSG_UNBIND = 0x55
  };

enum NumpadTarget {
  NUMPAD_TARGET_MIX_RATE,
  NUMPAD_TARGET_MIX_OFFSET,
  NUMPAD_TARGET_RATE_VALUE,
  NUMPAD_TARGET_EXPO_VALUE,
  NUMPAD_TARGET_ENDPOINT_VALUE,
  NUMPAD_TARGET_ELRS_TX_POWER,
  NUMPAD_TARGET_ELRS_RX_MODEL_ID,
  NUMPAD_TARGET_ELRS_RX_FAILSAFE,
  NUMPAD_TARGET_ELRS_RX_PWM_FAILSAFE,
  NUMPAD_TARGET_TIMER_VALUE
  };

enum StickCalibrationState {
  STICK_CAL_STATE_CAPTURE_CENTER,
  STICK_CAL_STATE_SWEEP_RANGE
};

  enum KeyboardTarget {
  KEYBOARD_TARGET_MODEL_NAME,
  KEYBOARD_TARGET_OTA_STA_SSID,
  KEYBOARD_TARGET_OTA_STA_PASSWORD,
  KEYBOARD_TARGET_OTA_AP_SSID,
  KEYBOARD_TARGET_20Q_LEARN
  };

  enum EspNowLinkFlags : uint8_t {
  ESPNOW_FLAG_BATTERY_PRESENT = 0x01,
  ESPNOW_FLAG_BATTERY_CHARGING = 0x02,
  ESPNOW_FLAG_OUTPUT_IBUS = 0x80
  };

  enum EspNowReceiverOutputMode : uint8_t {
  ESPNOW_OUTPUT_PWM = 0,
  ESPNOW_OUTPUT_IBUS = 1
  };

  struct __attribute__((packed)) EspNowControlPacket {
  uint16_t magic;
  uint8_t version;
  uint8_t messageType;
  uint32_t sequence;
  uint32_t txMillis;
  uint8_t modelIndex;
  uint8_t driveType;
  uint8_t tankMode;
  uint8_t flags;
  int16_t channels[CHANNEL_COUNT];
  uint16_t txBatteryMillivolts;
  uint16_t linkLatencyMs;
  char modelName[20];
  };

  struct __attribute__((packed)) EspNowTelemetryPacket {
  uint16_t magic;
  uint8_t version;
  uint8_t messageType;
  uint32_t sequence;
  uint32_t echoedTxMillis;
  uint16_t receiverBatteryMillivolts;
  uint16_t receiverPacketAgeMs;
  uint8_t flags;
  };

  struct __attribute__((packed)) EspNowPingPacket {
  uint16_t magic;
  uint8_t version;
  uint8_t messageType;
  uint32_t sequence;
  uint32_t txMillis;
  };

  struct __attribute__((packed)) EspNowPingAckPacket {
  uint16_t magic;
  uint8_t version;
  uint8_t messageType;
  uint32_t sequence;
  uint32_t echoedTxMillis;
  };

  struct __attribute__((packed)) EspNowBindPacket {
  uint16_t magic;
  uint8_t version;
  uint8_t messageType;
  uint32_t token;
  uint8_t flags;
  };

  struct MixData {
  bool enabled;
  uint8_t source;
  uint8_t destination;
  int8_t rate;
  int8_t offset;
  };

  struct ModelData {
  char name[20];

  bool reverse[CHANNEL_COUNT];
  bool failsafe[CHANNEL_COUNT];

  int trimX[2];
  int trimY[2];
  MixData mixes[MIX_COUNT];
  uint8_t driveType;
  };

  static_assert((MAX_MODELS * sizeof(ModelData)) < EEPROM_STICK_CAL_MAX_DATA_ADDR,
                "EEPROM model data overlaps settings storage");

  struct BattleEnemy {
  float x;
  float y;
  bool alive;
  uint8_t type;
  unsigned long nextShotAt;
  };

  struct BattleObstacle {
  float x;
  float y;
  float radius;
  bool pyramid;
  };

  struct TwentyQCandidate {
  const char *name;
  const char *category;
  int8_t answers[20];
  };

  struct TwentyQLearnedCandidate {
  char name[28];
  int8_t answers[20];
  };

  struct AsteroidsRock {
  float x;
  float y;
  float vx;
  float vy;
  float radius;
  uint8_t size;
  bool active;
  };

  struct AsteroidsBullet {
  float x;
  float y;
  float vx;
  float vy;
  unsigned long expiresAt;
  bool active;
  };

  struct StarSwarmEnemy {
  float x;
  float y;
  float homeX;
  float homeY;
  float startX;
  float startY;
  float t;
  int oldX;
  int oldY;
  unsigned long spawnAt;
  bool active;
  bool wasDrawn;
  uint8_t row;
  uint8_t path;
  uint8_t state; // 0 entering, 1 formation, 2 diving
  };

  struct StarSwarmShot {
  float x;
  float y;
  float vx;
  float vy;
  int oldX;
  int oldY;
  bool active;
  bool wasDrawn;
  };

  struct StarSwarmPowerup {
  float x;
  float y;
  float vy;
  int oldX;
  int oldY;
  bool active;
  bool wasDrawn;
  uint8_t type; // 0 spread shot, 1 speed boost
  };

  ModelData models[MAX_MODELS];
  int activeModel = 0;

  String currentModelName = "No Model";
  WiFiServer otaHttpServer(80);

  volatile int espNowLatency = 0;
  volatile float telemetryVoltage = 0.0;
  float transmitterBatteryVoltage = 0.0;
  bool espNowReady = false;
  bool espNowProtocolActive = false;
  bool espNowBindingMode = false;
  bool espNowOutputModePromptVisible = false;
  bool espNowOutputModePromptDirty = false;
  unsigned long espNowProtocolStartTime = 0;
  unsigned long lastEspNowSendTime = 0;
  unsigned long lastEspNowPingTime = 0;
  volatile unsigned long lastEspNowTelemetryTime = 0;
  volatile unsigned long lastEspNowAliveTime = 0;
  bool otaModeActive = false;
  bool otaServiceReady = false;
  bool otaUsingSoftAP = false;
  bool otaApReady = false;
  bool otaStaAutoConnectEnabled = false;
  bool otaStaConnectPending = false;
  bool otaUpdateInProgress = false;
  bool otaRestartPending = false;
  unsigned long otaStaAutoConnectAfterMs = 0;
  unsigned long otaStaConnectStartTime = 0;
  unsigned long espNowBindingStartTime = 0;
  unsigned long espNowBindSuccessTime = 0;
  unsigned long espNowUnbindTime = 0;
  unsigned long espNowLastBindCommitTime = 0;
  uint32_t espNowSequence = 0;
  uint32_t espNowPingSequence = 0;
  uint32_t espNowBindingToken = 0;
  esp_now_send_status_t lastEspNowSendStatus = ESP_NOW_SEND_FAIL;
  uint8_t boundReceiverMacs[MAX_MODELS][ESP_NOW_ETH_ALEN] = {};
  uint8_t espNowReceiverOutputModes[MAX_MODELS] = {};
  uint8_t espNowPendingBindMac[ESP_NOW_ETH_ALEN] = { 0, 0, 0, 0, 0, 0 };
  const uint8_t espNowBroadcastAddress[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
  HardwareSerial &elrsSerial = Serial1;
  HardwareSerial elrsHostSniffSerial(2);
  struct ElrsSniffStats {
    uint32_t byteCount = 0;
    uint32_t frameCount = 0;
    uint32_t type16Count = 0;
    uint32_t type2CCount = 0;
    uint32_t type2DCount = 0;
    uint32_t type28Count = 0;
    uint32_t type29Count = 0;
    uint32_t type2BCount = 0;
    uint32_t type2ECount = 0;
    uint32_t type32Count = 0;
    uint32_t type3ACount = 0;
    uint32_t otherCount = 0;
    uint8_t lastType = 0;
    uint8_t recentBytes[16] = {};
    uint8_t recentByteIndex = 0;
    uint8_t frame[64] = {};
    uint8_t frameLen = 0;
    bool frameUpdated = false;
  };
  ElrsSniffStats elrsHostToModuleSniff;
  ElrsSniffStats elrsModuleToHostSniff;
  bool elrsInitialized = false;
  bool elrsProtocolActive = false;
  volatile bool elrsModulePresent = false;
  volatile bool elrsLinkActive = false;
  volatile uint8_t elrsUplinkLq = 0;
  volatile int8_t elrsUplinkSnr = 0;
  volatile uint8_t elrsUplinkRssi1 = 0;
  volatile uint8_t elrsUplinkRssi2 = 0;
  volatile uint32_t elrsRxByteCount = 0;
  volatile uint32_t elrsRxFrameCount = 0;
  volatile uint32_t elrsDeviceInfoCount = 0;
  volatile uint32_t elrsRxEdgeCount = 0;
  volatile uint32_t elrsType16Count = 0;
  volatile uint32_t elrsType14Count = 0;
  volatile uint32_t elrsType1CCount = 0;
  volatile uint32_t elrsType1DCount = 0;
  volatile uint32_t elrsType29Count = 0;
  volatile uint32_t elrsType2BCount = 0;
  volatile uint32_t elrsType2ECount = 0;
  volatile uint32_t elrsType3ACount = 0;
  volatile uint32_t elrsTypeOtherCount = 0;
  volatile uint8_t elrsLastFrameType = 0;
  volatile uint8_t elrsLastOtherFrameType = 0;
  volatile uint32_t elrsSyncC8Count = 0;
  volatile uint32_t elrsSyncEECount = 0;
  volatile uint32_t elrsSyncEACount = 0;
  volatile uint32_t elrsSyncECCount = 0;
  volatile uint32_t elrsPrintableByteCount = 0;
  volatile uint8_t elrsRecentBytes[16] = {};
  volatile uint8_t elrsRecentByteIndex = 0;
  volatile uint8_t elrsLast2EFrame[32] = {};
  volatile uint8_t elrsLast2EFrameLen = 0;
  unsigned long lastElrsTxTime = 0;
  unsigned long lastElrsDevicePingTime = 0;
  unsigned long elrsBindCommandSentTime = 0;
  unsigned long elrsBindSuccessTime = 0;
  unsigned long elrsBindAwaitUntil = 0;
  unsigned long elrsBindBurstUntil = 0;
  unsigned long lastElrsBindBurstSendTime = 0;
  bool elrsBindAwaitingResult = false;
  uint8_t elrsBindFieldIndex = 0;
  bool elrsBindFieldKnown = false;
  uint8_t elrsTxPowerFieldIndex = 0;
  bool elrsTxPowerFieldKnown = false;
  bool elrsTxPowerFieldIsSelection = false;
  bool elrsTxPowerSelectionOneBased = false;
  uint8_t elrsTxPowerOptionCount = 0;
  uint16_t elrsTxPowerOptionsMw[16] = {};
  uint16_t elrsTxPowerMw = 100;
  uint16_t elrsTxPowerPendingMw = 100;
  bool elrsTxPowerWritePending = false;
  bool elrsTxPowerWritePersistPending = true;
  bool elrsLowPowerFallbackActive = false;
  bool elrsTxPowerUserOverrideActive = false;
  unsigned long elrsNoLinkPowerTimerStart = 0;
  uint16_t modelElrsTxPowerMw[MAX_MODELS] = {100, 100, 100, 100};
  uint8_t elrsBindCommandStep = 0;
  uint8_t elrsBindCommandTimeout = 0;
  uint8_t elrsParameterCount = 0;
  uint8_t elrsParameterVersion = 0;
  uint8_t elrsParamScanIndex = 0;
  unsigned long lastElrsParamReadTime = 0;
  unsigned long lastElrsBaudRetryTime = 0;
  volatile unsigned long lastElrsSerialRxTime = 0;
  volatile unsigned long lastElrsLinkStatsTime = 0;
  volatile unsigned long lastElrsDeviceInfoTime = 0;
  char elrsDeviceName[16] = "";
  char elrsBindCommandInfo[32] = "";
  const bool elrsInvertModes[][2] = {
    {false, false}, // RX normal, TX normal
    {true,  false}, // RX inverted, TX normal
    {true,  true}   // RX inverted, TX inverted
  };
  const int elrsInvertModeCount = (int)(sizeof(elrsInvertModes) / sizeof(elrsInvertModes[0]));
  int elrsInvertModeIndex = 0;
  bool elrsRxInvert = false;
  bool elrsTxInvert = false;
  const uint32_t elrsBaudCandidates[] = {ELRS_FORCE_BAUD};
  const int elrsBaudCandidateCount = (int)(sizeof(elrsBaudCandidates) / sizeof(elrsBaudCandidates[0]));
  int elrsBaudIndex = 0;
  uint32_t elrsActiveBaud = 0;
  const uint8_t elrsPinPairs[][2] = {
    {ELRS_UART_TX_PIN_DEFAULT, ELRS_UART_RX_PIN_DEFAULT}, // expected wiring
    {ELRS_UART_RX_PIN_DEFAULT, ELRS_UART_TX_PIN_DEFAULT}  // fallback swapped wiring
  };
  const int elrsPinPairCount = (int)(sizeof(elrsPinPairs) / sizeof(elrsPinPairs[0]));
  int elrsPinPairIndex = 0;
  uint8_t elrsActiveTxPin = ELRS_UART_TX_PIN_DEFAULT;
  uint8_t elrsActiveRxPin = ELRS_UART_RX_PIN_DEFAULT;
  uint8_t elrsActiveModuleProfile =
    (ELRS_MODULE_PROFILE == ELRS_MODULE_PROFILE_RANGER_NANO) ? ELRS_MODULE_PROFILE_RANGER_NANO : ELRS_MODULE_PROFILE_INTERNAL;
  bool elrsRuntimeHalfDuplex = (ELRS_MODULE_PROFILE == ELRS_MODULE_PROFILE_RANGER_NANO);
  bool elrsAutoDetectLocked = (ELRS_MODULE_PROFILE != ELRS_MODULE_PROFILE_AUTO);
  bool elrsNoModuleDetected = false;
  unsigned long elrsProfileDetectStartTime = 0;
  uint32_t elrsProfileDetectStartResponses = 0;
  bool elrsRxInterruptAttached = false;
  enum ElrsReceiverConfigState : uint8_t {
    RX_CONFIG_IDLE,
    RX_CONFIG_CONNECTING,
    RX_CONFIG_FETCHING,
    RX_CONFIG_DONE,
    RX_CONFIG_FAILED
  };
  bool elrsReceiverConfigActive = false;
  ElrsReceiverConfigState elrsReceiverConfigState = RX_CONFIG_IDLE;
  unsigned long elrsReceiverConfigStartTime = 0;
  unsigned long elrsReceiverConfigLastUiTime = 0;
  char elrsReceiverConfigStatus[72] = "Idle";
  char elrsReceiverConfigDetail[96] = "";
  char elrsReceiverConfigLastEndpoint[24] = "";
  char elrsReceiverProductName[44] = "-";
  char elrsReceiverLuaName[18] = "-";
  char elrsReceiverUidText[28] = "-";
  int elrsReceiverWifiInterval = -1;
  int elrsReceiverUartBaud = -1;
  int elrsReceiverFailsafe = -1;
  int elrsReceiverModelId = -1;
  bool elrsReceiverHasFailsafe = false;
  bool elrsReceiverHasModelId = false;
  bool elrsReceiverHasPwmConfig = false;
  int elrsReceiverEditFailsafe = 0;
  int elrsReceiverEditModelId = 255;
  uint8_t elrsReceiverPwmCount = 0;
  uint32_t elrsReceiverPwmRaw[16] = {};
  uint32_t elrsReceiverPwmEditRaw[16] = {};
  int elrsReceiverPwmFailsafeUs[16] = {};
  int elrsReceiverPwmEditFailsafeUs[16] = {};
  int elrsReceiverPwmPins[16] = {};
  bool elrsReceiverPwmEditTouched[16] = {};
  uint32_t elrsReceiverPwmEditGeneration[16] = {};
  int elrsReceiverEditPwmIndex = 0;
  bool elrsReceiverPwmNumpadArmed = false;
  int elrsReceiverPwmNumpadArmedIndex = -1;
  int elrsReceiverPwmNumpadOriginalUs = 0;
  bool elrsReceiverSaveArmed = false;
  bool elrsReceiverConfigTouchLocked = false;
  unsigned long elrsReceiverConfigTouchUnlockAt = 0;
  unsigned long elrsReceiverConfigNoTouchSince = 0;
  bool elrsReceiverConfigDirty = false;
  bool elrsReceiverConfigStaticDirty = true;
  uint32_t elrsReceiverConfigGeneration = 0;
  unsigned long elrsReceiverConfigLoadedAt = 0;
  String elrsReceiverConfigBody = "";

  int focusIndex = 0;
  unsigned long lastDpadTime = 0;
  int controllerSettingsPage = 0;
  int modelSettingsPage = 0;
  const int controllerSettingsPageCount = STICK_CAL_SCREEN_ENABLED ? 3 : 2;

  DriveType currentDrive = DRIVE_TANK;
  bool droneArmActive = false;
  bool droneArmWarningVisible = false;
  bool droneArmWarningTouchLocked = false;
  bool droneArmWarningStaticDirty = false;
  bool droneArmWarningDynamicDirty = false;
  bool droneSafetyThrottleLockActive = false;
  Screen currentScreen = SCREEN_SPLASH;
  ButtonID pressedButton = BTN_NONE;
  ButtonID selectedButton = BTN_NONE;
  bool dpadFocusVisible = false;

  bool uiNeedsRedraw = true;
  bool topBarNeedsRedraw = true;
  bool topBarForceRedraw = true;
  bool touchActive = false;
  bool waitingForRelease = false;
  bool screenChangePending = false;
  bool screenAwake = true;
  bool displayDimmed = false;
  bool screenOffManual = false;
  bool suppressWakeUntilRelease = false;
  bool fullRedraw = true;
  Screen nextScreen;
  Screen lastScreen = SCREEN_SPLASH;
  Screen reverseReturnScreen = SCREEN_CONTROLLER_SETTINGS;
  bool backHoldActive = false;
  bool backHoldTriggered = false;
  bool backHoldSuppressUntilRelease = false;
  unsigned long backHoldStartTime = 0;
  Screen backHoldShortTarget = SCREEN_MAIN;
  #define BACK_HOLD_HOME_MS 1000UL

  enum TimerMode : uint8_t {
    TIMER_MODE_COUNT_DOWN,
    TIMER_MODE_COUNT_UP
  };
  TimerMode timerMode = TIMER_MODE_COUNT_DOWN;
  bool timerRunning = false;
  bool timerNeedsRedraw = true;
  bool timerStaticNeedsRedraw = true;
  unsigned long timerRunStartedAt = 0;
  unsigned long timerAccumulatedMs = 0;
  unsigned long timerLastUiTickMs = 0;
  uint32_t timerTargetSeconds = 0;
  String timerInputBuffer = "";
  char lastTimerDisplayText[12] = "";
  TimerMode lastTimerDrawMode = TIMER_MODE_COUNT_DOWN;
  bool lastTimerDrawRunning = false;
  bool lastTimerAlarmActive = false;
  bool lastTimerAlarmVisible = true;
  bool mainSwipeActive = false;
  bool mainSwipeConsumedTouch = false;
  int mainSwipeStartX = 0;
  int mainSwipeStartY = 0;
  int mainSwipeLastX = 0;
  int mainSwipeLastY = 0;
  unsigned long mainSwipeStartTime = 0;
  #define MAIN_SWIPE_MIN_DX 70
  #define MAIN_SWIPE_MAX_DY 70
  #define MAIN_SWIPE_MAX_MS 900UL
  #define TIMER_REFRESH_MS 200UL
  #define TIMER_ALARM_BLINK_MS 500UL
  #define TIMER_KEY_MAX_DIGITS 4
  #define TIMER_MAX_SECONDS (99UL * 60UL + 59UL)
  #define TIMER_DISPLAY_X 14
  #define TIMER_DISPLAY_Y 62
  #define TIMER_DISPLAY_W 212
  #define TIMER_DISPLAY_H 82
  #define TIMER_MODE_Y 154
  #define TIMER_MODE_W 98
  #define TIMER_MODE_H 28
  #define TIMER_COUNT_DOWN_X 14
  #define TIMER_COUNT_UP_X 128
  #define TIMER_ACTION_Y 194
  #define TIMER_ACTION_W 98
  #define TIMER_ACTION_H 28
  #define TIMER_START_X 14
  #define TIMER_RESET_X 128

  bool powerButtonPressed = false;
  bool powerButtonShutdownPending = false;
  bool powerButtonSuppressShortPressUntilRelease = false;
  bool powerLedOn = false;
  bool powerLedBreathing = false;
  uint8_t powerLedDuty = 0;
  unsigned long powerLedBreatheStartTime = 0;
  unsigned long powerButtonPressedAt = 0;

  enum StatusRgbDiagCode : uint8_t {
    STATUS_RGB_OK = 0,
    STATUS_RGB_NO_BATTERY,
    STATUS_RGB_NO_ADS,
    STATUS_RGB_NO_ELRS,
    STATUS_RGB_NO_MCP23017,
    STATUS_RGB_DPAD_SELECT,
    STATUS_RGB_DPAD_LEFT,
    STATUS_RGB_DPAD_UP,
    STATUS_RGB_DPAD_DOWN,
    STATUS_RGB_DPAD_RIGHT
  };
  StatusRgbDiagCode currentStatusRgbDiagCode = STATUS_RGB_OK;
  StatusRgbDiagCode lastStatusRgbDiagCode = STATUS_RGB_OK;
  uint8_t lastStatusRgbStep = 255;
  uint8_t lastStatusRgbHalfStep = 255;
  uint16_t lastStatusRgbActiveMask = 0;
  unsigned long statusRgbCycleStartTime = 0;
  bool lastStatusRgbPauseActive = false;

  unsigned long lastActivityTime = 0;
  unsigned long splashStartTime = 0;
  unsigned long startupDeviceDiscoveryStartTime = 0;
  bool startupDeviceDiscoveryComplete = false;
  bool startupDiscoveryScreenCleared = false;
  bool startupThrottleSafetyCleared = false;
  bool startupThrottleSafetyScreenDrawn = false;
  bool startupThrottleBypassTouchActive = false;
  uint8_t startupThrottleBypassTapCount = 0;
  unsigned long lastStartupThrottleSafetyDrawTime = 0;
  bool autoDeepSleepWarningVisible = false;
  bool autoDeepSleepWarningScreenDrawn = false;
  int autoDeepSleepLastWarningSeconds = -1;
  unsigned long fpsLastTime = 0;
  int frameCount = 0;
  bool splashDone = false;
  int8_t modelFailsafeValues[MAX_MODELS][CHANNEL_COUNT] = {};
  uint8_t modelRateValues[MAX_MODELS][CHANNEL_COUNT] = {};
  int8_t modelExpoValues[MAX_MODELS][CHANNEL_COUNT] = {};
  uint8_t modelEndpointLowValues[MAX_MODELS][CHANNEL_COUNT] = {};
  uint8_t modelEndpointHighValues[MAX_MODELS][CHANNEL_COUNT] = {};
  uint8_t modelTankLeftEscChannel[MAX_MODELS] = {1, 1, 1, 1};   // default left track -> CH2
  uint8_t modelTankRightEscChannel[MAX_MODELS] = {0, 0, 0, 0};  // default right track -> CH1
  uint8_t escSetupStep = 0;                // 0 = choose left ESC, 1 = choose right ESC
  uint8_t escSetupPendingLeftChannel = 1;  // channel index 0..3

  #define DISPLAY_BL_PIN 45
  #define DISPLAY_BL_PWM_FREQ 1000
  #define DISPLAY_BL_PWM_BITS 8
  #define BRIGHTNESS_ON 255
  #define BRIGHTNESS_OFF 0
  #define DISPLAY_BACKLIGHT_SERIAL_DEBUG false
  #define STICK_CAL_USE_HARDCODED_DEFAULTS true
  #define STARTUP_THROTTLE_SAFETY_ENABLED true
  #define STARTUP_THROTTLE_SAFETY_CHANNEL 2
  #define STARTUP_THROTTLE_SAFE_THRESHOLD -0.85f
  #define STARTUP_THROTTLE_SCREEN_REFRESH_MS 150UL
  #define STARTUP_THROTTLE_BYPASS_TAPS 8
  #define STARTUP_THROTTLE_BYPASS_Y_MIN 232
  #define ELRS_MODULE_PROBE_TIMEOUT_MS 5000UL
  #define POWER_BUTTON_ENABLED true
  #define POWER_BUTTON_SENSE_PIN 14
  #define POWER_BUTTON_REF_PIN 21
  #define POWER_BUTTON_HOLD_MS 3000UL
  #define POWER_LED_ENABLED false
  #define POWER_LED_POS_PIN 14
  #define POWER_LED_NEG_PIN 21
  #define POWER_LED_SHUTDOWN_BLINK_MS 250UL
  #define POWER_LED_PWM_FREQ 500
  #define POWER_LED_PWM_BITS 8
  #define POWER_LED_SLEEP_BREATHE_PERIOD_MS 3000UL
  #define POWER_LED_SLEEP_MIN_DUTY 8
  #define POWER_LED_SLEEP_MAX_DUTY 150
  #define STATUS_RGB_LED_ENABLED true
  #define STATUS_RGB_LED_PIN 42
  #define STATUS_RGB_LED_BRIGHTNESS 32
  #define STATUS_RGB_LED_STEP_MS 1000UL
  #define STATUS_RGB_LED_REPEAT_GAP_MS 500UL
  #define STATUS_RGB_LED_CODE_PAUSE_MS 2000UL
  #define STATUS_RGB_DPAD_DIAGNOSTICS true
  #define STATUS_RGB_PERIPHERAL_ERRORS_ENABLED 0
  #define SOFT_POWER_PREF_NAMESPACE "power"
  #define SOFT_POWER_PREF_KEY "softOff"
  #define AUDIO_ENABLED true
  #define AUDIO_CODEC_I2C_ADDR 0x18
  #define AUDIO_SAMPLE_RATE 48000
  #define AUDIO_DEFAULT_USER_VOLUME_PERCENT 55
  #define AUDIO_PCM_HEADROOM_PERCENT 85
  #define AUDIO_PREF_NAMESPACE "audio"
  #define AUDIO_PREF_VOLUME_KEY "volume"
  #define AUDIO_PREF_MUTED_KEY "muted"
  #define AUDIO_PREF_PRE_MUTE_KEY "preMute"
  #define AUDIO_CLICK_AMPLITUDE 30000
  #define AUDIO_TONE_AMPLITUDE 32000
  #define AUDIO_PA_ENABLE_PIN 1
  #define AUDIO_PA_ENABLE_LEVEL LOW
  #define AUDIO_I2S_MCLK_PIN 4
  #define AUDIO_I2S_BCLK_PIN 5
  // Playback feeds the ES8311 DSDIN input on GPIO8. GPIO6 is the codec's
  // microphone/ADC output and must not be used as the ESP32 playback output.
  #define AUDIO_I2S_PLAYBACK_DATA_PIN 8
  #define AUDIO_I2S_MIC_DATA_PIN 6
  #define AUDIO_I2S_WS_PIN 7
  #define AUDIO_CLICK_MIN_INTERVAL_MS 35UL
  #define AUDIO_STARTUP_TEST_TONE false
  #define AUDIO_USE_SQUARE_TEST_TONE false
  #define AUDIO_AMP_SETTLE_MS 25
  #define AUDIO_ZERO_FLUSH_FRAMES 96
  #define AUDIO_DRAIN_SILENCE_MS 30
  #define AUDIO_RETEST_DELAY_MS 3000UL
  #define STARTUP_AUDIO_ENABLED true
  #define STARTUP_AUDIO_CHUNK_FRAMES 512
  #define STARTUP_SPLASH_DURATION_MS CAVE_JOHNSON_STARTUP_DURATION_MS
  #define STARTUP_DEVICE_DISCOVERY_MS 5000UL

  const uint8_t displayBrightnessOptions[] = {48, 80, 112, 144, 176, 208, 232, 255};
  const uint8_t displaySleepBrightnessOptions[] = {0, 12, 24, 40, 56, 72, 96, 128};
  const uint16_t displayTimeoutOptionsSec[] = {10, 15, 30, 60, 120, 300, 0};
  const uint16_t displayOffTimeoutOptionsSec[] = {5, 10, 15, 30, 60, 120, 0};
  const uint8_t displayBrightnessOptionCount = (uint8_t)(sizeof(displayBrightnessOptions) / sizeof(displayBrightnessOptions[0]));
  const uint8_t displaySleepBrightnessOptionCount = (uint8_t)(sizeof(displaySleepBrightnessOptions) / sizeof(displaySleepBrightnessOptions[0]));
  const uint8_t displayTimeoutOptionCount = (uint8_t)(sizeof(displayTimeoutOptionsSec) / sizeof(displayTimeoutOptionsSec[0]));
  const uint8_t displayOffTimeoutOptionCount = (uint8_t)(sizeof(displayOffTimeoutOptionsSec) / sizeof(displayOffTimeoutOptionsSec[0]));
  const uint8_t DEFAULT_DISPLAY_BRIGHTNESS = BRIGHTNESS_ON;
  const uint8_t DEFAULT_DISPLAY_SLEEP_BRIGHTNESS = 128; // ~50% of 255
  const uint8_t DEFAULT_DISPLAY_TIMEOUT_INDEX = 3;      // 1 minute
  const uint8_t DEFAULT_DISPLAY_OFF_TIMEOUT_INDEX = 5;  // 2 minutes
  const uint8_t THEME_DARK = 0;
  const uint8_t THEME_LIGHT = 1;
  uint8_t displayBrightness = DEFAULT_DISPLAY_BRIGHTNESS;
  uint8_t displaySleepBrightness = DEFAULT_DISPLAY_SLEEP_BRIGHTNESS;
  uint8_t displayTimeoutIndex = DEFAULT_DISPLAY_TIMEOUT_INDEX;
  uint8_t displayOffTimeoutIndex = DEFAULT_DISPLAY_OFF_TIMEOUT_INDEX;
  uint8_t themeMode = THEME_DARK;
  uint8_t lastAppliedBacklightBrightness = 255;
  bool panelDisplayEnabled = true;
  bool stickBaseSpriteNeedsRebuild = true;
#if AUDIO_ENABLED
  DriverPins hosyondAudioPins;
  AudioBoard hosyondAudioBoard(AudioDriverES8311, hosyondAudioPins);
  AudioBoardStream hosyondAudioOutput(hosyondAudioBoard);
  bool audioReady = false;
  bool audioCodecReady = false;
  bool audioI2sReady = false;
  bool audioStartupRetestPending = false;
  unsigned long audioStartupRetestTime = 0;
  bool startupAudioPlaying = false;
  bool startupAudioStarted = false;
  uint32_t startupAudioSampleIndex = 0;
  uint32_t startupAudioSourcePhase = 0;
  uint8_t audioUserVolumePercent = AUDIO_DEFAULT_USER_VOLUME_PERCENT;
  uint8_t audioVolumeBeforeMutePercent = AUDIO_DEFAULT_USER_VOLUME_PERCENT;
  bool audioUserMuted = false;
  bool audioVolumePanelVisible = false;
  bool audioSettingsDirty = false;
  unsigned long lastAudioClickMs = 0;
#endif

  #define TARGET_FPS 24
  #define MAIN_FRAME_INTERVAL_MS (1000UL / TARGET_FPS)
  #define TOUCH_POLL_INTERVAL_MS 16
  #define MIX_PAGE_TOGGLE_DEBOUNCE_MS 450UL
  #define MODEL_NAME_HOLD_MS 700
  #define BATTERY_ADC_PIN 9
  #define I2C_SDA_PIN 16
  #define I2C_SCL_PIN 15
  #define PERIPHERAL_POWER_SWITCHES_ENABLED true
  #define UART_5V_ENABLE_PIN 2
  #define I2C_3V3_ENABLE_PIN 3
  #define PERIPHERAL_POWER_ENABLE_ACTIVE_HIGH true
  #define BQ25887_I2C_ADDR 0x6B
  #define BQ25887_REG_CHARGER_CONTROL1 0x05
  #define BQ25887_REG_STATUS1 0x0B
  #define BQ25887_REG_STATUS2 0x0C
  #define BQ25887_REG_FAULT 0x0E
  #define BQ25887_REG_ADC_CONTROL 0x15
  #define BQ25887_REG_VBAT_ADC_MSB 0x1D
  #define BQ25887_REG_PART_INFO 0x25
  #define ADS1115_PRIMARY_I2C_ADDR 0x48
  #define ADS1115_AUX_I2C_ADDR 0x49
  #define ADS1115_TOTAL_ANALOG_INPUTS 8
  #define ADS1115_PRIMARY_ANALOG_COUNT 4
  #define MCP23017_I2C_ADDR 0x20
  #define MCP23017_RECONNECT_INTERVAL_MS 1000UL
  #define MCP23017_PROBE_TIMEOUT_MS 5000UL
  #define MCP23017_SAMPLE_INTERVAL_MS 10UL
  #define I2C_TRANSACTION_TIMEOUT_MS 10U
  #define I2C_STARTUP_SCAN_ENABLED false
  #define MCP23017_IODIRA_REG 0x00
  #define MCP23017_IODIRB_REG 0x01
  #define MCP23017_GPPUA_REG 0x0C
  #define MCP23017_GPPUB_REG 0x0D
  #define MCP23017_GPIOA_REG 0x12
  #define MCP23017_GPIOB_REG 0x13
  #define MCP23017_DPAD_SELECT_BIT 0
  #define MCP23017_DPAD_LEFT_BIT 1
  #define MCP23017_DPAD_UP_BIT 2
  #define MCP23017_DPAD_DOWN_BIT 3
  #define MCP23017_DPAD_RIGHT_BIT 4
  #define MCP23017_SWITCH_LOW_BIT 5
  #define MCP23017_SWITCH_HIGH_BIT 6
  #define ADS1115_REG_CONVERSION 0x00
  #define ADS1115_REG_CONFIG 0x01
  #define ADS1115_GAIN_4_096V 0x0200
  #define ADS1115_MODE_SINGLE 0x0100
  #define ADS1115_DATA_RATE_860 0x00E0
  #define ADS1115_COMP_DISABLE 0x0003
  #define ADS1115_JOYSTICK_MAX_COUNT 26400
  #define STICK_SAMPLE_INTERVAL_MS 20
  #define ADS1115_RECONNECT_INTERVAL_MS 1000
  #define ADS1115_PROBE_TIMEOUT_MS 5000UL
  #define ADS1115_MAX_CONSECUTIVE_READ_FAILS 4
  #define ADS1115_DEFAULT_MIN_COUNT 0
  #define ADS1115_DEFAULT_MAX_COUNT ADS1115_JOYSTICK_MAX_COUNT
  const int16_t STICK_CAL_HARDCODED_MIN[STICK_AXIS_COUNT] = {
    4494, 3062, 3103, 3499
  };
  // A1/CH3 is a throttle-style axis; use the measured endpoints with a
  // synthetic midpoint so it can still reach true 0% and 100%.
  const int16_t STICK_CAL_HARDCODED_CENTER[STICK_AXIS_COUNT] = {14925, 12816, 12786, 12935};
  const int16_t STICK_CAL_HARDCODED_MAX[STICK_AXIS_COUNT] = {
    24224, 22571, 22063, 22541
  };
  #define STICK_DEADZONE 0.07f
  #define STICK_FILTER_ALPHA 0.35f
  #define STICK_DISPLAY_WAKE_DELTA 0.12f
  #define BATTERY_DIVIDER_RATIO 2.0f
  #define BATTERY_PRESENT_MIN_V 2.50f
  #define BATTERY_EMPTY_V 3.30f
  #define BATTERY_FULL_V 4.20f
  #define BATTERY_2S_PRESENT_MIN_V 5.00f
  #define BATTERY_2S_EMPTY_V 6.60f
  #define BATTERY_2S_FULL_V 8.40f
  #define BATTERY_2S_LOW_WARN_V 7.00f
  #define BATTERY_2S_CRITICAL_SLEEP_V 6.60f
  #define BATTERY_SAMPLE_INTERVAL_MS 500
  #define BATTERY_CHARGE_WINDOW_MS 5000
  #define BATTERY_CHARGING_DELTA_V 0.006f
  #define BATTERY_CHARGING_HOLD_MS 30000
  #define BATTERY_LOW_WARN_V 3.50f
  #define BATTERY_CRITICAL_SLEEP_V 3.30f
  #define BATTERY_CRITICAL_GRACE_MS 10000UL
  #define BATTERY_LOW_WARNING_REPEAT_MS 60000UL
  #define AUTO_DEEP_SLEEP_ENABLED true
  #define AUTO_DEEP_SLEEP_TIMEOUT_MS 300000UL
  #define AUTO_DEEP_SLEEP_WARNING_MS 30000UL

  // ==== BOTTOM NAV BAR ====
  #define FOOTER_H 36
  #define FOOTER_Y (320 - FOOTER_H)
  #define NAV_BTN_HEIGHT 30
  #define NAV_BTN_Y_OFFSET 3

  // ===== UI LAYOUT =====
  #define BTN_HEIGHT 50
  #define BTN_RADIUS 12

  // Menu button
  #define MENU_BTN_X 60
  #define MENU_BTN_Y (FOOTER_Y + NAV_BTN_Y_OFFSET)
  #define MENU_BTN_W 120
  #define MENU_BTN_H NAV_BTN_HEIGHT

  // Main-screen audio control beside the centered Menu button.
  #define VOLUME_BTN_X 190
  #define VOLUME_BTN_Y MENU_BTN_Y
  #define VOLUME_BTN_W 40
  #define VOLUME_BTN_H MENU_BTN_H
  #define VOLUME_PANEL_X 18
  #define VOLUME_PANEL_Y 222
  #define VOLUME_PANEL_W 204
  #define VOLUME_PANEL_H 54
  #define VOLUME_MUTE_X 26
  #define VOLUME_MUTE_Y 232
  #define VOLUME_MUTE_W 32
  #define VOLUME_MUTE_H 34
  #define VOLUME_SLIDER_X 72
  #define VOLUME_SLIDER_Y 244
  #define VOLUME_SLIDER_W 134
  #define VOLUME_SLIDER_H 12

  // Back button
  #define BACK_BTN_X 10
  #define BACK_BTN_Y (FOOTER_Y + NAV_BTN_Y_OFFSET)
  #define BACK_BTN_W 100
  #define BACK_BTN_H NAV_BTN_HEIGHT

  // Menu screen buttons
  #define MENU_BTN_HEIGHT 70

  #define CTRL_BTN_X 20
  #define CTRL_BTN_Y 100
  #define CTRL_BTN_W 200
  #define CTRL_BTN_H MENU_BTN_HEIGHT

  #define MODEL_BTN_X 20
  #define MODEL_BTN_Y 170
  #define MODEL_BTN_W 200
  #define MODEL_BTN_H MENU_BTN_HEIGHT

  #define DISPLAY_MENU_BTN_X 20
  #define DISPLAY_MENU_BTN_Y 228
  #define DISPLAY_MENU_BTN_W 48
  #define DISPLAY_MENU_BTN_H 48

  #define TX_UPDATE_MENU_BTN_X 96
  #define TX_UPDATE_MENU_BTN_Y 228
  #define TX_UPDATE_MENU_BTN_W 48
  #define TX_UPDATE_MENU_BTN_H 48

  #define GAME_BTN_X 172
  #define GAME_BTN_Y 228
  #define GAME_BTN_W 48
  #define GAME_BTN_H 48

  #define GAME_MENU_BTN_X 20
  #define GAME_MENU_BTN_W 200
  #define GAME_MENU_BTN_H 42
  #define GAME_MENU_BATTLEZONE_Y 58
  #define GAME_MENU_20Q_Y 108
  #define GAME_MENU_ASTEROIDS_Y 158
  #define GAME_MENU_INVADERS_Y 208
  #define GAME_MENU_PAGE_BTN_X 130
  #define GAME_MENU_PAGE_BTN_Y BACK_BTN_Y
  #define GAME_MENU_PAGE_BTN_W 100
  #define GAME_MENU_PAGE_BTN_H BACK_BTN_H
  #define GAME_MENU_ITEMS_PER_PAGE 4
  #define TWENTYQ_MAX_QUESTIONS 20
  #define TWENTYQ_MAX_LEARNED 8
  #define TWENTYQ_LEARNED_NAME_LEN 28
  #define TWENTYQ_PREF_NAMESPACE "twentyq"
  #define TWENTYQ_PREF_COUNT_KEY "count"
  #define TWENTYQ_ANSWER_COUNT 3
  #define TWENTYQ_BACK_X 8
  #define TWENTYQ_BACK_Y 6
  #define TWENTYQ_BACK_W 56
  #define TWENTYQ_BACK_H 20
  #define TWENTYQ_PANEL_X 12
  #define TWENTYQ_PANEL_Y 58
  #define TWENTYQ_PANEL_W 216
  #define TWENTYQ_PANEL_H 142
  #define TWENTYQ_BTN_Y 218
  #define TWENTYQ_BTN_W 68
  #define TWENTYQ_BTN_H 38
  #define TWENTYQ_YES_X 10
  #define TWENTYQ_UNSURE_X 86
  #define TWENTYQ_NO_X 162
  #define TWENTYQ_RESTART_X 42
  #define TWENTYQ_RESTART_Y 264
  #define TWENTYQ_RESTART_W 156
  #define TWENTYQ_RESTART_H 32
  #define TWENTYQ_FEEDBACK_Y 218
  #define TWENTYQ_FEEDBACK_W 92
  #define TWENTYQ_FEEDBACK_H 38
  #define TWENTYQ_CORRECT_X 22
  #define TWENTYQ_WRONG_X 126

  #define DISPLAY_SETTINGS_ROW_X 12
  #define DISPLAY_SETTINGS_ROW_W 216
  #define DISPLAY_SETTINGS_ROW_H 42
  #define DISPLAY_SETTINGS_BRIGHTNESS_Y 64
  #define DISPLAY_SETTINGS_TIMEOUT_Y 114
  #define DISPLAY_SETTINGS_OFF_TIMEOUT_Y 164
  #define DISPLAY_SETTINGS_SLEEP_Y 214
  #define DISPLAY_SETTINGS_ADJUST_BTN_W 44
  #define DISPLAY_SETTINGS_ADJUST_BTN_H 28
  #define DISPLAY_SETTINGS_ADJUST_BTN_Y_OFFSET 10
  #define DISPLAY_SETTINGS_MINUS_X 18
  #define DISPLAY_SETTINGS_PLUS_X 178
  #define DISPLAY_SETTINGS_SLEEP_NOW_X 138
  #define DISPLAY_SETTINGS_SLEEP_NOW_Y 258
  #define DISPLAY_SETTINGS_SLEEP_NOW_W 92
  #define DISPLAY_SETTINGS_SLEEP_NOW_H 26
  #define DISPLAY_SETTINGS_THEME_X 20
  #define DISPLAY_SETTINGS_THEME_Y 258
  #define DISPLAY_SETTINGS_THEME_W 200
  #define DISPLAY_SETTINGS_THEME_H 26

  #define SPACE_STATUS_H 28
  #define SPACE_CONTROL_Y 278
  #define SPACE_CONTROL_H 32
  #define SPACE_LEFT_BTN_X 10
  #define SPACE_FIRE_BTN_X 86
  #define SPACE_RIGHT_BTN_X 162
  #define SPACE_CTRL_BTN_W 68
  #define SPACE_CTRL_BTN_H 32
  #define SPACE_EXIT_X 8
  #define SPACE_EXIT_Y 6
  #define SPACE_EXIT_W 56
  #define SPACE_EXIT_H 20
  #define SPACE_FRAME_INTERVAL_MS 33
  #define BZ_VIEW_TOP (SPACE_STATUS_H + 2)
  #define BZ_VIEW_BOTTOM 312
  #define BZ_HORIZON_Y ((BZ_VIEW_TOP + BZ_VIEW_BOTTOM) / 2)
  #define BZ_FOV_SCALE 110.0f
  #define BZ_PLAYER_SPEED 0.055f
  #define BZ_TURN_SPEED 0.07f
  #define BZ_MAX_ENEMIES 5
  #define BZ_MAX_OBSTACLES 8
  #define BZ_SHOT_SPEED 2.6f
  #define BZ_SHOT_RANGE 80.0f
  #define BZ_FIRE_COOLDOWN_MS 220
  #define BZ_RESPAWN_DELAY_MS 1200
  #define BZ_ENEMY_TANK 0
  #define BZ_ENEMY_SUPER 1
  #define BZ_ENEMY_UFO 2
  #define BZ_MISSILE_SPEED 1.35f
  #define BZ_MISSILE_TURN 0.06f

  #define ASTEROIDS_EXIT_X 8
  #define ASTEROIDS_EXIT_Y 6
  #define ASTEROIDS_EXIT_W 56
  #define ASTEROIDS_EXIT_H 20
  #define ASTEROIDS_STATUS_H 28
  #define ASTEROIDS_VIEW_TOP (ASTEROIDS_STATUS_H + 2)
  #define ASTEROIDS_VIEW_BOTTOM 318
  #define ASTEROIDS_FRAME_INTERVAL_MS 33
  #define ASTEROIDS_MAX_ROCKS 10
  #define ASTEROIDS_MAX_BULLETS 5
  #define ASTEROIDS_SHIP_RADIUS 7.0f
  #define ASTEROIDS_TURN_SPEED 0.085f
  #define ASTEROIDS_THRUST 0.115f
  #define ASTEROIDS_DRAG 0.992f
  #define ASTEROIDS_BULLET_SPEED 4.2f
  #define ASTEROIDS_BULLET_TTL_MS 850UL
  #define ASTEROIDS_FIRE_COOLDOWN_MS 190UL
  #define ASTEROIDS_RESPAWN_MS 1200UL

  #define STAR_SWARM_EXIT_X 8
  #define STAR_SWARM_EXIT_Y 6
  #define STAR_SWARM_EXIT_W 56
  #define STAR_SWARM_EXIT_H 20
  #define STAR_SWARM_STATUS_H 28
  #define STAR_SWARM_VIEW_TOP (STAR_SWARM_STATUS_H + 14)
  #define STAR_SWARM_VIEW_BOTTOM 318
  #define STAR_SWARM_ENEMY_W 20
  #define STAR_SWARM_ENEMY_H 14
  #define STAR_SWARM_PLAYER_W 24
  #define STAR_SWARM_PLAYER_H 24
  #define STAR_SWARM_PLAYER_HIT_W 12
  #define STAR_SWARM_PLAYER_HIT_H 12
  #define STAR_SWARM_ENEMY_HIT_W 14
  #define STAR_SWARM_ENEMY_HIT_H 10
  #define STAR_SWARM_SHOT_W 7
  #define STAR_SWARM_SHOT_H 18
  #define STAR_SWARM_FRAME_INTERVAL_MS 33UL
  #define STAR_SWARM_MAX_ENEMIES 24
  #define STAR_SWARM_MAX_PLAYER_SHOTS 8
  #define STAR_SWARM_MAX_ENEMY_SHOTS 5
  #define STAR_SWARM_MAX_POWERUPS 3
  #define STAR_SWARM_PLAYER_START_Y 292
  #define STAR_SWARM_PLAYER_MIN_Y 222
  #define STAR_SWARM_PLAYER_MAX_Y 304
  #define STAR_SWARM_FIRE_COOLDOWN_MS 165UL
  #define STAR_SWARM_ENEMY_FIRE_MS 1350UL
  #define STAR_SWARM_DIVE_INTERVAL_MS 2600UL
  #define STAR_SWARM_FIRST_DIVE_GRACE_MS 3500UL
  #define STAR_SWARM_SPEED_RAMP_MS 35000UL
  #define STAR_SWARM_POWERUP_DROP_PCT 14
  #define STAR_SWARM_RESPAWN_SAFE_MS 2000UL
  #define STAR_SWARM_FLASH_MS 140UL

  #define DUNGEON_EXIT_X 8
  #define DUNGEON_EXIT_Y 6
  #define DUNGEON_EXIT_W 56
  #define DUNGEON_EXIT_H 20
  #define DUNGEON_STATUS_H 28
  #define DUNGEON_HUD_H 21
  #define DUNGEON_HUD_Y (320 - DUNGEON_HUD_H)
  #define DUNGEON_HUD_FACE_X 97
  #define DUNGEON_HUD_FACE_Y (DUNGEON_HUD_Y + 1)
  #define DUNGEON_VIEW_TOP (DUNGEON_STATUS_H + 2)
  #define DUNGEON_VIEW_BOTTOM DUNGEON_HUD_Y
  #define DUNGEON_VIEW_H (DUNGEON_VIEW_BOTTOM - DUNGEON_VIEW_TOP)
  #define DUNGEON_FRAME_INTERVAL_MS 33
  #define DUNGEON_RAY_STEP 2
  #define DUNGEON_RAY_COUNT (240 / DUNGEON_RAY_STEP)
  #define DUNGEON_MAP_W 12
  #define DUNGEON_MAP_H 12
  #define DUNGEON_FOV 1.0471976f
  #define DUNGEON_MOVE_SPEED 0.069f
  #define DUNGEON_STRAFE_SPEED 0.069f
  #define DUNGEON_TURN_SPEED 0.065f
  #define DUNGEON_FIRE_COOLDOWN_MS 220
  #define DUNGEON_FIRE_ANIM_MS 210
  #define DUNGEON_MUZZLE_FLASH_MS 70
  #define DUNGEON_GUN_X 42
  #define DUNGEON_GUN_Y (DUNGEON_VIEW_BOTTOM - 132)
  #define DUNGEON_GUN_W 156
  #define DUNGEON_GUN_H 132
  #define DUNGEON_PISTOL_MUZZLE_SRC_X 84
  #define DUNGEON_PISTOL_FLASH_CENTER_X 20
  #define DUNGEON_IMPACT_MS 130

  #define BACK_TEXT_OFFSET 50

  #define TRIM_CENTER_X 120
  #define TRIM_CENTER_Y 152
  #define TRIM_SIZE     60
  #define TRIM_BTN_SIZE 28
  #define TRIM_BTN_GAP  8
  #define TRIM_VALUE_Y  38
  #define TRIM_RESET_BTN_W 76
  #define TRIM_RESET_BTN_H 30
  #define TRIM_RESET_BTN_X (240 - TRIM_RESET_BTN_W - 10)
  #define TRIM_RESET_BTN_Y (FOOTER_Y - TRIM_RESET_BTN_H - 6)

  #define DRIVE_BTN_W 94
  #define DRIVE_BTN_H 82
  #define DRIVE_BTN_X1 20
  #define DRIVE_BTN_X2 126
  #define DRIVE_BTN_Y1 58
  #define DRIVE_BTN_Y2 148
  #define DRIVE_TOUCH_PAD 8

  #define MIX_TAB_Y        42
  #define MIX_TAB_W        46
  #define MIX_TAB_H        28
  #define MIX_TAB_GAP      8
  #define MIX_TAB_X(i)     (16 + ((i) * (MIX_TAB_W + MIX_TAB_GAP)))

  #define MIX_FIELD_X      120
  #define MIX_FIELD_W      92
  #define MIX_FIELD_H      28
  #define MIX_PAGE_BTN_X   130
  #define MIX_PAGE_BTN_Y   (FOOTER_Y + NAV_BTN_Y_OFFSET)
  #define MIX_PAGE_BTN_W   100
  #define MIX_PAGE_BTN_H   NAV_BTN_HEIGHT
  #define MIX_MINUS_X      120
  #define MIX_PLUS_X       186
  #define MIX_ADJUST_W     26
  #define MIX_VALUE_X      150
  #define MIX_VALUE_W      32
  #define MIX_ROW_ENABLE_Y 84
  #define MIX_ROW_SOURCE_Y 118
  #define MIX_ROW_DEST_Y   146
  #define MIX_ROW_LINK_Y   174
  #define MIX_ROW_RATE_Y   210
  #define MIX_ROW_OFFS_Y   242

  #define MIX_NUMPAD_X     10
  #define MIX_NUMPAD_Y     118
  #define MIX_NUMPAD_W     220
  #define MIX_NUMPAD_H     137
  #define MIX_NUMPAD_BOX_X 22
  #define MIX_NUMPAD_BOX_Y 126
  #define MIX_NUMPAD_BOX_W 132
  #define MIX_NUMPAD_BOX_H 24
  #define MIX_NUMPAD_OK_X  162
  #define MIX_NUMPAD_OK_Y  126
  #define MIX_NUMPAD_OK_W  56
  #define MIX_NUMPAD_OK_H  24
  #define MIX_NUMPAD_KEY_W 64
  #define MIX_NUMPAD_KEY_H 22
  #define MIX_NUMPAD_KEY_GAP 6
  #define MIX_NUMPAD_GRID_X 17
  #define MIX_NUMPAD_GRID_Y 157
  #define PROTOCOL_BIND_BTN_W 72
  #define PROTOCOL_BIND_BTN_H 28
  #define PROTOCOL_BIND_BTN_X (240 - PROTOCOL_BIND_BTN_W - 10)
  #define PROTOCOL_BIND_BTN_Y (FOOTER_Y - PROTOCOL_BIND_BTN_H - 10)
  #define PROTOCOL_MODE_BTN_X 20
  #define PROTOCOL_MODE_BTN_W 200
  #define PROTOCOL_MODE_BTN_H BTN_HEIGHT
  #define PROTOCOL_ELRS_BTN_Y 56
  #define PROTOCOL_ESPNOW_BTN_Y 122
  #define TX_UPDATE_OTA_BTN_W 112
  #define TX_UPDATE_OTA_BTN_H 36
  #define TX_UPDATE_OTA_BTN_X 18
  #define TX_UPDATE_OTA_BTN_Y 148
  #define TX_UPDATE_CFG_BTN_W 82
  #define TX_UPDATE_CFG_BTN_H 36
  #define TX_UPDATE_CFG_BTN_X 140
  #define TX_UPDATE_CFG_BTN_Y TX_UPDATE_OTA_BTN_Y
  #define TX_UPDATE_STATUS_X 12
  #define TX_UPDATE_STATUS_Y 96
  #define TX_UPDATE_STATUS_W 216
  #define TX_UPDATE_STATUS_H 40
  #define PROTOCOL_STATUS_X 14
  #define PROTOCOL_STATUS_Y (PROTOCOL_BIND_BTN_Y - 24)
  #define PROTOCOL_STATUS_W 212
  #define PROTOCOL_STATUS_H 20
  #define ELRS_BIND_BTN_X 20
  #define ELRS_BIND_BTN_Y 132
  #define ELRS_BIND_BTN_W 200
  #define ELRS_BIND_BTN_H 72
  #define ELRS_RX_CONFIG_BTN_X 20
  #define ELRS_RX_CONFIG_BTN_Y 104
  #define ELRS_RX_CONFIG_BTN_W 200
  #define ELRS_RX_CONFIG_BTN_H 24
  #define ELRS_RX_FIELD_X 118
  #define ELRS_RX_FIELD_W 90
  #define ELRS_RX_FIELD_H 24
  #define ELRS_RX_MODEL_ID_Y 146
  #define ELRS_RX_PWM_ROW1_Y 190
  #define ELRS_RX_PWM_ROW2_Y 222
  #define ELRS_RX_PWM_LABEL_W 28
  #define ELRS_RX_PWM_FIELD_W 64
  #define ELRS_RX_PWM_LEFT_LABEL_X 14
  #define ELRS_RX_PWM_LEFT_FIELD_X 50
  #define ELRS_RX_PWM_RIGHT_LABEL_X 124
  #define ELRS_RX_PWM_RIGHT_FIELD_X 160
  #define ELRS_RX_PUSH_FS_X 14
  #define ELRS_RX_PUSH_FS_Y 252
  #define ELRS_RX_PUSH_FS_W 94
  #define ELRS_RX_PUSH_FS_H 28
  #define ELRS_RX_SAVE_X 132
  #define ELRS_RX_SAVE_Y 252
  #define ELRS_RX_SAVE_W 94
  #define ELRS_RX_SAVE_H 28
  #define ELRS_RX_PWM_FAILSAFE_MIN_US 1000
  #define ELRS_RX_PWM_FAILSAFE_MAX_US 2000
  #define ELRS_RX_PWM_FAILSAFE_OFFSET_US 476
  #define ELRS_RX_PWM_FAILSAFE_MASK 0x7FFUL
  #define ELRS_RX_PWM_INPUT_CHANNEL_SHIFT 11
  #define ELRS_RX_PWM_INPUT_CHANNEL_MASK 0x0FUL
  #define ELRS_TX_POWER_MIN_MW 0
  #define ELRS_TX_POWER_MAX_MW 150
  #define ELRS_POWER_SLIDER_X 24
  #define ELRS_POWER_SLIDER_Y 222
  #define ELRS_POWER_SLIDER_W 192
  #define ELRS_POWER_SLIDER_H 14
  #define ELRS_POWER_SLIDER_KNOB_RADIUS 9
  #define ELRS_POWER_VALUE_BOX_X 74
  #define ELRS_POWER_VALUE_BOX_Y 242
  #define ELRS_POWER_VALUE_BOX_W 92
  #define ELRS_POWER_VALUE_BOX_H 28
  #define OTA_HOSTNAME "anubis-tx"
  #define OTA_STA_SSID ""
  #define OTA_STA_PASSWORD ""
  #define OTA_AP_SSID "ANUBIS-OTA"
  #define OTA_AP_PASSWORD "anubisota"
  #define RATE_LOW_VALUE 40
  #define RATE_NORMAL_VALUE 65
  #define RATE_HIGH_VALUE 80
  #define EXPO_LOW_VALUE -40
  #define EXPO_NORMAL_VALUE 0
  #define EXPO_HIGH_VALUE 40
  #define RATE_TAB_Y 58
  #define RATE_TAB_W 34
  #define RATE_TAB_H 28
  #define RATE_TAB_GAP 4
  #define RATE_TAB_X(i) (16 + ((i) * (RATE_TAB_W + RATE_TAB_GAP)))
  #define RATE_TOGGLE_Y 112
  #define RATE_TOGGLE_W 60
  #define RATE_TOGGLE_H 28
  #define RATE_TOGGLE_GAP 10
  #define RATE_TOGGLE_X(i) (15 + ((i) * (RATE_TOGGLE_W + RATE_TOGGLE_GAP)))
#define RATE_SLIDER_X 24
#define RATE_SLIDER_Y 172
#define RATE_SLIDER_W 192
#define RATE_SLIDER_H 22
#define RATE_SLIDER_KNOB_RADIUS 11
  #define RATE_VALUE_BOX_X 82
  #define RATE_VALUE_BOX_Y 218
  #define RATE_VALUE_BOX_W 76
  #define RATE_VALUE_BOX_H 34
  #define EXPO_GRAPH_X 28
  #define EXPO_GRAPH_Y 108
  #define EXPO_GRAPH_W 184
  #define EXPO_GRAPH_H 100
  #define EXPO_VALUE_LABEL_Y 216
  #define EXPO_VALUE_BOX_Y 232

  // ==== SPLASH SCREEN ====
  #define ANUBIS_WIDTH 160
  #define ANUBIS_HEIGHT 160

  #define MAX_MODELS 4

  int modelPanelX = 0;
  int modelPanelY = 0;
  int modelPanelW = 0;
  int modelPanelH = 0;
  bool mainOutputPanelNeedsRedraw = true;

  uint16_t COLOR_BG = 0;
  uint16_t COLOR_PANEL = 0;
  uint16_t COLOR_TEXT = 0;
  uint16_t COLOR_ACCENT = 0;
  uint16_t COLOR_ACCENT_HI = 0;
  uint16_t COLOR_SIG = 0;
  uint16_t COLOR_STICK_PANEL = 0;

  int currentTrimPage = 0; // 0 = left gimbal, 1 = right gimbal

  bool trimNeedsRedraw = true;
  bool trimDirty = true;

  static int lastSignal = -1;
  static int lastBattery = -1;

  bool reverseNeedsRedraw = true;
  bool reverseChannelDirty[CHANNEL_COUNT] = { true, true, true, true, true, true };

  bool failsafeNeedsRedraw = true;
  bool failsafeDirty[CHANNEL_COUNT] = { true, true, true, true, true, true };
  bool mixingNeedsRedraw = true;
  bool batteryPresent = false;
  bool batteryCharging = false;
  bool lowBatteryWarningVisible = false;
  bool lowBatteryWarningScreenDrawn = false;
  bool ads1115Ready = false;
  bool ads1115PrimaryReady = false;
  bool ads1115AuxReady = false;
  bool ads1115StatusLogged = false;
  bool ads1115LastLoggedReady = false;
  bool bq25887Ready = false;
  bool bq25887AdcReady = false;
  bool bq25887StatusLogged = false;
  bool bq25887LastLoggedReady = false;
  bool mcp23017Ready = false;
  bool adsStickCalibrationValid = false;
  bool stickFilterInitialized = false;
  uint8_t mcp23017Address = MCP23017_I2C_ADDR;
  uint16_t mcp23017LastValue = 0xFFFF;
  uint8_t adsConsecutiveReadFails = 0;
  unsigned long lastAdsReconnectAttemptMs = 0;
  unsigned long lastAdsAuxReconnectAttemptMs = 0;
  unsigned long lastMcpReconnectAttemptMs = 0;
  unsigned long lastMcpSampleMs = 0;
  unsigned long adsPrimaryProbeStartMs = 0;
  unsigned long adsAuxProbeStartMs = 0;
  unsigned long mcp23017ProbeStartMs = 0;
  bool adsPrimaryProbeAbandoned = false;
  bool adsAuxProbeAbandoned = false;
  bool mcp23017ProbeAbandoned = false;
  unsigned long lastBatterySampleTime = 0;
  unsigned long lastStickSampleTime = 0;
  float batteryFilteredVoltage = 0.0f;
  bool batteryFilterInitialized = false;
  bool batteryUsingBq25887Voltage = false;
  float batteryPresentMinVoltage = BATTERY_PRESENT_MIN_V;
  float batteryEmptyVoltage = BATTERY_EMPTY_V;
  float batteryFullVoltage = BATTERY_FULL_V;
  float batteryLowWarnVoltage = BATTERY_LOW_WARN_V;
  float batteryCriticalSleepVoltage = BATTERY_CRITICAL_SLEEP_V;
  uint8_t bq25887ChargeStatus = 0;
  uint8_t bq25887VbusStatus = 0;
  uint8_t bq25887FaultStatus = 0;
  bool bq25887PowerGood = false;
  bool bq25887FaultActive = false;
  uint8_t lastLoggedBq25887ChargeStatus = 0xFF;
  uint8_t lastLoggedBq25887VbusStatus = 0xFF;
  uint8_t lastLoggedBq25887FaultStatus = 0xFF;
  unsigned long batteryChargeWindowStart = 0;
  float batteryChargeWindowVoltage = 0.0f;
  unsigned long batteryChargingHoldUntil = 0;
  unsigned long batteryCriticalSince = 0;
  unsigned long lowBatteryWarningDismissedUntil = 0;
  int16_t adsStickMin[STICK_AXIS_COUNT] = {
    ADS1115_DEFAULT_MIN_COUNT,
    ADS1115_DEFAULT_MIN_COUNT,
    ADS1115_DEFAULT_MIN_COUNT,
    ADS1115_DEFAULT_MIN_COUNT
  };
  int16_t adsStickCenter[STICK_AXIS_COUNT] = { 0, 0, 0, 0 };
  int16_t adsStickMax[STICK_AXIS_COUNT] = {
    ADS1115_JOYSTICK_MAX_COUNT,
    ADS1115_JOYSTICK_MAX_COUNT,
    ADS1115_JOYSTICK_MAX_COUNT,
    ADS1115_JOYSTICK_MAX_COUNT
  };
  int16_t adsLastRawValues[STICK_AXIS_COUNT] = { 0, 0, 0, 0 };
  int16_t adsRawInputs[ADS1115_TOTAL_ANALOG_INPUTS] = { 0, 0, 0, 0, 0, 0, 0, 0 };
  float adsNormalizedInputs[ADS1115_TOTAL_ANALOG_INPUTS] = {
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f
  };
  float filteredStickAxis[STICK_AXIS_COUNT] = { 0.0f, 0.0f, 0.0f, 0.0f };
  float accessoryInputChannels[CHANNEL_COUNT - STICK_AXIS_COUNT] = { 0.0f, 0.0f };
  float lastDisplayWakeStickChannels[STICK_AXIS_COUNT] = { 0.0f, 0.0f, 0.0f, 0.0f };
  bool displayWakeStickBaselineValid = false;
  StickCalibrationState stickCalibrationState = STICK_CAL_STATE_CAPTURE_CENTER;
  bool stickCalibrationNeedsRedraw = true;
  int16_t stickCalibrationSweepMin[STICK_AXIS_COUNT] = { 0, 0, 0, 0 };
  int16_t stickCalibrationSweepMax[STICK_AXIS_COUNT] = { 0, 0, 0, 0 };
  BattleEnemy spaceEnemies[BZ_MAX_ENEMIES];
  BattleObstacle spaceObstacles[BZ_MAX_OBSTACLES];
  float spacePlayerX = 0.0f;
  float spacePlayerY = 0.0f;
  float spacePlayerHeading = 0.0f;
  bool spacePlayerShotActive = false;
  float spacePlayerShotX = 0.0f;
  float spacePlayerShotY = 0.0f;
  float spacePlayerShotVX = 0.0f;
  float spacePlayerShotVY = 0.0f;
  float spacePlayerShotDistance = 0.0f;
  bool spaceEnemyShotActive = false;
  float spaceEnemyShotX = 0.0f;
  float spaceEnemyShotY = 0.0f;
  float spaceEnemyShotVX = 0.0f;
  float spaceEnemyShotVY = 0.0f;
  bool spaceMissileActive = false;
  float spaceMissileX = 0.0f;
  float spaceMissileY = 0.0f;
  float spaceMissileVX = 0.0f;
  float spaceMissileVY = 0.0f;
  unsigned long spaceNextMissileAt = 0;
  unsigned long spaceMuzzleFlashUntil = 0;
  int spaceScore = 0;
  int spaceLives = 3;
  int spaceWave = 1;
  unsigned long lastSpaceFrameTime = 0;
  unsigned long lastSpaceShotTime = 0;
  unsigned long lastSpaceRespawnAt = 0;
  bool spaceGameOver = false;
  bool spaceWaveCleared = false;
  bool spaceGameStarted = false;
  AsteroidsRock asteroidsRocks[ASTEROIDS_MAX_ROCKS];
  AsteroidsBullet asteroidsBullets[ASTEROIDS_MAX_BULLETS];
  float asteroidsShipX = 120.0f;
  float asteroidsShipY = 174.0f;
  float asteroidsShipVX = 0.0f;
  float asteroidsShipVY = 0.0f;
  float asteroidsShipHeading = -1.5707963f;
  int asteroidsScore = 0;
  int asteroidsLives = 3;
  int asteroidsLevel = 1;
  bool asteroidsGameOver = false;
  bool asteroidsStarted = false;
  unsigned long lastAsteroidsFrameTime = 0;
  unsigned long lastAsteroidsShotTime = 0;
  unsigned long asteroidsInvulnerableUntil = 0;
  StarSwarmEnemy starSwarmEnemies[STAR_SWARM_MAX_ENEMIES];
  StarSwarmShot starSwarmPlayerShots[STAR_SWARM_MAX_PLAYER_SHOTS];
  StarSwarmShot starSwarmEnemyShots[STAR_SWARM_MAX_ENEMY_SHOTS];
  StarSwarmPowerup starSwarmPowerups[STAR_SWARM_MAX_POWERUPS];
  float starSwarmPlayerX = 120.0f;
  float starSwarmPlayerY = STAR_SWARM_PLAYER_START_Y;
  int starSwarmOldPlayerX = 120;
  int starSwarmOldPlayerY = STAR_SWARM_PLAYER_START_Y;
  bool starSwarmPlayerWasDrawn = false;
  int starSwarmScore = 0;
  int starSwarmLives = 3;
  int starSwarmWave = 1;
  int starSwarmDir = 1;
  bool starSwarmGameOver = false;
  bool starSwarmStarted = false;
  bool starSwarmTouchControl = false;
  int starSwarmTouchTargetX = 120;
  int starSwarmTouchTargetY = STAR_SWARM_PLAYER_START_Y;
  bool starSwarmFireQueued = false;
  uint8_t starSwarmWeaponLevel = 0;
  unsigned long starSwarmSpeedBoostUntil = 0;
  unsigned long lastStarSwarmFrameTime = 0;
  unsigned long lastStarSwarmShotTime = 0;
  unsigned long lastStarSwarmEnemyShotTime = 0;
  unsigned long lastStarSwarmDiveTime = 0;
  unsigned long starSwarmWaveStartTime = 0;
  unsigned long starSwarmInvulnerableUntil = 0;
  int lastStarSwarmScore = -1;
  int lastStarSwarmLives = -1;
  int lastStarSwarmWave = -1;
  bool lastStarSwarmGameOver = false;
  bool lastStarSwarmStarted = false;
  float dungeonPlayerX = 2.5f;
  float dungeonPlayerY = 2.5f;
  float dungeonPlayerHeading = 1.5707963f;
  unsigned long lastDungeonFrameTime = 0;
  unsigned long lastDungeonShotTime = 0;
  unsigned long dungeonMuzzleFlashUntil = 0;
  int dungeonHealth = 100;
  int dungeonAmmo = 50;
  bool dungeonStarted = false;
  bool dungeonFrameDirty = true;
  bool dungeonStatusDirty = true;
  bool dungeonGunDirty = true;
  bool dungeonMuzzleWasVisible = false;
  bool dungeonWeaponAnimWasVisible = false;
  bool dungeonRestoreWeaponBackdrop = false;
  bool dungeonImpactWasVisible = false;
  unsigned long dungeonImpactUntil = 0;
  bool dungeonPreserveIdlePistol = false;
  bool dungeonIdlePistolMaskReady = false;
  int16_t dungeonIdlePistolMinX[DUNGEON_RUN_PISTOL_IDLE_H];
  int16_t dungeonIdlePistolMaxX[DUNGEON_RUN_PISTOL_IDLE_H];
  int lastDungeonDrawnHealth = -1;
  int lastDungeonDrawnAmmo = -1;
  int gamesMenuPage = 0;
  int twentyQQuestionIndex = 0;
  int8_t twentyQUserAnswers[TWENTYQ_MAX_QUESTIONS] = { 0 };
  bool twentyQGuessReady = false;
  bool twentyQNeedsRedraw = true;
  int twentyQBestCandidate = 0;
  int twentyQSecondCandidate = 0;
  int twentyQBestScore = 0;
  int twentyQSecondScore = 0;
  bool twentyQAnsweredFeedback = false;
  bool twentyQLearnedLastRound = false;
  bool twentyQDuplicateLastRound = false;
  TwentyQLearnedCandidate twentyQLearnedCandidates[TWENTYQ_MAX_LEARNED];
  int twentyQLearnedCount = 0;
  int selectedMixIndex = 0;
  int selectedMixPage = MIX_PAGE_PRESET;
  unsigned long mixPageToggleLockedUntil = 0;
  int selectedRateChannel = 0;
  int selectedExpoChannel = 0;
  int endpointNumpadChannel = -1;
  bool endpointNumpadHighSide = true;
  bool mixNumpadActive = false;
  bool mixNumpadNeedsRedraw = false;
  bool ratesNeedsRedraw = true;
  bool expoNeedsRedraw = true;
  bool endpointNeedsRedraw = true;
  bool displaySettingsNeedsRedraw = true;
  bool ratesValueDirty = false;
  bool expoValueDirty = false;
  uint8_t endpointSideLatch[CHANNEL_COUNT] = { 1, 1, 1, 1, 1, 1 };
  bool endpointAutoFocusActive = false;
  bool mixNumpadEditingRate = true;
  NumpadTarget mixNumpadTarget = NUMPAD_TARGET_MIX_RATE;
  String mixNumpadBuffer = "";
  int mixNumpadCursorRow = 0;
  int mixNumpadCursorCol = 0;
  extern char otaStaSsid[OTA_STA_SSID_STORAGE_BYTES + 1];
  extern char otaStaPassword[OTA_STA_PASSWORD_STORAGE_BYTES + 1];
  extern char otaApSsid[OTA_AP_SSID_STORAGE_BYTES + 1];

  // placeholder values for now (you’ll replace with real captured values later)
  bool isInside(int x, int y, int bx, int by, int bw, int bh);
  int mapTouch(int val, int in_min, int in_max, int out_min, int out_max);
  int cycleFocusIndex(int current, int count, bool forward);
  ButtonID cycleButtonOrder(ButtonID current, const ButtonID *order, int count, bool forward);
  void setScreen(Screen screen);
  void queueScreenButton(ButtonID button, Screen screen);
  void saveKeyboardBufferToModelSlot();
  void sanitizeDisplaySettings();
  void resetDisplaySettingsToDefaults();
  bool loadDisplaySettings();
  void saveDisplaySettings();
  void applyThemePalette();
  void applyDisplayBacklight();
  void initStatusRgbLed();
  void setStatusRgbLed(uint8_t red, uint8_t green, uint8_t blue);
  bool isI2cDevicePresent(uint8_t address);
  void initAudio();
  void setAudioAmpEnabled(bool enabled);
  void silenceAudioOutput(bool stopI2s);
  void updateAudioDiagnostics(unsigned long now);
  void writeAudioStereoSample(int16_t sample);
  int16_t scaleAudioSampleForUserVolume(int16_t sample);
  void applyAudioUserMuteState();
  void loadAudioSettings();
  void saveAudioSettings();
  void startStartupAudioClip();
  void updateStartupAudioClip();
  void playAudioTone(uint16_t frequency, uint16_t durationMs, uint16_t amplitude, bool gated);
  void playAudioClick(uint16_t frequency = 1800, uint16_t durationMs = 32);
  void drawMainVolumeButton();
  void drawMainVolumePanel();
  bool handleMainVolumeTouch(int x, int y);
  void getStatusRgbLedPatternColor(StatusRgbDiagCode code, uint8_t step, uint8_t &red, uint8_t &green, uint8_t &blue);
  void setStatusRgbLedPattern(StatusRgbDiagCode code, uint8_t step, unsigned long stepElapsed);
  bool isElrsModuleMissingForStatus(unsigned long now);
  void updateStatusRgbLed(unsigned long now);
  uint8_t collectStatusRgbDiagCodes(StatusRgbDiagCode *codes, uint8_t maxCodes);
  void initPowerLed();
  void setPowerLed(bool on);
  void setPowerLedDuty(uint8_t duty);
  void updatePowerLed(unsigned long now);
  void initPowerButton();
  bool isPowerButtonPressed();
  void updatePowerButton(unsigned long now);
  void handlePowerButtonShortPress();
  void drawPowerButtonShutdownPrompt();
  bool isPowerButtonWake();
  bool isSoftPowerOffStored();
  void setSoftPowerOffStored(bool off);
  void handleSoftPowerBootState();
  void startPowerButtonDeepSleep();
  void enterDeepSleepPowerOff();
  void initPeripheralPowerSwitches();
  void setPeripheralPowerRails(bool on);
  void prepareExternalBusesForDeepSleep();
  unsigned long getDisplayDimTimeoutMs();
  unsigned long getDisplayOffTimeoutMs();
  int findDisplayOptionIndex(const uint8_t *options, int optionCount, uint8_t value);
  void stepDisplayOption(uint8_t &value, const uint8_t *options, int optionCount, int delta);
  const char* formatDisplayTimeoutValue(uint8_t optionIndex, const uint16_t *options, uint8_t optionCount,
                                        char *buffer, size_t bufferSize);
  const char* getThemeToggleLabel();
  void loadOtaSettings();
  void saveOtaSettings();
  void applyKeyboardBufferToTarget();
  int getKeyboardMaxLength();
  void beginOtaFieldEdit(KeyboardTarget target);
  void drawOtaSettingsStatic();
  void drawOtaSettingsDynamic();
  void drawDisplaySettingsStatic();
  void drawDisplaySettingsDynamic();
  void drawGamesMenuScreen();
  void resetTwentyQGame();
  void drawTwentyQScreen();
  void handleTwentyQTouch(int x, int y);
  void handleTwentyQAnswer(int8_t answer);
  void updateTwentyQGuess();
  void drawGameExitButton(int x, int y, int w, int h, const char *label, bool pressed, bool selected);
  void loadTwentyQLearnedCandidates();
  void saveTwentyQLearnedCandidates();
  void saveTwentyQLearnedAnswer(const char *name);
  bool twentyQNameMatches(const char *a, const char *b);
  bool isTwentyQDuplicateAnswer(const char *name);
  const char* getTwentyQCandidateName(int index);
  const char* getTwentyQCandidateCategory(int index);
  int8_t getTwentyQCandidateAnswer(int index, int question);
  void drawWrappedCenteredText(const char *text, int x, int y, int w, int lineH, int maxLines, uint16_t fg, uint16_t bg);
  int gamesMenuPageCount();
  int getGameMenuIndexForButton(ButtonID button);
  ButtonID getGameMenuButtonForIndex(int index);
  const char* getGameMenuLabelForIndex(int index);
  bool isGameMenuButton(ButtonID button);
  void selectGameMenuPage(int page);
  void activateGameMenuButton(ButtonID button);
  void drawDisplaySettingsRow(int y, const char* label, const char* valueText,
                              bool minusPressed, bool minusSelected,
                              bool plusPressed, bool plusSelected,
                              uint16_t previewColor, int previewWidth);
  void drawGearIcon(int x, int y, int size, uint16_t iconColor);
  const char* getKeyboardKey(int row, int col);
  void processKeyboardKey(const char* key);
  bool modelSlotUninitialized(int i);
  bool modelHasBoundReceiver(int modelIndex);
  void clearBoundReceiver(int modelIndex);
  void setBoundReceiver(int modelIndex, const uint8_t *macAddress);
  const uint8_t* getBoundReceiverMac(int modelIndex);
  int getModelFailsafeValue(int modelIndex, int channel);
  void setModelFailsafeValue(int modelIndex, int channel, int value);
  void clearModelFailsafeValues(int modelIndex);
  int getModelRateValue(int modelIndex, int channel);
  void setModelRateValue(int modelIndex, int channel, int value);
  void clearModelRateValues(int modelIndex);
  int getModelExpoValue(int modelIndex, int channel);
  void setModelExpoValue(int modelIndex, int channel, int value);
  void clearModelExpoValues(int modelIndex);
  int getModelEndpointLowValue(int modelIndex, int channel);
  int getModelEndpointHighValue(int modelIndex, int channel);
  void setModelEndpointLowValue(int modelIndex, int channel, int value);
  void setModelEndpointHighValue(int modelIndex, int channel, int value);
  void clearModelEndpointValues(int modelIndex);
  void loadReceiverBindings();
  void saveReceiverBindings();
  void loadEspNowReceiverOutputModes();
  void saveEspNowReceiverOutputModes();
  EspNowReceiverOutputMode getModelEspNowOutputMode(int modelIndex);
  void setModelEspNowOutputMode(int modelIndex, EspNowReceiverOutputMode mode);
  void loadFailsafeValues();
  void saveFailsafeValues();
  void loadRateValues();
  void saveRateValues();
  void loadExpoValues();
  void saveExpoValues();
  void loadEndpointValues();
  void saveEndpointValues();
  bool loadTankEscChannelMap();
  void saveTankEscChannelMap();
  int loadActiveModelIndex();
  void saveActiveModelIndex();
  void initDefaultMixSlot(int modelIndex, int mixIndex);
  void applyDrivePresetMixes(int modelIndex);
  void setDroneArmActive(bool armed);
  float getDroneArmOutput();
  void showDroneArmWarning();
  void closeDroneArmWarning();
  void drawDroneArmWarning();
  void drawDroneArmWarningStatic();
  void drawDroneArmWarningDynamic();
  bool handleDroneArmWarningTouch(bool isTouching, int x, int y);
  bool drivePresetUsesVisibleMixes(int modelIndex);
  bool sanitizeModelMixes(int modelIndex);
  bool sanitizeModelDriveType(int modelIndex);
  bool sanitizeModelProtocol(int modelIndex);
  bool sanitizeModelTankEscChannels(int modelIndex);
  uint8_t getMixSource(const MixData &mix);
  void setMixSource(MixData &mix, uint8_t source);
  uint8_t getMixDestination(const MixData &mix);
  void setMixDestination(MixData &mix, uint8_t destination);
  bool isMixReverseLinked(const MixData &mix);
  void setMixReverseLinked(MixData &mix, bool linked);
  void openMixNumpad(NumpadTarget target);
  void openElrsReceiverPwmNumpad(int pwmIndex);
  void closeMixNumpad(bool commitValue);
  bool handleMixNumpadTouch(int x, int y);
  void drawMixNumpad();
  void drawDriveTypeScreen();
  void drawTankModeScreen();
  void drawEscChannelSetupScreen();
  void drawSpaceGameStatic();
  void drawSpaceGameDynamic();
  void resetStarSwarmGame();
  void updateStarSwarmGame(unsigned long now, bool firePressed, bool exitPressed);
  void handleStarSwarmTouch(int x, int y);
  void drawStarSwarmStatic();
  void drawStarSwarmDynamic();
  void resetAsteroidsGame();
  void updateAsteroidsGame(unsigned long now, bool firePressed, bool exitPressed);
  void handleAsteroidsTouch(int x, int y);
  void drawAsteroidsStatic();
  void drawAsteroidsDynamic();
  void fireAsteroidsBullet();
  void spawnAsteroidsWave();
  void wrapAsteroidsPoint(float &x, float &y);
  void drawTankBars(int x, int y, int w, int h, float left, float right);
  void drawTankAuxBars(int x, int y, int w, int h, float turret, float weapon);
  void drawCarBars(int x, int y, int w, int h, float steering, float throttle);
  void drawOmniBars(int x, int y, int w, int h, float ch4, float ch1, float ch2);
  void drawDroneCommandBars(int x, int y, int w, int h, float yaw, float m1, float m2, float m3, float m4);
  void restoreQuadXIconClip(int iconX, int iconY, int iconW, int iconH, int clipX, int clipY, int clipW, int clipH);
  void drawThickArc(int cx, int cy, int innerR, int outerR, int startDeg, int endDeg, uint16_t color);
  void drawDriveTypeOption(int x, int y, int w, int h, const char* label, DriveType drive, ButtonID button);
  void drawDriveTypeOptionIcon(int x, int y, int w, int h, DriveType drive, uint16_t iconColor);
  void drawLegacyTankIcon(int x, int y, int w, int h, uint16_t iconColor);
  void drawLegacyQuadXIcon(int x, int y, int w, int h, uint16_t iconColor);
  void drawLegacyCarIcon(int x, int y, int s, uint16_t iconColor);
  uint16_t tintRgb565Pixel(uint16_t srcColor, uint16_t tintColor);
  void drawRgb565IconTinted(int x, int y, int w, int h, const uint16_t* iconData, int iconW, int iconH, uint16_t tintColor);
  void drawBackIcon(int x, int y, int s, uint16_t iconColor);
  void drawOmniIcon(int x, int y, int w, int h, uint16_t iconColor);
  void drawEndpointIcon(int x, int y, int s, uint16_t iconColor);
  void drawExpoIcon(int x, int y, int s, uint16_t iconColor);
  void drawRatesIcon(int x, int y, int s, uint16_t iconColor);
  void drawProtocolIcon(int x, int y, int s, uint16_t iconColor);
  void getLinkedReverseChannels(int modelIndex, int channel, bool linked[CHANNEL_COUNT]);
  bool hasLinkedReversePeers(int modelIndex, int channel);
  bool shouldForceTankSingleReversePair(int modelIndex);
  void drawTrimGraphBase();
  void drawTrimButtons();
  void drawMixingStatic();
  void drawMixingDynamic();
  int getMixPageBase(int page);
  int getMixLocalIndex();
  void setSelectedMixPage(int page);
  bool toggleSelectedMixPageDebounced(unsigned long now);
  const char* getMixPageLabel(int page);
  void drawStickCalibrationScreen();
  void updateBatteryState();
  bool initBq25887Charger();
  bool readBq25887Reg(uint8_t reg, uint8_t &value);
  bool writeBq25887Reg(uint8_t reg, uint8_t value);
  bool readBq25887Word(uint8_t msbReg, uint16_t &value);
  bool configureBq25887Adc();
  bool readBq25887BatteryVoltage(float &voltage);
  bool updateBq25887Status();
  bool isBq25887ChargingState(uint8_t chargeStatus);
  const char* getBq25887ChargeStatusName(uint8_t chargeStatus);
  const char* getBq25887VbusStatusName(uint8_t vbusStatus);
  void drawSplash();
  bool isStartupThrottleSafe();
  bool handleStartupThrottleSafetyBypass(uint8_t touchCount, int rawX, int rawY, unsigned long now);
  void updateStartupThrottleSafetyScreen(unsigned long now);
  bool updateAutoDeepSleep(unsigned long now, bool inputDetected, bool powerButtonInput);
  bool isAutoDeepSleepHardwareMissing(unsigned long now);
  void drawAutoDeepSleepWarningScreen(unsigned long now);
  bool updateBatterySafety(unsigned long now, bool userDismiss);
  void drawBatterySafetyScreen(bool critical, unsigned long now);
  void drawGameMenuButton();
  int getThreePosSwitchPosition();
  void drawThreePosSwitchIndicator(int cx, int y, int position);
  void runMainToTimerSwipeTransition();
  bool handleMainSwipeGesture(bool isTouching, int x, int y, unsigned long now);
  uint32_t getTimerInputSeconds();
  unsigned long getTimerElapsedMs(unsigned long now);
  bool isTimerElapsed(unsigned long now);
  unsigned long getTimerDisplayMs(unsigned long now);
  void formatTimerDisplay(unsigned long displayMs, char *out, size_t outSize);
  void formatTimerTargetBuffer(char *out, size_t outSize);
  void resetTimerRuntime();
  void drawTimerScreen();
  void drawTimerDynamic();
  void handleTimerTouch(int x, int y);
  bool initAds1115();
  bool isAds1115Present(uint8_t address);
  bool readAds1115SingleEnded(uint8_t channel, int16_t &value);
  void loadStickCalibration();
  void saveStickCalibration();
  void resetStickCalibrationDefaults();
  bool captureStickCalibrationCenter();
  void updateStickCalibrationSweep();
  bool finalizeStickCalibration();
  ButtonID getControllerSettingsDefaultButtonForPage(int page);
  bool initMcp23017();
  bool writeMcp23017Reg(uint8_t reg, uint8_t value);
  bool readMcp23017Reg(uint8_t reg, uint8_t &value);
  bool readMcp23017(uint16_t &value);
  void printI2cStartupScan();
  void updateStickInputs(unsigned long now);
  void updateAccessoryInputs(unsigned long now);
  bool updateStickDisplayWake(unsigned long now);
  float normalizeStickAxis(int16_t raw, int channel);
  void updateChannelOutputs();
  void buildProtocolOutputChannels(float channels[CHANNEL_COUNT]);
  float applyExpoCurve(float value, int expoPercent);
  bool updateEndpointSideLatch(int channel);
  bool isEndpointHighSideSelected(int channel);
  float applyEndpointScaling(float value, int channel);
  float getChannelTrimOffset(int channel);
  const char* getChannelAxisName(uint8_t channel);
  float getCarSteeringOutput();
  float getCarThrottleOutput();
  DriveType getModelDriveType(int modelIndex);
  void setModelDriveType(int modelIndex, DriveType driveType);
  TankControlMode getModelTankMode(int modelIndex);
  void setModelTankMode(int modelIndex, TankControlMode tankMode);
  uint8_t getModelTankLeftEscChannel(int modelIndex);
  uint8_t getModelTankRightEscChannel(int modelIndex);
  void setModelTankEscChannels(int modelIndex, uint8_t leftChannel, uint8_t rightChannel);
  ProtocolType getModelProtocol(int modelIndex);
  void setModelProtocol(int modelIndex, ProtocolType protocolType);
  void drawExpoStatic();
  void drawExpoDynamic();
  void drawRatesStatic();
  void drawRatesDynamic();
  void drawEndpointStatic();
  void drawEndpointDynamic();
  void composeProtocolStatusText(char *bindStatus, size_t bindStatusSize, uint16_t *statusColor);
  void composeElrsStatusText(char *statusText, size_t statusTextSize, uint16_t *statusColor);
  void drawProtocolScreen();
  void drawProtocolDynamic();
  void drawTxUpdateScreen();
  void drawTxUpdateDynamic();
  void drawElrsScreen();
  void drawElrsDynamic();
  void sanitizeWifiText(char *value, size_t maxLen, bool allowEmpty);
  void sanitizeOtaSettings();
  void logOtaNetworkState(const char *label);
  void logPartitionInfo(const char *label, const esp_partition_t *partition);
  bool isOtaKeyboardTarget(KeyboardTarget target);
  const char* getKeyboardTargetLabel(KeyboardTarget target);
  void drawKeyboardPreview();
  void startOtaMode();
  void stopOtaMode();
  void updateOtaService();
  void beginElrsReceiverConfig();
  void stopElrsReceiverConfig();
  void updateElrsReceiverConfig();
  bool fetchElrsReceiverConfig();
  bool requestElrsReceiverMethod(const char *method, const char *path, const char *body, const char *contentType, String &response);
  bool requestElrsReceiverPath(const char *path, String &response);
  bool saveElrsReceiverConfig();
  bool extractJsonObjectValue(const String &json, const char *key, String &out);
  bool replaceJsonIntValue(String &json, const char *key, int value);
  bool parseElrsReceiverPwmConfig(const String &json);
  int decodeElrsPwmFailsafeUs(uint32_t raw);
  uint32_t encodeElrsPwmFailsafeUs(uint32_t raw, int failsafeUs);
  int displayElrsPwmFailsafeUs(int failsafeUs);
  bool isElrsReceiverPwmConfigEdited(int index);
  bool replaceElrsPwmConfigValues(String &json);
  bool isElrsReceiverConfigEdited();
  int modelFailsafePercentToPwmUs(int percent);
  bool applyModelFailsafesToElrsReceiverPwm();
  ButtonID getElrsReceiverPwmButton(int index);
  int getElrsReceiverPwmIndexForButton(ButtonID button);
  void logElrsReceiverWriteEndpoints();
  bool parseElrsReceiverConfigJson(const String &response);
  bool extractJsonStringValue(const String &json, const char *key, char *out, size_t outSize);
  int extractJsonIntValue(const String &json, const char *key, int fallback);
  bool extractJsonUidValue(const String &json, char *out, size_t outSize);
  void drawElrsReceiverConfigScreen();
  void drawElrsReceiverConfigDynamic();
  void drawControllerPlaceholderScreen(const char* title, const char* line1, const char* line2);
  bool initEspNowLink();
  bool setEspNowWifiChannel(uint8_t channel);
  bool ensureEspNowPeer(const uint8_t *peerAddress);
  void resetEspNowTelemetry();
  void updateEspNowLink(unsigned long now);
  void initElrsUart();
  void stopElrsUart();
  void updateElrsLink(unsigned long now);
  void initElrsPassiveSniffer();
  void updateElrsPassiveSniffer(unsigned long now);
  void readElrsSniffSerial(HardwareSerial &port, ElrsSniffStats &stats);
  void logElrsSniffImmediateFrame(const char *label, const uint8_t *frame, uint8_t frameLen);
  void logElrsSniffSummary(const char *label, ElrsSniffStats &stats);
  void logElrsSniffFrame(const char *label, ElrsSniffStats &stats);
  void sendElrsChannelFrame();
  void elrsSendFrame(const uint8_t *frame, size_t len);
  void sendElrsDevicePing();
  void sendElrsBindCommandTo(uint8_t destination);
  void sendElrsLuaBindWrite(uint8_t fieldIndex);
  void sendElrsLuaBindExecWrite(uint8_t fieldIndex);
  void sendElrsTxPowerWrite(uint16_t powerMw, bool persistToModel = true);
  void sendElrsBindCommand();
  void sendElrsParameterRead(uint8_t fieldIndex, uint8_t chunkIndex);
  void updateElrsParameterDiscovery(unsigned long now);
  void parseElrsDeviceInfoFrame(const uint8_t *frame, uint8_t expected);
  void parseElrsParameterEntryFrame(const uint8_t *frame, uint8_t expected);
  void parseElrsParameterValueFrame(const uint8_t *frame, uint8_t expected);
  void drawElrsPowerControl(bool selectedSlider, bool selectedValue, bool forceRedraw);
  void readElrsSerial(unsigned long now);
  void restartElrsUart(uint32_t baud);
  void advanceElrsSerialMode();
  void applyElrsUartPinPair(int pinPairIndex);
  const char* elrsModuleProfileName(uint8_t profile);
  void applyElrsModuleProfile(uint8_t profile, bool resetDiscovery);
  void updateElrsProfileAutoDetect(unsigned long now);
  uint8_t getElrsBindFallbackFieldIndex();
  uint32_t getElrsModuleResponseCount();
  bool hasElrsModuleFrames();
  bool hasElrsTxOnlyActivity(unsigned long now);
  void IRAM_ATTR onElrsRxEdge();
  bool containsIgnoreCase(const char *haystack, const char *needle);
  uint8_t crsfCrc8Poly(const uint8_t *data, size_t length, uint8_t poly);
  uint8_t crsfCrc8(const uint8_t *data, size_t length);
  int channelToCrsf(float value);
  int elrsLqToBars(uint8_t linkQuality);
  bool hasEspNowSignal(unsigned long now);
  bool hasEspNowHeaderSignal(unsigned long now);
  void updateTopBarDirtyState(unsigned long now);
  uint16_t getLatencyIndicatorColor(int latencyMs);
  void sendEspNowControlPacket(unsigned long now);
  void sendEspNowPing(unsigned long now);
  void sendEspNowBindCommit(const uint8_t *receiverMac);
  void sendEspNowUnbindCommand();
  void showEspNowOutputModePrompt();
  void drawEspNowOutputModePrompt();
  bool handleEspNowOutputModePromptTouch(bool isTouching, int x, int y);
  void startEspNowBindingWithOutputMode(EspNowReceiverOutputMode mode);
  bool hasEspNowPendingBindMac();
  void beginEspNowBinding();
  void cancelEspNowBinding(bool clearPendingMac);
  void handleEspNowBindPacket(const esp_now_recv_info_t *info, const uint8_t *data, int len);
  void formatReceiverMacShort(const uint8_t *macAddress, char *buffer, size_t bufferSize);
  bool isEspNowPacketFromActiveBoundReceiver(const esp_now_recv_info_t *info);
  void clearActiveEspNowSessionTelemetry();
  void handleEspNowTelemetryPacket(const esp_now_recv_info_t *info, const uint8_t *data, int len);
  void handleEspNowPingAckPacket(const esp_now_recv_info_t *info, const uint8_t *data, int len);
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
  void onEspNowSend(const esp_now_send_info_t *tx_info, esp_now_send_status_t status);
#else
  void onEspNowSend(const uint8_t *macAddr, esp_now_send_status_t status);
#endif
  void onEspNowReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len);
  void resetSpaceGame();
  void updateSpaceGame(unsigned long now, bool leftPressed, bool rightPressed, bool firePressed, bool exitPressed);
  void handleSpaceGameTouch(int x, int y);
  void moveSpacePlayer(int delta);
  void fireSpacePlayerBullet();
  void fireSpaceEnemyBullet();
  void resetDungeonRun();
  void updateDungeonRun(unsigned long now, bool firePressed, bool exitPressed);
  void handleDungeonRunTouch(int x, int y);
  void fireDungeonWeapon();
  void drawDungeonRunSpriteKeyed(int x, int y, int w, int h, const uint16_t *data);
  void drawDungeonRunHudNumber(int value, int x, int y, int digits);
  void drawDungeonRunHud();
  void drawDungeonRunStatic();
  void drawDungeonRunDynamic();
  bool spaceEnemiesRemaining();

  uint8_t getMixSource(const MixData &mix) {
    return mix.source & MIX_CHANNEL_MASK;
  }

  void setMixSource(MixData &mix, uint8_t source) {
    mix.source = (mix.source & MIX_REVERSE_SEPARATE_FLAG) | (source & MIX_CHANNEL_MASK);
  }

  uint8_t getMixDestination(const MixData &mix) {
    return mix.destination & MIX_CHANNEL_MASK;
  }

  void setMixDestination(MixData &mix, uint8_t destination) {
    mix.destination = destination & MIX_CHANNEL_MASK;
  }

  bool isMixReverseLinked(const MixData &mix) {
    return (mix.source & MIX_REVERSE_SEPARATE_FLAG) == 0;
  }

  void setMixReverseLinked(MixData &mix, bool linked) {
    if (linked) {
      mix.source &= ~MIX_REVERSE_SEPARATE_FLAG;
    } else {
      mix.source |= MIX_REVERSE_SEPARATE_FLAG;
    }
  }

  bool modelHasBoundReceiver(int modelIndex) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return false;

    for (int i = 0; i < ESP_NOW_ETH_ALEN; i++) {
      if (boundReceiverMacs[modelIndex][i] != 0x00) return true;
    }

    return false;
  }

  void clearBoundReceiver(int modelIndex) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return;
    memset(boundReceiverMacs[modelIndex], 0, ESP_NOW_ETH_ALEN);
  }

  void setBoundReceiver(int modelIndex, const uint8_t *macAddress) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS || macAddress == nullptr) return;
    memcpy(boundReceiverMacs[modelIndex], macAddress, ESP_NOW_ETH_ALEN);
  }

  const uint8_t* getBoundReceiverMac(int modelIndex) {
    if (!modelHasBoundReceiver(modelIndex)) return nullptr;
    return boundReceiverMacs[modelIndex];
  }

  void writeEepromInt16(int addr, int16_t value) {
    EEPROM.write(addr, (uint8_t)(value & 0xFF));
    EEPROM.write(addr + 1, (uint8_t)((value >> 8) & 0xFF));
  }

  int16_t readEepromInt16(int addr) {
    uint16_t lo = EEPROM.read(addr);
    uint16_t hi = EEPROM.read(addr + 1);
    return (int16_t)((hi << 8) | lo);
  }

  void resetStickCalibrationDefaults() {
    adsStickCalibrationValid = false;
    for (int channel = 0; channel < STICK_AXIS_COUNT; channel++) {
      adsStickMin[channel] = ADS1115_DEFAULT_MIN_COUNT;
      adsStickCenter[channel] = 0;
      adsStickMax[channel] = ADS1115_JOYSTICK_MAX_COUNT;
      adsLastRawValues[channel] = 0;
      stickCalibrationSweepMin[channel] = 0;
      stickCalibrationSweepMax[channel] = 0;
    }
  }

  void loadStickCalibration() {
    if (STICK_CAL_USE_HARDCODED_DEFAULTS) {
      for (int channel = 0; channel < STICK_AXIS_COUNT; channel++) {
        adsStickMin[channel] = STICK_CAL_HARDCODED_MIN[channel];
        adsStickCenter[channel] = STICK_CAL_HARDCODED_CENTER[channel];
        adsStickMax[channel] = STICK_CAL_HARDCODED_MAX[channel];
      }
      adsStickCalibrationValid = true;
      return;
    }

    if (EEPROM.read(EEPROM_STICK_CAL_VERSION_ADDR) != STICK_CAL_STORAGE_VERSION) {
      resetStickCalibrationDefaults();
      return;
    }

    bool valid = true;
    int minAddr = EEPROM_STICK_CAL_MIN_DATA_ADDR;
    int centerAddr = EEPROM_STICK_CAL_CENTER_DATA_ADDR;
    int maxAddr = EEPROM_STICK_CAL_MAX_DATA_ADDR;

    for (int channel = 0; channel < STICK_AXIS_COUNT; channel++) {
      int16_t minValue = readEepromInt16(minAddr);
      int16_t centerValue = readEepromInt16(centerAddr);
      int16_t maxValue = readEepromInt16(maxAddr);
      minAddr += 2;
      centerAddr += 2;
      maxAddr += 2;

      if (!(minValue < centerValue && centerValue < maxValue)) {
        valid = false;
        break;
      }

      adsStickMin[channel] = minValue;
      adsStickCenter[channel] = centerValue;
      adsStickMax[channel] = maxValue;
    }

    adsStickCalibrationValid = valid;
    if (!valid) {
      resetStickCalibrationDefaults();
    }
  }

  void saveStickCalibration() {
    int minAddr = EEPROM_STICK_CAL_MIN_DATA_ADDR;
    int centerAddr = EEPROM_STICK_CAL_CENTER_DATA_ADDR;
    int maxAddr = EEPROM_STICK_CAL_MAX_DATA_ADDR;

    for (int channel = 0; channel < STICK_AXIS_COUNT; channel++) {
      writeEepromInt16(minAddr, adsStickMin[channel]);
      writeEepromInt16(centerAddr, adsStickCenter[channel]);
      writeEepromInt16(maxAddr, adsStickMax[channel]);
      minAddr += 2;
      centerAddr += 2;
      maxAddr += 2;
    }

    EEPROM.write(EEPROM_STICK_CAL_VERSION_ADDR, STICK_CAL_STORAGE_VERSION);
    EEPROM.commit();
    adsStickCalibrationValid = true;
    Serial.printf(
      "STICK_CAL_HARDCODED min={%d,%d,%d,%d} center={%d,%d,%d,%d} max={%d,%d,%d,%d}\n",
      adsStickMin[0], adsStickMin[1], adsStickMin[2], adsStickMin[3],
      adsStickCenter[0], adsStickCenter[1], adsStickCenter[2], adsStickCenter[3],
      adsStickMax[0], adsStickMax[1], adsStickMax[2], adsStickMax[3]
    );
  }

  ButtonID getControllerSettingsDefaultButtonForPage(int page) {
    if (page <= 0) return BTN_TRIM;
    if (page == 1) return BTN_EXPO;
    return STICK_CAL_SCREEN_ENABLED ? BTN_STICK_CAL : BTN_TRIM;
  }

  int getModelFailsafeValue(int modelIndex, int channel) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return 0;
    if (channel < 0 || channel >= CHANNEL_COUNT) return 0;
    return modelFailsafeValues[modelIndex][channel];
  }

  void setModelFailsafeValue(int modelIndex, int channel, int value) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return;
    if (channel < 0 || channel >= CHANNEL_COUNT) return;
    modelFailsafeValues[modelIndex][channel] = (int8_t)constrain(value, -100, 100);
  }

  void clearModelFailsafeValues(int modelIndex) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return;
    for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
      modelFailsafeValues[modelIndex][channel] = 0;
    }
  }

  int getModelRateValue(int modelIndex, int channel) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return RATE_NORMAL_VALUE;
    if (channel < 0 || channel >= CHANNEL_COUNT) return RATE_NORMAL_VALUE;
    return modelRateValues[modelIndex][channel];
  }

  void setModelRateValue(int modelIndex, int channel, int value) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return;
    if (channel < 0 || channel >= CHANNEL_COUNT) return;
    modelRateValues[modelIndex][channel] = (uint8_t)constrain(value, 1, 100);
  }

  void clearModelRateValues(int modelIndex) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return;
    for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
      modelRateValues[modelIndex][channel] = RATE_NORMAL_VALUE;
    }
  }

  int getModelExpoValue(int modelIndex, int channel) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return EXPO_NORMAL_VALUE;
    if (channel < 0 || channel >= CHANNEL_COUNT) return EXPO_NORMAL_VALUE;
    return modelExpoValues[modelIndex][channel];
  }

  void setModelExpoValue(int modelIndex, int channel, int value) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return;
    if (channel < 0 || channel >= CHANNEL_COUNT) return;
    modelExpoValues[modelIndex][channel] = (int8_t)constrain(value, -100, 100);
  }

  void clearModelExpoValues(int modelIndex) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return;
    for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
      modelExpoValues[modelIndex][channel] = EXPO_NORMAL_VALUE;
    }
  }

  int getModelEndpointLowValue(int modelIndex, int channel) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return 100;
    if (channel < 0 || channel >= CHANNEL_COUNT) return 100;
    return modelEndpointLowValues[modelIndex][channel];
  }

  int getModelEndpointHighValue(int modelIndex, int channel) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return 100;
    if (channel < 0 || channel >= CHANNEL_COUNT) return 100;
    return modelEndpointHighValues[modelIndex][channel];
  }

void setModelEndpointLowValue(int modelIndex, int channel, int value) {
  if (modelIndex < 0 || modelIndex >= MAX_MODELS) return;
  if (channel < 0 || channel >= CHANNEL_COUNT) return;
  modelEndpointLowValues[modelIndex][channel] = (uint8_t)constrain(value, 1, 120);
}

void setModelEndpointHighValue(int modelIndex, int channel, int value) {
  if (modelIndex < 0 || modelIndex >= MAX_MODELS) return;
  if (channel < 0 || channel >= CHANNEL_COUNT) return;
  modelEndpointHighValues[modelIndex][channel] = (uint8_t)constrain(value, 1, 120);
}

  void clearModelEndpointValues(int modelIndex) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return;
    for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
      modelEndpointLowValues[modelIndex][channel] = 100;
      modelEndpointHighValues[modelIndex][channel] = 100;
    }
  }

  void loadReceiverBindings() {
    if (EEPROM.read(EEPROM_BIND_VERSION_ADDR) != ESPNOW_BIND_STORAGE_VERSION) {
      for (int i = 0; i < MAX_MODELS; i++) {
        clearBoundReceiver(i);
      }
      return;
    }

    int addr = EEPROM_BIND_DATA_ADDR;
    for (int i = 0; i < MAX_MODELS; i++) {
      for (int b = 0; b < ESP_NOW_ETH_ALEN; b++) {
        boundReceiverMacs[i][b] = EEPROM.read(addr++);
      }
    }
  }

  void saveReceiverBindings() {
    int addr = EEPROM_BIND_DATA_ADDR;
    for (int i = 0; i < MAX_MODELS; i++) {
      for (int b = 0; b < ESP_NOW_ETH_ALEN; b++) {
        EEPROM.write(addr++, boundReceiverMacs[i][b]);
      }
    }

    EEPROM.write(EEPROM_BIND_VERSION_ADDR, ESPNOW_BIND_STORAGE_VERSION);
    EEPROM.commit();
  }

  void loadFailsafeValues() {
    if (EEPROM.read(EEPROM_FAILSAFE_VERSION_ADDR) != FAILSAFE_STORAGE_VERSION) {
      for (int i = 0; i < MAX_MODELS; i++) {
        clearModelFailsafeValues(i);
      }
      return;
    }

    int addr = EEPROM_FAILSAFE_DATA_ADDR;
    for (int i = 0; i < MAX_MODELS; i++) {
      for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
        modelFailsafeValues[i][channel] = (int8_t)EEPROM.read(addr++);
      }
    }
  }

  void saveFailsafeValues() {
    int addr = EEPROM_FAILSAFE_DATA_ADDR;
    for (int i = 0; i < MAX_MODELS; i++) {
      for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
        EEPROM.write(addr++, (uint8_t)modelFailsafeValues[i][channel]);
      }
    }

    EEPROM.write(EEPROM_FAILSAFE_VERSION_ADDR, FAILSAFE_STORAGE_VERSION);
    EEPROM.commit();
  }

  void loadRateValues() {
    if (EEPROM.read(EEPROM_RATE_VERSION_ADDR) != RATE_STORAGE_VERSION) {
      for (int i = 0; i < MAX_MODELS; i++) {
        clearModelRateValues(i);
      }
      return;
    }

    int addr = EEPROM_RATE_DATA_ADDR;
    for (int i = 0; i < MAX_MODELS; i++) {
      for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
        uint8_t storedValue = EEPROM.read(addr++);
        modelRateValues[i][channel] = constrain(storedValue, 1, 100);
      }
    }
  }

  void saveRateValues() {
    int addr = EEPROM_RATE_DATA_ADDR;
    for (int i = 0; i < MAX_MODELS; i++) {
      for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
        EEPROM.write(addr++, modelRateValues[i][channel]);
      }
    }

    EEPROM.write(EEPROM_RATE_VERSION_ADDR, RATE_STORAGE_VERSION);
    EEPROM.commit();
  }

  void loadExpoValues() {
    if (EEPROM.read(EEPROM_EXPO_VERSION_ADDR) != EXPO_STORAGE_VERSION) {
      for (int i = 0; i < MAX_MODELS; i++) {
        clearModelExpoValues(i);
      }
      return;
    }

    int addr = EEPROM_EXPO_DATA_ADDR;
    for (int i = 0; i < MAX_MODELS; i++) {
      for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
        int8_t storedValue = (int8_t)EEPROM.read(addr++);
        modelExpoValues[i][channel] = (int8_t)constrain((int)storedValue, -100, 100);
      }
    }
  }

  void saveExpoValues() {
    int addr = EEPROM_EXPO_DATA_ADDR;
    for (int i = 0; i < MAX_MODELS; i++) {
      for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
        EEPROM.write(addr++, (uint8_t)modelExpoValues[i][channel]);
      }
    }

    EEPROM.write(EEPROM_EXPO_VERSION_ADDR, EXPO_STORAGE_VERSION);
    EEPROM.commit();
  }

  void loadEndpointValues() {
    if (EEPROM.read(EEPROM_ENDPOINT_VERSION_ADDR) != ENDPOINT_STORAGE_VERSION) {
      for (int i = 0; i < MAX_MODELS; i++) {
        clearModelEndpointValues(i);
      }
      return;
    }

    int addrLow = EEPROM_ENDPOINT_LOW_DATA_ADDR;
    int addrHigh = EEPROM_ENDPOINT_HIGH_DATA_ADDR;
    for (int i = 0; i < MAX_MODELS; i++) {
      for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
        uint8_t lowStored = EEPROM.read(addrLow++);
        uint8_t highStored = EEPROM.read(addrHigh++);
        modelEndpointLowValues[i][channel] = constrain(lowStored, 1, 120);
        modelEndpointHighValues[i][channel] = constrain(highStored, 1, 120);
      }
    }
  }

  void saveEndpointValues() {
    int addrLow = EEPROM_ENDPOINT_LOW_DATA_ADDR;
    int addrHigh = EEPROM_ENDPOINT_HIGH_DATA_ADDR;
    for (int i = 0; i < MAX_MODELS; i++) {
      for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
        EEPROM.write(addrLow++, modelEndpointLowValues[i][channel]);
        EEPROM.write(addrHigh++, modelEndpointHighValues[i][channel]);
      }
    }

    EEPROM.write(EEPROM_ENDPOINT_VERSION_ADDR, ENDPOINT_STORAGE_VERSION);
    EEPROM.commit();
  }

  bool loadTankEscChannelMap() {
    if (EEPROM.read(EEPROM_ESC_MAP_VERSION_ADDR) != ESC_MAP_STORAGE_VERSION) {
      for (int i = 0; i < MAX_MODELS; i++) {
        setModelTankEscChannels(i, 1, 0);
      }
      return false;
    }

    uint8_t packed0 = EEPROM.read(EEPROM_ESC_MAP_DATA_ADDR);
    uint8_t packed1 = EEPROM.read(EEPROM_ESC_MAP_DATA_ADDR + 1);

    for (int i = 0; i < MAX_MODELS; i++) {
      uint8_t nibble = (i < 2)
        ? ((packed0 >> (i * 4)) & 0x0F)
        : ((packed1 >> ((i - 2) * 4)) & 0x0F);
      uint8_t left = nibble & 0x03;
      uint8_t right = (nibble >> 2) & 0x03;
      setModelTankEscChannels(i, left, right);
    }

    return true;
  }

  void saveTankEscChannelMap() {
    uint8_t packed0 = 0;
    uint8_t packed1 = 0;

    for (int i = 0; i < MAX_MODELS; i++) {
      sanitizeModelTankEscChannels(i);
      uint8_t nibble =
        (modelTankLeftEscChannel[i] & 0x03) |
        ((modelTankRightEscChannel[i] & 0x03) << 2);
      if (i < 2) {
        packed0 |= (nibble << (i * 4));
      } else {
        packed1 |= (nibble << ((i - 2) * 4));
      }
    }

    EEPROM.write(EEPROM_ESC_MAP_DATA_ADDR, packed0);
    EEPROM.write(EEPROM_ESC_MAP_DATA_ADDR + 1, packed1);
    EEPROM.write(EEPROM_ESC_MAP_VERSION_ADDR, ESC_MAP_STORAGE_VERSION);
    EEPROM.commit();
  }

  int loadActiveModelIndex() {
    if (EEPROM.read(EEPROM_ACTIVE_MODEL_VERSION_ADDR) != ACTIVE_MODEL_STORAGE_VERSION) {
      return 0;
    }

    uint8_t storedIndex = EEPROM.read(EEPROM_ACTIVE_MODEL_INDEX_ADDR);
    if (storedIndex >= MAX_MODELS) return 0;
    return storedIndex;
  }

  void saveActiveModelIndex() {
    uint8_t index = (activeModel >= 0 && activeModel < MAX_MODELS) ? activeModel : 0;
    EEPROM.write(EEPROM_ACTIVE_MODEL_INDEX_ADDR, index);
    EEPROM.write(EEPROM_ACTIVE_MODEL_VERSION_ADDR, ACTIVE_MODEL_STORAGE_VERSION);
    EEPROM.commit();
  }

  void loadEspNowReceiverOutputModes() {
    if (EEPROM.read(EEPROM_ESPNOW_OUTPUT_MODE_VERSION_ADDR) != ESPNOW_OUTPUT_MODE_STORAGE_VERSION) {
      for (int i = 0; i < MAX_MODELS; i++) {
        espNowReceiverOutputModes[i] = ESPNOW_OUTPUT_PWM;
      }
      return;
    }

    for (int i = 0; i < MAX_MODELS; i++) {
      uint8_t storedMode = EEPROM.read(EEPROM_ESPNOW_OUTPUT_MODE_DATA_ADDR + i);
      espNowReceiverOutputModes[i] = (storedMode == ESPNOW_OUTPUT_IBUS)
        ? ESPNOW_OUTPUT_IBUS
        : ESPNOW_OUTPUT_PWM;
    }
  }

  void saveEspNowReceiverOutputModes() {
    for (int i = 0; i < MAX_MODELS; i++) {
      uint8_t storedMode = (espNowReceiverOutputModes[i] == ESPNOW_OUTPUT_IBUS)
        ? ESPNOW_OUTPUT_IBUS
        : ESPNOW_OUTPUT_PWM;
      EEPROM.write(EEPROM_ESPNOW_OUTPUT_MODE_DATA_ADDR + i, storedMode);
    }
    EEPROM.write(EEPROM_ESPNOW_OUTPUT_MODE_VERSION_ADDR, ESPNOW_OUTPUT_MODE_STORAGE_VERSION);
    EEPROM.commit();
  }

  EspNowReceiverOutputMode getModelEspNowOutputMode(int modelIndex) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return ESPNOW_OUTPUT_PWM;
    return (espNowReceiverOutputModes[modelIndex] == ESPNOW_OUTPUT_IBUS)
      ? ESPNOW_OUTPUT_IBUS
      : ESPNOW_OUTPUT_PWM;
  }

  void setModelEspNowOutputMode(int modelIndex, EspNowReceiverOutputMode mode) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return;
    espNowReceiverOutputModes[modelIndex] =
      (mode == ESPNOW_OUTPUT_IBUS) ? ESPNOW_OUTPUT_IBUS : ESPNOW_OUTPUT_PWM;
  }

  void sanitizeDisplaySettings() {
    bool brightnessPresetValid = false;
    for (int i = 0; i < displayBrightnessOptionCount; i++) {
      if (displayBrightness == displayBrightnessOptions[i]) {
        brightnessPresetValid = true;
        break;
      }
    }
    if (!brightnessPresetValid) {
      displayBrightness = DEFAULT_DISPLAY_BRIGHTNESS;
    }

    bool sleepPresetValid = false;
    for (int i = 0; i < displaySleepBrightnessOptionCount; i++) {
      if (displaySleepBrightness == displaySleepBrightnessOptions[i]) {
        sleepPresetValid = true;
        break;
      }
    }
    if (!sleepPresetValid) {
      displaySleepBrightness = DEFAULT_DISPLAY_SLEEP_BRIGHTNESS;
    }

    displaySleepBrightness = constrain(displaySleepBrightness, (uint8_t)0, displayBrightness);
    if (displayTimeoutIndex >= displayTimeoutOptionCount) {
      displayTimeoutIndex = DEFAULT_DISPLAY_TIMEOUT_INDEX;
    }
    if (displayOffTimeoutIndex >= displayOffTimeoutOptionCount) {
      displayOffTimeoutIndex = DEFAULT_DISPLAY_OFF_TIMEOUT_INDEX;
    }
    if (themeMode > THEME_LIGHT) {
      themeMode = THEME_DARK;
    }

    // Keep dim timeout <= off timeout whenever off timeout is finite.
    uint16_t dimSec = displayTimeoutOptionsSec[displayTimeoutIndex];
    uint16_t offSec = displayOffTimeoutOptionsSec[displayOffTimeoutIndex];
    if (offSec != 0 && (dimSec == 0 || dimSec > offSec)) {
      int bestDimIndex = -1;
      for (int i = 0; i < displayTimeoutOptionCount; i++) {
        uint16_t sec = displayTimeoutOptionsSec[i];
        if (sec == 0) continue; // Never is not valid when off timeout is finite.
        if (sec <= offSec) bestDimIndex = i;
      }

      if (bestDimIndex >= 0) {
        displayTimeoutIndex = (uint8_t)bestDimIndex;
      } else {
        // Off timeout is lower than the minimum dim option.
        // Raise off timeout to the smallest finite value that can accommodate dim.
        displayTimeoutIndex = 0;
        uint16_t minDimSec = displayTimeoutOptionsSec[displayTimeoutIndex];
        for (int j = 0; j < displayOffTimeoutOptionCount; j++) {
          uint16_t offCandidate = displayOffTimeoutOptionsSec[j];
          if (offCandidate != 0 && offCandidate >= minDimSec) {
            displayOffTimeoutIndex = (uint8_t)j;
            break;
          }
        }
      }
    }
  }

  void resetDisplaySettingsToDefaults() {
    displayBrightness = DEFAULT_DISPLAY_BRIGHTNESS;
    displaySleepBrightness = DEFAULT_DISPLAY_SLEEP_BRIGHTNESS;
    displayTimeoutIndex = DEFAULT_DISPLAY_TIMEOUT_INDEX;
    displayOffTimeoutIndex = DEFAULT_DISPLAY_OFF_TIMEOUT_INDEX;
    themeMode = THEME_DARK;
    sanitizeDisplaySettings();
  }

  bool loadDisplaySettings() {
    if (EEPROM.read(EEPROM_DISPLAY_VERSION_ADDR) != DISPLAY_SETTINGS_STORAGE_VERSION) {
      resetDisplaySettingsToDefaults();
      return true;
    }

    uint8_t loadedBrightness = EEPROM.read(EEPROM_DISPLAY_BRIGHTNESS_ADDR);
    uint8_t loadedSleepBrightness = EEPROM.read(EEPROM_DISPLAY_SLEEP_BRIGHTNESS_ADDR);
    uint8_t loadedTimeoutIndex = EEPROM.read(EEPROM_DISPLAY_TIMEOUT_INDEX_ADDR);
    uint8_t loadedOffTimeoutIndex = EEPROM.read(EEPROM_DISPLAY_OFF_TIMEOUT_INDEX_ADDR);
    uint8_t loadedThemeMode = EEPROM.read(EEPROM_DISPLAY_THEME_ADDR);

    displayBrightness = loadedBrightness;
    displaySleepBrightness = loadedSleepBrightness;
    displayTimeoutIndex = loadedTimeoutIndex;
    displayOffTimeoutIndex = loadedOffTimeoutIndex;
    themeMode = loadedThemeMode;
    sanitizeDisplaySettings();

    return (displayBrightness != loadedBrightness) ||
           (displaySleepBrightness != loadedSleepBrightness) ||
           (displayTimeoutIndex != loadedTimeoutIndex) ||
           (displayOffTimeoutIndex != loadedOffTimeoutIndex) ||
           (themeMode != loadedThemeMode);
  }

  void saveDisplaySettings() {
    sanitizeDisplaySettings();
    EEPROM.write(EEPROM_DISPLAY_BRIGHTNESS_ADDR, displayBrightness);
    EEPROM.write(EEPROM_DISPLAY_SLEEP_BRIGHTNESS_ADDR, displaySleepBrightness);
    EEPROM.write(EEPROM_DISPLAY_TIMEOUT_INDEX_ADDR, displayTimeoutIndex);
    EEPROM.write(EEPROM_DISPLAY_OFF_TIMEOUT_INDEX_ADDR, displayOffTimeoutIndex);
    EEPROM.write(EEPROM_DISPLAY_THEME_ADDR, themeMode);
    EEPROM.write(EEPROM_DISPLAY_VERSION_ADDR, DISPLAY_SETTINGS_STORAGE_VERSION);
    EEPROM.commit();
  }

  void applyThemePalette() {
    if (themeMode == THEME_LIGHT) {
      COLOR_BG = tft.color565(244, 236, 214);
      COLOR_PANEL = tft.color565(230, 219, 193);
      COLOR_TEXT = tft.color565(50, 36, 16);
      COLOR_ACCENT = tft.color565(180, 140, 40);
      COLOR_ACCENT_HI = tft.color565(226, 184, 84);
      COLOR_SIG = tft.color565(56, 132, 64);
      COLOR_STICK_PANEL = tft.color565(216, 204, 178);
    } else {
      COLOR_BG = tft.color565(0, 0, 0);
      COLOR_PANEL = tft.color565(30, 30, 30);
      COLOR_TEXT = tft.color565(255, 255, 255);
      COLOR_ACCENT = tft.color565(180, 140, 40);
      COLOR_ACCENT_HI = tft.color565(255, 210, 80);
      COLOR_SIG = tft.color565(49, 146, 49);
      COLOR_STICK_PANEL = tft.color565(20, 20, 20);
    }

    stickBaseSpriteNeedsRebuild = true;
  }

  int findDisplayOptionIndex(const uint8_t *options, int optionCount, uint8_t value) {
    int bestIndex = 0;
    int bestDistance = 1000;

    for (int i = 0; i < optionCount; i++) {
      int distance = abs((int)options[i] - (int)value);
      if (distance < bestDistance) {
        bestDistance = distance;
        bestIndex = i;
      }
    }

    return bestIndex;
  }

  void stepDisplayOption(uint8_t &value, const uint8_t *options, int optionCount, int delta) {
    int index = findDisplayOptionIndex(options, optionCount, value);
    index = constrain(index + delta, 0, optionCount - 1);
    value = options[index];
  }

  unsigned long getDisplayDimTimeoutMs() {
    if (displayTimeoutIndex >= displayTimeoutOptionCount) {
      return (unsigned long)displayTimeoutOptionsSec[DEFAULT_DISPLAY_TIMEOUT_INDEX] * 1000UL;
    }

    uint16_t seconds = displayTimeoutOptionsSec[displayTimeoutIndex];
    if (seconds == 0) return 0;
    return (unsigned long)seconds * 1000UL;
  }

  unsigned long getDisplayOffTimeoutMs() {
    if (displayOffTimeoutIndex >= displayOffTimeoutOptionCount) {
      return (unsigned long)displayOffTimeoutOptionsSec[DEFAULT_DISPLAY_OFF_TIMEOUT_INDEX] * 1000UL;
    }

    uint16_t seconds = displayOffTimeoutOptionsSec[displayOffTimeoutIndex];
    if (seconds == 0) return 0;
    return (unsigned long)seconds * 1000UL;
  }

  const char* formatDisplayTimeoutValue(uint8_t optionIndex, const uint16_t *options, uint8_t optionCount,
                                        char *buffer, size_t bufferSize) {
    if (optionIndex >= optionCount) {
      snprintf(buffer, bufferSize, "30s");
      return buffer;
    }

    uint16_t seconds = options[optionIndex];
    if (seconds == 0) {
      snprintf(buffer, bufferSize, "Never");
    }
    else if (seconds < 60) {
      snprintf(buffer, bufferSize, "%us", (unsigned int)seconds);
    }
    else {
      snprintf(buffer, bufferSize, "%um", (unsigned int)(seconds / 60));
    }
    return buffer;
  }

  const char* getThemeToggleLabel() {
    return (themeMode == THEME_DARK) ? "Light Mode" : "Dark Mode";
  }

  void applyDisplayBacklight() {
    uint8_t targetBrightness = BRIGHTNESS_OFF;
    if (screenAwake) {
      targetBrightness = displayDimmed ? displaySleepBrightness : displayBrightness;
    }
    targetBrightness = min(targetBrightness, displayBrightness);

    bool shouldEnablePanel = (screenAwake && targetBrightness > 0);
    if (panelDisplayEnabled != shouldEnablePanel) {
      tft.writecommand(shouldEnablePanel ? ILI9341_DISPON : ILI9341_DISPOFF);
      panelDisplayEnabled = shouldEnablePanel;
    }

    if (lastAppliedBacklightBrightness != targetBrightness) {
      analogWrite(DISPLAY_BL_PIN, targetBrightness);
      if (targetBrightness == BRIGHTNESS_OFF) {
        digitalWrite(DISPLAY_BL_PIN, !TFT_BACKLIGHT_ON);
      }
      lastAppliedBacklightBrightness = targetBrightness;

#if DISPLAY_BACKLIGHT_SERIAL_DEBUG
      Serial.printf("DISPLAY: backlight target=%u awake=%u dimmed=%u duty=%lu freq=%lu\n",
                    targetBrightness,
                    screenAwake ? 1 : 0,
                    displayDimmed ? 1 : 0,
                    (unsigned long)ledcRead(DISPLAY_BL_PIN),
                    (unsigned long)ledcReadFreq(DISPLAY_BL_PIN));
#endif
    }
  }

  void initStatusRgbLed() {
    if (!STATUS_RGB_LED_ENABLED) return;
    pinMode(STATUS_RGB_LED_PIN, OUTPUT);
    setStatusRgbLed(0, 0, 0);
  }

  void setStatusRgbLed(uint8_t red, uint8_t green, uint8_t blue) {
    if (!STATUS_RGB_LED_ENABLED) return;

    uint16_t brightness = STATUS_RGB_LED_BRIGHTNESS;
    uint8_t scaledRed = (uint8_t)(((uint16_t)red * brightness) / 255U);
    uint8_t scaledGreen = (uint8_t)(((uint16_t)green * brightness) / 255U);
    uint8_t scaledBlue = (uint8_t)(((uint16_t)blue * brightness) / 255U);
    neopixelWrite(STATUS_RGB_LED_PIN, scaledRed, scaledGreen, scaledBlue);
  }

  bool isI2cDevicePresent(uint8_t address) {
    Wire.beginTransmission(address);
    return Wire.endTransmission() == 0;
  }

  void setAudioAmpEnabled(bool enabled) {
#if AUDIO_ENABLED
    pinMode(AUDIO_PA_ENABLE_PIN, OUTPUT);
    digitalWrite(AUDIO_PA_ENABLE_PIN, enabled ? AUDIO_PA_ENABLE_LEVEL : !AUDIO_PA_ENABLE_LEVEL);
#else
    (void)enabled;
#endif
  }

  int16_t scaleAudioSampleForUserVolume(int16_t sample) {
#if AUDIO_ENABLED
    if (audioUserMuted || audioUserVolumePercent == 0) return 0;
    int32_t scaled = ((int32_t)sample * (int32_t)audioUserVolumePercent *
                      (int32_t)AUDIO_PCM_HEADROOM_PERCENT) / 10000L;
    return (int16_t)constrain(scaled, (int32_t)-32768, (int32_t)32767);
#else
    return sample;
#endif
  }

  void applyAudioUserMuteState() {
#if AUDIO_ENABLED
    if (!audioReady) return;
    float codecVolume = (float)audioUserVolumePercent / 100.0f;
    hosyondAudioOutput.setVolume(codecVolume);
    hosyondAudioOutput.setMute(audioUserMuted || audioUserVolumePercent == 0);
    setAudioAmpEnabled(!audioUserMuted && audioUserVolumePercent > 0);
#endif
  }

  void loadAudioSettings() {
#if AUDIO_ENABLED
    Preferences preferences;
    if (!preferences.begin(AUDIO_PREF_NAMESPACE, true)) return;
    audioUserVolumePercent = constrain(
      preferences.getUChar(AUDIO_PREF_VOLUME_KEY, AUDIO_DEFAULT_USER_VOLUME_PERCENT),
      0, 100);
    audioUserMuted = preferences.getBool(AUDIO_PREF_MUTED_KEY, false);
    audioVolumeBeforeMutePercent = constrain(
      preferences.getUChar(AUDIO_PREF_PRE_MUTE_KEY, AUDIO_DEFAULT_USER_VOLUME_PERCENT),
      1, 100);
    preferences.end();
    if (audioUserVolumePercent == 0) audioUserMuted = true;
    audioSettingsDirty = false;
#endif
  }

  void saveAudioSettings() {
#if AUDIO_ENABLED
    if (!audioSettingsDirty) return;
    Preferences preferences;
    if (!preferences.begin(AUDIO_PREF_NAMESPACE, false)) return;
    preferences.putUChar(AUDIO_PREF_VOLUME_KEY, audioUserVolumePercent);
    preferences.putBool(AUDIO_PREF_MUTED_KEY, audioUserMuted);
    preferences.putUChar(AUDIO_PREF_PRE_MUTE_KEY, audioVolumeBeforeMutePercent);
    preferences.end();
    audioSettingsDirty = false;
    Serial.printf("AUDIO: saved volume=%u%% muted=%u\n",
                  audioUserVolumePercent, audioUserMuted ? 1 : 0);
#endif
  }

  void silenceAudioOutput(bool stopI2s) {
#if AUDIO_ENABLED
    audioStartupRetestPending = false;
    setAudioAmpEnabled(false);
    if (audioCodecReady) {
      hosyondAudioOutput.setMute(true);
    }
    if (audioI2sReady) {
      int silentFrames = (int)(((uint32_t)AUDIO_SAMPLE_RATE * AUDIO_DRAIN_SILENCE_MS) / 1000U);
      if (silentFrames < AUDIO_ZERO_FLUSH_FRAMES) silentFrames = AUDIO_ZERO_FLUSH_FRAMES;
      int16_t zeroFrame[2] = {0, 0};
      for (int i = 0; i < silentFrames; i++) {
        hosyondAudioOutput.write((const uint8_t*)zeroFrame, sizeof(zeroFrame));
      }
      if (stopI2s) {
        hosyondAudioOutput.end();
        audioI2sReady = false;
        audioReady = false;
      }
    }
#else
    (void)stopI2s;
#endif
  }

  void initAudio() {
#if AUDIO_ENABLED
    audioReady = false;
    audioCodecReady = false;
    audioI2sReady = false;
    audioStartupRetestPending = false;
    audioStartupRetestTime = 0;
    Serial.printf("AUDIO: init begin addr=0x%02X paPin=%d activeLevel=%d\n",
                  AUDIO_CODEC_I2C_ADDR,
                  AUDIO_PA_ENABLE_PIN,
                  AUDIO_PA_ENABLE_LEVEL);
    setAudioAmpEnabled(false);

    if (!isI2cDevicePresent(AUDIO_CODEC_I2C_ADDR)) {
      Serial.printf("AUDIO: ES8311 not detected at 0x%02X; UI clicks disabled\n", AUDIO_CODEC_I2C_ADDR);
      return;
    }
    Serial.printf("AUDIO: ES8311 ACK at 0x%02X\n", AUDIO_CODEC_I2C_ADDR);

    hosyondAudioPins.addI2C(PinFunction::CODEC, Wire, false);
    hosyondAudioPins.addI2S(PinFunction::CODEC,
                            AUDIO_I2S_MCLK_PIN,
                            AUDIO_I2S_BCLK_PIN,
                            AUDIO_I2S_WS_PIN,
                            AUDIO_I2S_PLAYBACK_DATA_PIN,
                            -1,
                            0);

    auto audioConfig = hosyondAudioOutput.defaultConfig(TX_MODE);
    audioConfig.sample_rate = AUDIO_SAMPLE_RATE;
    audioConfig.channels = 2;
    audioConfig.bits_per_sample = 16;
    audioReady = hosyondAudioOutput.begin(audioConfig);
    audioCodecReady = audioReady;
    audioI2sReady = audioReady;
    Serial.printf("AUDIO: stream begin result=%u sampleRate=%u square=%u\n",
                  audioReady ? 1 : 0,
                  AUDIO_SAMPLE_RATE,
                  AUDIO_USE_SQUARE_TEST_TONE ? 1 : 0);
    if (audioCodecReady) {
      hosyondAudioOutput.setVolume((float)audioUserVolumePercent / 100.0f);
      hosyondAudioOutput.setMute(audioUserMuted);
    }

    setAudioAmpEnabled(audioReady && !audioUserMuted);
    if (audioReady) {
      delay(AUDIO_AMP_SETTLE_MS);
    }
    Serial.printf("AUDIO: ES8311=%u i2s=%u amp=%s ampPin=%d active=%d level=%d userVolume=%u%% muted=%u pins mclk=%d bclk=%d playback=%d mic=%d ws=%d\n",
                  audioCodecReady ? 1 : 0,
                  audioI2sReady ? 1 : 0,
                  audioReady ? "on" : "off",
                  AUDIO_PA_ENABLE_PIN,
                  AUDIO_PA_ENABLE_LEVEL,
                  digitalRead(AUDIO_PA_ENABLE_PIN),
                  audioUserVolumePercent,
                  audioUserMuted ? 1 : 0,
                  AUDIO_I2S_MCLK_PIN,
                  AUDIO_I2S_BCLK_PIN,
                  AUDIO_I2S_PLAYBACK_DATA_PIN,
                  AUDIO_I2S_MIC_DATA_PIN,
                  AUDIO_I2S_WS_PIN);

    if (audioReady && AUDIO_STARTUP_TEST_TONE) {
      playAudioTone(660, 180, AUDIO_TONE_AMPLITUDE, false);
      delay(35);
      playAudioTone(990, 180, AUDIO_TONE_AMPLITUDE, false);
      audioStartupRetestPending = true;
      audioStartupRetestTime = millis() + AUDIO_RETEST_DELAY_MS;
    }
#endif
  }

  void updateAudioDiagnostics(unsigned long now) {
#if AUDIO_ENABLED
    if (!audioStartupRetestPending || !audioReady) return;
    if ((long)(now - audioStartupRetestTime) < 0) return;
    audioStartupRetestPending = false;
    setAudioAmpEnabled(true);
    delay(AUDIO_AMP_SETTLE_MS);
    Serial.printf("AUDIO: delayed test tone ampLevel=%d ready=%u/%u\n",
                  digitalRead(AUDIO_PA_ENABLE_PIN),
                  audioCodecReady ? 1 : 0,
                  audioI2sReady ? 1 : 0);
    playAudioTone(440, 350, AUDIO_TONE_AMPLITUDE, false);
#else
    (void)now;
#endif
  }

  void writeAudioStereoSample(int16_t sample) {
#if AUDIO_ENABLED
    sample = scaleAudioSampleForUserVolume(sample);
    int16_t stereoFrame[2] = { sample, sample };
    hosyondAudioOutput.write((const uint8_t*)stereoFrame, sizeof(stereoFrame));
#else
    (void)sample;
#endif
  }

  void startStartupAudioClip() {
#if AUDIO_ENABLED
    if (!STARTUP_AUDIO_ENABLED || startupAudioStarted || !audioReady) return;
    startupAudioStarted = true;
    startupAudioPlaying = true;
    startupAudioSampleIndex = 0;
    startupAudioSourcePhase = 0;
    applyAudioUserMuteState();
    Serial.printf("AUDIO: startup clip start samples=%lu sourceRate=%lu targetRate=%u duration=%lums\n",
                  (unsigned long)CAVE_JOHNSON_STARTUP_SAMPLE_COUNT,
                  (unsigned long)CAVE_JOHNSON_STARTUP_SAMPLE_RATE,
                  AUDIO_SAMPLE_RATE,
                  (unsigned long)CAVE_JOHNSON_STARTUP_DURATION_MS);
#endif
  }

  void updateStartupAudioClip() {
#if AUDIO_ENABLED
    if (!startupAudioPlaying || !audioReady) return;

    int16_t stereoFrames[STARTUP_AUDIO_CHUNK_FRAMES * 2];
    uint16_t framesWritten = 0;
    while (framesWritten < STARTUP_AUDIO_CHUNK_FRAMES &&
           startupAudioSampleIndex < CAVE_JOHNSON_STARTUP_SAMPLE_COUNT) {
      uint32_t nextIndex = (startupAudioSampleIndex + 1U < CAVE_JOHNSON_STARTUP_SAMPLE_COUNT)
        ? startupAudioSampleIndex + 1U
        : startupAudioSampleIndex;
      int32_t sample0 = (int16_t)pgm_read_word(&CAVE_JOHNSON_STARTUP_PCM[startupAudioSampleIndex]);
      int32_t sample1 = (int16_t)pgm_read_word(&CAVE_JOHNSON_STARTUP_PCM[nextIndex]);
      int32_t interpolated = sample0 + (int32_t)(
        ((int64_t)(sample1 - sample0) * (int64_t)startupAudioSourcePhase) /
        (int64_t)AUDIO_SAMPLE_RATE);
      int16_t sample = (int16_t)interpolated;
      sample = scaleAudioSampleForUserVolume(sample);
      stereoFrames[framesWritten * 2] = sample;
      stereoFrames[framesWritten * 2 + 1] = sample;
      framesWritten++;

      startupAudioSourcePhase += CAVE_JOHNSON_STARTUP_SAMPLE_RATE;
      while (startupAudioSourcePhase >= AUDIO_SAMPLE_RATE) {
        startupAudioSourcePhase -= AUDIO_SAMPLE_RATE;
        startupAudioSampleIndex++;
      }
    }

    if (framesWritten > 0) {
      const uint8_t *audioBytes = (const uint8_t*)stereoFrames;
      size_t totalBytes = (size_t)framesWritten * 2U * sizeof(int16_t);
      size_t bytesWritten = 0;
      unsigned long writeStartedAt = millis();
      while (bytesWritten < totalBytes && millis() - writeStartedAt < 100UL) {
        size_t written = hosyondAudioOutput.write(audioBytes + bytesWritten,
                                                  totalBytes - bytesWritten);
        if (written == 0) {
          delay(1);
        } else {
          bytesWritten += written;
        }
      }
      if (bytesWritten < totalBytes) {
        Serial.printf("AUDIO: short startup write %u/%u bytes\n",
                      (unsigned int)bytesWritten, (unsigned int)totalBytes);
      }
    }

    if (startupAudioSampleIndex >= CAVE_JOHNSON_STARTUP_SAMPLE_COUNT) {
      startupAudioPlaying = false;
      silenceAudioOutput(false);
      Serial.println("AUDIO: startup clip complete");
    }
#endif
  }

  void playAudioTone(uint16_t frequency, uint16_t durationMs, uint16_t amplitude, bool gated) {
#if AUDIO_ENABLED
    if (!audioReady || audioUserMuted || audioUserVolumePercent == 0) return;
    applyAudioUserMuteState();
    unsigned long now = millis();
    if (gated) {
      if (now - lastAudioClickMs < AUDIO_CLICK_MIN_INTERVAL_MS) return;
      lastAudioClickMs = now;
    }

    int sampleCount = (int)(((uint32_t)AUDIO_SAMPLE_RATE * durationMs) / 1000U);
    if (sampleCount < 1) sampleCount = 1;
    int fadeSamples = (int)((AUDIO_SAMPLE_RATE * 6UL) / 1000UL);
    if (fadeSamples < 1) fadeSamples = 1;
    if (fadeSamples > (sampleCount / 2)) fadeSamples = sampleCount / 2;

    int16_t stereoFrame[2] = {0, 0};
    for (int i = 0; i < sampleCount; i++) {
      float envelope = 1.0f;
      if (i < fadeSamples) {
        envelope = (float)i / (float)fadeSamples;
      } else if (i > (sampleCount - fadeSamples)) {
        envelope = (float)(sampleCount - i) / (float)fadeSamples;
      }
      if (envelope < 0.0f) envelope = 0.0f;
      int16_t sample = 0;
      if (AUDIO_USE_SQUARE_TEST_TONE) {
        uint32_t cyclePos = ((uint32_t)i * (uint32_t)frequency * 2UL) / (uint32_t)AUDIO_SAMPLE_RATE;
        sample = (cyclePos & 1U) ? -(int16_t)amplitude : (int16_t)amplitude;
        sample = (int16_t)((float)sample * envelope);
      } else {
        float phase = (2.0f * 3.14159265f * (float)frequency * (float)i) / (float)AUDIO_SAMPLE_RATE;
        sample = (int16_t)(sinf(phase) * (float)amplitude * envelope);
      }
      sample = scaleAudioSampleForUserVolume(sample);
      if (AUDIO_USE_SQUARE_TEST_TONE) {
        int16_t scaledFrame[2] = { sample, sample };
        hosyondAudioOutput.write((const uint8_t*)scaledFrame, sizeof(scaledFrame));
      } else {
        stereoFrame[0] = sample;
        stereoFrame[1] = sample;
        hosyondAudioOutput.write((const uint8_t*)stereoFrame, sizeof(stereoFrame));
      }
    }

    stereoFrame[0] = 0;
    stereoFrame[1] = 0;
    for (uint8_t i = 0; i < AUDIO_ZERO_FLUSH_FRAMES; i++) {
      if (AUDIO_USE_SQUARE_TEST_TONE) {
        writeAudioStereoSample(0);
      } else {
        hosyondAudioOutput.write((const uint8_t*)stereoFrame, sizeof(stereoFrame));
      }
    }
#else
    (void)frequency;
    (void)durationMs;
    (void)amplitude;
    (void)gated;
#endif
  }

  void playAudioClick(uint16_t frequency, uint16_t durationMs) {
#if AUDIO_ENABLED
    playAudioTone(frequency, durationMs, AUDIO_CLICK_AMPLITUDE, true);
#else
    (void)frequency;
    (void)durationMs;
#endif
  }

  void getStatusRgbLedPatternColor(StatusRgbDiagCode code, uint8_t step, uint8_t &red, uint8_t &green, uint8_t &blue) {
    red = 0;
    green = 0;
    blue = 0;

    switch (code) {
      case STATUS_RGB_NO_BATTERY:
        // No battery detected: red, off, red. Highest-priority power fault.
        if (step != 1) red = 255;
        break;

      case STATUS_RGB_NO_ADS:
        // No stick ADC: red, blue, green.
        if (step == 0) red = 255;
        else if (step == 1) blue = 255;
        else green = 255;
        break;

      case STATUS_RGB_NO_ELRS:
        // No ELRS module response: red, blue, blue.
        if (step == 0) red = 255;
        else blue = 255;
        break;

      case STATUS_RGB_NO_MCP23017:
        // No daughterboard GPIO expander: red, green, red.
        if (step == 1) green = 255;
        else red = 255;
        break;

      case STATUS_RGB_DPAD_SELECT:
        // Select button: white, white, off.
        if (step < 2) {
          red = 255;
          green = 255;
          blue = 255;
        }
        break;

      case STATUS_RGB_DPAD_LEFT:
        // Left: blue, off, off.
        if (step == 0) blue = 255;
        break;

      case STATUS_RGB_DPAD_UP:
        // Up: green, off, off.
        if (step == 0) green = 255;
        break;

      case STATUS_RGB_DPAD_DOWN:
        // Down: red, off, off.
        if (step == 0) red = 255;
        break;

      case STATUS_RGB_DPAD_RIGHT:
        // Right: yellow, off, off.
        if (step == 0) {
          red = 255;
          green = 180;
        }
        break;

      case STATUS_RGB_OK:
      default:
        break;
    }
  }

  void setStatusRgbLedPattern(StatusRgbDiagCode code, uint8_t step, unsigned long stepElapsed) {
    uint8_t red = 0;
    uint8_t green = 0;
    uint8_t blue = 0;
    getStatusRgbLedPatternColor(code, step, red, green, blue);

    bool currentOn = red != 0 || green != 0 || blue != 0;

    uint8_t prevRed = 0;
    uint8_t prevGreen = 0;
    uint8_t prevBlue = 0;
    uint8_t prevStep = (step == 0) ? 2 : (uint8_t)(step - 1);
    getStatusRgbLedPatternColor(code, prevStep, prevRed, prevGreen, prevBlue);

    bool repeatedPrevious = currentOn && red == prevRed && green == prevGreen && blue == prevBlue;
    if (repeatedPrevious && stepElapsed < STATUS_RGB_LED_REPEAT_GAP_MS) {
      setStatusRgbLed(0, 0, 0);
    } else {
      setStatusRgbLed(red, green, blue);
    }
  }

  uint8_t collectStatusRgbDiagCodes(StatusRgbDiagCode *codes, uint8_t maxCodes) {
    uint8_t count = 0;
    auto addCode = [&](StatusRgbDiagCode code) {
      if (count < maxCodes) {
        codes[count++] = code;
      }
    };

    // Ordered by severity. Each active code gets its own full blink window.
    if (!batteryPresent) addCode(STATUS_RGB_NO_BATTERY);
#if STATUS_RGB_PERIPHERAL_ERRORS_ENABLED
    if (!ads1115Ready) addCode(STATUS_RGB_NO_ADS);
    if (isElrsModuleMissingForStatus(millis())) addCode(STATUS_RGB_NO_ELRS);
    if (!mcp23017Ready) {
      addCode(STATUS_RGB_NO_MCP23017);
      return count;
    }
#endif

#if STATUS_RGB_DPAD_DIAGNOSTICS
    if (mcp23017Ready) {
      bool selectPressed = ((mcp23017LastValue & (1U << MCP23017_DPAD_SELECT_BIT)) == 0);
      bool leftPressed = ((mcp23017LastValue & (1U << MCP23017_DPAD_LEFT_BIT)) == 0);
      bool upPressed = ((mcp23017LastValue & (1U << MCP23017_DPAD_UP_BIT)) == 0);
      bool downPressed = ((mcp23017LastValue & (1U << MCP23017_DPAD_DOWN_BIT)) == 0);
      bool rightPressed = ((mcp23017LastValue & (1U << MCP23017_DPAD_RIGHT_BIT)) == 0);

      if (selectPressed) addCode(STATUS_RGB_DPAD_SELECT);
      else if (leftPressed) addCode(STATUS_RGB_DPAD_LEFT);
      else if (upPressed) addCode(STATUS_RGB_DPAD_UP);
      else if (downPressed) addCode(STATUS_RGB_DPAD_DOWN);
      else if (rightPressed) addCode(STATUS_RGB_DPAD_RIGHT);
    }
#endif
    return count;
  }

  bool isElrsModuleMissingForStatus(unsigned long now) {
    if (getModelProtocol(activeModel) != PROTOCOL_ELRS) return false;
    if (elrsNoModuleDetected) return true;
    if (!elrsInitialized) return false;
    if (elrsModulePresent || lastElrsSerialRxTime > 0 || hasElrsModuleFrames()) return false;

    unsigned long graceMs = ELRS_PROFILE_DETECT_ACTIVE_MS;
    if (graceMs < ELRS_MODULE_TIMEOUT_MS) graceMs = ELRS_MODULE_TIMEOUT_MS;
    return (now - elrsProfileDetectStartTime) >= graceMs;
  }

  void updateStatusRgbLed(unsigned long now) {
    if (!STATUS_RGB_LED_ENABLED) return;

    StatusRgbDiagCode activeCodes[10] = {};
    uint8_t activeCodeCount = collectStatusRgbDiagCodes(activeCodes, (uint8_t)(sizeof(activeCodes) / sizeof(activeCodes[0])));
    uint16_t activeMask = 0;
    for (uint8_t i = 0; i < activeCodeCount; i++) {
      activeMask |= (uint16_t)(1U << activeCodes[i]);
    }
    if (activeMask != lastStatusRgbActiveMask) {
      lastStatusRgbActiveMask = activeMask;
      statusRgbCycleStartTime = now;
      lastStatusRgbStep = 255;
      lastStatusRgbHalfStep = 255;
      lastStatusRgbPauseActive = false;
    }

    bool pauseActive = false;
    unsigned long patternElapsed = 0;
    if (activeCodeCount == 0) {
      currentStatusRgbDiagCode = STATUS_RGB_OK;
    } else {
      const unsigned long patternMs = STATUS_RGB_LED_STEP_MS * 3UL;
      const unsigned long codeWindowMs = patternMs + STATUS_RGB_LED_CODE_PAUSE_MS;
      unsigned long cycleElapsed = now - statusRgbCycleStartTime;
      unsigned long windowElapsed = cycleElapsed % codeWindowMs;
      if (windowElapsed >= patternMs) {
        currentStatusRgbDiagCode = STATUS_RGB_OK;
        pauseActive = true;
      } else {
        uint8_t codeIndex = (uint8_t)((cycleElapsed / codeWindowMs) % activeCodeCount);
        currentStatusRgbDiagCode = activeCodes[codeIndex];
        patternElapsed = windowElapsed;
      }
    }

    uint8_t step = (uint8_t)((patternElapsed / STATUS_RGB_LED_STEP_MS) % 3UL);
    unsigned long stepElapsed = patternElapsed % STATUS_RGB_LED_STEP_MS;
    uint8_t halfStep = (stepElapsed >= STATUS_RGB_LED_STEP_MS - STATUS_RGB_LED_REPEAT_GAP_MS) ? 1 : 0;
    if (currentStatusRgbDiagCode == lastStatusRgbDiagCode &&
        step == lastStatusRgbStep &&
        halfStep == lastStatusRgbHalfStep &&
        pauseActive == lastStatusRgbPauseActive) return;

    lastStatusRgbDiagCode = currentStatusRgbDiagCode;
    lastStatusRgbStep = step;
    lastStatusRgbHalfStep = halfStep;
    lastStatusRgbPauseActive = pauseActive;
    setStatusRgbLedPattern(currentStatusRgbDiagCode, step, stepElapsed);
  }

  void initPowerLed() {
    if (!POWER_LED_ENABLED) return;

    gpio_num_t ledPosPin = (gpio_num_t)POWER_LED_POS_PIN;
    gpio_num_t ledNegPin = (gpio_num_t)POWER_LED_NEG_PIN;
    if (rtc_gpio_is_valid_gpio(ledPosPin)) {
      rtc_gpio_hold_dis(ledPosPin);
    }
    if (rtc_gpio_is_valid_gpio(ledNegPin)) {
      rtc_gpio_hold_dis(ledNegPin);
    }

    pinMode(POWER_LED_NEG_PIN, OUTPUT);
    digitalWrite(POWER_LED_NEG_PIN, LOW);
    pinMode(POWER_LED_POS_PIN, OUTPUT);
    analogWriteResolution(POWER_LED_POS_PIN, POWER_LED_PWM_BITS);
    analogWriteFrequency(POWER_LED_POS_PIN, POWER_LED_PWM_FREQ);
    setPowerLed(true);
  }

  void setPowerLed(bool on) {
    if (!POWER_LED_ENABLED) return;

    powerLedBreathing = false;
    setPowerLedDuty(on ? 255 : 0);
  }

  void setPowerLedDuty(uint8_t duty) {
    if (!POWER_LED_ENABLED) return;

    digitalWrite(POWER_LED_NEG_PIN, LOW);
    analogWrite(POWER_LED_POS_PIN, duty);
    powerLedDuty = duty;
    powerLedOn = duty > 0;
  }

  void updatePowerLed(unsigned long now) {
    if (!POWER_LED_ENABLED || powerButtonShutdownPending) return;

    if (screenAwake) {
      if (powerLedBreathing || powerLedDuty != 255) {
        setPowerLed(true);
      }
      return;
    }

    if (!powerLedBreathing) {
      powerLedBreathing = true;
      powerLedBreatheStartTime = now;
    }

    unsigned long elapsed = (now - powerLedBreatheStartTime) % POWER_LED_SLEEP_BREATHE_PERIOD_MS;
    float phase = (float)elapsed / (float)POWER_LED_SLEEP_BREATHE_PERIOD_MS;
    float wave = 0.5f - (0.5f * cosf(phase * 6.2831853f));
    uint8_t duty = (uint8_t)roundf(POWER_LED_SLEEP_MIN_DUTY +
      (wave * (POWER_LED_SLEEP_MAX_DUTY - POWER_LED_SLEEP_MIN_DUTY)));
    if (duty != powerLedDuty) {
      setPowerLedDuty(duty);
    }
  }

  void setPeripheralPowerRails(bool on) {
    if (!PERIPHERAL_POWER_SWITCHES_ENABLED) return;

    int level = PERIPHERAL_POWER_ENABLE_ACTIVE_HIGH
      ? (on ? HIGH : LOW)
      : (on ? LOW : HIGH);

    if (UART_5V_ENABLE_PIN >= 0) {
      digitalWrite(UART_5V_ENABLE_PIN, level);
    }
    if (I2C_3V3_ENABLE_PIN >= 0) {
      digitalWrite(I2C_3V3_ENABLE_PIN, level);
    }
  }

  void initPeripheralPowerSwitches() {
    if (!PERIPHERAL_POWER_SWITCHES_ENABLED) return;

    if (UART_5V_ENABLE_PIN >= 0) {
      pinMode(UART_5V_ENABLE_PIN, OUTPUT);
    }
    if (I2C_3V3_ENABLE_PIN >= 0) {
      pinMode(I2C_3V3_ENABLE_PIN, OUTPUT);
    }
    setPeripheralPowerRails(true);
  }

  void prepareExternalBusesForDeepSleep() {
#if AUDIO_ENABLED
    silenceAudioOutput(true);
#endif

    elrsSerial.flush();
    elrsSerial.end();
    elrsHostSniffSerial.end();

    Wire.end();
    pinMode(I2C_SDA_PIN, INPUT);
    pinMode(I2C_SCL_PIN, INPUT);
    pinMode(ELRS_UART_TX_PIN_DEFAULT, INPUT);
    pinMode(ELRS_UART_RX_PIN_DEFAULT, INPUT);
    if (elrsActiveTxPin != ELRS_UART_TX_PIN_DEFAULT) {
      pinMode(elrsActiveTxPin, INPUT);
    }
    if (elrsActiveRxPin != ELRS_UART_RX_PIN_DEFAULT) {
      pinMode(elrsActiveRxPin, INPUT);
    }

    setPeripheralPowerRails(false);
  }

  void initPowerButton() {
    if (!POWER_BUTTON_ENABLED) return;

    initPowerLed();

    gpio_num_t refPin = (gpio_num_t)POWER_BUTTON_REF_PIN;
    if (rtc_gpio_is_valid_gpio(refPin)) {
      rtc_gpio_hold_dis(refPin);
    }

    pinMode(POWER_BUTTON_REF_PIN, OUTPUT);
    digitalWrite(POWER_BUTTON_REF_PIN, LOW);
    pinMode(POWER_BUTTON_SENSE_PIN, INPUT_PULLUP);

    powerButtonPressed = isPowerButtonPressed();
    powerButtonPressedAt = powerButtonPressed ? millis() : 0;
    powerButtonSuppressShortPressUntilRelease = powerButtonPressed || isPowerButtonWake();
    powerButtonShutdownPending = false;
  }

  bool isPowerButtonPressed() {
    if (!POWER_BUTTON_ENABLED) return false;
    return digitalRead(POWER_BUTTON_SENSE_PIN) == LOW;
  }

  void drawPowerButtonShutdownPrompt() {
    screenAwake = true;
    displayDimmed = false;
    applyDisplayBacklight();

    tft.fillScreen(COLOR_BG);
    drawGradientControl(14, 78, 212, 150, 14, COLOR_PANEL, COLOR_ACCENT);
    tft.setTextFont(2);
    tft.setTextColor(COLOR_ACCENT_HI, COLOR_PANEL);
    tft.drawCentreString("POWER BUTTON", 120, 96, 2);
    tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
    tft.drawCentreString("Release button to", 120, 128, 2);
    tft.drawCentreString("enter deep sleep.", 120, 148, 2);
    tft.setTextColor(TFT_ORANGE, COLOR_PANEL);
    tft.drawCentreString("Press again to wake.", 120, 188, 2);
  }

  void updatePowerButton(unsigned long now) {
    if (!POWER_BUTTON_ENABLED) return;

    bool pressed = isPowerButtonPressed();
    if (powerButtonShutdownPending) {
      bool blinkOn = ((now / POWER_LED_SHUTDOWN_BLINK_MS) % 2) == 0;
      if (blinkOn != powerLedOn) {
        setPowerLed(blinkOn);
      }
      powerButtonPressed = pressed;
      if (!pressed) {
        enterDeepSleepPowerOff();
      }
      return;
    }

    if (pressed) {
      if (!powerButtonPressed) {
        powerButtonPressedAt = now;
      }
      else if (powerButtonPressedAt != 0 &&
               now - powerButtonPressedAt >= POWER_BUTTON_HOLD_MS) {
        powerButtonShutdownPending = true;
        drawPowerButtonShutdownPrompt();
      }
    }
    else {
      if (powerButtonPressed && powerButtonPressedAt != 0 &&
          !powerButtonSuppressShortPressUntilRelease &&
          now - powerButtonPressedAt < POWER_BUTTON_HOLD_MS) {
        handlePowerButtonShortPress();
      }

      powerButtonPressedAt = 0;
      powerButtonSuppressShortPressUntilRelease = false;
      if (screenAwake && !powerLedOn) {
        setPowerLed(true);
      }
    }

    powerButtonPressed = pressed;
  }

  void handlePowerButtonShortPress() {
    screenAwake = !screenAwake;
    displayDimmed = false;
    screenOffManual = !screenAwake;
    suppressWakeUntilRelease = !screenAwake;
    if (screenAwake) {
      lastActivityTime = millis();
    }
    applyDisplayBacklight();

    if (screenAwake) {
      fullRedraw = true;
      uiNeedsRedraw = true;
      topBarNeedsRedraw = true;
    }
  }

  bool isPowerButtonWake() {
    return esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0;
  }

  bool isSoftPowerOffStored() {
    Preferences preferences;
    if (!preferences.begin(SOFT_POWER_PREF_NAMESPACE, true)) {
      return false;
    }
    bool stored = preferences.getBool(SOFT_POWER_PREF_KEY, false);
    preferences.end();
    return stored;
  }

  void setSoftPowerOffStored(bool off) {
    Preferences preferences;
    if (!preferences.begin(SOFT_POWER_PREF_NAMESPACE, false)) {
      return;
    }
    if (preferences.getBool(SOFT_POWER_PREF_KEY, false) != off) {
      preferences.putBool(SOFT_POWER_PREF_KEY, off);
    }
    preferences.end();
  }

  void handleSoftPowerBootState() {
    if (!POWER_BUTTON_ENABLED) return;

    if (isPowerButtonWake()) {
      if (isSoftPowerOffStored()) {
        Serial.println("Soft power: power-button wake, clearing stored off state");
        setSoftPowerOffStored(false);
      }
      return;
    }

    if (isSoftPowerOffStored()) {
      Serial.println("Soft power: stored off state active, returning to deep sleep");
      setPowerLed(false);
      startPowerButtonDeepSleep();
    }
  }

  void startPowerButtonDeepSleep() {
    gpio_num_t sensePin = (gpio_num_t)POWER_BUTTON_SENSE_PIN;
    gpio_num_t refPin = (gpio_num_t)POWER_BUTTON_REF_PIN;
    silenceAudioOutput(true);
    setStatusRgbLed(0, 0, 0);
    powerLedBreathing = false;
    powerLedDuty = 0;
    powerLedOn = false;
    pinMode(POWER_BUTTON_REF_PIN, OUTPUT);
    digitalWrite(POWER_BUTTON_REF_PIN, LOW);
    pinMode(POWER_BUTTON_SENSE_PIN, INPUT_PULLUP);
    if (POWER_LED_ENABLED) {
      pinMode(POWER_LED_POS_PIN, OUTPUT);
      digitalWrite(POWER_LED_POS_PIN, LOW);
      pinMode(POWER_LED_NEG_PIN, OUTPUT);
      digitalWrite(POWER_LED_NEG_PIN, LOW);
    }

    if (rtc_gpio_is_valid_gpio(refPin)) {
      rtc_gpio_init(refPin);
      rtc_gpio_set_direction(refPin, RTC_GPIO_MODE_OUTPUT_ONLY);
      rtc_gpio_set_level(refPin, 0);
      rtc_gpio_hold_en(refPin);
    }

    if (POWER_LED_ENABLED) {
      gpio_num_t ledPosPin = (gpio_num_t)POWER_LED_POS_PIN;
      gpio_num_t ledNegPin = (gpio_num_t)POWER_LED_NEG_PIN;
      if (rtc_gpio_is_valid_gpio(ledPosPin)) {
        rtc_gpio_init(ledPosPin);
        rtc_gpio_set_direction(ledPosPin, RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_set_level(ledPosPin, 0);
        rtc_gpio_hold_en(ledPosPin);
      }
      if (rtc_gpio_is_valid_gpio(ledNegPin)) {
        rtc_gpio_init(ledNegPin);
        rtc_gpio_set_direction(ledNegPin, RTC_GPIO_MODE_OUTPUT_ONLY);
        rtc_gpio_set_level(ledNegPin, 0);
        rtc_gpio_hold_en(ledNegPin);
      }
    }

    if (rtc_gpio_is_valid_gpio(sensePin)) {
      rtc_gpio_init(sensePin);
      rtc_gpio_set_direction(sensePin, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_en(sensePin);
      rtc_gpio_pulldown_dis(sensePin);
    }

    esp_sleep_enable_ext0_wakeup(sensePin, 0);
    esp_deep_sleep_start();
  }

  void enterDeepSleepPowerOff() {
    if (!POWER_BUTTON_ENABLED) return;

    Serial.println("Power button: entering deep sleep");
    setSoftPowerOffStored(true);
    prepareExternalBusesForDeepSleep();
#if !ELRS_PASSIVE_SNIFF_MODE
    esp_now_deinit();
#endif
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);

    screenAwake = false;
    displayDimmed = false;
    applyDisplayBacklight();
    setPowerLed(false);
    setStatusRgbLed(0, 0, 0);
    delay(50);

    startPowerButtonDeepSleep();
  }

  void sanitizeWifiText(char *value, size_t maxLen, bool allowEmpty) {
    if (value == nullptr || maxLen == 0) return;

    size_t writeIndex = 0;
    for (size_t readIndex = 0; readIndex < maxLen; readIndex++) {
      unsigned char ch = (unsigned char)value[readIndex];
      if (ch == '\0') {
        break;
      }

      if (isprint(ch)) {
        value[writeIndex++] = (char)ch;
      }
    }

    value[writeIndex] = '\0';

    if (!allowEmpty && writeIndex == 0) {
      strncpy(value, OTA_AP_SSID, maxLen);
      value[maxLen] = '\0';
    }
  }

  void sanitizeOtaSettings() {
    sanitizeWifiText(otaStaSsid, OTA_STA_SSID_STORAGE_BYTES, true);
    sanitizeWifiText(otaStaPassword, OTA_STA_PASSWORD_STORAGE_BYTES, true);
    sanitizeWifiText(otaApSsid, OTA_AP_SSID_STORAGE_BYTES, false);
  }

  void loadOtaSettings() {
    if (EEPROM.read(EEPROM_OTA_VERSION_ADDR) != OTA_SETTINGS_STORAGE_VERSION) {
      strncpy(otaStaSsid, OTA_STA_SSID, OTA_STA_SSID_STORAGE_BYTES);
      otaStaSsid[OTA_STA_SSID_STORAGE_BYTES] = '\0';
      strncpy(otaStaPassword, OTA_STA_PASSWORD, OTA_STA_PASSWORD_STORAGE_BYTES);
      otaStaPassword[OTA_STA_PASSWORD_STORAGE_BYTES] = '\0';
      strncpy(otaApSsid, OTA_AP_SSID, OTA_AP_SSID_STORAGE_BYTES);
      otaApSsid[OTA_AP_SSID_STORAGE_BYTES] = '\0';
      sanitizeOtaSettings();
      return;
    }

    int addr = EEPROM_OTA_STA_SSID_ADDR;
    for (int i = 0; i < OTA_STA_SSID_STORAGE_BYTES; i++) {
      otaStaSsid[i] = (char)EEPROM.read(addr++);
    }
    otaStaSsid[OTA_STA_SSID_STORAGE_BYTES] = '\0';

    addr = EEPROM_OTA_STA_PASSWORD_ADDR;
    for (int i = 0; i < OTA_STA_PASSWORD_STORAGE_BYTES; i++) {
      otaStaPassword[i] = (char)EEPROM.read(addr++);
    }
    otaStaPassword[OTA_STA_PASSWORD_STORAGE_BYTES] = '\0';

    addr = EEPROM_OTA_AP_SSID_ADDR;
    for (int i = 0; i < OTA_AP_SSID_STORAGE_BYTES; i++) {
      otaApSsid[i] = (char)EEPROM.read(addr++);
    }
    otaApSsid[OTA_AP_SSID_STORAGE_BYTES] = '\0';

    sanitizeOtaSettings();
  }

  void saveOtaSettings() {
    sanitizeOtaSettings();

    int addr = EEPROM_OTA_STA_SSID_ADDR;
    for (int i = 0; i < OTA_STA_SSID_STORAGE_BYTES; i++) {
      char ch = (i < (int)strlen(otaStaSsid)) ? otaStaSsid[i] : '\0';
      EEPROM.write(addr++, (uint8_t)ch);
    }

    addr = EEPROM_OTA_STA_PASSWORD_ADDR;
    for (int i = 0; i < OTA_STA_PASSWORD_STORAGE_BYTES; i++) {
      char ch = (i < (int)strlen(otaStaPassword)) ? otaStaPassword[i] : '\0';
      EEPROM.write(addr++, (uint8_t)ch);
    }

    addr = EEPROM_OTA_AP_SSID_ADDR;
    for (int i = 0; i < OTA_AP_SSID_STORAGE_BYTES; i++) {
      char ch = (i < (int)strlen(otaApSsid)) ? otaApSsid[i] : '\0';
      EEPROM.write(addr++, (uint8_t)ch);
    }

    EEPROM.write(EEPROM_OTA_VERSION_ADDR, OTA_SETTINGS_STORAGE_VERSION);
    EEPROM.commit();
  }

  int getMixPageBase(int page) {
    page = constrain(page, 0, MIX_PAGE_COUNT - 1);
    return page * MIXES_PER_PAGE;
  }

  int getMixLocalIndex() {
    int base = getMixPageBase(selectedMixPage);
    int local = selectedMixIndex - base;
    return constrain(local, 0, MIXES_PER_PAGE - 1);
  }

  void setSelectedMixPage(int page) {
    int local = getMixLocalIndex();
    selectedMixPage = constrain(page, 0, MIX_PAGE_COUNT - 1);
    selectedMixIndex = getMixPageBase(selectedMixPage) + local;
    selectedMixIndex = constrain(selectedMixIndex, 0, MIX_COUNT - 1);
  }

  bool toggleSelectedMixPageDebounced(unsigned long now) {
    if (now < mixPageToggleLockedUntil) {
      return false;
    }

    setSelectedMixPage(selectedMixPage == MIX_PAGE_PRESET ? MIX_PAGE_USER : MIX_PAGE_PRESET);
    mixPageToggleLockedUntil = now + MIX_PAGE_TOGGLE_DEBOUNCE_MS;
    mixingNeedsRedraw = true;
    uiNeedsRedraw = true;
    return true;
  }

  const char* getMixPageLabel(int page) {
    return (page == MIX_PAGE_USER) ? "User" : "Preset";
  }

  void initDefaultMixSlot(int modelIndex, int mixIndex) {
    models[modelIndex].mixes[mixIndex].enabled = false;
    models[modelIndex].mixes[mixIndex].source = 0;
    models[modelIndex].mixes[mixIndex].destination = 0;
    setMixSource(models[modelIndex].mixes[mixIndex], mixIndex % CHANNEL_COUNT);
    setMixDestination(models[modelIndex].mixes[mixIndex], (mixIndex + 1) % CHANNEL_COUNT);
    setMixReverseLinked(models[modelIndex].mixes[mixIndex], true);
    models[modelIndex].mixes[mixIndex].rate = 0;
    models[modelIndex].mixes[mixIndex].offset = 0;
  }

  void setPresetMixSlot(int modelIndex, int mixIndex, uint8_t source, uint8_t destination, int8_t rate) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return;
    if (mixIndex < 0 || mixIndex >= MIX_COUNT) return;

    MixData &mix = models[modelIndex].mixes[mixIndex];
    mix.enabled = true;
    setMixSource(mix, source);
    setMixDestination(mix, destination);
    // Preset mixes are implementation details for remapping drive behavior.
    // Keep reverse independent so users can tune each real ESC/servo output.
    setMixReverseLinked(mix, false);
    mix.rate = constrain(rate, -100, 100);
    mix.offset = 0;
  }

  bool appendPresetMix(int modelIndex, int &mixIndex, uint8_t source, uint8_t destination, int8_t rate) {
    if (rate == 0) return true;
    if (mixIndex >= PRESET_MIX_COUNT) return false;

    setPresetMixSlot(modelIndex, mixIndex++, source, destination, rate);
    return true;
  }

  bool appendPresetRemap(int modelIndex, int &mixIndex, uint8_t source, uint8_t destination) {
    if (source == destination) return true;
    if (mixIndex + 1 >= PRESET_MIX_COUNT) return false;

    // Zero the destination's own stick axis, then add the requested source.
    setPresetMixSlot(modelIndex, mixIndex++, destination, destination, -100);
    setPresetMixSlot(modelIndex, mixIndex++, source, destination, 100);
    return true;
  }

  int oneStickTankMixCost(uint8_t destination, int8_t turnRate) {
    // Target formula is RY + (RX * turnRate). CH1 already starts as RX,
    // CH2 already starts as RY, so some destinations need fewer mix slots.
    if (destination == 0) {
      return (turnRate == 100) ? 1 : 3;
    }
    if (destination == 1) {
      return 1;
    }
    return 3;
  }

  bool appendOneStickTankPresetOutput(int modelIndex, int &mixIndex, uint8_t destination, int8_t turnRate) {
    if (destination >= CHANNEL_COUNT) return false;
    turnRate = constrain(turnRate, -100, 100);

    if (destination == 0 && turnRate == 100) {
      // CH1 already carries RX, so adding RY makes RX + RY.
      return appendPresetMix(modelIndex, mixIndex, 1, destination, 100);
    }

    if (destination == 1) {
      // CH2 already carries RY, so adding RX makes RY +/- RX.
      return appendPresetMix(modelIndex, mixIndex, 0, destination, turnRate);
    }

    if (mixIndex + 2 >= PRESET_MIX_COUNT) return false;

    // Other destinations, or CH1 with a negative turn term, need a full formula:
    // clear destination, add RY, then add signed RX.
    setPresetMixSlot(modelIndex, mixIndex++, destination, destination, -100);
    setPresetMixSlot(modelIndex, mixIndex++, 1, destination, 100);
    setPresetMixSlot(modelIndex, mixIndex++, 0, destination, turnRate);
    return true;
  }

  bool drivePresetUsesVisibleMixes(int modelIndex) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return false;
    if (getModelDriveType(modelIndex) != DRIVE_TANK) return false;

    uint8_t leftChannel = getModelTankLeftEscChannel(modelIndex);
    uint8_t rightChannel = getModelTankRightEscChannel(modelIndex);
    if (leftChannel >= CHANNEL_COUNT || rightChannel >= CHANNEL_COUNT || leftChannel == rightChannel) {
      return false;
    }

    if (getModelTankMode(modelIndex) == TANK_MODE_RIGHT_STICK) {
      int neededMixes = oneStickTankMixCost(leftChannel, 100) +
                        oneStickTankMixCost(rightChannel, -100);
      return neededMixes <= PRESET_MIX_COUNT;
    }

    int neededMixes = 0;
    if (leftChannel != 2) neededMixes += 2;   // left track source is LY
    if (rightChannel != 1) neededMixes += 2;  // right track source is RY
    return neededMixes <= PRESET_MIX_COUNT;
  }

  void applyDrivePresetMixes(int modelIndex) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return;

    for (int mix = 0; mix < PRESET_MIX_COUNT; mix++) {
      initDefaultMixSlot(modelIndex, mix);
    }

    for (int ch = 0; ch < CHANNEL_COUNT; ch++) {
      models[modelIndex].reverse[ch] = false;
      reverseChannelDirty[ch] = true;
    }

    DriveType driveType = getModelDriveType(modelIndex);
    if (driveType == DRIVE_X_DRONE) {
      // Flight controllers handle motor mixing. Keep the transmitter on the
      // standard Betaflight-style AETR channel map:
      // CH1 roll, CH2 pitch, CH3 throttle, CH4 yaw.
      mixingNeedsRedraw = true;
      reverseNeedsRedraw = true;
      return;
    }

    if (driveType != DRIVE_TANK) {
      mixingNeedsRedraw = true;
      reverseNeedsRedraw = true;
      return;
    }

    uint8_t leftChannel = getModelTankLeftEscChannel(modelIndex);
    uint8_t rightChannel = getModelTankRightEscChannel(modelIndex);
    if (leftChannel >= CHANNEL_COUNT || rightChannel >= CHANNEL_COUNT || leftChannel == rightChannel) {
      leftChannel = 1;
      rightChannel = 0;
    }

    int mixIndex = 0;
    if (getModelTankMode(modelIndex) == TANK_MODE_RIGHT_STICK) {
      if (drivePresetUsesVisibleMixes(modelIndex)) {
        // 1-stick tank: right stick Y is throttle, right stick X is turn.
        // Left track = RY + RX, right track = RY - RX.
        appendOneStickTankPresetOutput(modelIndex, mixIndex, leftChannel, 100);
        appendOneStickTankPresetOutput(modelIndex, mixIndex, rightChannel, -100);
      }
    } else {
      // 2-stick tank: left track follows LY, right track follows RY.
      appendPresetRemap(modelIndex, mixIndex, 2, leftChannel);
      appendPresetRemap(modelIndex, mixIndex, 1, rightChannel);
    }

    selectedMixIndex = 0;
    selectedMixPage = MIX_PAGE_PRESET;
    mixingNeedsRedraw = true;
    reverseNeedsRedraw = true;
  }

  DriveType getModelDriveType(int modelIndex) {
    uint8_t drive = models[modelIndex].driveType & 0x0F;
    if (drive > DRIVE_X_DRONE) drive = DRIVE_TANK;
    return (DriveType)drive;
  }

  void setModelDriveType(int modelIndex, DriveType driveType) {
    models[modelIndex].driveType =
      (models[modelIndex].driveType & 0xF0) | ((uint8_t)driveType & 0x0F);
    if (modelIndex == activeModel) {
      droneArmWarningVisible = false;
      droneArmWarningTouchLocked = false;
      droneArmWarningStaticDirty = false;
      droneArmWarningDynamicDirty = false;
      droneSafetyThrottleLockActive = false;
      setDroneArmActive(false);
    }
  }

  void setDroneArmActive(bool armed) {
    bool nextState = armed && (currentDrive == DRIVE_X_DRONE);
    if (droneArmActive == nextState) return;
    droneArmActive = nextState;
    mainOutputPanelNeedsRedraw = true;
    uiNeedsRedraw = true;
  }

  float getDroneArmOutput() {
    return droneArmActive ? DRONE_ARM_ARMED_VALUE : DRONE_ARM_DISARMED_VALUE;
  }

  void showDroneArmWarning() {
    if (currentDrive != DRIVE_X_DRONE) return;
    droneArmWarningVisible = true;
    droneArmWarningTouchLocked = true;
    droneArmWarningStaticDirty = true;
    droneArmWarningDynamicDirty = true;
    screenAwake = true;
    displayDimmed = false;
    screenOffManual = false;
    applyDisplayBacklight();
    uiNeedsRedraw = true;
  }

  void closeDroneArmWarning() {
    if (!droneArmWarningVisible) return;
    droneArmWarningVisible = false;
    droneArmWarningTouchLocked = true;
    droneArmWarningStaticDirty = false;
    droneArmWarningDynamicDirty = false;
    fullRedraw = true;
    uiNeedsRedraw = true;
  }

  TankControlMode getModelTankMode(int modelIndex) {
    return (models[modelIndex].driveType & 0x10)
      ? TANK_MODE_RIGHT_STICK
      : TANK_MODE_DUAL_STICK;
  }

  void setModelTankMode(int modelIndex, TankControlMode tankMode) {
    if (tankMode == TANK_MODE_RIGHT_STICK) {
      models[modelIndex].driveType |= 0x10;
    } else {
      models[modelIndex].driveType &= ~0x10;
    }
  }

  bool sanitizeModelTankEscChannels(int modelIndex) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return false;
    bool changed = false;

    uint8_t left = modelTankLeftEscChannel[modelIndex];
    uint8_t right = modelTankRightEscChannel[modelIndex];

    if (left >= CHANNEL_COUNT) {
      left = 1;  // CH2 default left track
      changed = true;
    }
    if (right >= CHANNEL_COUNT) {
      right = 0;  // CH1 default right track
      changed = true;
    }
    if (left == right) {
      left = 1;
      right = 0;
      changed = true;
    }

    modelTankLeftEscChannel[modelIndex] = left;
    modelTankRightEscChannel[modelIndex] = right;
    return changed;
  }

  uint8_t getModelTankLeftEscChannel(int modelIndex) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return 1;
    sanitizeModelTankEscChannels(modelIndex);
    return modelTankLeftEscChannel[modelIndex];
  }

  uint8_t getModelTankRightEscChannel(int modelIndex) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return 0;
    sanitizeModelTankEscChannels(modelIndex);
    return modelTankRightEscChannel[modelIndex];
  }

  void setModelTankEscChannels(int modelIndex, uint8_t leftChannel, uint8_t rightChannel) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return;
    modelTankLeftEscChannel[modelIndex] = leftChannel;
    modelTankRightEscChannel[modelIndex] = rightChannel;
    sanitizeModelTankEscChannels(modelIndex);
  }

  ProtocolType getModelProtocol(int modelIndex) {
    uint8_t protocol = (models[modelIndex].driveType & PROTOCOL_MASK) >> PROTOCOL_SHIFT;
    if (protocol > PROTOCOL_ESPNOW) protocol = PROTOCOL_ELRS;
    return (ProtocolType)protocol;
  }

  void setModelProtocol(int modelIndex, ProtocolType protocolType) {
    models[modelIndex].driveType =
      (models[modelIndex].driveType & ~PROTOCOL_MASK) |
      ((((uint8_t)protocolType) << PROTOCOL_SHIFT) & PROTOCOL_MASK);
  }

  bool shouldForceTankSingleReversePair(int modelIndex) {
    if (modelIndex < 0 || modelIndex >= MAX_MODELS) return false;
    if (getModelDriveType(modelIndex) != DRIVE_TANK) return false;
    if (getModelTankMode(modelIndex) != TANK_MODE_RIGHT_STICK) return false;

    uint8_t leftChannel = getModelTankLeftEscChannel(modelIndex);
    uint8_t rightChannel = getModelTankRightEscChannel(modelIndex);
    if (leftChannel >= CHANNEL_COUNT || rightChannel >= CHANNEL_COUNT || leftChannel == rightChannel) {
      leftChannel = 1;
      rightChannel = 0;
    }

    // In 1-stick tank mode, the real ESC outputs can be tied by default.
    // If any visible preset/user mix touching those outputs has reverse
    // linking separated, that explicit per-side tuning choice wins.
    for (int mixIndex = 0; mixIndex < MIX_COUNT; mixIndex++) {
      MixData &mix = models[modelIndex].mixes[mixIndex];
      if (!mix.enabled) continue;

      uint8_t source = getMixSource(mix);
      uint8_t destination = getMixDestination(mix);
      bool touchesTankOutput =
        (source == leftChannel) ||
        (source == rightChannel) ||
        (destination == leftChannel) ||
        (destination == rightChannel);

      if (!touchesTankOutput) continue;
      if (!isMixReverseLinked(mix)) return false;
    }

    return true;
  }

  void getLinkedReverseChannels(int modelIndex, int channel, bool linked[CHANNEL_COUNT]) {
    for (int i = 0; i < CHANNEL_COUNT; i++) linked[i] = false;
    if (channel < 0 || channel >= CHANNEL_COUNT) return;

    linked[channel] = true;

    // Tank presets use mixes to synthesize motor outputs, but reverse is a
    // receiver-output/motor-direction setting. Keep each tank output isolated
    // so one backwards motor can be corrected without flipping the mix inputs.
    if (modelIndex >= 0 && modelIndex < MAX_MODELS && getModelDriveType(modelIndex) == DRIVE_TANK) {
      return;
    }

    bool changed = true;
    while (changed) {
      changed = false;

      for (int mixIndex = 0; mixIndex < MIX_COUNT; mixIndex++) {
        MixData &mix = models[modelIndex].mixes[mixIndex];
        if (!mix.enabled) continue;
        if (!isMixReverseLinked(mix)) continue;

        uint8_t source = getMixSource(mix);
        uint8_t destination = getMixDestination(mix);
        if (source >= CHANNEL_COUNT || destination >= CHANNEL_COUNT) continue;

        if (linked[source] && !linked[destination]) {
          linked[destination] = true;
          changed = true;
        }

        if (linked[destination] && !linked[source]) {
          linked[source] = true;
          changed = true;
        }
      }
    }

    if (shouldForceTankSingleReversePair(modelIndex)) {
      uint8_t leftChannel = getModelTankLeftEscChannel(modelIndex);
      uint8_t rightChannel = getModelTankRightEscChannel(modelIndex);
      if (leftChannel >= CHANNEL_COUNT || rightChannel >= CHANNEL_COUNT || leftChannel == rightChannel) {
        leftChannel = 1;
        rightChannel = 0;
      }
      if (channel == leftChannel || channel == rightChannel) {
        linked[leftChannel] = true;
        linked[rightChannel] = true;
      }
    }
  }

  bool hasLinkedReversePeers(int modelIndex, int channel) {
    bool linked[CHANNEL_COUNT];
    getLinkedReverseChannels(modelIndex, channel, linked);

    for (int i = 0; i < CHANNEL_COUNT; i++) {
      if (i != channel && linked[i]) return true;
    }

    return false;
  }

  bool sanitizeModelMixes(int modelIndex) {
    bool changed = false;

    for (int mixIndex = 0; mixIndex < MIX_COUNT; mixIndex++) {
      MixData &mix = models[modelIndex].mixes[mixIndex];
      uint8_t source = getMixSource(mix);
      uint8_t destination = getMixDestination(mix);

      bool invalid =
        ((mix.source & ~(MIX_CHANNEL_MASK | MIX_REVERSE_SEPARATE_FLAG)) != 0) ||
        ((mix.destination & ~MIX_CHANNEL_MASK) != 0) ||
        (source >= CHANNEL_COUNT) ||
        (destination >= CHANNEL_COUNT) ||
        (mix.rate < -100) || (mix.rate > 100) ||
        (mix.offset < -100) || (mix.offset > 100);

      if (invalid) {
        initDefaultMixSlot(modelIndex, mixIndex);
        changed = true;
        continue;
      }

      bool normalizedEnabled = mix.enabled ? true : false;
      if (mix.enabled != normalizedEnabled) {
        mix.enabled = normalizedEnabled;
        changed = true;
      }

      // Presets can intentionally use source == destination with a -100% rate
      // to clear that channel before adding a remapped source.
    }

    return changed;
  }

  bool sanitizeModelDriveType(int modelIndex) {
    uint8_t drive = models[modelIndex].driveType & 0x0F;
    if (drive > DRIVE_X_DRONE) {
      models[modelIndex].driveType &= 0xF0;
      setModelDriveType(modelIndex, DRIVE_TANK);
      return true;
    }

    return false;
  }

  bool sanitizeModelProtocol(int modelIndex) {
    uint8_t protocol = (models[modelIndex].driveType & PROTOCOL_MASK) >> PROTOCOL_SHIFT;
    if (protocol > PROTOCOL_ESPNOW) {
      setModelProtocol(modelIndex, PROTOCOL_ELRS);
      return true;
    }

    return false;
  }

  void initModelDefaults(int i) {
    for (int ch = 0; ch < CHANNEL_COUNT; ch++) {
      models[i].reverse[ch] = false;
      models[i].failsafe[ch] = false;
    }

    clearModelFailsafeValues(i);
    clearModelRateValues(i);
    clearModelExpoValues(i);
    clearModelEndpointValues(i);

    for (int g = 0; g < 2; g++) {
      models[i].trimX[g] = 0;
      models[i].trimY[g] = 0;
    }

    for (int mix = 0; mix < MIX_COUNT; mix++) {
      initDefaultMixSlot(i, mix);
    }

    models[i].driveType = 0;
    setModelDriveType(i, DRIVE_TANK);
    setModelTankMode(i, TANK_MODE_DUAL_STICK);
    setModelTankEscChannels(i, 1, 0);
    applyDrivePresetMixes(i);
    modelElrsTxPowerMw[i] = 100;

    strcpy(models[i].name, "");
  }

  float leftThrottle  = 0.0;
  float rightThrottle = 0.0;
  float leftY  = 0.0;
  float rightY = 0.0;
  float inputChannels[CHANNEL_COUNT] = { 0.0f, 0.0f, 0.0f, 0.0f };
  float driveSourceChannels[CHANNEL_COUNT] = { 0.0f, 0.0f, 0.0f, 0.0f };
  float outputChannels[CHANNEL_COUNT] = { 0.0f, 0.0f, 0.0f, 0.0f };

  float m1 = 0.0;
  float m2 = 0.0;
  float m3 = 0.0;
  float m4 = 0.0;

  float trimRenderX = 0;
  float trimRenderY = 0;

  int signalStrength = 3;
  int batteryLevel   = 75;

  int trimBtnLeftX, trimBtnRightX;
  int trimBtnTopY, trimBtnBottomY;

  // ===== KEYBOARD STATE =====
  bool keyboardActive = false;
  bool keyboardNeedsRedraw = true;
  bool keyboardLowercase = false;

  String keyboardBuffer = "";
  KeyboardTarget keyboardTarget = KEYBOARD_TARGET_MODEL_NAME;

  #define KB_ROWS 5
  #define KB_COLS 10

  int kbX = 0;
  int kbW = 240;
  int kbH = 160;
int kbY = FOOTER_Y - kbH - 5;   // bottom half of screen
int keyW = 22;
int keyH = 30;
int keySpacing = 2;
const int KB_TOUCH_X_MIN = 8;
const int KB_TOUCH_X_MAX = 232;
#define MODEL_NAME_INPUT_X 10
#define MODEL_NAME_INPUT_Y 60
#define MODEL_NAME_INPUT_W 220
#define MODEL_NAME_INPUT_H 42
#define MODEL_NAME_LIST_Y 140
#define MODEL_NAME_ROW_H 34
#define MODEL_NAME_NAME_X 42
#define MODEL_NAME_BOX_X 34
#define MODEL_NAME_BOX_W 150
#define MODEL_NAME_DELETE_X 196
#define MODEL_NAME_DELETE_W 30

  // track pressed key for visual feedback
  int kbPressedRow = -1;
  int kbPressedCol = -1;
  int kbCursorRow = 1;   // start at "Q"
  int kbCursorCol = 0;

  const char* keyboardLayoutUpper[KB_ROWS][KB_COLS] = {
  {"1","2","3","4","5","6","7","8","9","0"},
  {"Q","W","E","R","T","Y","U","I","O","P"},
  {"A","S","D","F","G","H","J","K","L","<"},
  {"Aa","Z","X","C","V","B","N","M","OK",""},
  {" ","","","","","","","","",""}
  };

  const char* keyboardLayoutLower[KB_ROWS][KB_COLS] = {
  {"1","2","3","4","5","6","7","8","9","0"},
  {"q","w","e","r","t","y","u","i","o","p"},
  {"a","s","d","f","g","h","j","k","l","<"},
  {"Aa","z","x","c","v","b","n","m","OK",""},
  {" ","","","","","","","","",""}
  };

  // ===== MODEL NAME =====
  String modelNames[4] = {"", "", "", ""};
  int selectedModelIndex = -1;
  bool modelNameNeedsRedraw = true;
  bool modelNameDirty = true;
  bool inputBoxSelected = false;
  int pendingModelHoldIndex = -1;
  unsigned long pendingModelHoldStart = 0;
  bool pendingModelHoldTriggered = false;
  char otaStaSsid[OTA_STA_SSID_STORAGE_BYTES + 1] = OTA_STA_SSID;
  char otaStaPassword[OTA_STA_PASSWORD_STORAGE_BYTES + 1] = OTA_STA_PASSWORD;
  char otaApSsid[OTA_AP_SSID_STORAGE_BYTES + 1] = OTA_AP_SSID;
  bool otaSettingsNeedsRedraw = true;
  bool txUpdateNeedsRedraw = true;

// On-screen text entry is shared by model naming, OTA network fields, and the
// 20 Questions learning prompt. Keep target-specific behavior in
// applyKeyboardBufferToTarget() so the key grid stays generic.
const char* getKeyboardKey(int row, int col) {
  if (row < 0 || row >= KB_ROWS || col < 0 || col >= KB_COLS) return "";
  return keyboardLowercase ? keyboardLayoutLower[row][col] : keyboardLayoutUpper[row][col];
}

int getKeyboardGridStartX() {
  return kbX + 5;
}

int adjustKeyboardTouchX(int x) {
  return constrain(mapTouch(x, KB_TOUCH_X_MIN, KB_TOUCH_X_MAX, 0, 239), 0, 239);
}

void getKeyboardKeyRect(int row, int col, int &x, int &y, int &w, int &h) {
  x = getKeyboardGridStartX() + col * (keyW + keySpacing);
  y = kbY + row * (keyH + keySpacing) + 5;
  w = keyW;
  h = keyH;

  if (strcmp(getKeyboardKey(row, col), " ") == 0) {
    x = kbX + 10;
    w = kbW - 20;
  }
  else if (strcmp(getKeyboardKey(row, col), "OK") == 0) {
    w = keyW * 2;
  }
}

int getKeyboardMaxLength() {
  if (keyboardTarget == KEYBOARD_TARGET_20Q_LEARN) {
    return TWENTYQ_LEARNED_NAME_LEN - 1;
  }
  if (keyboardTarget == KEYBOARD_TARGET_OTA_STA_SSID) {
    return OTA_STA_SSID_STORAGE_BYTES;
  }
  if (keyboardTarget == KEYBOARD_TARGET_OTA_STA_PASSWORD) {
    return OTA_STA_PASSWORD_STORAGE_BYTES;
  }
  if (keyboardTarget == KEYBOARD_TARGET_OTA_AP_SSID) {
    return OTA_AP_SSID_STORAGE_BYTES;
  }
  return 18;
}

void applyKeyboardBufferToTarget() {
  if (keyboardTarget == KEYBOARD_TARGET_20Q_LEARN) {
    if (keyboardBuffer.length() > 0) {
      saveTwentyQLearnedAnswer(keyboardBuffer.c_str());
      twentyQAnsweredFeedback = true;
      twentyQNeedsRedraw = true;
      fullRedraw = true;
      uiNeedsRedraw = true;
    }
    return;
  }

  if (keyboardTarget == KEYBOARD_TARGET_MODEL_NAME) {
    if (keyboardBuffer.length() > 0) {
      saveKeyboardBufferToModelSlot();
    }
    return;
  }

  if (keyboardTarget == KEYBOARD_TARGET_OTA_STA_SSID) {
    strncpy(otaStaSsid, keyboardBuffer.c_str(), OTA_STA_SSID_STORAGE_BYTES);
    otaStaSsid[OTA_STA_SSID_STORAGE_BYTES] = '\0';
  }
  else if (keyboardTarget == KEYBOARD_TARGET_OTA_STA_PASSWORD) {
    strncpy(otaStaPassword, keyboardBuffer.c_str(), OTA_STA_PASSWORD_STORAGE_BYTES);
    otaStaPassword[OTA_STA_PASSWORD_STORAGE_BYTES] = '\0';
  }
  else if (keyboardTarget == KEYBOARD_TARGET_OTA_AP_SSID) {
    strncpy(otaApSsid, keyboardBuffer.c_str(), OTA_AP_SSID_STORAGE_BYTES);
    otaApSsid[OTA_AP_SSID_STORAGE_BYTES] = '\0';
    if (strlen(otaApSsid) == 0) {
      strncpy(otaApSsid, OTA_AP_SSID, OTA_AP_SSID_STORAGE_BYTES);
      otaApSsid[OTA_AP_SSID_STORAGE_BYTES] = '\0';
    }
  }

  saveOtaSettings();
  otaSettingsNeedsRedraw = true;
}

void beginOtaFieldEdit(KeyboardTarget target) {
  keyboardTarget = target;
  keyboardBuffer = "";

  keyboardActive = true;
  keyboardLowercase = false;
  keyboardNeedsRedraw = true;
  kbCursorRow = 1;
  kbCursorCol = 0;
  uiNeedsRedraw = true;
  otaSettingsNeedsRedraw = true;
}

void processKeyboardKey(const char* key) {
  if (strlen(key) == 0) return;

  if (strcmp(key, "<") == 0) {
    if (keyboardBuffer.length() > 0) {
      keyboardBuffer.remove(keyboardBuffer.length() - 1);
      modelNameDirty = true;
    }
  }
  else if (strcmp(key, "Aa") == 0) {
    keyboardLowercase = !keyboardLowercase;
    keyboardNeedsRedraw = true;
  }
  else if (strcmp(key, "OK") == 0) {
    applyKeyboardBufferToTarget();
    keyboardBuffer = "";
    keyboardTarget = KEYBOARD_TARGET_MODEL_NAME;
    modelNameDirty = true;
    modelNameNeedsRedraw = true;
    otaSettingsNeedsRedraw = true;
    uiNeedsRedraw = true;
    closeKeyboard();
  }
  else {
    if (keyboardBuffer.length() < getKeyboardMaxLength()) {
      keyboardBuffer += key;
      modelNameDirty = true;
      otaSettingsNeedsRedraw = true;
    }
  }

  keyboardNeedsRedraw = true;
  uiNeedsRedraw = true;
}

// Model selection owns the runtime handoff between saved configuration and the
// live transmitter state. Protocol changes happen here because ELRS/ESP-NOW
// need different hardware services running.
void selectModelSlot(int modelIndex) {
  activeModel = modelIndex;
  selectedModelIndex = modelIndex;
  saveActiveModelIndex();
  clearActiveEspNowSessionTelemetry();
  sanitizeModelTankEscChannels(modelIndex);
  currentModelName = String(models[modelIndex].name);
  currentDrive = getModelDriveType(modelIndex);
  droneArmWarningVisible = false;
  droneArmWarningTouchLocked = false;
  droneArmWarningStaticDirty = false;
  droneArmWarningDynamicDirty = false;
  droneSafetyThrottleLockActive = false;
  setDroneArmActive(false);
  elrsTxPowerUserOverrideActive = false;
  elrsLowPowerFallbackActive = true;
  elrsTxPowerMw = clampElrsTxPowerMw(ELRS_LOW_POWER_FALLBACK_MW);
  if (getModelProtocol(modelIndex) == PROTOCOL_ELRS) {
    initElrsUart();
    sendElrsTxPowerWrite(ELRS_LOW_POWER_FALLBACK_MW, false);
  } else {
    stopElrsUart();
  }

  trimRenderX = models[modelIndex].trimX[currentTrimPage];
  trimRenderY = models[modelIndex].trimY[currentTrimPage];
  selectedMixIndex = 0;
  mixingNeedsRedraw = true;

  trimNeedsRedraw = true;
  reverseNeedsRedraw = true;
  failsafeNeedsRedraw = true;
  trimDirty = true;
  modelNameDirty = true;
  modelNameNeedsRedraw = true;

  for (int c = 0; c < CHANNEL_COUNT; c++) {
    reverseChannelDirty[c] = true;
    failsafeDirty[c] = true;
  }

  uiNeedsRedraw = true;
}

void beginModelNameEdit(int modelIndex) {
  selectedModelIndex = modelIndex;
  keyboardBuffer = "";
  keyboardTarget = KEYBOARD_TARGET_MODEL_NAME;
  keyboardActive = true;
  keyboardLowercase = false;
  keyboardNeedsRedraw = true;
  inputBoxSelected = true;
  modelNameDirty = true;
  modelNameNeedsRedraw = true;
  uiNeedsRedraw = true;
}

bool setEspNowWifiChannel(uint8_t channel) {
  if (WiFi.setChannel(channel)) {
    return true;
  }

  esp_err_t promiscOn = esp_wifi_set_promiscuous(true);
  esp_err_t setChan = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_err_t promiscOff = esp_wifi_set_promiscuous(false);

  if (promiscOn == ESP_OK && setChan == ESP_OK && promiscOff == ESP_OK) {
    return true;
  }

  if (RADIO_PROTOCOL_SERIAL_DEBUG) {
    Serial.printf("ESP-NOW channel set failed (set=%d on=%d off=%d)\n",
                  (int)setChan, (int)promiscOn, (int)promiscOff);
  }
  return false;
}

bool ensureEspNowPeer(const uint8_t *peerAddress) {
  if (!espNowReady || peerAddress == nullptr) return false;
  if (esp_now_is_peer_exist(peerAddress)) return true;

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerAddress, ESP_NOW_ETH_ALEN);
  peerInfo.channel = 0;
  peerInfo.ifidx = WIFI_IF_STA;
  peerInfo.encrypt = false;

  esp_err_t result = esp_now_add_peer(&peerInfo);
  if (result != ESP_OK) {
    if (RADIO_PROTOCOL_SERIAL_DEBUG) {
      Serial.printf("ESP-NOW add peer failed: %d\n", (int)result);
    }
    return false;
  }

  return true;
}

// ESP-NOW is the lightweight receiver link used by non-ELRS models. Binding,
// telemetry, and control packets all share this peer/session state.
bool initEspNowLink() {
  if (otaModeActive) {
    return false;
  }

  if (espNowReady) {
    return ensureEspNowPeer(espNowBroadcastAddress);
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  unsigned long wifiStart = millis();
  while (!WiFi.STA.started() && millis() - wifiStart < 1000) {
    delay(10);
  }

  if (!WiFi.STA.started()) {
    if (RADIO_PROTOCOL_SERIAL_DEBUG) {
      Serial.println("ESP-NOW STA start timed out");
    }
    return false;
  }

  WiFi.disconnect();
  if (!setEspNowWifiChannel(ESPNOW_WIFI_CHANNEL)) {
    if (RADIO_PROTOCOL_SERIAL_DEBUG) {
      Serial.println("ESP-NOW channel set failed");
    }
    return false;
  }

  esp_err_t initResult = esp_now_init();
  if (initResult != ESP_OK) {
    if (RADIO_PROTOCOL_SERIAL_DEBUG) {
      Serial.printf("ESP-NOW init failed: %d\n", (int)initResult);
    }
    return false;
  }

  if (esp_now_register_recv_cb(onEspNowReceive) != ESP_OK) {
    if (RADIO_PROTOCOL_SERIAL_DEBUG) {
      Serial.println("ESP-NOW recv callback registration failed");
    }
    esp_now_deinit();
    return false;
  }

  if (esp_now_register_send_cb(onEspNowSend) != ESP_OK) {
    if (RADIO_PROTOCOL_SERIAL_DEBUG) {
      Serial.println("ESP-NOW send callback registration failed");
    }
    esp_now_deinit();
    return false;
  }

  espNowReady = true;

  if (RADIO_PROTOCOL_SERIAL_DEBUG) {
    Serial.print("ESP-NOW transmitter ready on channel ");
    Serial.println(ESPNOW_WIFI_CHANNEL);
  }

  return ensureEspNowPeer(espNowBroadcastAddress);
}

void logOtaNetworkState(const char *label) {
  wifi_mode_t wifiMode = WIFI_MODE_NULL;
  esp_wifi_get_mode(&wifiMode);

  IPAddress apIp = WiFi.softAPIP();
  IPAddress staIp = WiFi.localIP();

  Serial.printf(
    "%s mode=%d apIP=%u.%u.%u.%u staIP=%u.%u.%u.%u staStatus=%d channel=%u apSSID='%s'\n",
    (label != nullptr) ? label : "OTA",
    (int)wifiMode,
    apIp[0], apIp[1], apIp[2], apIp[3],
    staIp[0], staIp[1], staIp[2], staIp[3],
    (int)WiFi.status(),
    WiFi.channel(),
    otaApSsid);
}

void logPartitionInfo(const char *label, const esp_partition_t *partition) {
  if (partition == nullptr) {
    Serial.printf("%s <null>\n", (label != nullptr) ? label : "partition");
    return;
  }

  Serial.printf("%s label=%s type=%u subtype=%u addr=0x%08lX size=0x%08lX\n",
                (label != nullptr) ? label : "partition",
                partition->label,
                (unsigned int)partition->type,
                (unsigned int)partition->subtype,
                (unsigned long)partition->address,
                (unsigned long)partition->size);
}

bool isOtaKeyboardTarget(KeyboardTarget target) {
  return target == KEYBOARD_TARGET_OTA_STA_SSID ||
         target == KEYBOARD_TARGET_OTA_STA_PASSWORD ||
         target == KEYBOARD_TARGET_OTA_AP_SSID ||
         target == KEYBOARD_TARGET_20Q_LEARN;
}

const char* getKeyboardTargetLabel(KeyboardTarget target) {
  if (target == KEYBOARD_TARGET_OTA_STA_SSID) return "WiFi SSID";
  if (target == KEYBOARD_TARGET_OTA_STA_PASSWORD) return "Home Pass";
  if (target == KEYBOARD_TARGET_OTA_AP_SSID) return "AP Name";
  if (target == KEYBOARD_TARGET_20Q_LEARN) return "Correct Answer";
  return "Text";
}

// OTA mode temporarily turns the transmitter into a Wi-Fi update target. The
// normal control loop keeps running, but radio and UI screens should treat OTA
// as a special service mode while it is active.
void startOtaMode() {
  if (otaModeActive || otaUpdateInProgress) return;

  if (espNowBindingMode) {
    cancelEspNowBinding(true);
  }
  if (espNowReady) {
    esp_now_deinit();
    espNowReady = false;
  }
  mixNumpadActive = false;
  mixNumpadNeedsRedraw = false;
  mixNumpadBuffer = "";
  mixNumpadTarget = NUMPAD_TARGET_MIX_RATE;
  elrsReceiverPwmNumpadArmed = false;
  elrsReceiverPwmNumpadArmedIndex = -1;
  elrsReceiverPwmNumpadOriginalUs = 0;
  resetEspNowTelemetry();
  espNowProtocolActive = false;
  espNowProtocolStartTime = 0;

  sanitizeOtaSettings();
  Serial.println("OTA: starting WiFi service");

  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, false);
  delay(100);
  WiFi.mode(WIFI_MODE_NULL);
  delay(100);

  if (!WiFi.mode(WIFI_AP_STA)) {
    Serial.println("OTA: failed to enter WIFI_AP_STA mode");
    otaModeActive = false;
    otaServiceReady = false;
    otaUsingSoftAP = false;
    otaApReady = false;
    otaStaAutoConnectEnabled = false;
    otaStaAutoConnectAfterMs = 0;
    otaStaConnectPending = false;
    fullRedraw = true;
    uiNeedsRedraw = true;
    return;
  }
  WiFi.setSleep(false);
  WiFi.setHostname(OTA_HOSTNAME);

  otaApReady = WiFi.softAP(otaApSsid, OTA_AP_PASSWORD, 1, false, 1);
  if (!otaApReady) {
    Serial.printf("OTA: softAP start failed for SSID '%s'\n", otaApSsid);
  } else {
    delay(100);
    logOtaNetworkState("OTA: AP started");
  }

  bool staConfigured = false;
  otaStaAutoConnectEnabled = false;
  otaStaAutoConnectAfterMs = 0;
  otaStaConnectPending = false;
  otaStaConnectStartTime = 0;
  if (strlen(otaStaSsid) > 0) {
    size_t passwordLen = strlen(otaStaPassword);
    bool passwordUsable = (passwordLen == 0 || passwordLen >= 8);
    if (!passwordUsable) {
      Serial.printf("OTA: STA password for '%s' is too short (%u), skipping STA connect\n",
                    otaStaSsid, (unsigned int)passwordLen);
    } else {
      staConfigured = true;
      otaStaAutoConnectEnabled = true;
      otaStaAutoConnectAfterMs = millis() + 15000UL;
      Serial.printf("OTA: AP ready, deferring STA connect to '%s' while AP is available\n",
                    otaStaSsid);
    }
  }

  if (!otaApReady && !staConfigured) {
    otaModeActive = false;
    otaServiceReady = false;
    otaUsingSoftAP = false;
    otaApReady = false;
    otaStaAutoConnectEnabled = false;
    otaStaAutoConnectAfterMs = 0;
    otaStaConnectPending = false;
    fullRedraw = true;
    uiNeedsRedraw = true;
    return;
  }

  otaUsingSoftAP = otaApReady;
  otaUpdateInProgress = false;
  otaRestartPending = false;
  otaHttpServer.begin();
  logOtaNetworkState("OTA: HTTP server ready");

  otaModeActive = true;
  otaServiceReady = true;
  fullRedraw = true;
  uiNeedsRedraw = true;
}

void stopOtaMode() {
  if (!otaModeActive) return;
  if (otaUpdateInProgress) return;

  otaHttpServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  otaModeActive = false;
  otaServiceReady = false;
  otaUsingSoftAP = false;
  otaApReady = false;
  otaStaAutoConnectEnabled = false;
  otaStaAutoConnectAfterMs = 0;
  otaStaConnectPending = false;
  otaStaConnectStartTime = 0;
  otaRestartPending = false;

  if (getModelProtocol(activeModel) == PROTOCOL_ESPNOW) {
    initEspNowLink();
  }

  fullRedraw = true;
  uiNeedsRedraw = true;
}

void updateOtaService() {
  if (!otaModeActive || !otaServiceReady) return;

  if (otaStaAutoConnectEnabled &&
      !otaStaConnectPending &&
      WiFi.status() != WL_CONNECTED) {
    bool apHasClients = otaApReady && WiFi.softAPgetStationNum() > 0;
    if (!apHasClients && millis() >= otaStaAutoConnectAfterMs) {
      Serial.printf("OTA: starting deferred STA connect to '%s'\n", otaStaSsid);
      WiFi.begin(otaStaSsid, otaStaPassword);
      otaStaConnectPending = true;
      otaStaConnectStartTime = millis();
      uiNeedsRedraw = true;
      fullRedraw = true;
    }
  }

  if (otaStaConnectPending) {
    wl_status_t staStatus = WiFi.status();
    if (staStatus == WL_CONNECTED) {
      otaStaConnectPending = false;
      otaUsingSoftAP = false;
      logOtaNetworkState("OTA: STA connected");
      uiNeedsRedraw = true;
      fullRedraw = true;
    } else if (millis() - otaStaConnectStartTime >= 8000) {
      otaStaConnectPending = false;
      if (!otaApReady) {
        otaHttpServer.stop();
        otaModeActive = false;
        otaServiceReady = false;
      }
      Serial.printf("OTA: STA connect timed out for '%s' (status=%d)\n",
                    otaStaSsid, (int)staStatus);
      uiNeedsRedraw = true;
      fullRedraw = true;
    }
  }

  WiFiClient client = otaHttpServer.available();
  if (client) {
    unsigned long requestStart = millis();
    while (client.connected() && !client.available() && millis() - requestStart < 1000) {
      delay(1);
    }

    if (client.available()) {
      String requestLine = client.readStringUntil('\r');
      client.readStringUntil('\n');
      requestLine.trim();

      int contentLength = -1;
      while (client.connected()) {
        String header = client.readStringUntil('\n');
        header.trim();
        if (header.length() == 0) break;

        if (header.startsWith("Content-Length:")) {
          String lengthValue = header.substring(strlen("Content-Length:"));
          lengthValue.trim();
          contentLength = lengthValue.toInt();
        }
      }

      if (requestLine.startsWith("GET / ")) {
        const char* page =
          "<!doctype html><html><head><meta charset='utf-8'><title>Anubis OTA</title></head>"
          "<body style='font-family:sans-serif;background:#101418;color:#e8edf2;padding:24px;'>"
          "<h2>Anubis Transmitter OTA</h2>"
          "<p>Select a firmware <code>.bin</code> file and send it as raw binary:</p>"
          "<input type='file' id='fw' accept='.bin'>"
          "<button onclick='up()'>Upload</button>"
          "<pre id='s'></pre>"
          "<script>"
          "async function up(){const f=document.getElementById('fw').files[0];"
          "if(!f){return;}document.getElementById('s').textContent='Uploading...';"
          "const r=await fetch('/update',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:f});"
          "document.getElementById('s').textContent=await r.text();}</script>"
          "</body></html>";
        client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
        client.print(page);
      } else if (requestLine.startsWith("POST /update")) {
        otaUpdateInProgress = true;
        fullRedraw = true;
        uiNeedsRedraw = true;

        bool updateOk = false;
        if (contentLength > 0) {
          const esp_partition_t* runningPartition = esp_ota_get_running_partition();
          const esp_partition_t* bootPartition = esp_ota_get_boot_partition();
          const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
          Serial.printf("OTA upload len=%d slots=%u\n",
                        contentLength,
                        (unsigned int)esp_ota_get_app_partition_count());
          logPartitionInfo("OTA running", runningPartition);
          logPartitionInfo("OTA boot", bootPartition);
          logPartitionInfo("OTA target", updatePartition);

          esp_ota_handle_t otaHandle = 0;
          esp_err_t beginErr = esp_ota_begin(updatePartition, (size_t)contentLength, &otaHandle);
          if (beginErr != ESP_OK) {
            Serial.printf("OTA begin failed: %d (%s)\n",
                          (int)beginErr,
                          esp_err_to_name(beginErr));
          } else {
          size_t remaining = (size_t)contentLength;
          uint8_t buffer[1024];
          unsigned long lastData = millis();

          while (remaining > 0 && millis() - lastData < 5000) {
            int availableBytes = client.available();
            if (availableBytes <= 0) {
              delay(1);
              continue;
            }

            size_t toRead = (size_t)availableBytes;
            if (toRead > sizeof(buffer)) toRead = sizeof(buffer);
            if (toRead > remaining) toRead = remaining;

            size_t readBytes = client.read(buffer, toRead);
            if (readBytes > 0) {
              lastData = millis();
              remaining -= readBytes;
              esp_err_t writeErr = esp_ota_write(otaHandle, (const void*)buffer, readBytes);
              if (writeErr != ESP_OK) {
                Serial.printf("OTA write failed: %d\n", (int)writeErr);
                break;
              }
            }
          }

          if (remaining == 0) {
            esp_err_t endErr = esp_ota_end(otaHandle);
            if (endErr == ESP_OK) {
              esp_err_t bootErr = esp_ota_set_boot_partition(updatePartition);
              if (bootErr == ESP_OK) {
                updateOk = true;
              } else {
                Serial.printf("OTA set boot partition failed: %d\n", (int)bootErr);
              }
            } else {
              Serial.printf("OTA end failed: %d\n", (int)endErr);
            }
          } else {
            esp_ota_abort(otaHandle);
          }
          }
        } else {
          Serial.println("OTA update rejected: missing content length");
        }

        otaUpdateInProgress = false;
        fullRedraw = true;
        uiNeedsRedraw = true;

        if (updateOk) {
          otaRestartPending = true;
          client.print("HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nUpdate complete. Rebooting...");
        } else {
          client.print("HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nUpdate failed.");
        }
      } else {
        client.print("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
      }
    }

    delay(1);
    client.stop();
  }

  if (otaRestartPending && !otaUpdateInProgress) {
    delay(250);
    ESP.restart();
  }
}

void beginElrsReceiverConfig() {
  if (otaUpdateInProgress) return;
  if (otaModeActive) {
    stopOtaMode();
  }
  if (espNowBindingMode) {
    cancelEspNowBinding(true);
  }
  if (espNowReady) {
    esp_now_deinit();
    espNowReady = false;
  }
  resetEspNowTelemetry();
  espNowProtocolActive = false;

  elrsReceiverConfigActive = true;
  elrsReceiverConfigState = RX_CONFIG_CONNECTING;
  elrsReceiverConfigStartTime = millis();
  elrsReceiverConfigLastUiTime = 0;
  elrsBindBurstUntil = 0;
  elrsBindAwaitingResult = false;
  snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "Waiting for ELRS RX WiFi...");
  snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "SSID: %s", ELRS_RX_WIFI_SSID);
  elrsReceiverConfigLastEndpoint[0] = '\0';
  strncpy(elrsReceiverProductName, "-", sizeof(elrsReceiverProductName) - 1);
  elrsReceiverProductName[sizeof(elrsReceiverProductName) - 1] = '\0';
  strncpy(elrsReceiverLuaName, "-", sizeof(elrsReceiverLuaName) - 1);
  elrsReceiverLuaName[sizeof(elrsReceiverLuaName) - 1] = '\0';
  strncpy(elrsReceiverUidText, "-", sizeof(elrsReceiverUidText) - 1);
  elrsReceiverUidText[sizeof(elrsReceiverUidText) - 1] = '\0';
  elrsReceiverWifiInterval = -1;
  elrsReceiverUartBaud = -1;
  elrsReceiverFailsafe = -1;
  elrsReceiverModelId = -1;
  elrsReceiverHasFailsafe = false;
  elrsReceiverHasModelId = false;
  elrsReceiverHasPwmConfig = false;
  elrsReceiverEditFailsafe = 0;
  elrsReceiverEditModelId = 255;
  elrsReceiverPwmCount = 0;
  for (int i = 0; i < 16; i++) {
    elrsReceiverPwmRaw[i] = 0;
    elrsReceiverPwmEditRaw[i] = 0;
    elrsReceiverPwmFailsafeUs[i] = 1500;
    elrsReceiverPwmEditFailsafeUs[i] = 1500;
    elrsReceiverPwmPins[i] = -1;
    elrsReceiverPwmEditTouched[i] = false;
    elrsReceiverPwmEditGeneration[i] = 0;
  }
  elrsReceiverEditPwmIndex = 0;
  elrsReceiverSaveArmed = false;
  elrsReceiverConfigTouchLocked = true;
  elrsReceiverConfigTouchUnlockAt = millis() + 1200UL;
  elrsReceiverConfigNoTouchSince = 0;
  elrsReceiverConfigDirty = false;
  elrsReceiverConfigGeneration++;
  elrsReceiverConfigLoadedAt = 0;
  elrsReceiverConfigBody = "";

  Serial.printf("ELRS RX config: connecting to '%s' with default password\n", ELRS_RX_WIFI_SSID);
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true, false);
  delay(80);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setHostname(OTA_HOSTNAME);
  WiFi.begin(ELRS_RX_WIFI_SSID, ELRS_RX_WIFI_PASSWORD);

  setScreen(SCREEN_ELRS_RX_CONFIG);
  fullRedraw = true;
  uiNeedsRedraw = true;
}

void stopElrsReceiverConfig() {
  if (!elrsReceiverConfigActive && elrsReceiverConfigState == RX_CONFIG_IDLE) return;

  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);

  elrsReceiverConfigActive = false;
  elrsReceiverConfigState = RX_CONFIG_IDLE;

  if (getModelProtocol(activeModel) == PROTOCOL_ESPNOW) {
    initEspNowLink();
  }

  fullRedraw = true;
  uiNeedsRedraw = true;
}

bool requestElrsReceiverMethod(const char *method, const char *path, const char *body, const char *contentType, String &response) {
  response = "";
  IPAddress host(10, 0, 0, 1);
  WiFiClient client;
  if (!client.connect(host, 80)) {
    return false;
  }

  int bodyLen = (body != nullptr) ? strlen(body) : 0;
  client.print(method);
  client.print(" ");
  client.print(path);
  client.print(" HTTP/1.1\r\n"
               "Host: 10.0.0.1\r\n"
               "Accept: application/json,text/plain,text/html,*/*\r\n"
               "Accept-Encoding: identity\r\n"
               "Connection: close\r\n"
               "User-Agent: AnubisTx\r\n");
  if (bodyLen > 0) {
    client.print("Content-Type: ");
    client.print((contentType != nullptr) ? contentType : "application/json");
    client.print("\r\nContent-Length: ");
    client.print(bodyLen);
    client.print("\r\n");
  }
  client.print("\r\n");
  if (bodyLen > 0) {
    client.print(body);
  }

  unsigned long start = millis();
  while (millis() - start < ELRS_RX_HTTP_TIMEOUT_MS) {
    while (client.available()) {
      char c = (char)client.read();
      if (response.length() < 4096) {
        response += c;
      }
      start = millis();
    }
    if (!client.connected()) break;
    delay(1);
  }

  client.stop();
  return response.length() > 0;
}

bool requestElrsReceiverPath(const char *path, String &response) {
  return requestElrsReceiverMethod("GET", path, nullptr, nullptr, response);
}

bool extractJsonObjectValue(const String &json, const char *key, String &out) {
  out = "";
  String needle = "\"";
  needle += key;
  needle += "\":";
  int keyIndex = json.indexOf(needle);
  if (keyIndex < 0) return false;

  int objectStart = json.indexOf('{', keyIndex + needle.length());
  if (objectStart < 0) return false;

  int depth = 0;
  bool inString = false;
  bool escaping = false;
  for (int i = objectStart; i < json.length(); i++) {
    char c = json.charAt(i);
    if (escaping) {
      escaping = false;
      continue;
    }
    if (c == '\\') {
      escaping = true;
      continue;
    }
    if (c == '"') {
      inString = !inString;
      continue;
    }
    if (inString) continue;

    if (c == '{') {
      depth++;
    } else if (c == '}') {
      depth--;
      if (depth == 0) {
        out = json.substring(objectStart, i + 1);
        return true;
      }
    }
  }

  return false;
}

bool replaceJsonIntValue(String &json, const char *key, int value) {
  String needle = "\"";
  needle += key;
  needle += "\":";
  int keyIndex = json.indexOf(needle);
  if (keyIndex < 0) return false;

  int valueStart = keyIndex + needle.length();
  while (valueStart < json.length() && isspace((unsigned char)json.charAt(valueStart))) {
    valueStart++;
  }

  int valueEnd = valueStart;
  if (valueEnd < json.length() && json.charAt(valueEnd) == '-') valueEnd++;
  while (valueEnd < json.length() && isdigit((unsigned char)json.charAt(valueEnd))) {
    valueEnd++;
  }
  if (valueEnd <= valueStart) return false;

  json = json.substring(0, valueStart) + String(value) + json.substring(valueEnd);
  return true;
}

int decodeElrsPwmFailsafeUs(uint32_t raw) {
  int failsafeOffset = (int)(raw & ELRS_RX_PWM_FAILSAFE_MASK);
  return constrain(failsafeOffset + ELRS_RX_PWM_FAILSAFE_OFFSET_US,
                   ELRS_RX_PWM_FAILSAFE_OFFSET_US,
                   ELRS_RX_PWM_FAILSAFE_OFFSET_US + (int)ELRS_RX_PWM_FAILSAFE_MASK);
}

uint32_t encodeElrsPwmFailsafeUs(uint32_t raw, int failsafeUs) {
  int clampedUs = constrain(failsafeUs,
                            ELRS_RX_PWM_FAILSAFE_OFFSET_US,
                            ELRS_RX_PWM_FAILSAFE_OFFSET_US + (int)ELRS_RX_PWM_FAILSAFE_MASK);
  uint32_t encodedFailsafe = (uint32_t)(clampedUs - ELRS_RX_PWM_FAILSAFE_OFFSET_US) & ELRS_RX_PWM_FAILSAFE_MASK;
  return (raw & ~ELRS_RX_PWM_FAILSAFE_MASK) | encodedFailsafe;
}

uint8_t decodeElrsPwmInputChannel(uint32_t raw) {
  return (uint8_t)((raw >> ELRS_RX_PWM_INPUT_CHANNEL_SHIFT) & ELRS_RX_PWM_INPUT_CHANNEL_MASK);
}

uint32_t encodeElrsPwmInputChannel(uint32_t raw, uint8_t inputChannel) {
  uint32_t inputBits = ((uint32_t)(inputChannel & ELRS_RX_PWM_INPUT_CHANNEL_MASK))
                       << ELRS_RX_PWM_INPUT_CHANNEL_SHIFT;
  return (raw & ~(ELRS_RX_PWM_INPUT_CHANNEL_MASK << ELRS_RX_PWM_INPUT_CHANNEL_SHIFT)) | inputBits;
}

int displayElrsPwmFailsafeUs(int failsafeUs) {
  return constrain(failsafeUs, ELRS_RX_PWM_FAILSAFE_MIN_US, ELRS_RX_PWM_FAILSAFE_MAX_US);
}

bool isElrsReceiverPwmConfigEdited(int index) {
  if (index < 0 || index >= elrsReceiverPwmCount) return false;
  if (!elrsReceiverPwmEditTouched[index]) return false;
  if (elrsReceiverPwmEditGeneration[index] != elrsReceiverConfigGeneration) return false;
  return elrsReceiverPwmEditRaw[index] != elrsReceiverPwmRaw[index];
}

bool parseElrsReceiverPwmConfig(const String &json) {
  int pwmIndex = json.indexOf("\"pwm\":[");
  if (pwmIndex < 0) return false;
  int cursor = pwmIndex;
  uint8_t count = 0;

  while (count < 16) {
    int configIndex = json.indexOf("\"config\":", cursor);
    if (configIndex < 0) break;
    int pinIndex = json.indexOf("\"pin\":", configIndex);
    if (pinIndex < 0) break;
    int nextObject = json.indexOf("}", pinIndex);
    if (nextObject < 0) break;

    int configStart = configIndex + strlen("\"config\":");
    while (configStart < json.length() && isspace((unsigned char)json.charAt(configStart))) configStart++;
    int configEnd = configStart;
    while (configEnd < json.length() && isdigit((unsigned char)json.charAt(configEnd))) configEnd++;

    int pinStart = pinIndex + strlen("\"pin\":");
    while (pinStart < json.length() && isspace((unsigned char)json.charAt(pinStart))) pinStart++;
    int pinEnd = pinStart;
    if (pinEnd < json.length() && json.charAt(pinEnd) == '-') pinEnd++;
    while (pinEnd < json.length() && isdigit((unsigned char)json.charAt(pinEnd))) pinEnd++;

    if (configEnd <= configStart || pinEnd <= pinStart) break;

    uint32_t raw = (uint32_t)json.substring(configStart, configEnd).toInt();
    int failsafeUs = decodeElrsPwmFailsafeUs(raw);

    elrsReceiverPwmRaw[count] = raw;
    elrsReceiverPwmEditRaw[count] = raw;
    elrsReceiverPwmFailsafeUs[count] = failsafeUs;
    elrsReceiverPwmEditFailsafeUs[count] = displayElrsPwmFailsafeUs(failsafeUs);
    elrsReceiverPwmPins[count] = json.substring(pinStart, pinEnd).toInt();
    elrsReceiverPwmEditTouched[count] = false;
    elrsReceiverPwmEditGeneration[count] = 0;
    count++;
    cursor = nextObject + 1;
  }

  elrsReceiverPwmCount = count;
  elrsReceiverHasPwmConfig = count > 0;
  if (count > 0) {
    Serial.print("ELRS RX PWM decoded:");
    for (int i = 0; i < count; i++) {
      Serial.printf(" out%u pin=%d input=CH%u raw=%lu fs=%dus display=%dus",
                    (unsigned int)(i + 1),
                    elrsReceiverPwmPins[i],
                    (unsigned int)(decodeElrsPwmInputChannel(elrsReceiverPwmRaw[i]) + 1),
                    (unsigned long)elrsReceiverPwmRaw[i],
                    elrsReceiverPwmFailsafeUs[i],
                    displayElrsPwmFailsafeUs(elrsReceiverPwmFailsafeUs[i]));
    }
    Serial.println();

  }
  return count > 0;
}

bool replaceElrsPwmConfigValues(String &json) {
  int pwmIndex = json.indexOf("\"pwm\":[");
  if (pwmIndex < 0) return false;
  int arrayStart = json.indexOf('[', pwmIndex);
  if (arrayStart < 0) return false;

  int depth = 0;
  bool inString = false;
  bool escaping = false;
  int arrayEnd = -1;
  for (int i = arrayStart; i < json.length(); i++) {
    char c = json.charAt(i);
    if (escaping) {
      escaping = false;
      continue;
    }
    if (c == '\\') {
      escaping = true;
      continue;
    }
    if (c == '"') {
      inString = !inString;
      continue;
    }
    if (inString) continue;

    if (c == '[') {
      depth++;
    } else if (c == ']') {
      depth--;
      if (depth == 0) {
        arrayEnd = i;
        break;
      }
    }
  }
  if (arrayEnd < 0) return false;

  String numericPwm = "\"pwm\":[";
  for (int i = 0; i < elrsReceiverPwmCount; i++) {
    uint32_t nextRaw = elrsReceiverPwmRaw[i];
    bool touchedThisLoad = elrsReceiverPwmEditTouched[i] &&
                           elrsReceiverPwmEditGeneration[i] == elrsReceiverConfigGeneration;
    if (touchedThisLoad) {
      nextRaw = elrsReceiverPwmEditRaw[i];
    }
    Serial.printf("ELRS RX PWM save out%d touched=%d gen=%lu/%lu raw=%lu next=%lu input=CH%u display=%d\n",
                  i + 1,
                  touchedThisLoad ? 1 : 0,
                  (unsigned long)elrsReceiverPwmEditGeneration[i],
                  (unsigned long)elrsReceiverConfigGeneration,
                  (unsigned long)elrsReceiverPwmRaw[i],
                  (unsigned long)nextRaw,
                  (unsigned int)(decodeElrsPwmInputChannel(nextRaw) + 1),
                  elrsReceiverPwmEditFailsafeUs[i]);
    elrsReceiverPwmEditRaw[i] = nextRaw;
    if (i > 0) numericPwm += ",";
    numericPwm += String((unsigned long)nextRaw);
  }
  numericPwm += "]";

  json = json.substring(0, pwmIndex) + numericPwm + json.substring(arrayEnd + 1);
  return true;
}

bool isElrsReceiverConfigEdited() {
  if (!elrsReceiverSaveArmed) return false;
  if (elrsReceiverHasModelId && elrsReceiverEditModelId != elrsReceiverModelId) return true;
  if (elrsReceiverHasFailsafe && elrsReceiverEditFailsafe != elrsReceiverFailsafe) return true;
  for (int i = 0; i < elrsReceiverPwmCount; i++) {
    if (isElrsReceiverPwmConfigEdited(i)) return true;
  }
  return false;
}

bool hasElrsReceiverPwmEdits() {
  for (int i = 0; i < elrsReceiverPwmCount; i++) {
    if (isElrsReceiverPwmConfigEdited(i)) return true;
  }
  return false;
}

int modelFailsafePercentToPwmUs(int percent) {
  return constrain(1500 + (constrain(percent, -100, 100) * 5),
                   ELRS_RX_PWM_FAILSAFE_MIN_US,
                   ELRS_RX_PWM_FAILSAFE_MAX_US);
}

bool applyModelFailsafesToElrsReceiverPwm() {
  if (elrsReceiverConfigState != RX_CONFIG_DONE || elrsReceiverPwmCount == 0) {
    snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "No PWM outputs found");
    snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "Load receiver config first");
    uiNeedsRedraw = true;
    return false;
  }

  int applied = 0;
  for (int i = 0; i < elrsReceiverPwmCount; i++) {
    uint8_t inputChannel = decodeElrsPwmInputChannel(elrsReceiverPwmRaw[i]);
    bool channelKnown = inputChannel < CHANNEL_COUNT;
    if (!channelKnown) {
      Serial.printf("ELRS RX PWM apply TX failsafe skipped out%d input=CH%u (not stored by TX)\n",
                    i + 1,
                    (unsigned int)(inputChannel + 1));
      continue;
    }

    int nextFailsafeUs = 1500;
    if (channelKnown && models[activeModel].failsafe[inputChannel]) {
      nextFailsafeUs = modelFailsafePercentToPwmUs(getModelFailsafeValue(activeModel, inputChannel));
    }

    elrsReceiverPwmEditFailsafeUs[i] = nextFailsafeUs;
    elrsReceiverPwmEditRaw[i] = encodeElrsPwmFailsafeUs(elrsReceiverPwmRaw[i], nextFailsafeUs);
    elrsReceiverPwmEditTouched[i] = true;
    elrsReceiverPwmEditGeneration[i] = elrsReceiverConfigGeneration;
    applied++;

    Serial.printf("ELRS RX PWM apply TX failsafe out%d input=CH%u known=%d enabled=%d value=%d\n",
                  i + 1,
                  (unsigned int)(inputChannel + 1),
                  1,
                  models[activeModel].failsafe[inputChannel] ? 1 : 0,
                  nextFailsafeUs);
  }

  elrsReceiverSaveArmed = true;
  elrsReceiverConfigDirty = isElrsReceiverConfigEdited();
  if (applied > 0) {
    snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "TX failsafes loaded");
    snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "Review values, then press SAVE");
  } else {
    snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "No matching TX channels");
    snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "Receiver outputs are CH7+");
  }
  fullRedraw = true;
  uiNeedsRedraw = true;
  return applied > 0;
}

ButtonID getElrsReceiverPwmButton(int index) {
  switch (index) {
    case 0: return BTN_ELRS_RX_PWM1;
    case 1: return BTN_ELRS_RX_PWM2;
    case 2: return BTN_ELRS_RX_PWM3;
    case 3: return BTN_ELRS_RX_PWM4;
    default: return BTN_NONE;
  }
}

int getElrsReceiverPwmIndexForButton(ButtonID button) {
  if (button == BTN_ELRS_RX_PWM1) return 0;
  if (button == BTN_ELRS_RX_PWM2) return 1;
  if (button == BTN_ELRS_RX_PWM3) return 2;
  if (button == BTN_ELRS_RX_PWM4) return 3;
  return -1;
}

bool saveElrsReceiverConfig() {
  if (!elrsReceiverConfigActive || WiFi.status() != WL_CONNECTED) {
    snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "RX WiFi not connected");
    snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "Reconnect before saving");
    fullRedraw = true;
    uiNeedsRedraw = true;
    return false;
  }

  if (elrsReceiverConfigBody.length() == 0) {
    snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "No config body cached");
    snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "Reopen Receiver Config");
    fullRedraw = true;
    uiNeedsRedraw = true;
    return false;
  }

  elrsReceiverConfigDirty = isElrsReceiverConfigEdited();
  if (!elrsReceiverConfigDirty) {
    snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "No RX config changes");
    snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "Nothing written to receiver");
    Serial.println("ELRS RX config: save skipped, no edited fields");
    uiNeedsRedraw = true;
    return false;
  }

  if (!elrsReceiverSaveArmed) {
    snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "No confirmed RX edits");
    snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "Nothing written to receiver");
    Serial.println("ELRS RX config: save skipped, no user-armed edits");
    uiNeedsRedraw = true;
    return false;
  }

  if (elrsReceiverConfigLoadedAt != 0 &&
      millis() - elrsReceiverConfigLoadedAt < 1500UL) {
    snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "RX config just loaded");
    snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "Wait before saving edits");
    Serial.println("ELRS RX config: save skipped, config was just loaded");
    elrsReceiverConfigDirty = isElrsReceiverConfigEdited();
    uiNeedsRedraw = true;
    return false;
  }

  String postBody = elrsReceiverConfigBody;
  bool replacedModel = replaceJsonIntValue(postBody, "modelid", constrain(elrsReceiverEditModelId, 0, 255));
  bool replacedFailsafe = replaceJsonIntValue(postBody, "sbus-failsafe", constrain(elrsReceiverEditFailsafe, 0, 255));
  bool pwmEdited = hasElrsReceiverPwmEdits();
  bool replacedPwm = true;
  if (pwmEdited) {
    replacedPwm = replaceElrsPwmConfigValues(postBody);
  }
  if ((elrsReceiverHasModelId && !replacedModel) ||
      (elrsReceiverHasFailsafe && !replacedFailsafe) ||
      !replacedPwm) {
    snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "Config edit failed");
    snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "Missing edited config key");
    fullRedraw = true;
    uiNeedsRedraw = true;
    return false;
  }

  String response;
  snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "Saving RX config...");
  snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "POST /config");
  fullRedraw = true;
  uiNeedsRedraw = true;

  Serial.println("ELRS RX config: POST http://10.0.0.1/config");
  Serial.println(postBody);
  bool gotResponse = requestElrsReceiverMethod("POST", "/config", postBody.c_str(), "application/json", response);
  if (gotResponse) {
    Serial.printf("ELRS RX config save response (%u bytes captured):\n", (unsigned int)response.length());
    Serial.println(response);
  } else {
    Serial.println("ELRS RX config save: no HTTP response");
  }

  bool httpOk = gotResponse && (response.indexOf("HTTP/1.1 200") >= 0 || response.indexOf("HTTP/1.0 200") >= 0);
  if (httpOk) {
    elrsReceiverModelId = constrain(elrsReceiverEditModelId, 0, 255);
    elrsReceiverFailsafe = constrain(elrsReceiverEditFailsafe, 0, 255);
    for (int i = 0; i < elrsReceiverPwmCount; i++) {
      elrsReceiverPwmRaw[i] = elrsReceiverPwmEditRaw[i];
      elrsReceiverPwmFailsafeUs[i] = decodeElrsPwmFailsafeUs(elrsReceiverPwmRaw[i]);
      elrsReceiverPwmEditFailsafeUs[i] = displayElrsPwmFailsafeUs(elrsReceiverPwmFailsafeUs[i]);
      elrsReceiverPwmEditTouched[i] = false;
      elrsReceiverPwmEditGeneration[i] = 0;
    }
    elrsReceiverSaveArmed = false;
    elrsReceiverConfigBody = postBody;
    elrsReceiverConfigDirty = isElrsReceiverConfigEdited();
    snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "Receiver config saved");
    snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "%s updated", elrsReceiverLuaName);
  } else {
    snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "Save may have failed");
    snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "Check Serial response");
  }

  fullRedraw = true;
  uiNeedsRedraw = true;
  return httpOk;
}

void logElrsReceiverWriteEndpoints() {
  Serial.println("ELRS RX config write endpoint: POST /config Content-Type: application/json");
  Serial.println("ELRS RX options endpoint: POST /options.json Content-Type: application/json");
  Serial.println("ELRS RX action endpoints exist but are not probed automatically: /sethome /forget /reset /reboot");
}

bool extractJsonStringValue(const String &json, const char *key, char *out, size_t outSize) {
  if (out == nullptr || outSize == 0) return false;
  out[0] = '\0';

  String needle = "\"";
  needle += key;
  needle += "\":";
  int keyIndex = json.indexOf(needle);
  if (keyIndex < 0) return false;

  int valueStart = json.indexOf('"', keyIndex + needle.length());
  if (valueStart < 0) return false;
  valueStart++;

  size_t outIndex = 0;
  bool escaping = false;
  for (int i = valueStart; i < json.length(); i++) {
    char c = json.charAt(i);
    if (escaping) {
      if (outIndex < outSize - 1) out[outIndex++] = c;
      escaping = false;
      continue;
    }
    if (c == '\\') {
      escaping = true;
      continue;
    }
    if (c == '"') break;
    if (outIndex < outSize - 1) out[outIndex++] = c;
  }
  out[outIndex] = '\0';
  return outIndex > 0;
}

int extractJsonIntValue(const String &json, const char *key, int fallback) {
  String needle = "\"";
  needle += key;
  needle += "\":";
  int keyIndex = json.indexOf(needle);
  if (keyIndex < 0) return fallback;

  int valueStart = keyIndex + needle.length();
  while (valueStart < json.length() && isspace((unsigned char)json.charAt(valueStart))) {
    valueStart++;
  }
  int valueEnd = valueStart;
  if (valueEnd < json.length() && json.charAt(valueEnd) == '-') valueEnd++;
  while (valueEnd < json.length() && isdigit((unsigned char)json.charAt(valueEnd))) {
    valueEnd++;
  }
  if (valueEnd <= valueStart) return fallback;
  return json.substring(valueStart, valueEnd).toInt();
}

bool extractJsonUidValue(const String &json, char *out, size_t outSize) {
  if (out == nullptr || outSize == 0) return false;
  out[0] = '\0';

  int uidIndex = json.indexOf("\"uid\":[");
  if (uidIndex < 0) return false;
  int valueStart = uidIndex + strlen("\"uid\":[");
  int valueEnd = json.indexOf(']', valueStart);
  if (valueEnd < 0) return false;

  String uid = json.substring(valueStart, valueEnd);
  uid.replace(",", ".");
  uid.trim();
  uid.toCharArray(out, outSize);
  return out[0] != '\0';
}

bool parseElrsReceiverConfigJson(const String &response) {
  int bodyStart = response.indexOf("\r\n\r\n");
  String json = (bodyStart >= 0) ? response.substring(bodyStart + 4) : response;
  json.trim();
  if (json.length() == 0 || json.charAt(0) != '{') return false;

  elrsReceiverConfigGeneration++;
  for (int i = 0; i < 16; i++) {
    elrsReceiverPwmEditTouched[i] = false;
    elrsReceiverPwmEditGeneration[i] = 0;
  }
  elrsReceiverPwmNumpadArmed = false;
  elrsReceiverPwmNumpadArmedIndex = -1;
  elrsReceiverPwmNumpadOriginalUs = 0;
  elrsReceiverSaveArmed = false;
  elrsReceiverConfigTouchLocked = true;
  elrsReceiverConfigTouchUnlockAt = millis() + 1200UL;
  elrsReceiverConfigNoTouchSince = 0;

  extractJsonObjectValue(json, "config", elrsReceiverConfigBody);
  parseElrsReceiverPwmConfig(json);

  bool parsedAny = false;
  parsedAny |= extractJsonStringValue(json, "product_name", elrsReceiverProductName, sizeof(elrsReceiverProductName));
  parsedAny |= extractJsonStringValue(json, "lua_name", elrsReceiverLuaName, sizeof(elrsReceiverLuaName));
  parsedAny |= extractJsonUidValue(json, elrsReceiverUidText, sizeof(elrsReceiverUidText));

  elrsReceiverWifiInterval = extractJsonIntValue(json, "wifi-on-interval", -1);
  elrsReceiverUartBaud = extractJsonIntValue(json, "rcvr-uart-baud", -1);
  elrsReceiverHasFailsafe = json.indexOf("\"sbus-failsafe\":") >= 0;
  elrsReceiverHasModelId = json.indexOf("\"modelid\":") >= 0;
  elrsReceiverFailsafe = extractJsonIntValue(json, "sbus-failsafe", -1);
  elrsReceiverModelId = extractJsonIntValue(json, "modelid", -1);
  elrsReceiverEditFailsafe = (elrsReceiverFailsafe >= 0) ? elrsReceiverFailsafe : 0;
  elrsReceiverEditModelId = (elrsReceiverModelId >= 0) ? elrsReceiverModelId : 255;
  elrsReceiverConfigDirty = isElrsReceiverConfigEdited();
  elrsReceiverConfigLoadedAt = millis();

  parsedAny |= (elrsReceiverWifiInterval >= 0);
  parsedAny |= (elrsReceiverUartBaud >= 0);
  parsedAny |= elrsReceiverHasFailsafe;
  parsedAny |= elrsReceiverHasModelId;
  parsedAny |= elrsReceiverHasPwmConfig;

  if (parsedAny) {
    Serial.printf("ELRS RX config parsed: product='%s' lua='%s' uid=%s wifi=%d uart=%d failsafe=%d model=%d\n",
                  elrsReceiverProductName,
                  elrsReceiverLuaName,
                  elrsReceiverUidText,
                  elrsReceiverWifiInterval,
                  elrsReceiverUartBaud,
                  elrsReceiverFailsafe,
                  elrsReceiverModelId);
  }
  return parsedAny;
}

bool fetchElrsReceiverConfig() {
  const char *paths[] = {
    "/config",
    "/config.json",
    "/options.json",
    "/api/config",
    "/api/options",
    "/api/settings",
    "/hardware.html",
    "/"
  };
  const int pathCount = (int)(sizeof(paths) / sizeof(paths[0]));
  bool anyResponse = false;
  bool sawCompressedPage = false;

  for (int i = 0; i < pathCount; i++) {
    String response;
    snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "Trying %s", paths[i]);
    fullRedraw = true;
    uiNeedsRedraw = true;

    Serial.printf("ELRS RX config: GET http://10.0.0.1%s\n", paths[i]);
    if (!requestElrsReceiverPath(paths[i], response)) {
      Serial.printf("ELRS RX config: no response from %s\n", paths[i]);
      continue;
    }

    anyResponse = true;
    strncpy(elrsReceiverConfigLastEndpoint, paths[i], sizeof(elrsReceiverConfigLastEndpoint) - 1);
    elrsReceiverConfigLastEndpoint[sizeof(elrsReceiverConfigLastEndpoint) - 1] = '\0';

    Serial.printf("ELRS RX config response from %s (%u bytes captured):\n",
                  paths[i],
                  (unsigned int)response.length());
    Serial.println(response);

    bool httpOk = response.indexOf("HTTP/1.1 200") >= 0 || response.indexOf("HTTP/1.0 200") >= 0;
    bool gzipEncoded = response.indexOf("Content-Encoding: gzip") >= 0 ||
                       response.indexOf("content-encoding: gzip") >= 0;
    bool looksJson = response.indexOf("application/json") >= 0 ||
                     response.indexOf("{") >= 0 ||
                     response.indexOf("[") >= 0;
    bool looksElrs = response.indexOf("ExpressLRS") >= 0 ||
                     response.indexOf("ELRS") >= 0 ||
                     response.indexOf("uid") >= 0 ||
                     response.indexOf("binding") >= 0 ||
                     response.indexOf("model") >= 0;

    if (httpOk && gzipEncoded) {
      sawCompressedPage = true;
      snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail),
               "Compressed page at %s", paths[i]);
      continue;
    }

    if (httpOk && strcmp(paths[i], "/config") == 0 && parseElrsReceiverConfigJson(response)) {
      snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail),
               "%s config loaded", elrsReceiverLuaName);
      logElrsReceiverWriteEndpoints();
      return true;
    }

    if (httpOk && (looksJson || looksElrs)) {
      parseElrsReceiverConfigJson(response);
      snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail),
               "Config response at %s", paths[i]);
      return true;
    }
  }

  if (sawCompressedPage) {
      snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail),
             "ELRS RX web UI found, gzip only");
    return true;
  } else if (anyResponse) {
    snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "ELRS RX answered, no config API");
  } else {
    snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "No HTTP response from 10.0.0.1");
  }
  return false;
}

void updateElrsReceiverConfig() {
  if (!elrsReceiverConfigActive) return;

  unsigned long now = millis();
  bool receiverConfigNeedsPollRedraw =
    (elrsReceiverConfigState == RX_CONFIG_CONNECTING ||
     elrsReceiverConfigState == RX_CONFIG_FETCHING);

  if (receiverConfigNeedsPollRedraw && now - elrsReceiverConfigLastUiTime >= 500UL) {
    elrsReceiverConfigLastUiTime = now;
    uiNeedsRedraw = true;
  }

  if (elrsReceiverConfigState == RX_CONFIG_CONNECTING) {
    wl_status_t status = WiFi.status();
    if (status == WL_CONNECTED) {
      IPAddress ip = WiFi.localIP();
      snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus),
               "Connected %u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
      snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "Fetching RX config...");
      Serial.printf("ELRS RX config: WiFi connected, IP %u.%u.%u.%u\n",
                    ip[0], ip[1], ip[2], ip[3]);
      elrsReceiverConfigState = RX_CONFIG_FETCHING;
      fullRedraw = true;
      uiNeedsRedraw = true;
    } else if (now - elrsReceiverConfigStartTime >= ELRS_RX_WIFI_CONNECT_TIMEOUT_MS) {
      snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "RX WiFi not found");
      snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail), "Put RX in WiFi mode and retry");
      Serial.printf("ELRS RX config: connect timed out, WiFi status=%d\n", (int)status);
      elrsReceiverConfigState = RX_CONFIG_FAILED;
      fullRedraw = true;
      uiNeedsRedraw = true;
    } else {
      unsigned long remaining = (ELRS_RX_WIFI_CONNECT_TIMEOUT_MS - (now - elrsReceiverConfigStartTime) + 999UL) / 1000UL;
      snprintf(elrsReceiverConfigDetail, sizeof(elrsReceiverConfigDetail),
               "Pausing ELRS; wait up to %lus", remaining);
    }
  } else if (elrsReceiverConfigState == RX_CONFIG_FETCHING) {
    if (fetchElrsReceiverConfig()) {
      snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "Receiver config found");
      elrsReceiverConfigState = RX_CONFIG_DONE;
    } else {
      snprintf(elrsReceiverConfigStatus, sizeof(elrsReceiverConfigStatus), "Config probe incomplete");
      elrsReceiverConfigState = RX_CONFIG_FAILED;
    }
    fullRedraw = true;
    uiNeedsRedraw = true;
  }
}

// ESP-NOW telemetry is deliberately volatile. Clearing it on bind/model changes
// prevents old receiver data from looking like a live link.
void resetEspNowTelemetry() {
  espNowLatency = 0;
  telemetryVoltage = 0.0f;
  lastEspNowTelemetryTime = 0;
  lastEspNowAliveTime = 0;
  lastEspNowPingTime = 0;
  espNowPingSequence = 0;
}

void clearActiveEspNowSessionTelemetry() {
  resetEspNowTelemetry();
  espNowProtocolActive = false;
  espNowProtocolStartTime = 0;
  lastEspNowSendStatus = ESP_NOW_SEND_FAIL;
}

bool isEspNowPacketFromActiveBoundReceiver(const esp_now_recv_info_t *info) {
  if (info == nullptr) return false;

  const uint8_t *boundReceiver = getBoundReceiverMac(activeModel);
  if (boundReceiver == nullptr) return false;

  return memcmp(info->src_addr, boundReceiver, ESP_NOW_ETH_ALEN) == 0;
}

void sendEspNowControlPacket(unsigned long now) {
  if (!initEspNowLink()) return;
  if (espNowBindingMode) return;

  float protocolChannels[CHANNEL_COUNT];
  buildProtocolOutputChannels(protocolChannels);

  EspNowControlPacket packet = {};
  packet.magic = ESPNOW_LINK_MAGIC;
  packet.version = ESPNOW_LINK_VERSION;
  packet.messageType = ESPNOW_MSG_CONTROL;
  packet.sequence = ++espNowSequence;
  packet.txMillis = now;
  packet.modelIndex = (uint8_t)activeModel;
  packet.driveType = (uint8_t)getModelDriveType(activeModel);
  packet.tankMode = (uint8_t)getModelTankMode(activeModel);
  packet.linkLatencyMs = (uint16_t)constrain(espNowLatency, 0, 65535);
  strncpy(packet.modelName, currentModelName.c_str(), sizeof(packet.modelName) - 1);
  packet.modelName[sizeof(packet.modelName) - 1] = '\0';

  if (batteryPresent) packet.flags |= ESPNOW_FLAG_BATTERY_PRESENT;
  if (batteryCharging) packet.flags |= ESPNOW_FLAG_BATTERY_CHARGING;
  if (getModelEspNowOutputMode(activeModel) == ESPNOW_OUTPUT_IBUS) {
    packet.flags |= ESPNOW_FLAG_OUTPUT_IBUS;
  }

  for (int i = 0; i < CHANNEL_COUNT; i++) {
    float constrainedValue = constrain(protocolChannels[i], -1.0f, 1.0f);
    packet.channels[i] = (int16_t)roundf(constrainedValue * 1000.0f);
  }

  if (transmitterBatteryVoltage > 0.05f) {
    packet.txBatteryMillivolts =
      (uint16_t)constrain((int)roundf(transmitterBatteryVoltage * 1000.0f), 0, 65535);
  }

  const uint8_t *destination = getBoundReceiverMac(activeModel);
  if (destination == nullptr) {
    return;
  }

  if (!ensureEspNowPeer(destination)) return;

  esp_err_t sendResult = esp_now_send(destination, (const uint8_t *)&packet, sizeof(packet));
  if (sendResult != ESP_OK) {
    if (ESPNOW_SEND_SERIAL_DEBUG) {
      Serial.printf("ESP-NOW send failed: %d\n", (int)sendResult);
    }
  }
}

void sendEspNowPing(unsigned long now) {
  if (!initEspNowLink()) return;
  if (espNowBindingMode) return;

  const uint8_t *destination = getBoundReceiverMac(activeModel);
  if (destination == nullptr) return;
  if (!ensureEspNowPeer(destination)) return;

  EspNowPingPacket packet = {};
  packet.magic = ESPNOW_LINK_MAGIC;
  packet.version = ESPNOW_LINK_VERSION;
  packet.messageType = ESPNOW_MSG_PING;
  packet.sequence = ++espNowPingSequence;
  packet.txMillis = now;

  esp_err_t sendResult = esp_now_send(destination, (const uint8_t *)&packet, sizeof(packet));
  if (sendResult != ESP_OK) {
    if (ESPNOW_SEND_SERIAL_DEBUG) {
      Serial.printf("ESP-NOW ping send failed: %d\n", (int)sendResult);
    }
  }
}

void sendEspNowBindCommit(const uint8_t *receiverMac) {
  if (!espNowBindingMode || receiverMac == nullptr) return;
  if (!initEspNowLink()) return;
  if (!ensureEspNowPeer(receiverMac)) return;

  EspNowBindPacket packet = {};
  packet.magic = ESPNOW_LINK_MAGIC;
  packet.version = ESPNOW_LINK_VERSION;
  packet.messageType = ESPNOW_MSG_BIND_COMMIT;
  packet.token = espNowBindingToken;
  if (getModelEspNowOutputMode(activeModel) == ESPNOW_OUTPUT_IBUS) {
    packet.flags |= ESPNOW_FLAG_OUTPUT_IBUS;
  }

  esp_err_t sendResult = esp_now_send(receiverMac, (const uint8_t *)&packet, sizeof(packet));
  if (sendResult != ESP_OK) {
    if (RADIO_PROTOCOL_SERIAL_DEBUG) {
      Serial.printf("ESP-NOW bind commit failed: %d\n", (int)sendResult);
    }
    return;
  }

  espNowLastBindCommitTime = millis();
}

void sendEspNowUnbindCommand() {
  const uint8_t *receiverMac = getBoundReceiverMac(activeModel);
  if (receiverMac == nullptr) return;

  uint8_t receiverCopy[ESP_NOW_ETH_ALEN];
  memcpy(receiverCopy, receiverMac, ESP_NOW_ETH_ALEN);

  if (initEspNowLink() && ensureEspNowPeer(receiverCopy)) {
    EspNowBindPacket packet = {};
    packet.magic = ESPNOW_LINK_MAGIC;
    packet.version = ESPNOW_LINK_VERSION;
    packet.messageType = ESPNOW_MSG_UNBIND;
    packet.token = ++espNowSequence;
    if (getModelEspNowOutputMode(activeModel) == ESPNOW_OUTPUT_IBUS) {
      packet.flags |= ESPNOW_FLAG_OUTPUT_IBUS;
    }

    for (int i = 0; i < ESPNOW_UNBIND_BURST_COUNT; i++) {
      esp_err_t sendResult = esp_now_send(receiverCopy, (const uint8_t *)&packet, sizeof(packet));
      if (sendResult != ESP_OK && RADIO_PROTOCOL_SERIAL_DEBUG) {
        Serial.printf("ESP-NOW unbind send failed: %d\n", (int)sendResult);
      }
      delay(2);
    }
  }

  clearBoundReceiver(activeModel);
  saveReceiverBindings();
  clearActiveEspNowSessionTelemetry();
  espNowUnbindTime = millis();
  espNowBindSuccessTime = 0;
  espNowBindingMode = false;
  memset(espNowPendingBindMac, 0, ESP_NOW_ETH_ALEN);
  topBarNeedsRedraw = true;
  fullRedraw = true;
  uiNeedsRedraw = true;
}

bool hasEspNowPendingBindMac() {
  for (int i = 0; i < ESP_NOW_ETH_ALEN; i++) {
    if (espNowPendingBindMac[i] != 0) {
      return true;
    }
  }
  return false;
}

void beginEspNowBinding() {
  espNowOutputModePromptVisible = false;
  espNowOutputModePromptDirty = false;
  setModelProtocol(activeModel, PROTOCOL_ESPNOW);
  stopElrsUart();
  saveModels();
  initEspNowLink();

  espNowBindingMode = true;
  espNowBindingStartTime = millis();
  espNowBindingToken = ++espNowSequence;
  espNowLastBindCommitTime = 0;
  memset(espNowPendingBindMac, 0, ESP_NOW_ETH_ALEN);
  uiNeedsRedraw = true;
  fullRedraw = true;
}

void showEspNowOutputModePrompt() {
  if (otaModeActive) return;
  espNowOutputModePromptVisible = true;
  espNowOutputModePromptDirty = true;
  dpadFocusVisible = false;
  uiNeedsRedraw = true;
}

void startEspNowBindingWithOutputMode(EspNowReceiverOutputMode mode) {
  setModelEspNowOutputMode(activeModel, mode);
  saveEspNowReceiverOutputModes();
  espNowOutputModePromptVisible = false;
  espNowOutputModePromptDirty = false;
  beginEspNowBinding();
}

void cancelEspNowBinding(bool clearPendingMac) {
  espNowBindingMode = false;
  espNowBindingStartTime = 0;
  espNowLastBindCommitTime = 0;
  if (clearPendingMac) {
    memset(espNowPendingBindMac, 0, ESP_NOW_ETH_ALEN);
  }
  uiNeedsRedraw = true;
}

void handleEspNowBindPacket(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (info == nullptr || data == nullptr || len != (int)sizeof(EspNowBindPacket)) return;

  EspNowBindPacket packet = {};
  memcpy(&packet, data, sizeof(packet));

  if (packet.magic != ESPNOW_LINK_MAGIC || packet.version != ESPNOW_LINK_VERSION) {
    return;
  }

  if (packet.messageType == ESPNOW_MSG_BIND_BEACON) {
    if (!espNowBindingMode) return;

    memcpy(espNowPendingBindMac, info->src_addr, ESP_NOW_ETH_ALEN);
    sendEspNowBindCommit(info->src_addr);
    return;
  }

  if (packet.messageType == ESPNOW_MSG_BIND_ACK) {
    if (!espNowBindingMode || packet.token != espNowBindingToken) return;

    setBoundReceiver(activeModel, info->src_addr);
    saveReceiverBindings();
    espNowBindSuccessTime = millis();
    cancelEspNowBinding(false);
    fullRedraw = true;
    uiNeedsRedraw = true;
  }
}

void formatReceiverMacShort(const uint8_t *macAddress, char *buffer, size_t bufferSize) {
  if (buffer == nullptr || bufferSize == 0) return;

  if (macAddress == nullptr) {
    snprintf(buffer, bufferSize, "No receiver bound");
    return;
  }

  snprintf(buffer, bufferSize, "Bound %02X:%02X:%02X",
           macAddress[3], macAddress[4], macAddress[5]);
}

void handleEspNowTelemetryPacket(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != (int)sizeof(EspNowTelemetryPacket)) return;
  if (!isEspNowPacketFromActiveBoundReceiver(info)) return;

  EspNowTelemetryPacket packet = {};
  memcpy(&packet, data, sizeof(packet));

  if (packet.magic != ESPNOW_LINK_MAGIC ||
      packet.version != ESPNOW_LINK_VERSION ||
      packet.messageType != ESPNOW_MSG_TELEMETRY) {
    return;
  }

  unsigned long now = millis();
  lastEspNowTelemetryTime = now;
  lastEspNowAliveTime = now;

  if (packet.receiverBatteryMillivolts > 0) {
    telemetryVoltage = packet.receiverBatteryMillivolts / 1000.0f;
  } else {
    telemetryVoltage = 0.0f;
  }
}

void handleEspNowPingAckPacket(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != (int)sizeof(EspNowPingAckPacket)) return;
  if (!isEspNowPacketFromActiveBoundReceiver(info)) return;

  EspNowPingAckPacket packet = {};
  memcpy(&packet, data, sizeof(packet));

  if (packet.magic != ESPNOW_LINK_MAGIC ||
      packet.version != ESPNOW_LINK_VERSION ||
      packet.messageType != ESPNOW_MSG_PING_ACK) {
    return;
  }

  unsigned long now = millis();
  lastEspNowAliveTime = now;

  if (packet.echoedTxMillis > 0) {
    int pingLatency = (int)(now - packet.echoedTxMillis);
    if (pingLatency >= 0 && pingLatency <= 60000) {
      espNowLatency = pingLatency;
    }
  }
}

void updateEspNowLink(unsigned long now) {
  if (otaModeActive) {
    if (espNowProtocolActive) {
      resetEspNowTelemetry();
      espNowProtocolActive = false;
      espNowProtocolStartTime = 0;
    }
    if (espNowBindingMode) {
      cancelEspNowBinding(true);
    }
    return;
  }

  if (getModelProtocol(activeModel) != PROTOCOL_ESPNOW) {
    if (espNowProtocolActive) {
      resetEspNowTelemetry();
      espNowProtocolActive = false;
      espNowProtocolStartTime = 0;
    }
    if (espNowBindingMode) {
      cancelEspNowBinding(true);
    }
    return;
  }

  if (!espNowBindingMode && !modelHasBoundReceiver(activeModel)) {
    if (espNowProtocolActive || lastEspNowAliveTime > 0 ||
        lastEspNowTelemetryTime > 0 || espNowLatency != 0 ||
        telemetryVoltage > 0.05f) {
      clearActiveEspNowSessionTelemetry();
      topBarNeedsRedraw = true;
      uiNeedsRedraw = true;
    }
    return;
  }

  if (!espNowProtocolActive) {
    espNowProtocolStartTime = now;
  }
  espNowProtocolActive = true;

  if (espNowBindingMode) {
    if (now - espNowBindingStartTime > ESPNOW_BIND_TIMEOUT_MS) {
      cancelEspNowBinding(true);
      fullRedraw = true;
      return;
    }

    if (hasEspNowPendingBindMac() &&
        (espNowLastBindCommitTime == 0 ||
         now - espNowLastBindCommitTime >= ESPNOW_BIND_COMMIT_RETRY_MS)) {
      sendEspNowBindCommit(espNowPendingBindMac);
    }
    return;
  }

  if (now - lastEspNowSendTime >= ESPNOW_SEND_INTERVAL_MS) {
    lastEspNowSendTime = now;
    sendEspNowControlPacket(now);
  }

  if (now - lastEspNowPingTime >= ESPNOW_PING_INTERVAL_MS) {
    lastEspNowPingTime = now;
    sendEspNowPing(now);
  }

}

// ELRS uses CRSF frames over UART. The helpers below build channel packets,
// parse module telemetry/config replies, and manage profile auto-detection.
uint8_t crsfCrc8Poly(const uint8_t *data, size_t length, uint8_t poly) {
  uint8_t crc = 0;
  for (size_t i = 0; i < length; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ poly);
      else crc <<= 1;
    }
  }
  return crc;
}

uint8_t crsfCrc8(const uint8_t *data, size_t length) {
  return crsfCrc8Poly(data, length, 0xD5);
}

int channelToCrsf(float value) {
  float constrained = constrain(value, -1.0f, 1.0f);
  int crsfValue = (int)roundf(992.0f + constrained * 819.0f);
  return constrain(crsfValue, 172, 1811);
}

int elrsLqToBars(uint8_t linkQuality) {
  if (linkQuality < 10) return 0;
  if (linkQuality < 30) return 1;
  if (linkQuality < 55) return 2;
  if (linkQuality < 80) return 3;
  return 4;
}

bool hasElrsTxOnlyActivity(unsigned long now) {
  return elrsProtocolActive &&
    lastElrsTxTime > 0 &&
    (now - lastElrsTxTime) <= ELRS_TX_ONLY_ACTIVE_WINDOW_MS;
}

bool hasElrsModuleFrames() {
  return
    (elrsDeviceInfoCount > 0) ||
    (elrsType14Count > 0) ||
    (elrsType1DCount > 0) ||
    (elrsType29Count > 0) ||
    (elrsType2BCount > 0) ||
    (elrsType2ECount > 0) ||
    (elrsType3ACount > 0);
}

uint32_t getElrsModuleResponseCount() {
  return elrsDeviceInfoCount +
    elrsType14Count +
    elrsType1CCount +
    elrsType1DCount +
    elrsType29Count +
    elrsType2BCount +
    elrsType2ECount +
    elrsType3ACount;
}

const char* elrsModuleProfileName(uint8_t profile) {
  if (profile == ELRS_MODULE_PROFILE_RANGER_NANO) return "Ranger Nano";
  if (profile == ELRS_MODULE_PROFILE_AUTO) return "Auto";
  return "Internal";
}

const char* driveTypeName(DriveType driveType) {
  switch (driveType) {
    case DRIVE_TANK: return "Tank";
    case DRIVE_CAR: return "Car";
    case DRIVE_OMNI: return "Omni";
    case DRIVE_X_DRONE: return "X-Drone";
    default: return "?";
  }
}

const char* protocolName(ProtocolType protocolType) {
  return (protocolType == PROTOCOL_ESPNOW) ? "ESP-NOW" : "ELRS";
}

bool shouldLogElrsSerialStatus() {
  return RADIO_PROTOCOL_SERIAL_DEBUG && getModelProtocol(activeModel) == PROTOCOL_ELRS;
}

uint8_t getElrsBindFallbackFieldIndex() {
  return (elrsActiveModuleProfile == ELRS_MODULE_PROFILE_RANGER_NANO) ? 29 : ELRS_BIND_FALLBACK_FIELD_INDEX;
}

bool containsIgnoreCase(const char *haystack, const char *needle) {
  if (haystack == nullptr || needle == nullptr) return false;
  size_t needleLen = strlen(needle);
  if (needleLen == 0) return true;
  size_t haystackLen = strlen(haystack);
  if (haystackLen < needleLen) return false;

  for (size_t i = 0; i <= haystackLen - needleLen; i++) {
    bool match = true;
    for (size_t j = 0; j < needleLen; j++) {
      char a = (char)tolower((unsigned char)haystack[i + j]);
      char b = (char)tolower((unsigned char)needle[j]);
      if (a != b) {
        match = false;
        break;
      }
    }
    if (match) return true;
  }
  return false;
}

void IRAM_ATTR onElrsRxEdge() {
  elrsRxEdgeCount++;
}

void elrsSendFrame(const uint8_t *frame, size_t len) {
  if (frame == nullptr || len == 0) return;
  if (!elrsInitialized) return;
#if ELRS_HALF_DUPLEX_MODE
  if (elrsRuntimeHalfDuplex) {
    pinMode(elrsActiveTxPin, OUTPUT);
  }
#endif
  elrsSerial.write(frame, len);
#if ELRS_HALF_DUPLEX_MODE
  // Ensure the outgoing frame has fully shifted out, then release the line
  // so the module can reply in the remaining slot.
  if (elrsRuntimeHalfDuplex) {
    elrsSerial.flush();
    pinMode(elrsActiveTxPin, INPUT_PULLUP);
  }
#endif
}

void sendElrsChannelFrame() {
  float protocolChannels[CHANNEL_COUNT];
  buildProtocolOutputChannels(protocolChannels);

#if DRIVE_OUTPUT_SERIAL_DEBUG
  static unsigned long lastDriveOutputPrintTime = 0;
  static float lastPrintedProtocolChannels[CHANNEL_COUNT] = {99.0f, 99.0f, 99.0f, 99.0f, 99.0f, 99.0f};
  unsigned long now = millis();
  bool outputChanged = false;
  for (int i = 0; i < CHANNEL_COUNT; i++) {
    if (fabs(protocolChannels[i] - lastPrintedProtocolChannels[i]) > 0.08f) {
      outputChanged = true;
      break;
    }
  }
  if (outputChanged && now - lastDriveOutputPrintTime >= 250UL) {
    uint8_t leftChannel = getModelTankLeftEscChannel(activeModel);
    uint8_t rightChannel = getModelTankRightEscChannel(activeModel);
    if (leftChannel >= CHANNEL_COUNT || rightChannel >= CHANNEL_COUNT || leftChannel == rightChannel) {
      leftChannel = 1;
      rightChannel = 0;
    }
    Serial.printf("DRIVE TX drive=%u tank=%u left=CH%u right=CH%u rev=%u%u%u%u%u%u "
                  "src(CH1..6)=%.2f,%.2f,%.2f,%.2f,%.2f,%.2f out(CH1..6)=%.2f,%.2f,%.2f,%.2f,%.2f,%.2f\n",
                  (unsigned int)getModelDriveType(activeModel),
                  (unsigned int)getModelTankMode(activeModel),
                  (unsigned int)(leftChannel + 1),
                  (unsigned int)(rightChannel + 1),
                  models[activeModel].reverse[0] ? 1 : 0,
                  models[activeModel].reverse[1] ? 1 : 0,
                  models[activeModel].reverse[2] ? 1 : 0,
                  models[activeModel].reverse[3] ? 1 : 0,
                  models[activeModel].reverse[4] ? 1 : 0,
                  models[activeModel].reverse[5] ? 1 : 0,
                  driveSourceChannels[0],
                  driveSourceChannels[1],
                  driveSourceChannels[2],
                  driveSourceChannels[3],
                  driveSourceChannels[4],
                  driveSourceChannels[5],
                  protocolChannels[0],
                  protocolChannels[1],
                  protocolChannels[2],
                  protocolChannels[3],
                  protocolChannels[4],
                  protocolChannels[5]);
    for (int i = 0; i < CHANNEL_COUNT; i++) {
      lastPrintedProtocolChannels[i] = protocolChannels[i];
    }
    lastDriveOutputPrintTime = now;
  }
#endif

  uint16_t channels[16];
  for (int i = 0; i < 16; i++) channels[i] = 992;

  for (int i = 0; i < CHANNEL_COUNT && i < 16; i++) {
    channels[i] = channelToCrsf(protocolChannels[i]);
  }

  uint8_t payload[22] = {0};
  uint16_t bitPos = 0;
  for (int ch = 0; ch < 16; ch++) {
    uint16_t value = channels[ch] & 0x07FF;
    for (uint8_t bit = 0; bit < 11; bit++) {
      if (value & (1U << bit)) {
        uint16_t pos = bitPos + bit;
        payload[pos >> 3] |= (1U << (pos & 0x07));
      }
    }
    bitPos += 11;
  }

  // CRSF packet format: [dest/sync][len][type][payload...][crc]
  uint8_t frame[26];
  // The Pocket drives host->module RC traffic with destination address 0xEE.
  frame[0] = ELRS_TX_MODULE_ADDR;
  frame[1] = 0x18;  // len = type(1) + payload(22) + crc(1)
  frame[2] = 0x16;  // RC_CHANNELS_PACKED
  memcpy(&frame[3], payload, sizeof(payload));
  frame[25] = crsfCrc8(&frame[2], 1 + sizeof(payload));

  elrsSendFrame(frame, sizeof(frame));
}

void sendElrsDevicePing() {
  // Broadcast and direct destination, with both common handset/Lua source IDs.
  const uint8_t destinations[] = {0xEE, 0x00};
  const uint8_t sources[] = {0xEA, 0xEF};
  for (size_t d = 0; d < sizeof(destinations); d++) {
    for (size_t s = 0; s < sizeof(sources); s++) {
      uint8_t frame[6];
      frame[0] = ELRS_TX_SYNC_PRIMARY;
      frame[1] = 0x04;
      frame[2] = 0x28;
      frame[3] = destinations[d];
      frame[4] = sources[s];
      frame[5] = crsfCrc8(&frame[2], 3);
      elrsSendFrame(frame, sizeof(frame));
      if (ELRS_TX_SYNC_PRIMARY != 0xC8) {
        frame[0] = 0xC8;
        elrsSendFrame(frame, sizeof(frame));
      }
    }
  }
}

void sendElrsBindCommand() {
  uint8_t bindFieldIndex = elrsBindFieldKnown ? elrsBindFieldIndex : getElrsBindFallbackFieldIndex();
  if (!elrsBindFieldKnown && strcmp(elrsDeviceName, "RM Pocket") == 0 && elrsParameterCount >= 28) {
    bindFieldIndex = 28;
  }
  if (RADIO_PROTOCOL_SERIAL_DEBUG) {
    DriveType driveType = getModelDriveType(activeModel);
    ProtocolType protocolType = getModelProtocol(activeModel);
    Serial.printf("ELRS bind request model=%d '%s' drive=%s protocol=%s profile=%s module=%d link=%d lq=%u field=%u known=%d fallback=%u name=%s pCnt=%u\n",
                  activeModel + 1,
                  (models[activeModel].name[0] != '\0') ? models[activeModel].name : "-",
                  driveTypeName(driveType),
                  protocolName(protocolType),
                  elrsModuleProfileName(elrsActiveModuleProfile),
                  (int)elrsModulePresent,
                  (int)elrsLinkActive,
                  (unsigned int)elrsUplinkLq,
                  (unsigned int)bindFieldIndex,
                  (int)elrsBindFieldKnown,
                  (unsigned int)getElrsBindFallbackFieldIndex(),
                  (elrsDeviceName[0] != '\0') ? elrsDeviceName : "-",
                  (unsigned int)elrsParameterCount);
    Serial.printf("ELRS TX bind start field=%u known=%d name=%s pCnt=%u\n",
                  (unsigned int)bindFieldIndex,
                  (int)elrsBindFieldKnown,
                  (elrsDeviceName[0] != '\0') ? elrsDeviceName : "-",
                  (unsigned int)elrsParameterCount);
  }
  // Mirror the real Pocket bind sequence:
  // 1) housekeeping write to field 0, value 0
  // 2) bind click on the discovered bind field
  // 3) keep nudging the same command while the receiver is in bind mode.
  elrsBindCommandSentTime = millis();
  elrsBindAwaitingResult = true;
  elrsBindAwaitUntil = elrsBindCommandSentTime + ELRS_BIND_COMMAND_WINDOW_MS;
  elrsBindBurstUntil = elrsBindAwaitUntil;
  lastElrsBindBurstSendTime = elrsBindCommandSentTime;
  sendElrsLuaCommandWrite(0x00, 0x00);
  sendElrsLuaBindWrite(bindFieldIndex);
}

void sendElrsBindCommandTo(uint8_t destination) {
  // Send both common command encodings for compatibility:
  // 1) with command CRC8-BA (newer spec-compliant parsers)
  // 2) without command CRC8-BA (legacy/transitional parsers)
  const uint8_t sourceCandidates[] = {0xEA, 0xEF};
  for (size_t s = 0; s < sizeof(sourceCandidates); s++) {
    uint8_t src = sourceCandidates[s];

    // Variant A: includes command CRC8-BA.
    uint8_t frameWithCmdCrc[9];
    frameWithCmdCrc[0] = ELRS_TX_SYNC_PRIMARY;
    frameWithCmdCrc[1] = 0x07;
    frameWithCmdCrc[2] = 0x32;
    frameWithCmdCrc[3] = destination;
    frameWithCmdCrc[4] = src;
    frameWithCmdCrc[5] = 0x10;
    frameWithCmdCrc[6] = 0x01;
    frameWithCmdCrc[7] = crsfCrc8Poly(&frameWithCmdCrc[2], 5, 0xBA);
    frameWithCmdCrc[8] = crsfCrc8(&frameWithCmdCrc[2], 6);
    elrsSendFrame(frameWithCmdCrc, sizeof(frameWithCmdCrc));
    if (ELRS_TX_SYNC_PRIMARY != 0xC8) {
      frameWithCmdCrc[0] = 0xC8;
      elrsSendFrame(frameWithCmdCrc, sizeof(frameWithCmdCrc));
    }

    // Variant B: no command CRC8-BA.
    uint8_t frameNoCmdCrc[8];
    frameNoCmdCrc[0] = ELRS_TX_SYNC_PRIMARY;
    frameNoCmdCrc[1] = 0x06;
    frameNoCmdCrc[2] = 0x32;
    frameNoCmdCrc[3] = destination;
    frameNoCmdCrc[4] = src;
    frameNoCmdCrc[5] = 0x10;
    frameNoCmdCrc[6] = 0x01;
    frameNoCmdCrc[7] = crsfCrc8(&frameNoCmdCrc[2], 5);
    elrsSendFrame(frameNoCmdCrc, sizeof(frameNoCmdCrc));
    if (ELRS_TX_SYNC_PRIMARY != 0xC8) {
      frameNoCmdCrc[0] = 0xC8;
      elrsSendFrame(frameNoCmdCrc, sizeof(frameNoCmdCrc));
    }
  }
}

void sendElrsLuaCommandWrite(uint8_t fieldIndex, uint8_t step) {
  uint8_t frame[8];
  frame[0] = ELRS_TX_MODULE_ADDR;
  frame[1] = 0x06;
  frame[2] = 0x2D;
  frame[3] = 0xEE;
  frame[4] = 0xEF;
  frame[5] = fieldIndex;
  frame[6] = step;
  frame[7] = crsfCrc8(&frame[2], 5);
  if (ELRS_VERBOSE_SERIAL_DEBUG ||
      (ELRS_BIND_SERIAL_DEBUG &&
       elrsBindAwaitingResult &&
       (fieldIndex == 0 || fieldIndex == elrsBindFieldIndex || fieldIndex == getElrsBindFallbackFieldIndex()))) {
    Serial.printf("ELRS TX 2D field=%u step=%u frame=", (unsigned int)fieldIndex, (unsigned int)step);
    for (size_t i = 0; i < sizeof(frame); i++) {
      Serial.printf("%02X", frame[i]);
      if (i + 1 != sizeof(frame)) Serial.print(' ');
    }
    Serial.println();
  }
  elrsSendFrame(frame, sizeof(frame));
}

void sendElrsLuaBindWrite(uint8_t fieldIndex) {
  // Step 1 = CLICK, which is what the ELRS Lua script sends when the user
  // presses the bind command.
  sendElrsLuaCommandWrite(fieldIndex, 0x01);
}

void sendElrsLuaBindExecWrite(uint8_t fieldIndex) {
  // The Pocket keeps nudging the bind command while the module reports EXEC.
  sendElrsLuaCommandWrite(fieldIndex, 0x06);
}

static uint16_t clampElrsTxPowerMw(uint16_t mw) {
  if (mw > ELRS_TX_POWER_MAX_MW) return ELRS_TX_POWER_MAX_MW;
  return mw;
}

static uint8_t parseElrsPowerOptions(const char *options, uint16_t *valuesOut, uint8_t maxValues) {
  if (options == nullptr || valuesOut == nullptr || maxValues == 0) return 0;
  uint8_t count = 0;
  const char *p = options;
  while (*p != '\0' && count < maxValues) {
    while (*p == ';' || *p == ' ') p++;
    if (*p == '\0') break;
    if (*p < '0' || *p > '9') break;
    int parsed = 0;
    while (*p >= '0' && *p <= '9') {
      parsed = (parsed * 10) + (*p - '0');
      p++;
    }
    valuesOut[count++] = (uint16_t)max(0, parsed);
    while (*p != '\0' && *p != ';') p++;
  }
  return count;
}

static void updateElrsTxPowerFromRaw(uint8_t rawValue) {
  if (elrsTxPowerFieldIsSelection && elrsTxPowerOptionCount > 0) {
    int idx = rawValue;
    if (elrsTxPowerSelectionOneBased) idx -= 1;
    if (idx < 0) idx = 0;
    if (idx >= elrsTxPowerOptionCount) idx = elrsTxPowerOptionCount - 1;
    elrsTxPowerMw = clampElrsTxPowerMw(elrsTxPowerOptionsMw[idx]);
  } else {
    elrsTxPowerMw = clampElrsTxPowerMw(rawValue);
  }
  if (elrsTxPowerUserOverrideActive && activeModel >= 0 && activeModel < MAX_MODELS) {
    modelElrsTxPowerMw[activeModel] = elrsTxPowerMw;
  }
}

void sendElrsTxPowerWrite(uint16_t powerMw, bool persistToModel) {
  uint16_t clampedMw = clampElrsTxPowerMw(powerMw);
  if (persistToModel) {
    elrsTxPowerUserOverrideActive = true;
    elrsLowPowerFallbackActive = false;
    elrsNoLinkPowerTimerStart = millis();
  }
  elrsTxPowerPendingMw = clampedMw;
  elrsTxPowerWritePersistPending = persistToModel;
  if (!elrsTxPowerFieldKnown) {
    elrsTxPowerWritePending = true;
    return;
  }

  uint8_t rawValue = (uint8_t)clampedMw;
  if (elrsTxPowerFieldIsSelection && elrsTxPowerOptionCount > 0) {
    int bestIndex = 0;
    int bestDiff = 100000;
    for (int i = 0; i < elrsTxPowerOptionCount; i++) {
      int diff = abs((int)clampedMw - (int)elrsTxPowerOptionsMw[i]);
      if (diff < bestDiff) {
        bestDiff = diff;
        bestIndex = i;
      }
    }
    rawValue = (uint8_t)(elrsTxPowerSelectionOneBased ? (bestIndex + 1) : bestIndex);
    clampedMw = clampElrsTxPowerMw(elrsTxPowerOptionsMw[bestIndex]);
  }

  sendElrsLuaCommandWrite(elrsTxPowerFieldIndex, rawValue);
  elrsTxPowerMw = clampedMw;
  elrsTxPowerWritePending = false;
  if (persistToModel && activeModel >= 0 && activeModel < MAX_MODELS) {
    modelElrsTxPowerMw[activeModel] = clampedMw;
  }
}

void updateElrsLowPowerFallback(unsigned long now) {
  if (getModelProtocol(activeModel) != PROTOCOL_ELRS) {
    elrsNoLinkPowerTimerStart = 0;
    elrsLowPowerFallbackActive = false;
    return;
  }

  if (elrsTxPowerUserOverrideActive) {
    elrsNoLinkPowerTimerStart = now;
    return;
  }

  uint16_t targetPower = elrsLinkActive
    ? (uint16_t)ELRS_LINK_DEFAULT_POWER_MW
    : (uint16_t)ELRS_LOW_POWER_FALLBACK_MW;
  bool targetLowPower = !elrsLinkActive;
  bool alreadyRequested = elrsTxPowerWritePending &&
                          elrsTxPowerPendingMw == targetPower;

  if (elrsLowPowerFallbackActive != targetLowPower ||
      (!alreadyRequested && elrsTxPowerMw != targetPower)) {
    elrsLowPowerFallbackActive = targetLowPower;
    elrsNoLinkPowerTimerStart = now;
    sendElrsTxPowerWrite(targetPower, false);
  }
}

void sendElrsParameterRead(uint8_t fieldIndex, uint8_t chunkIndex) {
  uint8_t frame[8];
  frame[0] = ELRS_TX_MODULE_ADDR;
  frame[1] = 0x06;
  frame[2] = 0x2C;
  frame[3] = 0xEE;
  frame[4] = 0xEF;
  frame[5] = fieldIndex;
  frame[6] = chunkIndex;
  frame[7] = crsfCrc8(&frame[2], 5);
  elrsSendFrame(frame, sizeof(frame));
}

void updateElrsParameterDiscovery(unsigned long now) {
  if (elrsBindFieldKnown && elrsTxPowerFieldKnown) return;
  if (now - lastElrsParamReadTime < ELRS_PARAM_READ_INTERVAL_MS) return;
  lastElrsParamReadTime = now;

  uint8_t maxIndex = (elrsParameterCount > 0) ? elrsParameterCount : ELRS_PARAM_SCAN_FALLBACK_MAX;
  if (maxIndex == 0) return;

  if (elrsParamScanIndex == 0 || elrsParamScanIndex > maxIndex) {
    elrsParamScanIndex = 1;
  }

  sendElrsParameterRead(elrsParamScanIndex, 0);
  elrsParamScanIndex++;
}

void parseElrsDeviceInfoFrame(const uint8_t *frame, uint8_t expected) {
  if (frame == nullptr || expected < 12) return;

  int payloadStart = 5;
  int payloadEnd = expected - 1; // before CRC
  int i = payloadStart;
  int out = 0;
  while (i < payloadEnd && out < (int)sizeof(elrsDeviceName) - 1) {
    char c = (char)frame[i++];
    if (c == '\0') break;
    if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) {
      out = 0;
      break;
    }
    elrsDeviceName[out++] = c;
  }
  elrsDeviceName[out] = '\0';
  elrsDeviceInfoCount++;
  lastElrsDeviceInfoTime = millis();

  // Device info trailing fields:
  // serial(4), hw(4), sw(4), param_count(1), param_version(1)
  if (i + 14 <= payloadEnd) {
    i += 12; // skip serial/hw/sw
    elrsParameterCount = frame[i++];
    elrsParameterVersion = frame[i++];
    if (elrsParameterCount > 0 && elrsParamScanIndex == 0) {
      elrsParamScanIndex = 1;
    }
  }
}

void parseElrsParameterEntryFrame(const uint8_t *frame, uint8_t expected) {
  if (frame == nullptr || expected < 12) return;
  if (frame[3] != 0xEA || frame[4] != 0xEE) return; // handset <- tx module

  uint8_t fieldIndex = frame[5];
  uint8_t chunksRemaining = frame[6];
  (void)chunksRemaining;
  uint8_t typeHidden = frame[8];
  uint8_t fieldType = (uint8_t)(typeHidden & 0x7F);

  int labelStart = 9;
  int payloadEnd = expected - 1; // before CRC
  if (labelStart >= payloadEnd) return;

  char label[32];
  int out = 0;
  for (int i = labelStart; i < payloadEnd && out < (int)sizeof(label) - 1; i++) {
    char c = (char)frame[i];
    if (c == '\0') break;
    if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) break;
    label[out++] = c;
  }
  label[out] = '\0';
  if (out == 0) return;

  int valueStart = labelStart + out + 1;

  bool isPowerField = containsIgnoreCase(label, "power");
  if (isPowerField) {
    elrsTxPowerFieldIndex = fieldIndex;
    elrsTxPowerFieldKnown = true;

    if (fieldType == 0x09) { // TEXT_SELECTION
      int optionsStart = valueStart;
      char options[64];
      int optionsOut = 0;
      int i = optionsStart;
      for (; i < payloadEnd && optionsOut < (int)sizeof(options) - 1; i++) {
        char c = (char)frame[i];
        if (c == '\0') break;
        if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) break;
        options[optionsOut++] = c;
      }
      options[optionsOut] = '\0';

      uint8_t parsedCount = parseElrsPowerOptions(options, elrsTxPowerOptionsMw, (uint8_t)(sizeof(elrsTxPowerOptionsMw) / sizeof(elrsTxPowerOptionsMw[0])));
      if (parsedCount > 0) {
        elrsTxPowerFieldIsSelection = true;
        elrsTxPowerOptionCount = parsedCount;
      } else {
        elrsTxPowerFieldIsSelection = false;
        elrsTxPowerOptionCount = 0;
      }

      // TEXT_SELECTION payload after options string:
      // value, min, max, default, unit...
      int metaStart = i + 1; // first byte after null terminator
      if (metaStart + 3 < payloadEnd) {
        uint8_t currentSelection = frame[metaStart];
        uint8_t minSelection = frame[metaStart + 1];
        uint8_t maxSelection = frame[metaStart + 2];
        // Determine 0-based vs 1-based selection encoding from min/max.
        elrsTxPowerSelectionOneBased = (minSelection == 1 && maxSelection >= 1);
        updateElrsTxPowerFromRaw(currentSelection);
      }
    } else if (fieldType == 0x08) { // FLOAT
      // FLOAT payload starts with int32 value (little-endian).
      if (valueStart + 3 < payloadEnd) {
        int32_t currentValue =
          (int32_t)((uint32_t)frame[valueStart] |
                    ((uint32_t)frame[valueStart + 1] << 8) |
                    ((uint32_t)frame[valueStart + 2] << 16) |
                    ((uint32_t)frame[valueStart + 3] << 24));
        elrsTxPowerFieldIsSelection = false;
        elrsTxPowerOptionCount = 0;
        if (currentValue < 0) currentValue = 0;
        elrsTxPowerMw = clampElrsTxPowerMw((uint16_t)currentValue);
      }
    } else {
      elrsTxPowerFieldIsSelection = false;
      elrsTxPowerOptionCount = 0;
    }

    if (elrsTxPowerWritePending) {
      sendElrsTxPowerWrite(elrsTxPowerPendingMw, elrsTxPowerWritePersistPending);
    }
  }

  if (fieldType != 0x0D) return; // command-type parameters only

  bool isBindField = containsIgnoreCase(label, "bind");
  if (isBindField) {
    elrsBindFieldIndex = fieldIndex;
    elrsBindFieldKnown = true;
  }

  if (!isBindField || valueStart + 1 >= payloadEnd) return;

  elrsBindCommandStep = frame[valueStart];
  elrsBindCommandTimeout = frame[valueStart + 1];

  int infoStart = valueStart + 2;
  int infoOut = 0;
  for (int i = infoStart; i < payloadEnd && infoOut < (int)sizeof(elrsBindCommandInfo) - 1; i++) {
    char c = (char)frame[i];
    if (c == '\0') break;
    if ((uint8_t)c < 0x20 || (uint8_t)c > 0x7E) break;
    elrsBindCommandInfo[infoOut++] = c;
  }
  elrsBindCommandInfo[infoOut] = '\0';
}

void parseElrsParameterValueFrame(const uint8_t *frame, uint8_t expected) {
  if (frame == nullptr || expected < 9) return;
  if (frame[3] != 0xEA || frame[4] != 0xEE) return; // handset <- tx module

  uint8_t fieldIndex = frame[5];
  if (!elrsTxPowerFieldKnown || fieldIndex != elrsTxPowerFieldIndex) return;

  // PARAM_VALUE payload is parameter_number + data payload.
  // For TX power TEXT_SELECTION, data payload is 1 byte selection index.
  if (elrsTxPowerFieldIsSelection) {
    if (expected >= 8) {
      uint8_t rawSelection = frame[6];
      updateElrsTxPowerFromRaw(rawSelection);
    }
    return;
  }

  // For numeric value frames, accept at least one data byte and clamp.
  if (expected >= 8) {
    elrsTxPowerMw = clampElrsTxPowerMw((uint16_t)frame[6]);
  }
}

void readElrsSniffSerial(HardwareSerial &port, ElrsSniffStats &stats) {
  static uint8_t frameBuffers[2][64];
  static uint8_t frameIndices[2] = {0, 0};
  static uint8_t frameExpected[2] = {0, 0};
  static uint8_t lastRcFrames[2][32] = {};
  static uint8_t lastRcFrameLens[2] = {0, 0};
  static unsigned long lastRcFrameLogTimes[2] = {0, 0};

  int slot = (&port == &elrsSerial) ? 0 : 1;
  const char *label = (slot == 0) ? "SNIFF M->H" : "SNIFF H->M";
  uint8_t *frame = frameBuffers[slot];
  uint8_t &index = frameIndices[slot];
  uint8_t &expected = frameExpected[slot];

  while (port.available() > 0) {
    uint8_t byteIn = (uint8_t)port.read();
    stats.byteCount++;
    stats.recentBytes[stats.recentByteIndex++ & 0x0F] = byteIn;

    if (index == 0) {
      if (byteIn != 0xC8 && byteIn != 0xEE && byteIn != 0xEA && byteIn != 0xEC) {
        continue;
      }
      frame[index++] = byteIn;
      continue;
    }

    if (index == 1) {
      if (byteIn < 2 || byteIn > 62) {
        index = 0;
        expected = 0;
        continue;
      }
      frame[index++] = byteIn;
      expected = byteIn + 2;
      continue;
    }

    frame[index++] = byteIn;
    if (expected == 0 || index < expected) continue;

    uint8_t len = frame[1];
    uint8_t type = frame[2];
    uint8_t crc = frame[expected - 1];
    uint8_t calculated = crsfCrc8(&frame[2], len - 1);
    if (crc == calculated) {
      stats.frameCount++;
      stats.lastType = type;

      if (type == 0x16) stats.type16Count++;
      else if (type == 0x2C) stats.type2CCount++;
      else if (type == 0x2D) stats.type2DCount++;
      else if (type == 0x28) stats.type28Count++;
      else if (type == 0x29) stats.type29Count++;
      else if (type == 0x2B) stats.type2BCount++;
      else if (type == 0x2E) stats.type2ECount++;
      else if (type == 0x32) stats.type32Count++;
      else if (type == 0x3A) stats.type3ACount++;
      else stats.otherCount++;

#if ELRS_SNIFF_LOG_WRITES_IMMEDIATE
      if (type == 0x2C || type == 0x2D) {
        logElrsSniffImmediateFrame(label, frame, expected);
      }
#endif

#if ELRS_SNIFF_LOG_RC_SAMPLES
      if (type == 0x16) {
        unsigned long now = millis();
        bool changed = (lastRcFrameLens[slot] != expected) ||
                       (memcmp(lastRcFrames[slot], frame, min((int)expected, 32)) != 0);
        if (changed && now - lastRcFrameLogTimes[slot] >= ELRS_SNIFF_RC_SAMPLE_MS) {
          logElrsSniffImmediateFrame(label, frame, expected);
          memcpy(lastRcFrames[slot], frame, min((int)expected, 32));
          lastRcFrameLens[slot] = expected;
          lastRcFrameLogTimes[slot] = now;
        }
      }
#endif

      if (type == 0x28 || type == 0x29 || type == 0x2B || type == 0x2C || type == 0x2D || type == 0x2E || type == 0x32) {
        memcpy(stats.frame, frame, expected);
        stats.frameLen = expected;
        stats.frameUpdated = true;
      }
    }

    index = 0;
    expected = 0;
  }
}

void logElrsSniffImmediateFrame(const char *label, const uint8_t *frame, uint8_t frameLen) {
  if (frame == nullptr || frameLen == 0) return;
  uint8_t type = (frameLen >= 3) ? frame[2] : 0;
  Serial.printf("%s instant type=0x%02X(%s) frame=", label, type, elrsFrameTypeName(type));
  for (uint8_t i = 0; i < frameLen; i++) {
    Serial.printf("%02X", frame[i]);
    if (i + 1 != frameLen) Serial.print(' ');
  }
  Serial.println();
}

void logElrsSniffFrame(const char *label, ElrsSniffStats &stats) {
  if (!stats.frameUpdated || stats.frameLen == 0) return;

  Serial.printf("%s frame=", label);
  for (uint8_t i = 0; i < stats.frameLen; i++) {
    Serial.printf("%02X", stats.frame[i]);
    if (i + 1 != stats.frameLen) Serial.print(' ');
  }
  Serial.println();
  stats.frameUpdated = false;
}

void logElrsSniffSummary(const char *label, ElrsSniffStats &stats) {
  Serial.printf("%s bytes=%lu frames=%lu t16=%lu t28=%lu t29=%lu t2B=%lu t2C=%lu t2D=%lu t2E=%lu t32=%lu t3A=%lu other=%lu lastType=0x%02X(%s)\n",
                label,
                (unsigned long)stats.byteCount,
                (unsigned long)stats.frameCount,
                (unsigned long)stats.type16Count,
                (unsigned long)stats.type28Count,
                (unsigned long)stats.type29Count,
                (unsigned long)stats.type2BCount,
                (unsigned long)stats.type2CCount,
                (unsigned long)stats.type2DCount,
                (unsigned long)stats.type2ECount,
                (unsigned long)stats.type32Count,
                (unsigned long)stats.type3ACount,
                (unsigned long)stats.otherCount,
                (unsigned int)stats.lastType,
                elrsFrameTypeName(stats.lastType));
  if (stats.byteCount > 0 && stats.frameCount == 0) {
    Serial.printf("%s recent=", label);
    for (uint8_t i = 0; i < 16; i++) {
      uint8_t idx = (stats.recentByteIndex + i) & 0x0F;
      Serial.printf("%02X", stats.recentBytes[idx]);
      if (i != 15) Serial.print(' ');
    }
    Serial.println();
  }
  logElrsSniffFrame(label, stats);
}

const char* elrsFrameTypeName(uint8_t type) {
  switch (type) {
    case 0x28: return "PING_DEVICES";
    case 0x14: return "LINK_STATS";
    case 0x16: return "RC_CHANNELS";
    case 0x1C: return "RX_ID";
    case 0x1D: return "LINK_STATS_TX";
    case 0x29: return "DEVICE_INFO";
    case 0x2B: return "PARAM_ENTRY";
    case 0x2C: return "PARAM_READ";
    case 0x2D: return "PARAM_WRITE";
    case 0x2E: return "PARAM_VALUE";
    case 0x32: return "COMMAND";
    case 0x3A: return "RADIO_ID";
    default:   return "OTHER";
  }
}

const char* elrsCommandStepName(uint8_t step) {
  switch (step) {
    case 0: return "IDLE";
    case 1: return "CLICK";
    case 2: return "EXEC";
    case 3: return "ASK";
    case 4: return "CONFIRM";
    case 5: return "CANCEL";
    case 6: return "QUERY";
    default: return "UNKNOWN";
  }
}

void readElrsSerial(unsigned long now) {
  static uint8_t frame[64];
  static uint8_t index = 0;
  static uint8_t expected = 0;

  uint16_t bytesProcessed = 0;
  while (elrsSerial.available() > 0 && bytesProcessed < ELRS_RX_READ_BUDGET) {
    uint8_t byteIn = (uint8_t)elrsSerial.read();
    bytesProcessed++;
    elrsRxByteCount++;
    lastElrsSerialRxTime = now;
    elrsRecentBytes[elrsRecentByteIndex] = byteIn;
    elrsRecentByteIndex = (uint8_t)((elrsRecentByteIndex + 1U) & 0x0F);
    if (byteIn == 0xC8) elrsSyncC8Count++;
    else if (byteIn == 0xEE) elrsSyncEECount++;
    else if (byteIn == 0xEA) elrsSyncEACount++;
    else if (byteIn == 0xEC) elrsSyncECCount++;
    if (byteIn >= 0x20 && byteIn <= 0x7E) elrsPrintableByteCount++;

    if (index == 0) {
      // Common CRSF device/sync bytes for robust framing.
      if (byteIn != 0xC8 && byteIn != 0xEE && byteIn != 0xEA && byteIn != 0xEC) {
        continue;
      }
      frame[index++] = byteIn;
      continue;
    }

    if (index == 1) {
      if (byteIn < 2 || byteIn > 62) {
        index = 0;
        expected = 0;
        continue;
      }
      frame[index++] = byteIn;
      expected = byteIn + 2;
      continue;
    }

    frame[index++] = byteIn;
    if (expected == 0 || index < expected) continue;

    uint8_t len = frame[1];
    uint8_t type = frame[2];
    uint8_t crc = frame[expected - 1];
    uint8_t calculated = crsfCrc8(&frame[2], len - 1);
    if (crc == calculated) {
      elrsRxFrameCount++;
      elrsLastFrameType = type;
      if (type == 0x16) {
        elrsType16Count++;
      } else if (type == 0x14) {
        elrsType14Count++;
      } else if (type == 0x1C) {
        elrsType1CCount++;
      } else if (type == 0x1D) {
        elrsType1DCount++;
      } else if (type == 0x29) {
        elrsType29Count++;
      } else if (type == 0x2E) {
        elrsType2ECount++;
        uint8_t copyLen = expected;
        if (copyLen > sizeof(elrsLast2EFrame)) copyLen = sizeof(elrsLast2EFrame);
        memcpy((void*)elrsLast2EFrame, frame, copyLen);
        elrsLast2EFrameLen = copyLen;
      } else if (type == 0x3A) {
        elrsType3ACount++;
      } else if (type == 0x2B) {
        elrsType2BCount++;
        // Parameter settings entry (response to 0x2C reads)
      } else {
        elrsTypeOtherCount++;
        elrsLastOtherFrameType = type;
      }

      if ((type == 0x14 || type == 0x1D) && len >= 12) {
        // Link statistics frame.
        elrsUplinkRssi1 = frame[3];
        elrsUplinkRssi2 = frame[4];
        elrsUplinkLq = frame[5];
        elrsUplinkSnr = (int8_t)frame[6];
        lastElrsLinkStatsTime = now;
      } else if (type == 0x29) {
        parseElrsDeviceInfoFrame(frame, expected);
      } else if (type == 0x2B) {
        parseElrsParameterEntryFrame(frame, expected);
      } else if (type == 0x2E) {
        parseElrsParameterValueFrame(frame, expected);
      }
    }

    index = 0;
    expected = 0;
  }
}

void applyElrsModuleProfile(uint8_t profile, bool resetDiscovery) {
  uint8_t previousRxPin = elrsActiveRxPin;
  elrsActiveModuleProfile = profile;
  if (profile == ELRS_MODULE_PROFILE_RANGER_NANO) {
    elrsRuntimeHalfDuplex = true;
    elrsActiveTxPin = ELRS_UART_DATA_PIN;
    elrsActiveRxPin = ELRS_UART_DATA_PIN;
    elrsInvertModeIndex = 2; // Ranger Nano module bay traffic is inverted on the shared data line.
    elrsPinPairIndex = 0;
  } else {
    elrsRuntimeHalfDuplex = false;
    elrsActiveTxPin = 43;
    elrsActiveRxPin = 44;
    elrsInvertModeIndex = 0;
    elrsPinPairIndex = 1;
  }

  if (elrsRxInterruptAttached) {
    detachInterrupt(digitalPinToInterrupt(previousRxPin));
    elrsRxInterruptAttached = false;
  }
  pinMode(elrsActiveRxPin, INPUT_PULLUP);
#if ELRS_FORCE_INVERT_MODE >= 0
  elrsInvertModeIndex = ELRS_FORCE_INVERT_MODE % elrsInvertModeCount;
#endif

  elrsRxInvert = elrsInvertModes[elrsInvertModeIndex][0];
  elrsTxInvert = elrsInvertModes[elrsInvertModeIndex][1];

  if (!resetDiscovery) return;
  elrsBindFieldKnown = false;
  elrsBindFieldIndex = 0;
  elrsTxPowerFieldKnown = false;
  elrsTxPowerFieldIndex = 0;
  elrsTxPowerFieldIsSelection = false;
  elrsTxPowerSelectionOneBased = false;
  elrsTxPowerOptionCount = 0;
  elrsParameterCount = 0;
  elrsParameterVersion = 0;
  elrsParamScanIndex = 1;
  lastElrsParamReadTime = 0;
  lastElrsSerialRxTime = 0;
  lastElrsLinkStatsTime = 0;
  lastElrsDeviceInfoTime = 0;
  lastElrsTxTime = 0;
  lastElrsDevicePingTime = 0;
  elrsModulePresent = false;
  elrsLinkActive = false;
  elrsRxByteCount = 0;
  elrsRxFrameCount = 0;
  elrsDeviceInfoCount = 0;
  elrsType16Count = 0;
  elrsType14Count = 0;
  elrsType1CCount = 0;
  elrsType1DCount = 0;
  elrsType29Count = 0;
  elrsType2BCount = 0;
  elrsType2ECount = 0;
  elrsType3ACount = 0;
  elrsTypeOtherCount = 0;
  elrsLastFrameType = 0;
  elrsLastOtherFrameType = 0;
  elrsDeviceName[0] = '\0';
  elrsBindCommandInfo[0] = '\0';
  elrsProfileDetectStartTime = millis();
  elrsProfileDetectStartResponses = getElrsModuleResponseCount();
  elrsNoModuleDetected = false;
}

void initElrsUart() {
  if (elrsInitialized) return;
#if ELRS_PASSIVE_SNIFF_MODE
  initElrsPassiveSniffer();
  elrsInitialized = true;
  return;
#endif
  if (ELRS_MODULE_PROFILE == ELRS_MODULE_PROFILE_RANGER_NANO) {
    applyElrsModuleProfile(ELRS_MODULE_PROFILE_RANGER_NANO, true);
  } else {
    applyElrsModuleProfile(ELRS_MODULE_PROFILE_INTERNAL, true);
  }
  elrsAutoDetectLocked = (ELRS_MODULE_PROFILE != ELRS_MODULE_PROFILE_AUTO);
  restartElrsUart(elrsBaudCandidates[elrsBaudIndex]);
  elrsInitialized = true;
  if (shouldLogElrsSerialStatus()) {
    Serial.printf("ELRS UART profile=%s mode=%s tx=%d rx=%d invert(rx=%d,tx=%d)\n",
                  elrsModuleProfileName(elrsActiveModuleProfile),
                  elrsRuntimeHalfDuplex ? "half-duplex" : "full-duplex",
                  (int)elrsActiveTxPin,
                  (int)elrsActiveRxPin,
                  elrsRxInvert ? 1 : 0,
                  elrsTxInvert ? 1 : 0);
  }
}

void stopElrsUart() {
  if (!elrsInitialized && !elrsProtocolActive && !elrsModulePresent && !elrsLinkActive) return;

  elrsSerial.flush();
  elrsSerial.end();
  elrsHostSniffSerial.end();
  if (elrsRxInterruptAttached) {
    detachInterrupt(digitalPinToInterrupt(elrsActiveRxPin));
    elrsRxInterruptAttached = false;
  }

  pinMode(elrsActiveTxPin, INPUT);
  pinMode(elrsActiveRxPin, INPUT);
  elrsInitialized = false;
  elrsProtocolActive = false;
  elrsModulePresent = false;
  elrsLinkActive = false;
  lastElrsSerialRxTime = 0;
  lastElrsLinkStatsTime = 0;
  elrsUplinkLq = 0;
  elrsUplinkSnr = 0;
  elrsUplinkRssi1 = 0;
  elrsUplinkRssi2 = 0;
  elrsNoModuleDetected = false;
  elrsNoLinkPowerTimerStart = 0;
  elrsLowPowerFallbackActive = false;
  signalStrength = 0;
  topBarNeedsRedraw = true;
}

void updateElrsProfileAutoDetect(unsigned long now) {
  if (ELRS_MODULE_PROFILE != ELRS_MODULE_PROFILE_AUTO || elrsAutoDetectLocked) return;

  uint32_t responses = getElrsModuleResponseCount();
  if (responses > elrsProfileDetectStartResponses) {
    elrsAutoDetectLocked = true;
    elrsNoModuleDetected = false;
    if (shouldLogElrsSerialStatus()) {
      Serial.printf("ELRS auto-detect: locked %s profile (%lu response frames)\n",
                    elrsModuleProfileName(elrsActiveModuleProfile),
                    (unsigned long)(responses - elrsProfileDetectStartResponses));
    }
    return;
  }

  if (now - elrsProfileDetectStartTime >= ELRS_MODULE_PROBE_TIMEOUT_MS) {
    elrsAutoDetectLocked = true;
    elrsNoModuleDetected = true;
    elrsProtocolActive = false;
    elrsModulePresent = false;
    elrsLinkActive = false;
    signalStrength = 0;
    elrsSerial.flush();
    elrsSerial.end();
    elrsHostSniffSerial.end();
    if (elrsRxInterruptAttached) {
      detachInterrupt(digitalPinToInterrupt(elrsActiveRxPin));
      elrsRxInterruptAttached = false;
    }
    pinMode(elrsActiveTxPin, INPUT);
    pinMode(elrsActiveRxPin, INPUT_PULLUP);
    if (shouldLogElrsSerialStatus()) {
      Serial.println("ELRS auto-detect: no module after 5 seconds; UART disabled until reboot or protocol reset");
    }
    return;
  }

  if (elrsActiveModuleProfile == ELRS_MODULE_PROFILE_INTERNAL &&
      now - elrsProfileDetectStartTime >= ELRS_PROFILE_DETECT_SWITCH_MS) {
#if !ELRS_AUTO_PROBE_RANGER_ON_SILENT_INTERNAL
    return;
#else
    if (shouldLogElrsSerialStatus()) {
      Serial.println("ELRS auto-detect: internal profile silent, trying Ranger Nano one-wire profile");
    }
    applyElrsModuleProfile(ELRS_MODULE_PROFILE_RANGER_NANO, true);
    restartElrsUart(elrsBaudCandidates[elrsBaudIndex]);
    return;
#endif
  }

  if (elrsActiveModuleProfile == ELRS_MODULE_PROFILE_RANGER_NANO &&
      now - elrsProfileDetectStartTime >= ELRS_PROFILE_DETECT_ACTIVE_MS) {
    return;
  }
}

void initElrsPassiveSniffer() {
  elrsSerial.end();
  elrsHostSniffSerial.end();

#if !ELRS_SNIFF_SINGLE_WIRE_ONLY
  pinMode(ELRS_SNIFF_MODULE_TO_HOST_PIN, INPUT_PULLUP);
#endif
  pinMode(ELRS_SNIFF_HOST_TO_MODULE_PIN, INPUT_PULLUP);

#if !ELRS_SNIFF_SINGLE_WIRE_ONLY
  elrsSerial.begin(ELRS_PASSIVE_SNIFF_BAUD, SERIAL_8N1, ELRS_SNIFF_MODULE_TO_HOST_PIN, -1, false);
  elrsSerial.setMode(UART_MODE_UART);
  elrsSerial.setRxInvert(ELRS_PASSIVE_SNIFF_RX_INVERT);
#endif

  elrsHostSniffSerial.begin(ELRS_PASSIVE_SNIFF_BAUD, SERIAL_8N1, ELRS_SNIFF_HOST_TO_MODULE_PIN, -1, false);
  elrsHostSniffSerial.setMode(UART_MODE_UART);
  elrsHostSniffSerial.setRxInvert(ELRS_PASSIVE_SNIFF_RX_INVERT);

  Serial.printf("ELRS passive sniffer: shared rx=%d module->host rx=%d enabled=%d baud=%lu rxInvert=%d\n",
                ELRS_SNIFF_HOST_TO_MODULE_PIN,
                ELRS_SNIFF_MODULE_TO_HOST_PIN,
                ELRS_SNIFF_SINGLE_WIRE_ONLY ? 0 : 1,
                (unsigned long)ELRS_PASSIVE_SNIFF_BAUD,
                ELRS_PASSIVE_SNIFF_RX_INVERT ? 1 : 0);
}

void applyElrsUartPinPair(int pinPairIndex) {
  int wrapped = pinPairIndex % elrsPinPairCount;
  if (wrapped < 0) wrapped += elrsPinPairCount;
  uint8_t newTx = elrsPinPairs[wrapped][0];
  uint8_t newRx = elrsPinPairs[wrapped][1];

  if (elrsRxInterruptAttached) {
    detachInterrupt(digitalPinToInterrupt(elrsActiveRxPin));
    elrsRxInterruptAttached = false;
  }

  elrsActiveTxPin = newTx;
  elrsActiveRxPin = newRx;

  pinMode(elrsActiveRxPin, INPUT_PULLUP);
  if (ELRS_VERBOSE_SERIAL_DEBUG) {
    attachInterrupt(digitalPinToInterrupt(elrsActiveRxPin), onElrsRxEdge, CHANGE);
    elrsRxInterruptAttached = true;
  }

}

void restartElrsUart(uint32_t baud) {
  elrsSerial.end();
  elrsSerial.begin(baud, SERIAL_8N1, elrsActiveRxPin, elrsActiveTxPin, false);
  elrsSerial.setMode(UART_MODE_UART);
  elrsSerial.setRxInvert(elrsRxInvert);
  elrsSerial.setTxInvert(elrsTxInvert);
  elrsActiveBaud = baud;
}

void advanceElrsSerialMode() {
  elrsInvertModeIndex++;
  if (elrsInvertModeIndex >= elrsInvertModeCount) {
    elrsInvertModeIndex = 0;
    elrsBaudIndex++;
    if (elrsBaudIndex >= elrsBaudCandidateCount) {
      elrsBaudIndex = 0;
      #if ELRS_FORCE_PIN_PAIR_INDEX < 0
      elrsPinPairIndex = (elrsPinPairIndex + 1) % elrsPinPairCount;
      applyElrsUartPinPair(elrsPinPairIndex);
      #endif
    }
  }
  elrsRxInvert = elrsInvertModes[elrsInvertModeIndex][0];
  elrsTxInvert = elrsInvertModes[elrsInvertModeIndex][1];
  restartElrsUart(elrsBaudCandidates[elrsBaudIndex]);
}

void updateElrsLink(unsigned long now) {
  if (getModelProtocol(activeModel) != PROTOCOL_ELRS) {
    stopElrsUart();
    return;
  }

  if (elrsNoModuleDetected) {
    elrsProtocolActive = false;
    signalStrength = 0;
    return;
  }

  if (!elrsInitialized) {
    initElrsUart();
  }
  if (!elrsInitialized) return;

#if ELRS_PASSIVE_SNIFF_MODE
  updateElrsPassiveSniffer(now);
  return;
#endif

  readElrsSerial(now);
  updateElrsProfileAutoDetect(now);
  if (elrsNoModuleDetected) return;

  elrsModulePresent = (lastElrsSerialRxTime > 0) &&
    ((now - lastElrsSerialRxTime) <= ELRS_MODULE_TIMEOUT_MS);
  elrsLinkActive = (lastElrsLinkStatsTime > 0) &&
    ((now - lastElrsLinkStatsTime) <= ELRS_LINK_TIMEOUT_MS);
  if (elrsBindAwaitingResult) {
    if (elrsLinkActive) {
      elrsBindSuccessTime = now;
      elrsBindAwaitingResult = false;
      elrsBindBurstUntil = 0;
    } else if (now > elrsBindAwaitUntil) {
      elrsBindAwaitingResult = false;
      elrsBindBurstUntil = 0;
    }
  }
  if (elrsBindBurstUntil > now && (now - lastElrsBindBurstSendTime >= ELRS_BIND_BURST_INTERVAL_MS)) {
    lastElrsBindBurstSendTime = now;
    uint8_t bindFieldIndex = elrsBindFieldKnown ? elrsBindFieldIndex : getElrsBindFallbackFieldIndex();
    if (!elrsBindFieldKnown && strcmp(elrsDeviceName, "RM Pocket") == 0 && elrsParameterCount >= 28) {
      bindFieldIndex = 28;
    }
    sendElrsLuaCommandWrite(0x00, 0x00);
    if (elrsBindCommandStep == 3) {
      sendElrsLuaCommandWrite(bindFieldIndex, 0x04);
    } else {
      sendElrsLuaBindExecWrite(bindFieldIndex);
    }
  }

  if (elrsReceiverConfigActive) {
    elrsProtocolActive = false;
    signalStrength = 0;
    return;
  }

  if (elrsNoModuleDetected) {
    elrsProtocolActive = false;
    signalStrength = 0;
    return;
  }

  elrsProtocolActive = true;
  updateElrsLowPowerFallback(now);

  bool haveModuleFrames = hasElrsModuleFrames();
  bool haveAnyValidFrames = (elrsRxFrameCount > 0);
  bool allowSerialModeRetry = false;
#if ELRS_HALF_DUPLEX_MODE
  // Single-wire mode is sensitive to UART restarts; hopping baud/invert states
  // here can wedge the link, so keep it on the known-stable mode.
  allowSerialModeRetry = false;
#endif
  if (allowSerialModeRetry &&
      !haveAnyValidFrames &&
      (now - lastElrsBaudRetryTime >= ELRS_BAUD_RETRY_MS)) {
    lastElrsBaudRetryTime = now;
    advanceElrsSerialMode();
  }

  bool onElrsControlScreen = (currentScreen == SCREEN_PROTOCOL || currentScreen == SCREEN_ELRS);
  bool keepAliveBindTraffic = elrsBindAwaitingResult || (elrsBindBurstUntil > now) || (elrsBindCommandStep == 2);
  unsigned long txIntervalMs = (onElrsControlScreen && !keepAliveBindTraffic) ? 200UL : ELRS_CRSF_TX_INTERVAL_MS;
  bool probeListenOnly = ELRS_PROBE_UNTIL_MODULE_FRAMES && !haveModuleFrames && !elrsLinkActive;
#if ELRS_RX_ONLY_DIAGNOSTIC
  probeListenOnly = true;
#endif
  if (now - lastElrsTxTime >= txIntervalMs) {
    lastElrsTxTime = now;
    // Keep channel traffic minimal while on ELRS control screens so module
    // can respond to ping/parameter/bind frames on a busy single-wire bus.
    if (!probeListenOnly && (!onElrsControlScreen || elrsLinkActive || keepAliveBindTraffic)) {
      sendElrsChannelFrame();
    }
  }
  if (now - lastElrsDevicePingTime >= ELRS_DEVICE_PING_INTERVAL_MS) {
    lastElrsDevicePingTime = now;
    if (!probeListenOnly) {
      sendElrsDevicePing();
    }
  }
  if (!probeListenOnly && !keepAliveBindTraffic) {
    updateElrsParameterDiscovery(now);
  }

  if (elrsLinkActive) {
    signalStrength = elrsLqToBars(elrsUplinkLq);
  } else if (hasElrsTxOnlyActivity(now)) {
    signalStrength = 1;
  } else {
    signalStrength = 0;
  }

  if (RADIO_PROTOCOL_SERIAL_DEBUG) {
    static bool lastModulePresent = false;
    static bool lastLinkActive = false;
    static bool lastBindFieldKnown = false;
    static uint8_t lastBindCommandStepLogged = 0xFF;
    static uint8_t lastBindCommandTimeoutLogged = 0xFF;
    static uint8_t lastLoggedLq = 0xFF;
    static char lastDeviceNameLogged[sizeof(elrsDeviceName)] = "";
    static char lastBindInfoLogged[sizeof(elrsBindCommandInfo)] = "";

    bool deviceNameChanged = (strcmp(lastDeviceNameLogged, elrsDeviceName) != 0);
    bool bindInfoChanged = (strcmp(lastBindInfoLogged, elrsBindCommandInfo) != 0);

    if (elrsModulePresent && (!lastModulePresent || deviceNameChanged)) {
      Serial.printf("ELRS module online: name=%s params=%u bindField=%s%u baud=%lu\n",
                    (elrsDeviceName[0] != '\0') ? elrsDeviceName : "-",
                    (unsigned int)elrsParameterCount,
                    elrsBindFieldKnown ? "" : "?",
                    (unsigned int)elrsBindFieldIndex,
                    (unsigned long)elrsActiveBaud);
    } else if (!elrsModulePresent && lastModulePresent) {
      Serial.println("ELRS module offline");
    }

    if (elrsBindFieldKnown && (!lastBindFieldKnown || deviceNameChanged)) {
      Serial.printf("ELRS bind field ready: index=%u name=%s params=%u\n",
                    (unsigned int)elrsBindFieldIndex,
                    (elrsDeviceName[0] != '\0') ? elrsDeviceName : "-",
                    (unsigned int)elrsParameterCount);
    }

    if (elrsBindCommandStep != lastBindCommandStepLogged ||
        elrsBindCommandTimeout != lastBindCommandTimeoutLogged ||
        bindInfoChanged) {
      if (elrsBindFieldKnown || elrsBindCommandStep != 0 || elrsBindCommandInfo[0] != '\0') {
        Serial.printf("ELRS bind state=%s(%u) timeout=%u info=%s\n",
                      elrsCommandStepName(elrsBindCommandStep),
                      (unsigned int)elrsBindCommandStep,
                      (unsigned int)elrsBindCommandTimeout,
                      (elrsBindCommandInfo[0] != '\0') ? elrsBindCommandInfo : "-");
      }
    }

    if (elrsLinkActive && (!lastLinkActive || elrsUplinkLq != lastLoggedLq)) {
      Serial.printf("ELRS link active: lq=%u name=%s\n",
                    (unsigned int)elrsUplinkLq,
                    (elrsDeviceName[0] != '\0') ? elrsDeviceName : "-");
    } else if (!elrsLinkActive && lastLinkActive) {
      Serial.println("ELRS link lost");
    }

    lastModulePresent = elrsModulePresent;
    lastLinkActive = elrsLinkActive;
    lastBindFieldKnown = elrsBindFieldKnown;
    lastBindCommandStepLogged = elrsBindCommandStep;
    lastBindCommandTimeoutLogged = elrsBindCommandTimeout;
    lastLoggedLq = elrsUplinkLq;
    strncpy(lastDeviceNameLogged, elrsDeviceName, sizeof(lastDeviceNameLogged) - 1);
    lastDeviceNameLogged[sizeof(lastDeviceNameLogged) - 1] = '\0';
    strncpy(lastBindInfoLogged, elrsBindCommandInfo, sizeof(lastBindInfoLogged) - 1);
    lastBindInfoLogged[sizeof(lastBindInfoLogged) - 1] = '\0';

    if (ELRS_VERBOSE_SERIAL_DEBUG) {
      static unsigned long lastElrsVerbosePrintTime = 0;
      if (now - lastElrsVerbosePrintTime >= 1000) {
        lastElrsVerbosePrintTime = now;
        Serial.printf("ELRS baud=%lu pins(tx=%d,rx=%d,pair=%d) inv(rx=%d,tx=%d) tx=%s rxBytes=%lu rxFrames=%lu devInfo=%lu t14=%lu t29=%lu t2E=%lu radioId=%lu other=%lu lastType=0x%02X(%s) otherLast=0x%02X(%s) bindIdx=%u(%d) bindCmd=%u(%s) bindTmo=%u bindInfo=%s pCnt=%u name=%s lq=%u\n",
                      (unsigned long)elrsActiveBaud,
                      (int)elrsActiveTxPin,
                      (int)elrsActiveRxPin,
                      elrsPinPairIndex,
                      (int)elrsRxInvert,
                      (int)elrsTxInvert,
                      hasElrsTxOnlyActivity(now) ? "on" : "off",
                      (unsigned long)elrsRxByteCount,
                      (unsigned long)elrsRxFrameCount,
                      (unsigned long)elrsDeviceInfoCount,
                      (unsigned long)elrsType14Count,
                      (unsigned long)elrsType29Count,
                      (unsigned long)elrsType2ECount,
                      (unsigned long)elrsType3ACount,
                      (unsigned long)elrsTypeOtherCount,
                      (unsigned int)elrsLastFrameType,
                      elrsFrameTypeName(elrsLastFrameType),
                      (unsigned int)elrsLastOtherFrameType,
                      elrsFrameTypeName(elrsLastOtherFrameType),
                      (unsigned int)elrsBindFieldIndex,
                      (int)elrsBindFieldKnown,
                      (unsigned int)elrsBindCommandStep,
                      elrsCommandStepName(elrsBindCommandStep),
                      (unsigned int)elrsBindCommandTimeout,
                      (elrsBindCommandInfo[0] != '\0') ? elrsBindCommandInfo : "-",
                      (unsigned int)elrsParameterCount,
                      (elrsDeviceName[0] != '\0') ? elrsDeviceName : "-",
                      (unsigned int)elrsUplinkLq);
        Serial.printf("ELRS pins tx=%d rx=%d modeSlot=%d\n",
                      (int)elrsActiveTxPin,
                      (int)elrsActiveRxPin,
                      elrsPinPairIndex);
        if (elrsRxFrameCount == 0) {
          Serial.printf("ELRS raw sig C8=%lu EE=%lu EA=%lu EC=%lu printable=%lu/%lu sample=",
                        (unsigned long)elrsSyncC8Count,
                        (unsigned long)elrsSyncEECount,
                        (unsigned long)elrsSyncEACount,
                        (unsigned long)elrsSyncECCount,
                        (unsigned long)elrsPrintableByteCount,
                        (unsigned long)elrsRxByteCount);
          uint8_t start = elrsRecentByteIndex;
          for (int i = 0; i < 16; i++) {
            uint8_t b = elrsRecentBytes[(start + i) & 0x0F];
            Serial.printf("%02X", b);
            if (i != 15) Serial.print(' ');
          }
          Serial.println();
        }
        Serial.printf("ELRS types 16=%lu 14=%lu 1C=%lu 1D=%lu 29=%lu 2E=%lu 3A=%lu other=%lu\n",
                      (unsigned long)elrsType16Count,
                      (unsigned long)elrsType14Count,
                      (unsigned long)elrsType1CCount,
                      (unsigned long)elrsType1DCount,
                      (unsigned long)elrsType29Count,
                      (unsigned long)elrsType2ECount,
                      (unsigned long)elrsType3ACount,
                      (unsigned long)elrsTypeOtherCount);
        if (elrsLast2EFrameLen > 0) {
          Serial.print("ELRS last2E=");
          for (uint8_t i = 0; i < elrsLast2EFrameLen; i++) {
            Serial.printf("%02X", elrsLast2EFrame[i]);
            if (i + 1 != elrsLast2EFrameLen) Serial.print(' ');
          }
          Serial.println();
        }
        Serial.printf("ELRS rxPin=%d edges=%lu\n",
                      elrsActiveRxPin,
                      (unsigned long)elrsRxEdgeCount);
      }
    }
  }
}

void updateElrsPassiveSniffer(unsigned long now) {
  (void)now;
  readElrsSniffSerial(elrsHostSniffSerial, elrsHostToModuleSniff);
#if !ELRS_SNIFF_SINGLE_WIRE_ONLY
  readElrsSniffSerial(elrsSerial, elrsModuleToHostSniff);
#endif

  static unsigned long lastElrsSniffPrintTime = 0;
  if (RADIO_PROTOCOL_SERIAL_DEBUG && millis() - lastElrsSniffPrintTime >= 1000) {
    lastElrsSniffPrintTime = millis();
    logElrsSniffSummary(ELRS_SNIFF_SINGLE_WIRE_ONLY ? "SNIFF SHARED" : "SNIFF H->M", elrsHostToModuleSniff);
#if !ELRS_SNIFF_SINGLE_WIRE_ONLY
    logElrsSniffSummary("SNIFF M->H", elrsModuleToHostSniff);
#endif
  }
}

bool hasEspNowSignal(unsigned long now) {
  return lastEspNowTelemetryTime > 0 &&
    (now - lastEspNowTelemetryTime) <= ESPNOW_TELEMETRY_TIMEOUT_MS;
}

bool hasEspNowHeaderSignal(unsigned long now) {
  if (lastEspNowAliveTime > 0) {
    return (now - lastEspNowAliveTime) <= ESPNOW_HEADER_SIGNAL_TIMEOUT_MS;
  }

  if (espNowProtocolStartTime == 0) return false;
  return (now - espNowProtocolStartTime) <= ESPNOW_HEADER_SIGNAL_TIMEOUT_MS;
}

void updateTopBarDirtyState(unsigned long now) {
  if (currentScreen == SCREEN_SPLASH ||
      currentScreen == SCREEN_SPACE_GAME ||
      currentScreen == SCREEN_20Q_GAME ||
      currentScreen == SCREEN_ASTEROIDS_GAME ||
      currentScreen == SCREEN_STAR_SWARM_GAME ||
      currentScreen == SCREEN_DUNGEON_RUN) return;

  static bool initialized = false;
  static ProtocolType lastProtocol = PROTOCOL_ELRS;
  static int lastBatteryLevel = -1;
  static bool lastBatteryPresent = false;
  static bool lastBatteryCharging = false;
  static int lastSignalBars = -1;
  static int lastLatency = -1;
  static bool lastEspNowSignalPresent = false;
  static bool lastEspNowNoSignalEligible = false;
  static bool lastElrsLinkActive = false;
  static bool lastElrsModulePresent = false;
  static bool lastElrsNoSignalEligible = false;
  static uint8_t lastElrsLq = 255;

  ProtocolType protocol = getModelProtocol(activeModel);
  bool espNowSignalPresent = hasEspNowHeaderSignal(now);
  bool espNowNoSignalEligible = (lastEspNowAliveTime > 0) ||
    (espNowProtocolStartTime > 0 &&
     (now - espNowProtocolStartTime) > ESPNOW_HEADER_SIGNAL_TIMEOUT_MS);
  bool elrsNoSignalEligible = elrsModulePresent || lastElrsSerialRxTime > 0;
  int latencySnapshot = espNowLatency;

  bool changed = !initialized ||
    protocol != lastProtocol ||
    batteryLevel != lastBatteryLevel ||
    batteryPresent != lastBatteryPresent ||
    batteryCharging != lastBatteryCharging ||
    signalStrength != lastSignalBars ||
    latencySnapshot != lastLatency ||
    espNowSignalPresent != lastEspNowSignalPresent ||
    espNowNoSignalEligible != lastEspNowNoSignalEligible ||
    elrsLinkActive != lastElrsLinkActive ||
    elrsModulePresent != lastElrsModulePresent ||
    elrsNoSignalEligible != lastElrsNoSignalEligible ||
    elrsUplinkLq != lastElrsLq;

  if (changed) {
    topBarNeedsRedraw = true;
    initialized = true;
    lastProtocol = protocol;
    lastBatteryLevel = batteryLevel;
    lastBatteryPresent = batteryPresent;
    lastBatteryCharging = batteryCharging;
    lastSignalBars = signalStrength;
    lastLatency = latencySnapshot;
    lastEspNowSignalPresent = espNowSignalPresent;
    lastEspNowNoSignalEligible = espNowNoSignalEligible;
    lastElrsLinkActive = elrsLinkActive;
    lastElrsModulePresent = elrsModulePresent;
    lastElrsNoSignalEligible = elrsNoSignalEligible;
    lastElrsLq = elrsUplinkLq;
  }
}

uint16_t getLatencyIndicatorColor(int latencyMs) {
  if (latencyMs <= 40) return COLOR_SIG;
  if (latencyMs <= 100) return COLOR_ACCENT_HI;
  return TFT_RED;
}

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 0)
void onEspNowSend(const esp_now_send_info_t *tx_info, esp_now_send_status_t status) {
  (void)tx_info;
#else
void onEspNowSend(const uint8_t *macAddr, esp_now_send_status_t status) {
  (void)macAddr;
#endif
  lastEspNowSendStatus = status;
}

void onEspNowReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (info == nullptr || data == nullptr || len <= 0) return;
  handleEspNowBindPacket(info, data, len);
  handleEspNowTelemetryPacket(info, data, len);
  handleEspNowPingAckPacket(info, data, len);
}

/*
  Boot sequence.

  Order matters here:
    1. Bring up serial, soft-power, EEPROM, SPI, display, and ADC basics.
    2. Start I2C before probing daughterboard peripherals.
    3. Load persistent settings before selecting the active model.
    4. Start the radio service that matches the active model protocol.
*/
void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.println("Booting Hosyond transmitter...");
  initStatusRgbLed();
  initPowerButton();
  handleSoftPowerBootState();
  initPeripheralPowerSwitches();
  EEPROM.begin(EEPROM_SIZE);

  SPI.begin(12, 13, 11, 10);

  pinMode(45, OUTPUT);
  pinMode(DISPLAY_BL_PIN, OUTPUT);
  analogWriteResolution(DISPLAY_BL_PIN, DISPLAY_BL_PWM_BITS);
  analogWriteFrequency(DISPLAY_BL_PIN, DISPLAY_BL_PWM_FREQ);
  analogWrite(DISPLAY_BL_PIN, DEFAULT_DISPLAY_BRIGHTNESS);
  digitalWrite(DISPLAY_BL_PIN, TFT_BACKLIGHT_ON);
  pinMode(BATTERY_ADC_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_11db);

  tft.init();
  tft.setRotation(0);
  tft.invertDisplay(true);
    
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  Wire.setTimeOut(I2C_TRANSACTION_TIMEOUT_MS);
  unsigned long peripheralProbeStartMs = millis();
  adsPrimaryProbeStartMs = peripheralProbeStartMs;
  adsAuxProbeStartMs = peripheralProbeStartMs;
  mcp23017ProbeStartMs = peripheralProbeStartMs;
  startupDeviceDiscoveryStartTime = peripheralProbeStartMs;
  printI2cStartupScan();
  initBq25887Charger();
  loadStickCalibration();
  touchPanel.begin();
  initAds1115();
  initMcp23017();
  loadAudioSettings();
  initAudio();
  lastActivityTime = millis();
  splashStartTime = 0;

  bool displaySettingsRepaired = loadDisplaySettings();
  applyThemePalette();
  loadModels();
  loadFailsafeValues();
  loadRateValues();
  loadExpoValues();
  loadEndpointValues();
  bool escMapLoaded = loadTankEscChannelMap();
  loadEspNowReceiverOutputModes();
  loadOtaSettings();
  loadReceiverBindings();
  loadTwentyQLearnedCandidates();
  applyDisplayBacklight();
  if (displaySettingsRepaired) {
    saveDisplaySettings();
  }

  // First-boot and schema migration repair. Each saved block has its own
  // version byte so new firmware can reset only the data that changed shape.
  bool initializedAnyModel = false;
  bool repairedAnyMix = false;
  bool resetMixSchema = (EEPROM.read(EEPROM_MIX_VERSION_ADDR) != MIX_STORAGE_VERSION);
  bool resetFailsafeSchema = (EEPROM.read(EEPROM_FAILSAFE_VERSION_ADDR) != FAILSAFE_STORAGE_VERSION);
  bool resetRateSchema = (EEPROM.read(EEPROM_RATE_VERSION_ADDR) != RATE_STORAGE_VERSION);
  bool resetExpoSchema = (EEPROM.read(EEPROM_EXPO_VERSION_ADDR) != EXPO_STORAGE_VERSION);
  bool resetEndpointSchema = (EEPROM.read(EEPROM_ENDPOINT_VERSION_ADDR) != ENDPOINT_STORAGE_VERSION);

  if (resetFailsafeSchema) {
    for (int i = 0; i < MAX_MODELS; i++) {
      clearModelFailsafeValues(i);
    }
  }
  if (resetRateSchema) {
    for (int i = 0; i < MAX_MODELS; i++) {
      clearModelRateValues(i);
    }
  }
  if (resetExpoSchema) {
    for (int i = 0; i < MAX_MODELS; i++) {
      clearModelExpoValues(i);
    }
  }
  if (resetEndpointSchema) {
    for (int i = 0; i < MAX_MODELS; i++) {
      clearModelEndpointValues(i);
    }
  }

  for (int i = 0; i < MAX_MODELS; i++) {
    if (modelSlotUninitialized(i)) {
      initModelDefaults(i);
      initializedAnyModel = true;
    }
    else {
      models[i].name[19] = '\0';
    }

    if (resetMixSchema) {
      for (int mix = 0; mix < MIX_COUNT; mix++) {
        initDefaultMixSlot(i, mix);
      }
      for (int ch = 0; ch < CHANNEL_COUNT; ch++) {
        models[i].reverse[ch] = false;
        models[i].failsafe[ch] = false;
      }
      for (int g = 0; g < 2; g++) {
        models[i].trimX[g] = 0;
        models[i].trimY[g] = 0;
      }
      repairedAnyMix = true;
    }

    if (sanitizeModelMixes(i)) {
      repairedAnyMix = true;
    }

    if (sanitizeModelDriveType(i)) {
      repairedAnyMix = true;
    }

    if (sanitizeModelProtocol(i)) {
      repairedAnyMix = true;
    }

    if (sanitizeModelTankEscChannels(i)) {
      repairedAnyMix = true;
    }
  }

  if (strlen(models[0].name) == 0) {
    strncpy(models[0].name, "Anubis", sizeof(models[0].name) - 1);
    models[0].name[sizeof(models[0].name) - 1] = '\0';
    initializedAnyModel = true;
  }

  if (initializedAnyModel || repairedAnyMix) {
    saveModels();
  }
  if (initializedAnyModel || resetFailsafeSchema) {
    saveFailsafeValues();
  }
  if (initializedAnyModel || resetRateSchema) {
    saveRateValues();
  }
  if (initializedAnyModel || resetExpoSchema) {
    saveExpoValues();
  }
  if (initializedAnyModel || resetEndpointSchema) {
    saveEndpointValues();
  }
  if (!escMapLoaded || initializedAnyModel || repairedAnyMix) {
    saveTankEscChannelMap();
  }
  if (EEPROM.read(EEPROM_ESPNOW_OUTPUT_MODE_VERSION_ADDR) != ESPNOW_OUTPUT_MODE_STORAGE_VERSION) {
    saveEspNowReceiverOutputModes();
  }
  if (EEPROM.read(EEPROM_OTA_VERSION_ADDR) != OTA_SETTINGS_STORAGE_VERSION) {
    saveOtaSettings();
  }
  if (EEPROM.read(EEPROM_DISPLAY_VERSION_ADDR) != DISPLAY_SETTINGS_STORAGE_VERSION) {
    saveDisplaySettings();
  }

  // Keep the legacy String array in sync with the packed model structs used for
  // persistent storage.
  for (int i = 0; i < 4; i++) {
    modelNames[i] = String(models[i].name);
  }

  // Select the startup model after all stored models have been repaired.
  int startupModel = loadActiveModelIndex();
  if (startupModel < 0 || startupModel >= MAX_MODELS || strlen(models[startupModel].name) == 0) {
    startupModel = 0;
  }
  activeModel = startupModel;
  selectedModelIndex = startupModel;
  currentModelName = String(models[activeModel].name);
  currentDrive = getModelDriveType(activeModel);
  elrsTxPowerUserOverrideActive = false;
  elrsLowPowerFallbackActive = true;
  elrsTxPowerMw = clampElrsTxPowerMw(ELRS_LOW_POWER_FALLBACK_MW);

  // Seed the UI and radio runtime from the active model.
  trimRenderX = models[activeModel].trimX[currentTrimPage];
  trimRenderY = models[activeModel].trimY[currentTrimPage];
  if (getModelProtocol(activeModel) == PROTOCOL_ELRS) {
    initElrsUart();
    sendElrsTxPowerWrite(ELRS_LOW_POWER_FALLBACK_MW, false);
  }
#if !ELRS_PASSIVE_SNIFF_MODE
  initEspNowLink();
#endif
  }

/*
  Main control/UI frame.

  Each pass polls human input, samples daughterboard hardware, updates battery
  and safety state, services the active radio protocol, then redraws only the
  parts of the current screen that changed.
*/
void loop() {
  unsigned long now = millis();

  // Soft-power and diagnostics run first so shutdown requests interrupt the
  // rest of the frame quickly.
  updatePowerButton(now);
  if (powerButtonShutdownPending) {
    delay(10);
    return;
  }
  updatePowerLed(now);
  updateAudioDiagnostics(now);
    
  // A screen change turns the next draw pass into a full repaint.
  if (currentScreen != lastScreen) {

    fullRedraw = true;
    uiNeedsRedraw = true;

    lastScreen = currentScreen;
  }

  // Once discovery is complete, keep the splash audio stream fed without
  // interleaving touch, ADS, battery, or radio transactions between buffers.
  if (startupDeviceDiscoveryComplete && currentScreen == SCREEN_SPLASH) {
    drawSplash();
    fullRedraw = true;
    applyDisplayBacklight();
    return;
  }

  static int rawX = 0;
  static int rawY = 0;
  static uint8_t touchCount = 0;
  static unsigned long lastTouchPollTime = 0;

  // Touch is rate-limited to avoid burning time on the I2C touch controller.
  if (now - lastTouchPollTime >= TOUCH_POLL_INTERVAL_MS) {
    touchCount = touchPanel.read_touch_number();

    if (touchCount > 0 && touchCount <= 2) {
      rawX = touchPanel.read_touch1_x();
      rawY = touchPanel.read_touch1_y();
    }
    else {
      touchCount = 0;
    }

    lastTouchPollTime = now;
  }

#if AUDIO_ENABLED
  if (touchCount == 0 && touchActive && audioSettingsDirty) {
    saveAudioSettings();
  }
#endif

  // The D-pad is active-low through the daughterboard MCP23017.
  if (!mcp23017Ready && !mcp23017ProbeAbandoned) {
    if (now - mcp23017ProbeStartMs >= MCP23017_PROBE_TIMEOUT_MS) {
      mcp23017ProbeAbandoned = true;
      Serial.println("MCP23017 not detected after 5 seconds; stopping reconnect attempts.");
    } else if (now - lastMcpReconnectAttemptMs >= MCP23017_RECONNECT_INTERVAL_MS) {
      lastMcpReconnectAttemptMs = now;
      initMcp23017();
    }
  }
  if (mcp23017Ready && now - lastMcpSampleMs >= MCP23017_SAMPLE_INTERVAL_MS) {
    lastMcpSampleMs = now;
    uint16_t mcpValue = 0xFFFF;
    readMcp23017(mcpValue);
  }
  uint16_t dpadPort = mcp23017Ready ? mcp23017LastValue : 0xFFFF;
  bool select = ((dpadPort & (1U << MCP23017_DPAD_SELECT_BIT)) == 0);
  bool left   = ((dpadPort & (1U << MCP23017_DPAD_LEFT_BIT)) == 0);
  bool up     = ((dpadPort & (1U << MCP23017_DPAD_UP_BIT)) == 0);
  bool down   = ((dpadPort & (1U << MCP23017_DPAD_DOWN_BIT)) == 0);
  bool right  = ((dpadPort & (1U << MCP23017_DPAD_RIGHT_BIT)) == 0);
  bool powerButtonInput = POWER_BUTTON_ENABLED && isPowerButtonPressed();
  bool earlyInputDetected = (touchCount > 0) || select || up || down || left || right || powerButtonInput;

  bool userActive = false;

  // Games get first chance at D-pad input because they have frame-based logic.
  if (currentScreen == SCREEN_SPACE_GAME) {
    updateSpaceGame(now, left, right, select, down);
    if (left || right || select || down) {
      userActive = true;
    }
  }
  else if (currentScreen == SCREEN_DUNGEON_RUN) {
    updateDungeonRun(now, select, down);
    if (select || down) {
      userActive = true;
    }
  }
  else if (currentScreen == SCREEN_ASTEROIDS_GAME) {
    updateAsteroidsGame(now, select, down);
    if (select || down) {
      userActive = true;
    }
  }
  else if (currentScreen == SCREEN_STAR_SWARM_GAME) {
    updateStarSwarmGame(now, select, down);
    if (select || down || left || right) {
      userActive = true;
    }
  }

  // Sample hardware inputs and power telemetry before any protocol packet is
  // built or any safety overlay is evaluated.
  updateAccessoryInputs(now);
  updateStickInputs(now);
  updateBatteryState();
  updateStatusRgbLed(now);
  bool stickInputDetected = updateStickDisplayWake(now);
  if (stickInputDetected || powerButtonInput) {
    userActive = true;
  }
  bool batteryUserDismiss = (touchCount > 0) || select || up || down || left || right;
  if (updateBatterySafety(now, batteryUserDismiss)) {
    return;
  }

  // Keep the logo and startup clip behind the finite device-discovery phase.
  // This lets missing daughterboard/UART devices time out before audio begins,
  // so their startup traffic cannot starve the splash audio stream.
  if (!startupDeviceDiscoveryComplete) {
#if !ELRS_PASSIVE_SNIFF_MODE
    updateEspNowLink(now);
#endif
    updateElrsLink(now);

    if (!startupDiscoveryScreenCleared) {
      tft.fillScreen(TFT_BLACK);
      startupDiscoveryScreenCleared = true;
    }

    if (now - startupDeviceDiscoveryStartTime < STARTUP_DEVICE_DISCOVERY_MS) {
      applyDisplayBacklight();
      return;
    }

    startupDeviceDiscoveryComplete = true;
    splashStartTime = 0;
    fullRedraw = true;
    uiNeedsRedraw = true;
    Serial.println("Startup device discovery complete; beginning splash audio.");
  }

  // Present the startup logo/audio after discovery even when stick hardware is
  // absent. Throttle safety still gates entry to the operational screens.
  if (currentScreen == SCREEN_SPLASH) {
    drawSplash();
    fullRedraw = true;
    applyDisplayBacklight();
    return;
  }

  // Block normal operation until throttle is safe or the user deliberately
  // bypasses the startup warning.
  if (!startupThrottleSafetyCleared) {
    if (isStartupThrottleSafe()) {
      startupThrottleSafetyCleared = true;
      startupThrottleSafetyScreenDrawn = false;
      startupThrottleBypassTapCount = 0;
      startupThrottleBypassTouchActive = false;
      fullRedraw = true;
      uiNeedsRedraw = true;
      topBarNeedsRedraw = true;
    } else if (handleStartupThrottleSafetyBypass(touchCount, rawX, rawY, now)) {
      startupThrottleSafetyCleared = true;
      startupThrottleSafetyScreenDrawn = false;
      startupThrottleBypassTapCount = 0;
      startupThrottleBypassTouchActive = false;
      fullRedraw = true;
      uiNeedsRedraw = true;
      topBarNeedsRedraw = true;
    } else {
      updateStartupThrottleSafetyScreen(now);
      applyDisplayBacklight();
      return;
    }
  }

  // Service background communications after safety gates but before UI redraw.
  updateOtaService();
  updateElrsReceiverConfig();
#if !ELRS_PASSIVE_SNIFF_MODE
  updateEspNowLink(now);
#endif
  updateElrsLink(now);
  updateStatusRgbLed(now);

  if (updateAutoDeepSleep(now, earlyInputDetected || stickInputDetected, powerButtonInput)) {
    applyDisplayBacklight();
    return;
  }

  // Endpoint tuning tracks the stick being moved so the correct channel/side is
  // selected without forcing the user through a lot of menu navigation.
  if (currentScreen == SCREEN_ENDPOINTS) {
    bool endpointFocusChanged = updateEndpointAutoFocus();
    bool endpointSideChanged = false;
    for (int i = 0; i < CHANNEL_COUNT; i++) {
      if (updateEndpointSideLatch(i)) endpointSideChanged = true;
    }
    if (endpointFocusChanged || endpointSideChanged) {
      endpointNeedsRedraw = true;
      uiNeedsRedraw = true;
    }
  }

  static int lastProtocolBindSeconds = -1;
  static bool lastProtocolBindingMode = false;
  static bool lastProtocolShowSuccess = false;
  static unsigned long lastProtocolAutoRefreshTime = 0;

  if (currentScreen == SCREEN_PROTOCOL) {
    ProtocolType protocol = getModelProtocol(activeModel);
    int bindSeconds = -1;
    if (espNowBindingMode) {
      unsigned long remainingMs = 0;
      if (now - espNowBindingStartTime < ESPNOW_BIND_TIMEOUT_MS) {
        remainingMs = ESPNOW_BIND_TIMEOUT_MS - (now - espNowBindingStartTime);
      }
      bindSeconds = (int)((remainingMs + 999) / 1000);
    }

    bool showSuccess = (!espNowBindingMode && (now - espNowBindSuccessTime < 2500));

    if (bindSeconds != lastProtocolBindSeconds ||
        espNowBindingMode != lastProtocolBindingMode ||
        showSuccess != lastProtocolShowSuccess) {
      uiNeedsRedraw = true;
      lastProtocolBindSeconds = bindSeconds;
      lastProtocolBindingMode = espNowBindingMode;
      lastProtocolShowSuccess = showSuccess;
    }

    // Keep protocol status text (ELRS UART/link state, bind countdown, etc.) live
    // even when there is no touch/D-pad activity.
    if (now - lastProtocolAutoRefreshTime >= 250) {
      lastProtocolAutoRefreshTime = now;
      if (protocol == PROTOCOL_ELRS || espNowBindingMode || showSuccess || otaModeActive) {
        uiNeedsRedraw = true;
      }
    }
  } else {
    lastProtocolBindSeconds = -1;
    lastProtocolBindingMode = false;
    lastProtocolShowSuccess = false;
    lastProtocolAutoRefreshTime = 0;
  }

  static unsigned long lastElrsScreenAutoRefreshTime = 0;
  if (currentScreen == SCREEN_ELRS) {
    if (now - lastElrsScreenAutoRefreshTime >= 250) {
      lastElrsScreenAutoRefreshTime = now;
      uiNeedsRedraw = true;
    }
  } else {
    lastElrsScreenAutoRefreshTime = 0;
  }

  if (currentScreen == SCREEN_TIMER) {
    bool timerAlarmBlinking = isTimerElapsed(now);
    unsigned long timerRefreshMs = timerAlarmBlinking ? TIMER_ALARM_BLINK_MS : TIMER_REFRESH_MS;
    if (timerNeedsRedraw ||
        ((timerRunning || timerAlarmBlinking) && now - timerLastUiTickMs >= timerRefreshMs)) {
      timerLastUiTickMs = now;
      uiNeedsRedraw = true;
    }
  }

  m1 = (leftThrottle + 1.0f) * 0.5f;
  m2 = (rightThrottle + 1.0f) * 0.5f;
  m3 = (leftY + 1.0f) * 0.5f;
  m4 = (rightY + 1.0f) * 0.5f;

  static unsigned long lastDpadTime = 0;
  static unsigned long lastMainFrameTime = 0;
  static bool displaySettingsSelectHeld = false;

  // Modal editors consume D-pad input before the current screen handler sees it.
  if (keyboardActive) {

  if (millis() - lastDpadTime > 150) {

    bool didInput = false;

    if (down || up) {
      kbCursorRow = cycleFocusIndex(kbCursorRow, KB_ROWS, down);
      keyboardNeedsRedraw = true;
      didInput = true;
    }

    if (left) {
      kbCursorCol--;
      if (kbCursorCol < 0) kbCursorCol = KB_COLS - 1;
      keyboardNeedsRedraw = true;
      didInput = true;
    }

    if (right) {
      kbCursorCol++;
      if (kbCursorCol >= KB_COLS) kbCursorCol = 0;
      keyboardNeedsRedraw = true;
      didInput = true;
    }

    if (select) {
      handleKeyboardSelect();
      didInput = true;
    }

    if (didInput) {
      lastDpadTime = millis();
    }

    userActive = true;
  }
}

  if (currentScreen == SCREEN_MAIN &&
      now - lastMainFrameTime >= MAIN_FRAME_INTERVAL_MS) {
    uiNeedsRedraw = true;
  }

  // Top bar has its own dirty check because signal/battery state changes even
  // while the body of the screen is static.
  updateTopBarDirtyState(now);

  if (currentScreen != SCREEN_SPACE_GAME &&
      currentScreen != SCREEN_STAR_SWARM_GAME &&
      millis() - lastDpadTime > 150) {

    bool didInput = false;

    if (mixNumpadActive) {
      dpadFocusVisible = true;
      const char* mixPadLayout[4][3] = {
        {"1", "2", "3"},
        {"4", "5", "6"},
        {"7", "8", "9"},
        {"<", "0", "-"}
      };

      if (down || up) {
        mixNumpadCursorRow = cycleFocusIndex(mixNumpadCursorRow, 4, down);
        if (mixNumpadCursorRow > 0 && mixNumpadCursorCol > 2) {
          mixNumpadCursorCol = 2;
        }
        mixNumpadNeedsRedraw = true;
        didInput = true;
      }

      if (left) {
        mixNumpadCursorCol--;
        if (mixNumpadCursorRow == 0) {
          if (mixNumpadCursorCol < 0) mixNumpadCursorCol = 3;
        } else {
          if (mixNumpadCursorCol < 0) mixNumpadCursorCol = 2;
        }
        mixNumpadNeedsRedraw = true;
        didInput = true;
      }

      if (right) {
        int maxCol = (mixNumpadCursorRow == 0) ? 4 : 3;
        mixNumpadCursorCol = (mixNumpadCursorCol + 1) % maxCol;
        mixNumpadNeedsRedraw = true;
        didInput = true;
      }

      if (select) {
        if (mixNumpadCursorRow == 0 && mixNumpadCursorCol == 3) {
          closeMixNumpad(true);
          mixNumpadNeedsRedraw = true;
          uiNeedsRedraw = true;
          didInput = true;
        }
        else {
          const char* key = mixPadLayout[mixNumpadCursorRow][mixNumpadCursorCol];
          bool positiveOnlyTarget = (mixNumpadTarget == NUMPAD_TARGET_RATE_VALUE ||
                                     mixNumpadTarget == NUMPAD_TARGET_ENDPOINT_VALUE ||
                                     mixNumpadTarget == NUMPAD_TARGET_ELRS_TX_POWER ||
                                     mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_MODEL_ID ||
                                     mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_FAILSAFE ||
                                     mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_PWM_FAILSAFE ||
                                     mixNumpadTarget == NUMPAD_TARGET_TIMER_VALUE);

          if (strcmp(key, "<") == 0) {
            if (mixNumpadBuffer.length() > 0) {
              mixNumpadBuffer.remove(mixNumpadBuffer.length() - 1);
              if (mixNumpadBuffer == "-") mixNumpadBuffer = "";
            }
          }
          else if (strcmp(key, "-") == 0) {
            if (!positiveOnlyTarget) {
              if (mixNumpadBuffer.startsWith("-")) mixNumpadBuffer.remove(0, 1);
              else mixNumpadBuffer = "-" + mixNumpadBuffer;
              if (mixNumpadBuffer.length() == 0) mixNumpadBuffer = "-";
            }
          }
          else {
            String candidate = mixNumpadBuffer;
            if (candidate == "0") candidate = "";
            if (candidate == "-0") candidate = "-";
            candidate += key;

            int digitCount = 0;
            for (int i = 0; i < candidate.length(); i++) {
              if (candidate[i] >= '0' && candidate[i] <= '9') digitCount++;
            }

            int parsedValue = candidate.toInt();
            bool validCandidate = false;
            if (positiveOnlyTarget) {
              int maxValue = 100;
              if (mixNumpadTarget == NUMPAD_TARGET_ENDPOINT_VALUE) maxValue = 120;
              else if (mixNumpadTarget == NUMPAD_TARGET_ELRS_TX_POWER) maxValue = ELRS_TX_POWER_MAX_MW;
              else if (mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_MODEL_ID ||
                       mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_FAILSAFE) maxValue = 255;
              else if (mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_PWM_FAILSAFE) maxValue = ELRS_RX_PWM_FAILSAFE_MAX_US;
              else if (mixNumpadTarget == NUMPAD_TARGET_TIMER_VALUE) maxValue = 9959;
              int maxDigits = (mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_PWM_FAILSAFE ||
                               mixNumpadTarget == NUMPAD_TARGET_TIMER_VALUE) ? 4 : 3;
              validCandidate = (digitCount <= maxDigits && parsedValue >= 0 && parsedValue <= maxValue);
              if (mixNumpadTarget == NUMPAD_TARGET_TIMER_VALUE) {
                validCandidate = validCandidate && ((parsedValue % 100) <= 59);
              }
            } else {
              validCandidate = (digitCount <= 3 && (candidate == "-" || (parsedValue >= -100 && parsedValue <= 100)));
            }

            if (validCandidate) {
              mixNumpadBuffer = candidate;
            }
          }

          mixNumpadNeedsRedraw = true;
          didInput = true;
        }
      }
    }
    else if (currentScreen == SCREEN_MAIN && !droneArmWarningVisible) {
      if (down || up || left || right || select) dpadFocusVisible = true;

      if (down || up) {
        if (currentDrive == DRIVE_X_DRONE) {
          const ButtonID order[] = {BTN_MENU, BTN_DRONE_ARM};
          selectedButton = cycleButtonOrder(selectedButton, order, 2, down);
        } else {
          selectedButton = BTN_MENU;
        }
        uiNeedsRedraw = true;
        didInput = true;
      }

      if (select && selectedButton == BTN_MENU) {
        setScreen(SCREEN_MENU);
        selectedButton = BTN_NONE;
        didInput = true;
      }
      else if (select && selectedButton == BTN_DRONE_ARM && currentDrive == DRIVE_X_DRONE) {
        if (droneArmActive) {
          setDroneArmActive(false);
        } else {
          showDroneArmWarning();
        }
        didInput = true;
      }
    }

  else if (currentScreen == SCREEN_MENU) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      const ButtonID order[] = {
        BTN_CTRL, BTN_MODEL, BTN_DISPLAY_SETTINGS, BTN_TX_UPDATE, BTN_GAME, BTN_BACK
      };
      selectedButton = cycleButtonOrder(selectedButton, order, 6, down);
          fullRedraw = true;
          uiNeedsRedraw = true;
          didInput = true;
          userActive = true;
      }

    if (left) {
      if (selectedButton == BTN_GAME) {
        selectedButton = BTN_TX_UPDATE;
        fullRedraw = true;
        uiNeedsRedraw = true;
      } else if (selectedButton == BTN_TX_UPDATE) {
        selectedButton = BTN_DISPLAY_SETTINGS;
        fullRedraw = true;
        uiNeedsRedraw = true;
      } else if (selectedButton == BTN_DISPLAY_SETTINGS) {
        selectedButton = BTN_MODEL;
        fullRedraw = true;
        uiNeedsRedraw = true;
      } else {
        setScreen(SCREEN_MAIN);
        selectedButton = BTN_NONE;
      }
      didInput = true;
      userActive = true;
    }

    if (right && selectedButton == BTN_MODEL) {
      selectedButton = BTN_DISPLAY_SETTINGS;
      uiNeedsRedraw = true;
      didInput = true;
      userActive = true;
    }
    else if (right && selectedButton == BTN_DISPLAY_SETTINGS) {
      selectedButton = BTN_TX_UPDATE;
      uiNeedsRedraw = true;
      didInput = true;
      userActive = true;
    }
    else if (right && selectedButton == BTN_TX_UPDATE) {
      selectedButton = BTN_GAME;
      uiNeedsRedraw = true;
      didInput = true;
      userActive = true;
    }

    if (select) {

      if (selectedButton == BTN_BACK) setScreen(SCREEN_MAIN);
      else if (selectedButton == BTN_CTRL) setScreen(SCREEN_CONTROLLER_SETTINGS);
      else if (selectedButton == BTN_MODEL) setScreen(SCREEN_MODEL_SETTINGS);
      else if (selectedButton == BTN_DISPLAY_SETTINGS) setScreen(SCREEN_DISPLAY_SETTINGS);
      else if (selectedButton == BTN_TX_UPDATE) setScreen(SCREEN_TX_UPDATE);
      else if (selectedButton == BTN_GAME) setScreen(SCREEN_GAMES_MENU);

      selectedButton = BTN_NONE;
      didInput = true;
      userActive = true;
    }
  }

  else if (currentScreen == SCREEN_GAMES_MENU) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      ButtonID order[GAME_MENU_ITEMS_PER_PAGE + 2];
      int orderCount = 0;
      int firstIndex = gamesMenuPage * GAME_MENU_ITEMS_PER_PAGE;
      for (int i = 0; i < GAME_MENU_ITEMS_PER_PAGE; i++) {
        ButtonID button = getGameMenuButtonForIndex(firstIndex + i);
        if (button != BTN_NONE) order[orderCount++] = button;
      }
      order[orderCount++] = BTN_PAGE_NAV;
      order[orderCount++] = BTN_BACK;
      selectedButton = cycleButtonOrder(selectedButton, order, orderCount, down);
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
      userActive = true;
    }

    if (left || right) {
      selectGameMenuPage(gamesMenuPage + (right ? 1 : -1));
      didInput = true;
      userActive = true;
    }

    if (select) {
      activateGameMenuButton(selectedButton);
      didInput = true;
      userActive = true;
    }
  }

  else if (currentScreen == SCREEN_20Q_GAME) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (twentyQGuessReady) {
      if (twentyQAnsweredFeedback) {
        if (up || down || left || right) {
          selectedButton = (selectedButton == BTN_BACK) ? BTN_20Q_RESTART : BTN_BACK;
          twentyQNeedsRedraw = true;
          uiNeedsRedraw = true;
          didInput = true;
          userActive = true;
        }
        if (select) {
          if (selectedButton == BTN_BACK) setScreen(SCREEN_GAMES_MENU);
          else resetTwentyQGame();
          fullRedraw = true;
          uiNeedsRedraw = true;
          didInput = true;
          userActive = true;
        }
      } else {
        if (left || right) {
          const ButtonID order[] = { BTN_20Q_YES, BTN_20Q_NO };
          selectedButton = cycleButtonOrder(selectedButton, order, 2, right);
          twentyQNeedsRedraw = true;
          uiNeedsRedraw = true;
          didInput = true;
          userActive = true;
        }
        if (up || down) {
          selectedButton = (selectedButton == BTN_BACK) ? BTN_20Q_YES : BTN_BACK;
          twentyQNeedsRedraw = true;
          uiNeedsRedraw = true;
          didInput = true;
          userActive = true;
        }
        if (select) {
          if (selectedButton == BTN_BACK) {
            setScreen(SCREEN_GAMES_MENU);
          } else if (selectedButton == BTN_20Q_YES) {
            twentyQAnsweredFeedback = true;
            twentyQLearnedLastRound = false;
            selectedButton = BTN_20Q_RESTART;
          } else {
            keyboardTarget = KEYBOARD_TARGET_20Q_LEARN;
            keyboardBuffer = "";
            keyboardActive = true;
            keyboardLowercase = false;
            keyboardNeedsRedraw = true;
            kbCursorRow = 1;
            kbCursorCol = 0;
          }
          twentyQNeedsRedraw = true;
          fullRedraw = true;
          uiNeedsRedraw = true;
          didInput = true;
          userActive = true;
        }
      }
    } else {
      if (left || right) {
        const ButtonID order[] = { BTN_20Q_YES, BTN_20Q_UNSURE, BTN_20Q_NO };
        selectedButton = cycleButtonOrder(selectedButton, order, 3, right);
        twentyQNeedsRedraw = true;
        uiNeedsRedraw = true;
        didInput = true;
        userActive = true;
      }
      if (up || down) {
        selectedButton = (selectedButton == BTN_BACK) ? BTN_20Q_YES : BTN_BACK;
        twentyQNeedsRedraw = true;
        uiNeedsRedraw = true;
        didInput = true;
        userActive = true;
      }
      if (select) {
        if (selectedButton == BTN_BACK) setScreen(SCREEN_GAMES_MENU);
        else if (selectedButton == BTN_20Q_YES) handleTwentyQAnswer(1);
        else if (selectedButton == BTN_20Q_NO) handleTwentyQAnswer(-1);
        else handleTwentyQAnswer(0);
        didInput = true;
        userActive = true;
      }
    }
  }

  else if (currentScreen == SCREEN_DISPLAY_SETTINGS) {
    if (down || up || left || right || select) dpadFocusVisible = true;
    bool selectPressed = (select && !displaySettingsSelectHeld);
    displaySettingsSelectHeld = select;

    if (down || up) {
      const ButtonID order[] = {
        BTN_DISPLAY_BRIGHTNESS_DEC, BTN_DISPLAY_BRIGHTNESS_INC,
        BTN_DISPLAY_TIMEOUT_DEC, BTN_DISPLAY_TIMEOUT_INC,
        BTN_DISPLAY_OFF_TIMEOUT_DEC, BTN_DISPLAY_OFF_TIMEOUT_INC,
        BTN_DISPLAY_SLEEP_DEC, BTN_DISPLAY_SLEEP_INC,
        BTN_DISPLAY_THEME_TOGGLE, BTN_BACK
      };
      selectedButton = cycleButtonOrder(selectedButton, order, 10, down);
      displaySettingsNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
      userActive = true;
    }

    if (left) {
      if (selectedButton == BTN_BACK) {
        setScreen(SCREEN_MENU);
      }
      else if (selectedButton == BTN_DISPLAY_BRIGHTNESS_INC) {
        selectedButton = BTN_DISPLAY_BRIGHTNESS_DEC;
        displaySettingsNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (selectedButton == BTN_DISPLAY_TIMEOUT_INC) {
        selectedButton = BTN_DISPLAY_TIMEOUT_DEC;
        displaySettingsNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (selectedButton == BTN_DISPLAY_OFF_TIMEOUT_INC) {
        selectedButton = BTN_DISPLAY_OFF_TIMEOUT_DEC;
        displaySettingsNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (selectedButton == BTN_DISPLAY_SLEEP_INC) {
        selectedButton = BTN_DISPLAY_SLEEP_DEC;
        displaySettingsNeedsRedraw = true;
        uiNeedsRedraw = true;
      } else if (selectedButton == BTN_DISPLAY_THEME_TOGGLE) {
        selectedButton = BTN_DISPLAY_SLEEP_INC;
        displaySettingsNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else {
        setScreen(SCREEN_MENU);
      }
      didInput = true;
      userActive = true;
    }

    if (right) {
      if (selectedButton == BTN_DISPLAY_BRIGHTNESS_DEC) {
        selectedButton = BTN_DISPLAY_BRIGHTNESS_INC;
      }
      else if (selectedButton == BTN_DISPLAY_TIMEOUT_DEC) {
        selectedButton = BTN_DISPLAY_TIMEOUT_INC;
      }
      else if (selectedButton == BTN_DISPLAY_OFF_TIMEOUT_DEC) {
        selectedButton = BTN_DISPLAY_OFF_TIMEOUT_INC;
      }
      else if (selectedButton == BTN_DISPLAY_SLEEP_DEC) {
        selectedButton = BTN_DISPLAY_SLEEP_INC;
      }
      else if (selectedButton == BTN_DISPLAY_SLEEP_INC) {
        selectedButton = BTN_DISPLAY_THEME_TOGGLE;
      }
      displaySettingsNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
      userActive = true;
    }

    if (selectPressed) {
      if (selectedButton == BTN_BACK) {
        setScreen(SCREEN_MENU);
      }
      else if (selectedButton == BTN_DISPLAY_BRIGHTNESS_DEC) {
        stepDisplayOption(displayBrightness, displayBrightnessOptions, displayBrightnessOptionCount, -1);
        displaySleepBrightness = min(displaySleepBrightness, displayBrightness);
        saveDisplaySettings();
        applyDisplayBacklight();
        displaySettingsNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (selectedButton == BTN_DISPLAY_BRIGHTNESS_INC) {
        stepDisplayOption(displayBrightness, displayBrightnessOptions, displayBrightnessOptionCount, 1);
        saveDisplaySettings();
        applyDisplayBacklight();
        displaySettingsNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (selectedButton == BTN_DISPLAY_TIMEOUT_DEC) {
        displayTimeoutIndex = constrain((int)displayTimeoutIndex - 1, 0, displayTimeoutOptionCount - 1);
        saveDisplaySettings();
        displaySettingsNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (selectedButton == BTN_DISPLAY_TIMEOUT_INC) {
        displayTimeoutIndex = constrain((int)displayTimeoutIndex + 1, 0, displayTimeoutOptionCount - 1);
        saveDisplaySettings();
        displaySettingsNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (selectedButton == BTN_DISPLAY_OFF_TIMEOUT_DEC) {
        displayOffTimeoutIndex = constrain((int)displayOffTimeoutIndex - 1, 0, displayOffTimeoutOptionCount - 1);
        saveDisplaySettings();
        displaySettingsNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (selectedButton == BTN_DISPLAY_OFF_TIMEOUT_INC) {
        displayOffTimeoutIndex = constrain((int)displayOffTimeoutIndex + 1, 0, displayOffTimeoutOptionCount - 1);
        saveDisplaySettings();
        displaySettingsNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (selectedButton == BTN_DISPLAY_SLEEP_DEC) {
        stepDisplayOption(displaySleepBrightness, displaySleepBrightnessOptions, displaySleepBrightnessOptionCount, -1);
        saveDisplaySettings();
        applyDisplayBacklight();
        displaySettingsNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (selectedButton == BTN_DISPLAY_SLEEP_INC) {
        stepDisplayOption(displaySleepBrightness, displaySleepBrightnessOptions, displaySleepBrightnessOptionCount, 1);
        saveDisplaySettings();
        applyDisplayBacklight();
        displaySettingsNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (selectedButton == BTN_DISPLAY_THEME_TOGGLE) {
        themeMode = (themeMode == THEME_DARK) ? THEME_LIGHT : THEME_DARK;
        applyThemePalette();
        saveDisplaySettings();
        fullRedraw = true;
        uiNeedsRedraw = true;
        topBarNeedsRedraw = true;
      }

      didInput = true;
      userActive = true;
    }
  }
  else if (currentScreen == SCREEN_CONTROLLER_SETTINGS) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      if (controllerSettingsPage == 0) {
        const ButtonID order[] = {BTN_TRIM, BTN_FAILSAFE, BTN_ENDPOINTS, BTN_PAGE_NAV, BTN_BACK};
        selectedButton = cycleButtonOrder(selectedButton, order, 5, down);
      } else if (controllerSettingsPage == 1) {
        const ButtonID order[] = {BTN_EXPO, BTN_RATES, BTN_PROTOCOL, BTN_PAGE_NAV, BTN_BACK};
        selectedButton = cycleButtonOrder(selectedButton, order, 5, down);
      } else if (STICK_CAL_SCREEN_ENABLED) {
        const ButtonID order[] = {BTN_STICK_CAL, BTN_PAGE_NAV, BTN_BACK};
        selectedButton = cycleButtonOrder(selectedButton, order, 3, down);
      } else {
        selectedButton = BTN_BACK;
      }
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left) {
      if (selectedButton == BTN_PAGE_NAV) selectedButton = BTN_BACK;
      else if (selectedButton != BTN_BACK) selectedButton = BTN_BACK;
      else setScreen(SCREEN_MENU);
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (right) {
      if (selectedButton == BTN_BACK) selectedButton = BTN_PAGE_NAV;
      else if (selectedButton != BTN_PAGE_NAV) selectedButton = BTN_PAGE_NAV;
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (select) {
      if (selectedButton == BTN_BACK) setScreen(SCREEN_MENU);
      else if (selectedButton == BTN_PAGE_NAV) {
        controllerSettingsPage = (controllerSettingsPage + 1) % controllerSettingsPageCount;
        selectedButton = getControllerSettingsDefaultButtonForPage(controllerSettingsPage);
        fullRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (selectedButton == BTN_TRIM) setScreen(SCREEN_TRIM);
      else if (selectedButton == BTN_FAILSAFE) setScreen(SCREEN_FAILSAFE);
      else if (selectedButton == BTN_ENDPOINTS) setScreen(SCREEN_ENDPOINTS);
      else if (selectedButton == BTN_EXPO) setScreen(SCREEN_EXPO);
      else if (selectedButton == BTN_RATES) setScreen(SCREEN_RATES);
      else if (selectedButton == BTN_PROTOCOL) setScreen(SCREEN_PROTOCOL);
      else if (STICK_CAL_SCREEN_ENABLED && selectedButton == BTN_STICK_CAL) setScreen(SCREEN_STICK_CALIBRATION);
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_STICK_CALIBRATION) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      focusIndex = cycleFocusIndex(focusIndex, 2, down);
      stickCalibrationNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left) {
      setScreen(SCREEN_CONTROLLER_SETTINGS);
      didInput = true;
    }

    if (select) {
      if (focusIndex == 1) {
        setScreen(SCREEN_CONTROLLER_SETTINGS);
      } else if (stickCalibrationState == STICK_CAL_STATE_CAPTURE_CENTER) {
        captureStickCalibrationCenter();
      } else if (finalizeStickCalibration()) {
        setScreen(SCREEN_CONTROLLER_SETTINGS);
      }
      stickCalibrationNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_ENDPOINTS) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      focusIndex = cycleFocusIndex(focusIndex, CHANNEL_COUNT + 1, down);
      endpointNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left && focusIndex == CHANNEL_COUNT) {
      setScreen(SCREEN_CONTROLLER_SETTINGS);
      didInput = true;
    }

    if (select) {
      if (focusIndex == CHANNEL_COUNT) {
        setScreen(SCREEN_CONTROLLER_SETTINGS);
      } else {
        endpointNumpadChannel = focusIndex;
        endpointNumpadHighSide = isEndpointHighSideSelected(focusIndex);
        openMixNumpad(NUMPAD_TARGET_ENDPOINT_VALUE);
      }
      endpointNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_EXPO) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    const int expoValueFocus = CHANNEL_COUNT + 1;
    if (down || up) {
      focusIndex = cycleFocusIndex(focusIndex, CHANNEL_COUNT + 2, down);
      expoNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left) {
      if (focusIndex == 0) {
        setScreen(SCREEN_CONTROLLER_SETTINGS);
        didInput = true;
      }
      else if (focusIndex >= 1 && focusIndex <= CHANNEL_COUNT) {
        selectedExpoChannel = (selectedExpoChannel + CHANNEL_COUNT - 1) % CHANNEL_COUNT;
        focusIndex = selectedExpoChannel + 1;
        expoNeedsRedraw = true;
        uiNeedsRedraw = true;
        didInput = true;
      }
      else if (focusIndex == expoValueFocus) {
        setModelExpoValue(activeModel, selectedExpoChannel,
                          getModelExpoValue(activeModel, selectedExpoChannel) - 1);
        saveExpoValues();
        expoNeedsRedraw = true;
        uiNeedsRedraw = true;
        didInput = true;
      }
    }

    if (right) {
      if (focusIndex >= 1 && focusIndex <= CHANNEL_COUNT) {
        selectedExpoChannel = (selectedExpoChannel + 1) % CHANNEL_COUNT;
        focusIndex = selectedExpoChannel + 1;
        expoNeedsRedraw = true;
        uiNeedsRedraw = true;
        didInput = true;
      }
      else if (focusIndex == expoValueFocus) {
        setModelExpoValue(activeModel, selectedExpoChannel,
                          getModelExpoValue(activeModel, selectedExpoChannel) + 1);
        saveExpoValues();
        expoNeedsRedraw = true;
        uiNeedsRedraw = true;
        didInput = true;
      }
    }

    if (select) {
      if (focusIndex == 0) {
        setScreen(SCREEN_CONTROLLER_SETTINGS);
      }
      else if (focusIndex >= 1 && focusIndex <= CHANNEL_COUNT) {
        selectedExpoChannel = focusIndex - 1;
        expoNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (focusIndex == expoValueFocus) {
        openMixNumpad(NUMPAD_TARGET_EXPO_VALUE);
      }
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_RATES) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    const int rateLowFocus = CHANNEL_COUNT + 1;
    const int rateNormalFocus = CHANNEL_COUNT + 2;
    const int rateHighFocus = CHANNEL_COUNT + 3;
    const int rateValueFocus = CHANNEL_COUNT + 4;
    if (down || up) {
      focusIndex = cycleFocusIndex(focusIndex, CHANNEL_COUNT + 5, down);
      ratesNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left) {
      if (focusIndex == 0) {
        setScreen(SCREEN_CONTROLLER_SETTINGS);
        didInput = true;
      }
      else if (focusIndex >= 1 && focusIndex <= CHANNEL_COUNT) {
        selectedRateChannel = (selectedRateChannel + CHANNEL_COUNT - 1) % CHANNEL_COUNT;
        focusIndex = selectedRateChannel + 1;
        ratesNeedsRedraw = true;
        uiNeedsRedraw = true;
        didInput = true;
      }
      else if (focusIndex == rateValueFocus) {
        setModelRateValue(activeModel, selectedRateChannel,
                          getModelRateValue(activeModel, selectedRateChannel) - 1);
        saveRateValues();
        ratesNeedsRedraw = true;
        uiNeedsRedraw = true;
        didInput = true;
      }
    }

    if (right) {
      if (focusIndex >= 1 && focusIndex <= CHANNEL_COUNT) {
        selectedRateChannel = (selectedRateChannel + 1) % CHANNEL_COUNT;
        focusIndex = selectedRateChannel + 1;
        ratesNeedsRedraw = true;
        uiNeedsRedraw = true;
        didInput = true;
      }
      else if (focusIndex == rateValueFocus) {
        setModelRateValue(activeModel, selectedRateChannel,
                          getModelRateValue(activeModel, selectedRateChannel) + 1);
        saveRateValues();
        ratesNeedsRedraw = true;
        uiNeedsRedraw = true;
        didInput = true;
      }
    }

    if (select) {
      if (focusIndex == 0) {
        setScreen(SCREEN_CONTROLLER_SETTINGS);
      }
      else if (focusIndex >= 1 && focusIndex <= CHANNEL_COUNT) {
        selectedRateChannel = focusIndex - 1;
        ratesNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (focusIndex == rateLowFocus) {
        setModelRateValue(activeModel, selectedRateChannel, RATE_LOW_VALUE);
        saveRateValues();
        ratesNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (focusIndex == rateNormalFocus) {
        setModelRateValue(activeModel, selectedRateChannel, RATE_NORMAL_VALUE);
        saveRateValues();
        ratesNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (focusIndex == rateHighFocus) {
        setModelRateValue(activeModel, selectedRateChannel, RATE_HIGH_VALUE);
        saveRateValues();
        ratesNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (focusIndex == rateValueFocus) {
        openMixNumpad(NUMPAD_TARGET_RATE_VALUE);
      }
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_PROTOCOL) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      const ButtonID order[] = {
        BTN_PROTOCOL_ELRS, BTN_PROTOCOL_ESPNOW, BTN_PROTOCOL_BIND, BTN_BACK
      };
      selectedButton = cycleButtonOrder(selectedButton, order, 4, down);
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left) {
      if (selectedButton == BTN_PROTOCOL_ESPNOW) {
        selectedButton = BTN_PROTOCOL_ELRS;
        uiNeedsRedraw = true;
      } else if (!otaUpdateInProgress && selectedButton == BTN_BACK) {
        setScreen(SCREEN_CONTROLLER_SETTINGS);
      } else if (!otaUpdateInProgress) {
        selectedButton = BTN_BACK;
        uiNeedsRedraw = true;
      }
      didInput = true;
    }

    if (right) {
      if (selectedButton == BTN_PROTOCOL_ELRS) {
        selectedButton = BTN_PROTOCOL_ESPNOW;
        uiNeedsRedraw = true;
        didInput = true;
      }
    }

  if (select) {
      if (selectedButton == BTN_BACK) {
        if (!otaUpdateInProgress) {
          setScreen(SCREEN_CONTROLLER_SETTINGS);
        }
      } else if (selectedButton == BTN_PROTOCOL_ELRS) {
        if (!otaModeActive) {
          cancelEspNowBinding(true);
          setModelProtocol(activeModel, PROTOCOL_ELRS);
          elrsTxPowerUserOverrideActive = false;
          elrsLowPowerFallbackActive = true;
          elrsTxPowerMw = clampElrsTxPowerMw(ELRS_LOW_POWER_FALLBACK_MW);
          initElrsUart();
          sendElrsTxPowerWrite(ELRS_LOW_POWER_FALLBACK_MW, false);
          saveModels();
          fullRedraw = true;
          uiNeedsRedraw = true;
        }
      } else if (selectedButton == BTN_PROTOCOL_ESPNOW) {
        if (!otaModeActive) {
          setModelProtocol(activeModel, PROTOCOL_ESPNOW);
          stopElrsUart();
          saveModels();
          fullRedraw = true;
          uiNeedsRedraw = true;
        }
      } else if (selectedButton == BTN_PROTOCOL_BIND) {
        if (!otaModeActive) {
          if (getModelProtocol(activeModel) == PROTOCOL_ELRS) {
            setScreen(SCREEN_ELRS);
          } else {
            if (espNowBindingMode) {
              cancelEspNowBinding(true);
            } else if (modelHasBoundReceiver(activeModel)) {
              sendEspNowUnbindCommand();
            } else {
              showEspNowOutputModePrompt();
            }
          }
          fullRedraw = true;
          uiNeedsRedraw = true;
        }
      }
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_TX_UPDATE) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      const ButtonID order[] = {BTN_PROTOCOL_OTA, BTN_PROTOCOL_OTA_CFG, BTN_BACK};
      selectedButton = cycleButtonOrder(selectedButton, order, 3, down);
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left) {
      if (selectedButton == BTN_PROTOCOL_OTA_CFG) {
        selectedButton = BTN_PROTOCOL_OTA;
        uiNeedsRedraw = true;
      } else if (!otaUpdateInProgress) {
        setScreen(SCREEN_MENU);
      }
      didInput = true;
    }

    if (right && selectedButton == BTN_PROTOCOL_OTA) {
      selectedButton = BTN_PROTOCOL_OTA_CFG;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (select) {
      if (selectedButton == BTN_BACK) {
        if (!otaUpdateInProgress) {
          setScreen(SCREEN_MENU);
        }
      } else if (selectedButton == BTN_PROTOCOL_OTA) {
        if (otaModeActive) stopOtaMode();
        else startOtaMode();
        fullRedraw = true;
        uiNeedsRedraw = true;
      } else if (selectedButton == BTN_PROTOCOL_OTA_CFG) {
        if (!otaUpdateInProgress) {
          setScreen(SCREEN_OTA_SETTINGS);
        }
      }
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_OTA_SETTINGS) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      focusIndex = cycleFocusIndex(focusIndex, 4, down);
      otaSettingsNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left) {
      setScreen(SCREEN_TX_UPDATE);
      didInput = true;
    }

    if (select) {
      if (focusIndex == 0) {
        setScreen(SCREEN_TX_UPDATE);
      } else if (focusIndex == 1) {
        beginOtaFieldEdit(KEYBOARD_TARGET_OTA_STA_SSID);
      } else if (focusIndex == 2) {
        beginOtaFieldEdit(KEYBOARD_TARGET_OTA_STA_PASSWORD);
      } else if (focusIndex == 3) {
        beginOtaFieldEdit(KEYBOARD_TARGET_OTA_AP_SSID);
      }
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_ELRS) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      const ButtonID order[] = {
        BTN_ELRS_BIND, BTN_ELRS_RX_CONFIG,
        BTN_ELRS_TX_POWER_SLIDER, BTN_ELRS_TX_POWER_VALUE, BTN_BACK
      };
      selectedButton = cycleButtonOrder(selectedButton, order, 5, down);
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left) {
      if (selectedButton == BTN_ELRS_TX_POWER_SLIDER) {
        uint16_t nextPower = (uint16_t)max(ELRS_TX_POWER_MIN_MW, (int)elrsTxPowerMw - 1);
        sendElrsTxPowerWrite(nextPower);
        uiNeedsRedraw = true;
      } else if (selectedButton == BTN_ELRS_RX_CONFIG) {
        selectedButton = BTN_ELRS_BIND;
        uiNeedsRedraw = true;
      } else if (selectedButton == BTN_ELRS_TX_POWER_VALUE) {
        selectedButton = BTN_ELRS_TX_POWER_SLIDER;
        uiNeedsRedraw = true;
      } else if (selectedButton == BTN_BACK) {
        setScreen(SCREEN_PROTOCOL);
      } else {
        selectedButton = BTN_BACK;
        uiNeedsRedraw = true;
      }
      didInput = true;
    }

    if (right) {
      if (selectedButton == BTN_ELRS_BIND) {
        selectedButton = BTN_ELRS_RX_CONFIG;
        uiNeedsRedraw = true;
        didInput = true;
      } else if (selectedButton == BTN_ELRS_TX_POWER_SLIDER) {
        uint16_t nextPower = (uint16_t)min(ELRS_TX_POWER_MAX_MW, (int)elrsTxPowerMw + 1);
        sendElrsTxPowerWrite(nextPower);
        uiNeedsRedraw = true;
        didInput = true;
      } else if (selectedButton == BTN_ELRS_TX_POWER_VALUE) {
        selectedButton = BTN_ELRS_TX_POWER_SLIDER;
        uiNeedsRedraw = true;
        didInput = true;
      }
    }

    if (select) {
      if (selectedButton == BTN_BACK) {
        setScreen(SCREEN_PROTOCOL);
      } else if (selectedButton == BTN_ELRS_BIND) {
        sendElrsBindCommand();
        fullRedraw = true;
        uiNeedsRedraw = true;
      } else if (selectedButton == BTN_ELRS_RX_CONFIG) {
        beginElrsReceiverConfig();
      } else if (selectedButton == BTN_ELRS_TX_POWER_VALUE) {
        openMixNumpad(NUMPAD_TARGET_ELRS_TX_POWER);
        uiNeedsRedraw = true;
      }
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_ELRS_RX_CONFIG) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      ButtonID order[8] = {};
      int count = 0;
      if (elrsReceiverHasModelId) order[count++] = BTN_ELRS_RX_MODEL_ID;
      if (elrsReceiverPwmCount > 0) order[count++] = BTN_ELRS_RX_PWM1;
      if (elrsReceiverPwmCount > 1) order[count++] = BTN_ELRS_RX_PWM2;
      if (elrsReceiverPwmCount > 2) order[count++] = BTN_ELRS_RX_PWM3;
      if (elrsReceiverPwmCount > 3) order[count++] = BTN_ELRS_RX_PWM4;
      if (elrsReceiverPwmCount > 0) order[count++] = BTN_ELRS_RX_PUSH_FAILSAFE;
      order[count++] = BTN_ELRS_RX_SAVE;
      order[count++] = BTN_BACK;
      selectedButton = cycleButtonOrder(selectedButton, order, count, down);
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left) {
      setScreen(SCREEN_ELRS);
      didInput = true;
    }

    if (select) {
      if (selectedButton == BTN_BACK || selectedButton == BTN_NONE) {
        setScreen(SCREEN_ELRS);
      } else if (selectedButton == BTN_ELRS_RX_MODEL_ID && elrsReceiverHasModelId) {
        openMixNumpad(NUMPAD_TARGET_ELRS_RX_MODEL_ID);
      } else if (getElrsReceiverPwmIndexForButton(selectedButton) >= 0) {
        openElrsReceiverPwmNumpad(getElrsReceiverPwmIndexForButton(selectedButton));
      } else if (selectedButton == BTN_ELRS_RX_PUSH_FAILSAFE) {
        applyModelFailsafesToElrsReceiverPwm();
      } else if (selectedButton == BTN_ELRS_RX_SAVE) {
        saveElrsReceiverConfig();
      }
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_MODEL_SETTINGS) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      if (modelSettingsPage == 0) {
        const ButtonID order[] = {BTN_MODEL_NAME, BTN_DRIVE_TYPE, BTN_MIXING, BTN_PAGE_NAV, BTN_BACK};
        selectedButton = cycleButtonOrder(selectedButton, order, 5, down);
      }
      else {
        const ButtonID order[] = {BTN_REVERSE, BTN_PAGE_NAV, BTN_BACK};
        selectedButton = cycleButtonOrder(selectedButton, order, 3, down);
      }
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left) {
      if (selectedButton == BTN_PAGE_NAV) selectedButton = BTN_BACK;
      else if (selectedButton != BTN_BACK) selectedButton = BTN_BACK;
      else setScreen(SCREEN_MENU);
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (right) {
      if (selectedButton == BTN_BACK) selectedButton = BTN_PAGE_NAV;
      else if (selectedButton != BTN_PAGE_NAV) selectedButton = BTN_PAGE_NAV;
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (select) {
      if (selectedButton == BTN_BACK) setScreen(SCREEN_MENU);
      else if (selectedButton == BTN_PAGE_NAV) {
        modelSettingsPage = 1 - modelSettingsPage;
        selectedButton = (modelSettingsPage == 0) ? BTN_MODEL_NAME : BTN_REVERSE;
        fullRedraw = true;
        uiNeedsRedraw = true;
      }
      else if (selectedButton == BTN_MODEL_NAME) setScreen(SCREEN_MODEL_NAME);
      else if (selectedButton == BTN_DRIVE_TYPE) setScreen(SCREEN_DRIVE_TYPE);
      else if (selectedButton == BTN_MIXING) setScreen(SCREEN_MIXING);
      else if (selectedButton == BTN_REVERSE) setScreen(SCREEN_REVERSE);
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_DRIVE_TYPE) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      const ButtonID order[] = {BTN_DRIVE_TANK, BTN_DRIVE_CAR, BTN_DRIVE_OMNI, BTN_DRIVE_X_DRONE, BTN_BACK};
      selectedButton = cycleButtonOrder(selectedButton, order, 5, down);
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left) {
      if (selectedButton == BTN_DRIVE_CAR) selectedButton = BTN_DRIVE_TANK;
      else if (selectedButton == BTN_DRIVE_X_DRONE) selectedButton = BTN_DRIVE_OMNI;
      else if (selectedButton == BTN_BACK) setScreen(SCREEN_MODEL_SETTINGS);
      else selectedButton = BTN_BACK;
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (right) {
      if (selectedButton == BTN_DRIVE_TANK) selectedButton = BTN_DRIVE_CAR;
      else if (selectedButton == BTN_DRIVE_OMNI) selectedButton = BTN_DRIVE_X_DRONE;
      else if (selectedButton == BTN_BACK) selectedButton = BTN_DRIVE_TANK;
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (select) {
      if (selectedButton == BTN_BACK) {
        setScreen(SCREEN_MODEL_SETTINGS);
      } else {
        if (selectedButton == BTN_DRIVE_TANK) {
          currentDrive = DRIVE_TANK;
          setModelDriveType(activeModel, DRIVE_TANK);
          applyDrivePresetMixes(activeModel);
          saveModels();
          setScreen(SCREEN_TANK_MODE);
        }
        else {
          if (selectedButton == BTN_DRIVE_CAR) {
            currentDrive = DRIVE_CAR;
            setModelDriveType(activeModel, DRIVE_CAR);
            applyDrivePresetMixes(activeModel);
            saveModels();
            setScreen(SCREEN_TANK_MODE);
          }
          else if (selectedButton == BTN_DRIVE_OMNI) currentDrive = DRIVE_OMNI;
          else if (selectedButton == BTN_DRIVE_X_DRONE) currentDrive = DRIVE_X_DRONE;

          if (selectedButton != BTN_DRIVE_CAR) {
            setModelDriveType(activeModel, currentDrive);
            applyDrivePresetMixes(activeModel);
            saveModels();
            fullRedraw = true;
            uiNeedsRedraw = true;
          }
        }
      }
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_TANK_MODE) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      const ButtonID order[] = {BTN_TANK_DUAL, BTN_TANK_SINGLE, BTN_BACK};
      selectedButton = cycleButtonOrder(selectedButton, order, 3, down);
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left) {
      if (selectedButton == BTN_TANK_SINGLE) selectedButton = BTN_TANK_DUAL;
      else if (selectedButton == BTN_BACK) setScreen(SCREEN_DRIVE_TYPE);
      else selectedButton = BTN_BACK;
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (right) {
      if (selectedButton == BTN_TANK_DUAL) selectedButton = BTN_TANK_SINGLE;
      else if (selectedButton == BTN_BACK) selectedButton = BTN_TANK_DUAL;
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (select) {
      if (selectedButton == BTN_BACK) {
        setScreen(SCREEN_DRIVE_TYPE);
      } else if (selectedButton == BTN_TANK_DUAL) {
        setModelTankMode(activeModel, TANK_MODE_DUAL_STICK);
        applyDrivePresetMixes(activeModel);
        escSetupStep = 0;
        escSetupPendingLeftChannel = getModelTankLeftEscChannel(activeModel);
        saveModels();
        setScreen(SCREEN_ESC_CHANNEL_SETUP);
      } else if (selectedButton == BTN_TANK_SINGLE) {
        setModelTankMode(activeModel, TANK_MODE_RIGHT_STICK);
        applyDrivePresetMixes(activeModel);
        escSetupStep = 0;
        escSetupPendingLeftChannel = getModelTankLeftEscChannel(activeModel);
        saveModels();
        setScreen(SCREEN_ESC_CHANNEL_SETUP);
      }
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_ESC_CHANNEL_SETUP) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      const ButtonID order[] = {BTN_ESC_CH1, BTN_ESC_CH2, BTN_ESC_CH3, BTN_ESC_CH4, BTN_BACK};
      selectedButton = cycleButtonOrder(selectedButton, order, 5, down);
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left) {
      if (selectedButton == BTN_ESC_CH2) selectedButton = BTN_ESC_CH1;
      else if (selectedButton == BTN_ESC_CH4) selectedButton = BTN_ESC_CH3;
      else if (selectedButton == BTN_BACK) setScreen(SCREEN_TANK_MODE);
      else selectedButton = BTN_BACK;
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (right) {
      if (selectedButton == BTN_ESC_CH1) selectedButton = BTN_ESC_CH2;
      else if (selectedButton == BTN_ESC_CH3) selectedButton = BTN_ESC_CH4;
      else if (selectedButton == BTN_BACK) selectedButton = BTN_ESC_CH1;
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (select) {
      if (selectedButton == BTN_BACK) {
        setScreen(SCREEN_TANK_MODE);
      } else {
        int selectedChannel = 0;
        if (selectedButton == BTN_ESC_CH1) selectedChannel = 0;
        else if (selectedButton == BTN_ESC_CH2) selectedChannel = 1;
        else if (selectedButton == BTN_ESC_CH3) selectedChannel = 2;
        else if (selectedButton == BTN_ESC_CH4) selectedChannel = 3;
        else selectedChannel = -1;

        if (selectedChannel >= 0) {
          if (escSetupStep == 0) {
            escSetupPendingLeftChannel = (uint8_t)selectedChannel;
            escSetupStep = 1;
            fullRedraw = true;
            uiNeedsRedraw = true;
          } else if ((uint8_t)selectedChannel != escSetupPendingLeftChannel) {
            setModelTankEscChannels(activeModel, escSetupPendingLeftChannel, (uint8_t)selectedChannel);
            applyDrivePresetMixes(activeModel);
            escSetupStep = 0;
            saveModels();
            setScreen(SCREEN_TANK_MODE);
          }
        }
      }
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_REVERSE) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      focusIndex = cycleFocusIndex(focusIndex, CHANNEL_COUNT + 1, down);
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left && focusIndex == CHANNEL_COUNT) {
      setScreen(reverseReturnScreen);
      didInput = true;
    }

    if (select || left || right) {
      if (focusIndex == CHANNEL_COUNT) {
        setScreen(reverseReturnScreen);
      } else {
        bool linked[CHANNEL_COUNT] = {false, false, false, false};
        getLinkedReverseChannels(activeModel, focusIndex, linked);
        bool newState = select ? !models[activeModel].reverse[focusIndex] : right;

        for (int ch = 0; ch < CHANNEL_COUNT; ch++) {
          if (!linked[ch]) continue;
          models[activeModel].reverse[ch] = newState;
          reverseChannelDirty[ch] = true;
        }

        saveModels();
        reverseNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_TRIM) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      focusIndex = cycleFocusIndex(focusIndex, 7, down);
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if ((select || right || left) && focusIndex == 0) {
      setScreen(SCREEN_CONTROLLER_SETTINGS);
      didInput = true;
    }
    else if ((select || right || left) && focusIndex == 1) {
      currentTrimPage = !currentTrimPage;
      trimRenderX = models[activeModel].trimX[currentTrimPage];
      trimRenderY = models[activeModel].trimY[currentTrimPage];
      trimNeedsRedraw = true;
      trimDirty = true;
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }
    else if ((select || right || left) && focusIndex >= 2 && focusIndex <= 5) {
      if (focusIndex == 2) {
        models[activeModel].trimX[currentTrimPage] =
          constrain(models[activeModel].trimX[currentTrimPage] - 1, -50, 50);
      }
      else if (focusIndex == 3) {
        models[activeModel].trimX[currentTrimPage] =
          constrain(models[activeModel].trimX[currentTrimPage] + 1, -50, 50);
      }
      else if (focusIndex == 4) {
        models[activeModel].trimY[currentTrimPage] =
          constrain(models[activeModel].trimY[currentTrimPage] + 1, -50, 50);
      }
      else if (focusIndex == 5) {
        models[activeModel].trimY[currentTrimPage] =
          constrain(models[activeModel].trimY[currentTrimPage] - 1, -50, 50);
      }

      saveModels();
      trimRenderX = models[activeModel].trimX[currentTrimPage];
      trimRenderY = models[activeModel].trimY[currentTrimPage];
      trimDirty = true;
      trimNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }
    else if ((select || right || left) && focusIndex == 6) {
      models[activeModel].trimX[currentTrimPage] = 0;
      models[activeModel].trimY[currentTrimPage] = 0;
      saveModels();

      trimRenderX = 0;
      trimRenderY = 0;
      trimDirty = true;
      trimNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_FAILSAFE) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      focusIndex = cycleFocusIndex(focusIndex, CHANNEL_COUNT + 1, down);
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left && focusIndex == CHANNEL_COUNT) {
      setScreen(SCREEN_CONTROLLER_SETTINGS);
      didInput = true;
    }

    if (select || left || right) {
      if (focusIndex == CHANNEL_COUNT) {
        setScreen(SCREEN_CONTROLLER_SETTINGS);
      } else {
        bool newState = select ? !models[activeModel].failsafe[focusIndex] : right;
        models[activeModel].failsafe[focusIndex] = newState;
        if (newState) {
          setModelFailsafeValue(activeModel, focusIndex,
                                (int)roundf(outputChannels[focusIndex] * 100.0f));
          saveFailsafeValues();
        }
        saveModels();
        failsafeDirty[focusIndex] = true;
        failsafeNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_MODEL_NAME) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      focusIndex = cycleFocusIndex(focusIndex, 10, down);
      fullRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    if (left) {
      if (focusIndex == 0) setScreen(SCREEN_MODEL_SETTINGS);
      else {
        focusIndex = 0;
        fullRedraw = true;
        uiNeedsRedraw = true;
      }
      didInput = true;
    }

    if (select) {
      if (focusIndex == 0) {
        setScreen(SCREEN_MODEL_SETTINGS);
      }
      else if (focusIndex == 1) {
        int targetModel = (selectedModelIndex >= 0) ? selectedModelIndex : activeModel;
        keyboardBuffer = "";
        selectedModelIndex = targetModel;
        keyboardTarget = KEYBOARD_TARGET_MODEL_NAME;
        keyboardActive = true;
        keyboardLowercase = false;
        keyboardNeedsRedraw = true;
        inputBoxSelected = true;
      }
      else if (focusIndex >= 2 && focusIndex <= 5) {
        int i = focusIndex - 2;
        selectedModelIndex = i;
        if (modelNames[i].length() == 0) {
          keyboardBuffer = "";
          keyboardTarget = KEYBOARD_TARGET_MODEL_NAME;
          keyboardActive = true;
          keyboardLowercase = false;
          keyboardNeedsRedraw = true;
          inputBoxSelected = true;
          modelNameDirty = true;
          modelNameNeedsRedraw = true;
          uiNeedsRedraw = true;
        } else {
          selectModelSlot(i);
        }
      }
      else if (focusIndex >= 6 && focusIndex <= 9) {
        int i = focusIndex - 6;
        for (int j = i; j < 3; j++) {
          modelNames[j] = modelNames[j + 1];
          models[j] = models[j + 1];
          modelTankLeftEscChannel[j] = modelTankLeftEscChannel[j + 1];
          modelTankRightEscChannel[j] = modelTankRightEscChannel[j + 1];
          memcpy(boundReceiverMacs[j], boundReceiverMacs[j + 1], ESP_NOW_ETH_ALEN);
          for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
            modelFailsafeValues[j][channel] = modelFailsafeValues[j + 1][channel];
            modelRateValues[j][channel] = modelRateValues[j + 1][channel];
            modelExpoValues[j][channel] = modelExpoValues[j + 1][channel];
            modelEndpointLowValues[j][channel] = modelEndpointLowValues[j + 1][channel];
            modelEndpointHighValues[j][channel] = modelEndpointHighValues[j + 1][channel];
          }
        }
        modelNames[3] = "";
        initModelDefaults(3);
        clearBoundReceiver(3);
        clearModelFailsafeValues(3);
        clearModelRateValues(3);
        clearModelExpoValues(3);
        clearModelEndpointValues(3);
        saveModels();
        saveReceiverBindings();
        saveFailsafeValues();
        saveRateValues();
        saveExpoValues();
        saveEndpointValues();
        modelNameDirty = true;
        modelNameNeedsRedraw = true;
        uiNeedsRedraw = true;
      }
      didInput = true;
    }
  }
  else if (currentScreen == SCREEN_MIXING) {
    if (down || up || left || right || select) dpadFocusVisible = true;

    if (down || up) {
      focusIndex = cycleFocusIndex(focusIndex, 16, down);
      mixingNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }

    MixData &mix = models[activeModel].mixes[selectedMixIndex];
    uint8_t source = getMixSource(mix);
    uint8_t destination = getMixDestination(mix);
    int pageBase = getMixPageBase(selectedMixPage);

    if (left && focusIndex == 0) {
      setScreen(SCREEN_MODEL_SETTINGS);
      didInput = true;
    }
    else if ((select || left || right) && focusIndex >= 1 && focusIndex <= 4) {
      if (left && focusIndex > 1) focusIndex--;
      else if (right && focusIndex < 4) focusIndex++;
      selectedMixIndex = pageBase + (focusIndex - 1);
      mixingNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }
    else if (focusIndex == 5 && (select || left || right)) {
      mix.enabled = !mix.enabled;
      saveModels();
      mixingNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }
    else if (focusIndex == 6 && (select || left || right)) {
      if (left) source = (source + CHANNEL_COUNT - 1) % CHANNEL_COUNT;
      else source = (source + 1) % CHANNEL_COUNT;
      setMixSource(mix, source);
      if (source == destination) {
        destination = (destination + 1) % CHANNEL_COUNT;
        setMixDestination(mix, destination);
      }
      saveModels();
      mixingNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }
    else if (focusIndex == 7 && (select || left || right)) {
      if (left) destination = (destination + CHANNEL_COUNT - 1) % CHANNEL_COUNT;
      else destination = (destination + 1) % CHANNEL_COUNT;
      setMixDestination(mix, destination);
      if (destination == source) {
        destination = (destination + 1) % CHANNEL_COUNT;
        setMixDestination(mix, destination);
      }
      saveModels();
      mixingNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }
    else if (focusIndex == 8 && (select || left || right)) {
      setMixReverseLinked(mix, !isMixReverseLinked(mix));
      saveModels();
      mixingNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }
    else if (focusIndex == 9 && (select || left || right)) {
      mix.rate = constrain(mix.rate - 1, -100, 100);
      saveModels();
      mixingNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }
    else if (focusIndex == 10 && select) {
      openMixNumpad(NUMPAD_TARGET_MIX_RATE);
      didInput = true;
    }
    else if (focusIndex == 11 && (select || left || right)) {
      mix.rate = constrain(mix.rate + 1, -100, 100);
      saveModels();
      mixingNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }
    else if (focusIndex == 12 && (select || left || right)) {
      mix.offset = constrain(mix.offset - 1, -100, 100);
      saveModels();
      mixingNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }
    else if (focusIndex == 13 && select) {
      openMixNumpad(NUMPAD_TARGET_MIX_OFFSET);
      didInput = true;
    }
    else if (focusIndex == 14 && (select || left || right)) {
      mix.offset = constrain(mix.offset + 1, -100, 100);
      saveModels();
      mixingNeedsRedraw = true;
      uiNeedsRedraw = true;
      didInput = true;
    }
    else if (focusIndex == 15 && (select || left || right)) {
      toggleSelectedMixPageDebounced(millis());
      didInput = true;
    }
  }

    if (didInput) {
      if (screenAwake) {
        playAudioClick();
      }
      lastDpadTime = millis();
      userActive = true;
    }
  }

  // ===== TOUCH DETECTION =====
  bool isTouching = (touchCount > 0);

  int x = mapTouch(rawX, 0, 240, 0, 240);
  int y = mapTouch(rawY, 320, 0, 320, 0);

  x = constrain(x, 0, 240);
  y = constrain(y, 0, 320);
  bool backHoldHandled = handleBackHoldTouch(isTouching, x, y);
  if (!backHoldHandled && handleMainSwipeGesture(isTouching, x, y, now)) {
    backHoldHandled = true;
    if (isTouching) {
      userActive = true;
    }
  }

  if (isTouching &&
      currentScreen == SCREEN_MODEL_NAME &&
      pendingModelHoldIndex >= 0 &&
      !pendingModelHoldTriggered &&
      !keyboardActive &&
      (millis() - pendingModelHoldStart >= MODEL_NAME_HOLD_MS)) {
    pendingModelHoldTriggered = true;
    beginModelNameEdit(pendingModelHoldIndex);
    uiNeedsRedraw = true;
  }

    // ===== DPAD =====
  if (select || down || left || right) {

    if (!screenAwake) {
      if (suppressWakeUntilRelease) {
        touchActive = true;
        return;
      }
      lastActivityTime = millis();
      screenAwake = true;
      displayDimmed = false;
      screenOffManual = false;
      suppressWakeUntilRelease = false;
      applyDisplayBacklight();
      fullRedraw = true;
      uiNeedsRedraw = true;

      delay(100);
      return;
    }
    if (displayDimmed) {
      displayDimmed = false;
      applyDisplayBacklight();
      fullRedraw = true;
      uiNeedsRedraw = true;
    }
  userActive = true;
}

if (!isTouching && ratesValueDirty) {
  saveRateValues();
  ratesValueDirty = false;
}
if (!isTouching && expoValueDirty) {
  saveExpoValues();
  expoValueDirty = false;
}

// ===== RELEASE → ACTION =====
if (!backHoldHandled && !isTouching && waitingForRelease) {
  if (screenAwake) {
    playAudioClick();
  }

  if (currentScreen == SCREEN_MODEL_NAME && pendingModelHoldIndex >= 0) {
    if (!pendingModelHoldTriggered) {
      selectModelSlot(pendingModelHoldIndex);
    }

    pendingModelHoldIndex = -1;
    pendingModelHoldStart = 0;
    pendingModelHoldTriggered = false;
  }

  waitingForRelease = false;
  pressedButton = BTN_NONE;

  if (currentScreen == SCREEN_DRIVE_TYPE) {
    fullRedraw = true;
  }
  if (currentScreen == SCREEN_DISPLAY_SETTINGS) {
    displaySettingsNeedsRedraw = true;
  }

  if (screenChangePending) {
    if (nextScreen == SCREEN_REVERSE) {
      reverseNeedsRedraw = true;
      for (int i = 0; i < CHANNEL_COUNT; i++) reverseChannelDirty[i] = true;
    }

    if (nextScreen == SCREEN_TRIM) {
      trimNeedsRedraw = true;
      trimDirty = true;
    }

    if (nextScreen == SCREEN_FAILSAFE) {
      failsafeNeedsRedraw = true;
      for (int i = 0; i < CHANNEL_COUNT; i++) failsafeDirty[i] = true;
    }
    if (nextScreen == SCREEN_ENDPOINTS) {
      endpointNeedsRedraw = true;
    }

    if (nextScreen == SCREEN_MIXING) {
      mixingNeedsRedraw = true;
    }

    if (nextScreen != currentScreen) {
      setScreen(nextScreen);
    }

    screenChangePending = false;
  }

  uiNeedsRedraw = true;
}

if (currentScreen == SCREEN_ELRS_RX_CONFIG && elrsReceiverConfigTouchLocked) {
  if (isTouching) {
    elrsReceiverConfigNoTouchSince = 0;
  } else {
    if (elrsReceiverConfigNoTouchSince == 0) {
      elrsReceiverConfigNoTouchSince = millis();
    }
    if (millis() >= elrsReceiverConfigTouchUnlockAt &&
        millis() - elrsReceiverConfigNoTouchSince >= 900UL) {
      elrsReceiverConfigTouchLocked = false;
      Serial.println("ELRS RX config: touch unlocked after quiet release");
    }
  }
}

  // ===== DRAW =====
  bool uiFrameRedrew = false;
  bool droneWarningBlocksScreenRedraw =
    droneArmWarningVisible && currentScreen == SCREEN_MAIN;

  if (uiNeedsRedraw && droneWarningBlocksScreenRedraw) {
    // The ARM warning is modal. Do not let stick/output redraws repaint
    // behind it; only the warning's own dirty flags may update the display.
    uiNeedsRedraw = false;
  }

  if (uiNeedsRedraw) {
    frameCount++;

    if (millis() - fpsLastTime >= 2000) {
      frameCount = 0;
      fpsLastTime = millis();
    }

    tft.startWrite();

    if (currentScreen == SCREEN_MAIN) {

      if (fullRedraw) {
        tft.fillScreen(COLOR_BG);
        drawMainScreenStatic();
        drawModelPanelSemiStatic();
        drawTopBarStatic();
        topBarNeedsRedraw = true;
        fullRedraw = false;
      }

    drawMainScreenDynamic();
    drawMainVolumeButton();
    drawMainVolumePanel();
    lastMainFrameTime = millis();
    }
    else if (currentScreen == SCREEN_SPACE_GAME) {
      if (fullRedraw) {
        drawSpaceGameStatic();
        fullRedraw = false;
      }
      drawSpaceGameDynamic();
    }
    else if (currentScreen == SCREEN_20Q_GAME) {
      if (fullRedraw || twentyQNeedsRedraw) {
        drawTwentyQScreen();
        fullRedraw = false;
      }
    }
    else if (currentScreen == SCREEN_ASTEROIDS_GAME) {
      if (fullRedraw) {
        drawAsteroidsStatic();
        fullRedraw = false;
      }
      drawAsteroidsDynamic();
    }
    else if (currentScreen == SCREEN_STAR_SWARM_GAME) {
      if (fullRedraw) {
        drawStarSwarmStatic();
        fullRedraw = false;
      }
      drawStarSwarmDynamic();
    }
    else if (currentScreen == SCREEN_DUNGEON_RUN) {
      if (fullRedraw) {
        drawDungeonRunStatic();
        fullRedraw = false;
      }
      drawDungeonRunDynamic();
    }
    else {

      if (fullRedraw) {

        switch (currentScreen) {
          case SCREEN_MENU:
          drawMenuScreen();
          break;

          case SCREEN_GAMES_MENU:
          drawGamesMenuScreen();
          break;

          case SCREEN_20Q_GAME:
          drawTwentyQScreen();
          break;

          case SCREEN_CONTROLLER_SETTINGS:
          drawControllerSettings();
          break;

          case SCREEN_EXPO:
          drawExpoStatic();
          expoNeedsRedraw = true;
          break;

          case SCREEN_RATES:
          drawRatesStatic();
          ratesNeedsRedraw = true;
          break;

          case SCREEN_PROTOCOL:
          drawProtocolScreen();
          break;

          case SCREEN_TX_UPDATE:
          drawTxUpdateScreen();
          break;

          case SCREEN_STICK_CALIBRATION:
          drawStickCalibrationScreen();
          stickCalibrationNeedsRedraw = false;
          break;

          case SCREEN_OTA_SETTINGS:
          drawOtaSettingsStatic();
          otaSettingsNeedsRedraw = true;
          break;

          case SCREEN_ELRS:
          drawElrsScreen();
          break;

          case SCREEN_ELRS_RX_CONFIG:
          drawElrsReceiverConfigScreen();
          break;

          case SCREEN_MODEL_SETTINGS:
          drawModelSettings();
          break;

          case SCREEN_REVERSE:
          drawReverseStatic();
          reverseNeedsRedraw = true;
          break;

          case SCREEN_TRIM:
          drawTrimStatic();
          fullRedraw = false;
          trimNeedsRedraw = true;
          break;

          case SCREEN_ENDPOINTS:
          drawEndpointStatic();
          endpointNeedsRedraw = true;
          break;

          case SCREEN_FAILSAFE:
          drawFailsafeStatic();
          failsafeNeedsRedraw = true;
          break;

          case SCREEN_MODEL_NAME:
          drawModelNameStatic();
          modelNameNeedsRedraw = true;
          break;

          case SCREEN_DRIVE_TYPE:
          drawDriveTypeScreen();
          break;

          case SCREEN_TANK_MODE:
          drawTankModeScreen();
          break;

          case SCREEN_ESC_CHANNEL_SETUP:
          drawEscChannelSetupScreen();
          break;

          case SCREEN_MIXING:
          drawMixingStatic();
          mixingNeedsRedraw = true;
          break;

          case SCREEN_DISPLAY_SETTINGS:
          drawDisplaySettingsStatic();
          displaySettingsNeedsRedraw = true;
          break;

          case SCREEN_TIMER:
          drawTimerScreen();
          timerStaticNeedsRedraw = false;
          timerNeedsRedraw = true;
          break;
        }

        drawTopBarStatic();
        topBarNeedsRedraw = true;
        fullRedraw = false;
        }

      if (currentScreen == SCREEN_REVERSE) {
        drawReverseDynamic();
      }
      if (currentScreen == SCREEN_RATES) {
        drawRatesDynamic();
      }
      if (currentScreen == SCREEN_EXPO) {
        drawExpoDynamic();
      }
      if (currentScreen == SCREEN_PROTOCOL) {
        drawProtocolDynamic();
      }
      if (currentScreen == SCREEN_TX_UPDATE) {
        drawTxUpdateDynamic();
      }
      if (currentScreen == SCREEN_STICK_CALIBRATION && stickCalibrationNeedsRedraw) {
        drawStickCalibrationScreen();
        stickCalibrationNeedsRedraw = false;
      }
      if (currentScreen == SCREEN_DISPLAY_SETTINGS) {
        drawDisplaySettingsDynamic();
      }
      if (currentScreen == SCREEN_OTA_SETTINGS) {
        drawOtaSettingsDynamic();
      }
      if (currentScreen == SCREEN_ELRS) {
        drawElrsDynamic();
      }
      if (currentScreen == SCREEN_ELRS_RX_CONFIG) {
        drawElrsReceiverConfigDynamic();
      }
      if (currentScreen == SCREEN_TRIM) {
        drawTrimDynamic();
      }
      if (currentScreen == SCREEN_FAILSAFE) {
      drawFailsafeDynamic();
      }
      if (currentScreen == SCREEN_ENDPOINTS) {
      drawEndpointDynamic();
      }
      if (currentScreen == SCREEN_MODEL_NAME) {
      drawModelNameDynamic();
      }
      if (currentScreen == SCREEN_MIXING) {
      drawMixingDynamic();
      }
      if (currentScreen == SCREEN_TIMER) {
      drawTimerDynamic();
      }
      if (currentScreen == SCREEN_20Q_GAME && twentyQNeedsRedraw) {
      drawTwentyQScreen();
      }
    }

    tft.endWrite();

    uiNeedsRedraw = false;
    uiFrameRedrew = true;
  }

  if (keyboardActive) {
    if (keyboardNeedsRedraw) {
      drawKeyboardStatic();
      keyboardNeedsRedraw = false;
    }
  drawKeyboardDynamic();
  }

  if (mixNumpadActive && (mixNumpadNeedsRedraw || uiFrameRedrew)) {
    drawMixNumpad();
    mixNumpadNeedsRedraw = false;
  }

  if (topBarNeedsRedraw &&
      currentScreen != SCREEN_SPLASH &&
      currentScreen != SCREEN_SPACE_GAME &&
      currentScreen != SCREEN_20Q_GAME &&
      currentScreen != SCREEN_ASTEROIDS_GAME &&
      currentScreen != SCREEN_STAR_SWARM_GAME &&
      currentScreen != SCREEN_DUNGEON_RUN) {
    tft.startWrite();
    drawTopBarDynamic();
    tft.endWrite();
    topBarNeedsRedraw = false;
  }

  if (droneArmWarningVisible &&
      (droneArmWarningStaticDirty || droneArmWarningDynamicDirty)) {
    tft.startWrite();
    drawDroneArmWarning();
    tft.endWrite();
  }

  if (espNowOutputModePromptVisible && (espNowOutputModePromptDirty || uiFrameRedrew)) {
    tft.startWrite();
    drawEspNowOutputModePrompt();
    tft.endWrite();
  }

  switch (currentScreen) {
  }
      
  // ===== TOUCH =====
  if (espNowOutputModePromptVisible) {
    handleEspNowOutputModePromptTouch(isTouching, x, y);
    if (isTouching) {
      touchActive = true;
      userActive = true;
    } else {
      touchActive = false;
    }
  }
  else if (droneArmWarningVisible) {
    handleDroneArmWarningTouch(isTouching, x, y);
    if (isTouching) {
      touchActive = true;
      userActive = true;
    } else {
      if (touchActive) delay(30);
      touchActive = false;
      if (!waitingForRelease) {
        pendingModelHoldIndex = -1;
        pendingModelHoldStart = 0;
        pendingModelHoldTriggered = false;
      }
    }
  }
  else if (isTouching) {
    if (dpadFocusVisible) {
      dpadFocusVisible = false;
      uiNeedsRedraw = true;
    }

    // ===== WAKE ONLY =====
    if (!screenAwake) {
      lastActivityTime = millis();
      screenAwake = true;
      displayDimmed = false;
      screenOffManual = false;
      applyDisplayBacklight();

      fullRedraw = true;
      uiNeedsRedraw = true;

      waitingForRelease = false;   // ✅ reset here
      touchActive = false;         // ✅ reset here

      delay(100);  // optional, but OK
      return;
    }
    if (displayDimmed) {
      displayDimmed = false;
      applyDisplayBacklight();
      fullRedraw = true;
      uiNeedsRedraw = true;
    }

  if (!backHoldHandled) {
    handleTouch(x, y);
  }
  touchActive = true;
  userActive = true;
  }
  else {
    if (touchActive) delay(30);
      touchActive = false;

    if (suppressWakeUntilRelease) {
      suppressWakeUntilRelease = false;
      waitingForRelease = false;
      pressedButton = BTN_NONE;
    }

    if (!waitingForRelease) {
      pendingModelHoldIndex = -1;
      pendingModelHoldStart = 0;
      pendingModelHoldTriggered = false;
    }
  }

  // ===== UPDATE TIMER =====
  if (userActive) {
    lastActivityTime = millis();
  }

  unsigned long idleTime = millis() - lastActivityTime;
  unsigned long dimTimeoutMs = getDisplayDimTimeoutMs();
  unsigned long offTimeoutMs = getDisplayOffTimeoutMs();
  bool shouldDim = (dimTimeoutMs > 0 && idleTime > dimTimeoutMs);
  bool shouldTurnOff = (offTimeoutMs > 0 && idleTime > offTimeoutMs);

  if (shouldTurnOff) {
    if (screenAwake) {
      screenAwake = false;
      displayDimmed = false;
      screenOffManual = false;
      applyDisplayBacklight();
    }
  }
  else {
    if (!screenAwake) {
      applyDisplayBacklight();
    }
    else {
      bool nextDimmed = shouldDim && displaySleepBrightness < displayBrightness;
      if (displayDimmed != nextDimmed) {
        displayDimmed = nextDimmed;
        applyDisplayBacklight();
      } else {
        applyDisplayBacklight();
      }
    }
  }
}

// I2C peripheral bring-up and low-level register helpers. These functions
// return false instead of blocking so the transmitter can still boot if a
// daughterboard device is missing or unplugged.
bool initAds1115() {
  ads1115PrimaryReady = isAds1115Present(ADS1115_PRIMARY_I2C_ADDR);
  ads1115AuxReady = isAds1115Present(ADS1115_AUX_I2C_ADDR);
  if (ads1115AuxReady) adsAuxProbeAbandoned = false;

  if (!ads1115PrimaryReady) {
    if (!ads1115StatusLogged || ads1115LastLoggedReady) {
      Serial.println("Primary ADS1115 at 0x48 not detected, using neutral stick inputs.");
    }
    ads1115StatusLogged = true;
    ads1115LastLoggedReady = false;
    ads1115Ready = false;
    return false;
  }

  ads1115Ready = true;
  adsPrimaryProbeAbandoned = false;
  stickFilterInitialized = false;
  adsConsecutiveReadFails = 0;
  lastStickSampleTime = 0;

  for (uint8_t channel = 0; channel < STICK_AXIS_COUNT; channel++) {
    filteredStickAxis[channel] = 0.0f;
  }

  if (!adsStickCalibrationValid) {
    const int calibrationSamples = 8;
    int32_t sums[STICK_AXIS_COUNT] = { 0, 0, 0, 0 };

    for (int sample = 0; sample < calibrationSamples; sample++) {
      for (uint8_t channel = 0; channel < STICK_AXIS_COUNT; channel++) {
        int16_t rawValue = 0;
        if (!readAds1115SingleEnded(channel, rawValue)) {
          if (!ads1115StatusLogged || ads1115LastLoggedReady) {
            Serial.println("ADS1115 calibration read failed, using neutral stick inputs.");
          }
          ads1115StatusLogged = true;
          ads1115LastLoggedReady = false;
          ads1115Ready = false;
          adsPrimaryProbeStartMs = millis();
          adsPrimaryProbeAbandoned = false;
          return false;
        }
        sums[channel] += rawValue;
      }
    }

    for (uint8_t channel = 0; channel < STICK_AXIS_COUNT; channel++) {
      adsStickCenter[channel] = (int16_t)(sums[channel] / calibrationSamples);
      adsStickMin[channel] = ADS1115_DEFAULT_MIN_COUNT;
      adsStickMax[channel] = ADS1115_JOYSTICK_MAX_COUNT;
    }

    if (!ads1115StatusLogged || !ads1115LastLoggedReady) {
      Serial.printf(
        "ADS1115 ready. 0x48 centers AIN0=%d AIN1=%d AIN2=%d AIN3=%d, 0x49=%s\n",
        adsStickCenter[0], adsStickCenter[1], adsStickCenter[2], adsStickCenter[3],
        ads1115AuxReady ? "present" : "missing"
      );
    }
  } else {
    if (!ads1115StatusLogged || !ads1115LastLoggedReady) {
      Serial.printf(
        "ADS1115 ready. 0x48 cal AIN0=%d/%d/%d AIN1=%d/%d/%d AIN2=%d/%d/%d AIN3=%d/%d/%d, 0x49=%s\n",
        adsStickMin[0], adsStickCenter[0], adsStickMax[0],
        adsStickMin[1], adsStickCenter[1], adsStickMax[1],
        adsStickMin[2], adsStickCenter[2], adsStickMax[2],
        adsStickMin[3], adsStickCenter[3], adsStickMax[3],
        ads1115AuxReady ? "present" : "missing"
      );
    }
  }
  ads1115StatusLogged = true;
  ads1115LastLoggedReady = true;
  return true;
}

bool isAds1115Present(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission() == 0;
}

// BQ25887 charger and 2S battery reporting. When the daughterboard charger is
// present, this becomes the preferred source for pack voltage and charge state.
// The legacy ADC path remains available so older hardware still shows battery
// level and charging inference.
bool initBq25887Charger() {
  Wire.beginTransmission(BQ25887_I2C_ADDR);
  if (Wire.endTransmission() != 0) {
    if (!bq25887StatusLogged || bq25887LastLoggedReady) {
      Serial.println("BQ25887 charger not detected; using legacy battery charge inference.");
    }
    bq25887Ready = false;
    bq25887AdcReady = false;
    bq25887StatusLogged = true;
    bq25887LastLoggedReady = false;
    return false;
  }

  uint8_t partInfo = 0;
  if (!readBq25887Reg(BQ25887_REG_PART_INFO, partInfo)) {
    Serial.println("BQ25887 charger detected but part-info read failed.");
    bq25887Ready = false;
    bq25887AdcReady = false;
    bq25887StatusLogged = true;
    bq25887LastLoggedReady = false;
    return false;
  }

  bq25887Ready = true;
  bq25887AdcReady = configureBq25887Adc();
  bq25887StatusLogged = true;
  bq25887LastLoggedReady = true;
  Serial.printf(
    "BQ25887 charger ready at 0x%02X, part info 0x%02X, adc=%s\n",
    BQ25887_I2C_ADDR,
    partInfo,
    bq25887AdcReady ? "on" : "unavailable"
  );
  updateBq25887Status();
  return true;
}

bool readBq25887Reg(uint8_t reg, uint8_t &value) {
  Wire.beginTransmission(BQ25887_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom((int)BQ25887_I2C_ADDR, 1) != 1) {
    return false;
  }

  value = Wire.read();
  return true;
}

bool writeBq25887Reg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(BQ25887_I2C_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readBq25887Word(uint8_t msbReg, uint16_t &value) {
  Wire.beginTransmission(BQ25887_I2C_ADDR);
  Wire.write(msbReg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom((int)BQ25887_I2C_ADDR, 2) != 2) {
    return false;
  }

  value = ((uint16_t)Wire.read() << 8) | Wire.read();
  return true;
}

// Enable the BQ25887 ADC used for pack-voltage reporting. The watchdog is
// disabled because any register write puts the charger in host mode, and the
// default watchdog would later reset this configuration.
bool configureBq25887Adc() {
  uint8_t control1 = 0;
  if (!readBq25887Reg(BQ25887_REG_CHARGER_CONTROL1, control1)) {
    return false;
  }

  // Any BQ25887 write enters host mode. Disable the charger watchdog so the
  // ADC enable bit is not reset every 40 seconds while the UI is running.
  control1 &= (uint8_t)~0x30;
  if (!writeBq25887Reg(BQ25887_REG_CHARGER_CONTROL1, control1)) {
    return false;
  }

  // ADC_EN=1, ADC_RATE=0 continuous, ADC_SAMPLE=11 default resolution.
  return writeBq25887Reg(BQ25887_REG_ADC_CONTROL, 0xB0);
}

bool readBq25887BatteryVoltage(float &voltage) {
  if (!bq25887Ready) {
    return false;
  }

  if (!bq25887AdcReady) {
    bq25887AdcReady = configureBq25887Adc();
    if (!bq25887AdcReady) {
      return false;
    }
  }

  uint16_t vbatMv = 0;
  if (!readBq25887Word(BQ25887_REG_VBAT_ADC_MSB, vbatMv) || vbatMv < 1000) {
    return false;
  }

  voltage = (float)vbatMv / 1000.0f;
  return true;
}

bool updateBq25887Status() {
  if (!bq25887Ready) {
    return false;
  }

  uint8_t status1 = 0;
  uint8_t status2 = 0;
  uint8_t fault = 0;
  if (!readBq25887Reg(BQ25887_REG_STATUS1, status1) ||
      !readBq25887Reg(BQ25887_REG_STATUS2, status2) ||
      !readBq25887Reg(BQ25887_REG_FAULT, fault)) {
    if (!bq25887StatusLogged || bq25887LastLoggedReady) {
      Serial.println("BQ25887 charger status read failed; falling back to legacy charge inference.");
    }
    bq25887Ready = false;
    bq25887AdcReady = false;
    bq25887StatusLogged = true;
    bq25887LastLoggedReady = false;
    return false;
  }

  bq25887ChargeStatus = status1 & 0x07;
  bq25887VbusStatus = (status2 >> 4) & 0x07;
  bq25887PowerGood = (status2 & 0x80) != 0;
  bq25887FaultStatus = fault;
  bq25887FaultActive = (fault & 0xD0) != 0;

  if (bq25887ChargeStatus != lastLoggedBq25887ChargeStatus ||
      bq25887VbusStatus != lastLoggedBq25887VbusStatus ||
      bq25887FaultStatus != lastLoggedBq25887FaultStatus) {
    Serial.printf(
      "BQ25887: charge=%s (%u), vbus=%s (%u), pg=%d, fault=0x%02X\n",
      getBq25887ChargeStatusName(bq25887ChargeStatus),
      (unsigned int)bq25887ChargeStatus,
      getBq25887VbusStatusName(bq25887VbusStatus),
      (unsigned int)bq25887VbusStatus,
      bq25887PowerGood ? 1 : 0,
      bq25887FaultStatus
    );
    lastLoggedBq25887ChargeStatus = bq25887ChargeStatus;
    lastLoggedBq25887VbusStatus = bq25887VbusStatus;
    lastLoggedBq25887FaultStatus = bq25887FaultStatus;
  }

  return true;
}

bool isBq25887ChargingState(uint8_t chargeStatus) {
  return chargeStatus >= 1 && chargeStatus <= 5;
}

const char* getBq25887ChargeStatusName(uint8_t chargeStatus) {
  switch (chargeStatus) {
    case 0: return "not charging";
    case 1: return "trickle";
    case 2: return "precharge";
    case 3: return "fast charge";
    case 4: return "taper";
    case 5: return "top-off";
    case 6: return "done";
    default: return "reserved";
  }
}

const char* getBq25887VbusStatusName(uint8_t vbusStatus) {
  switch (vbusStatus) {
    case 0: return "no input";
    case 1: return "USB host";
    case 2: return "USB CDP";
    case 3: return "adapter";
    case 4: return "poor source";
    case 5: return "unknown adapter";
    case 6: return "non-standard adapter";
    default: return "reserved";
  }
}

bool captureStickCalibrationCenter() {
  if (!ads1115Ready) return false;

  int16_t rawValues[STICK_AXIS_COUNT] = { 0, 0, 0, 0 };
  for (uint8_t channel = 0; channel < STICK_AXIS_COUNT; channel++) {
    if (!readAds1115SingleEnded(channel, rawValues[channel])) {
      return false;
    }
  }

  for (uint8_t channel = 0; channel < STICK_AXIS_COUNT; channel++) {
    adsStickCenter[channel] = rawValues[channel];
    adsLastRawValues[channel] = rawValues[channel];
    stickCalibrationSweepMin[channel] = rawValues[channel];
    stickCalibrationSweepMax[channel] = rawValues[channel];
  }

  stickCalibrationState = STICK_CAL_STATE_SWEEP_RANGE;
  stickCalibrationNeedsRedraw = true;
  return true;
}

void updateStickCalibrationSweep() {
  if (currentScreen != SCREEN_STICK_CALIBRATION ||
      stickCalibrationState != STICK_CAL_STATE_SWEEP_RANGE) {
    return;
  }

  for (uint8_t channel = 0; channel < STICK_AXIS_COUNT; channel++) {
    stickCalibrationSweepMin[channel] = min(stickCalibrationSweepMin[channel], adsLastRawValues[channel]);
    stickCalibrationSweepMax[channel] = max(stickCalibrationSweepMax[channel], adsLastRawValues[channel]);
  }
}

bool finalizeStickCalibration() {
  for (uint8_t channel = 0; channel < STICK_AXIS_COUNT; channel++) {
    if (stickCalibrationSweepMin[channel] >= adsStickCenter[channel] ||
        stickCalibrationSweepMax[channel] <= adsStickCenter[channel]) {
      return false;
    }
    adsStickMin[channel] = stickCalibrationSweepMin[channel];
    adsStickMax[channel] = stickCalibrationSweepMax[channel];
  }

  saveStickCalibration();
  stickFilterInitialized = false;
  stickCalibrationState = STICK_CAL_STATE_CAPTURE_CENTER;
  stickCalibrationNeedsRedraw = true;
  return true;
}

// GPIO expander helpers for the daughterboard MCP23017 at 0x20.
bool initMcp23017() {
  mcp23017Address = MCP23017_I2C_ADDR;
  Wire.beginTransmission(mcp23017Address);
  if (Wire.endTransmission() != 0) {
    Serial.println("MCP23017 not detected at 0x20, GPIO expander inputs unavailable.");
    mcp23017Ready = false;
    mcp23017LastValue = 0xFFFF;
    return false;
  }

  // The daughterboard exposes both ports on protected solder pads. Default all
  // pins to pulled-up inputs so unconnected pads read high and active-low
  // buttons/switches can be wired without extra parts.
  if (!writeMcp23017Reg(MCP23017_IODIRA_REG, 0xFF) ||
      !writeMcp23017Reg(MCP23017_IODIRB_REG, 0xFF) ||
      !writeMcp23017Reg(MCP23017_GPPUA_REG, 0xFF) ||
      !writeMcp23017Reg(MCP23017_GPPUB_REG, 0xFF)) {
    Serial.println("MCP23017 input/pullup config failed.");
    mcp23017Ready = false;
    mcp23017LastValue = 0xFFFF;
    return false;
  }

  mcp23017Ready = true;
  if (!readMcp23017(mcp23017LastValue)) {
    return false;
  }
  mcp23017ProbeAbandoned = false;
  Serial.printf("MCP23017 ready at 0x%02X for GPIO expander inputs\n", mcp23017Address);
  return true;
}

bool writeMcp23017Reg(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(mcp23017Address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readMcp23017Reg(uint8_t reg, uint8_t &value) {
  if (!mcp23017Ready) return false;

  Wire.beginTransmission(mcp23017Address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;

  if (Wire.requestFrom((int)mcp23017Address, 1) != 1) {
    return false;
  }

  value = Wire.read();
  return true;
}

bool readMcp23017(uint16_t &value) {
  uint8_t gpioA = 0xFF;
  uint8_t gpioB = 0xFF;
  if (!readMcp23017Reg(MCP23017_GPIOA_REG, gpioA) ||
      !readMcp23017Reg(MCP23017_GPIOB_REG, gpioB)) {
    mcp23017Ready = false;
    mcp23017LastValue = 0xFFFF;
    mcp23017ProbeStartMs = millis();
    mcp23017ProbeAbandoned = false;
    Serial.println("MCP23017 read failed, retrying later.");
    return false;
  }

  value = (uint16_t)gpioA | ((uint16_t)gpioB << 8);
  mcp23017LastValue = value;
  return true;
}

const char* identifyI2cDevice(uint8_t addr) {
  if (addr == MCP23017_I2C_ADDR) return "MCP23017 GPIO expander";
  if (addr == 0x38) return "touch-controller?";
  if (addr == ADS1115_PRIMARY_I2C_ADDR) return "ADS1115 primary";
  if (addr == ADS1115_AUX_I2C_ADDR) return "ADS1115 auxiliary";
  if (addr == BQ25887_I2C_ADDR) return "BQ25887 charger";
  return "unknown";
}

void printI2cStartupScan() {
  if (!I2C_STARTUP_SCAN_ENABLED) return;

  Serial.println("I2C scan (startup):");
  Serial.println("ADDR  STATUS  DEVICE");
  for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
    Wire.beginTransmission(addr);
    uint8_t error = Wire.endTransmission();
    if (error == 0) {
      Serial.printf("0x%02X  ACK     %s\n", addr, identifyI2cDevice(addr));
    } else {
      Serial.printf("0x%02X  --\n", addr);
    }
    delay(1);
  }
}

bool readAds1115SingleEnded(uint8_t channel, int16_t &value) {
  if (channel >= ADS1115_TOTAL_ANALOG_INPUTS) return false;

  uint8_t address = (channel < ADS1115_PRIMARY_ANALOG_COUNT)
    ? ADS1115_PRIMARY_I2C_ADDR
    : ADS1115_AUX_I2C_ADDR;
  uint8_t localChannel = channel % ADS1115_PRIMARY_ANALOG_COUNT;

  if (address == ADS1115_PRIMARY_I2C_ADDR && !ads1115PrimaryReady) return false;
  if (address == ADS1115_AUX_I2C_ADDR && !ads1115AuxReady) return false;

  static const uint16_t muxByChannel[4] = {
    0x4000, // AIN0 vs GND
    0x5000, // AIN1 vs GND
    0x6000, // AIN2 vs GND
    0x7000  // AIN3 vs GND
  };

  uint16_t config =
    0x8000 |
    muxByChannel[localChannel] |
    ADS1115_GAIN_4_096V |
    ADS1115_MODE_SINGLE |
    ADS1115_DATA_RATE_860 |
    ADS1115_COMP_DISABLE;

  Wire.beginTransmission(address);
  Wire.write(ADS1115_REG_CONFIG);
  Wire.write((uint8_t)(config >> 8));
  Wire.write((uint8_t)(config & 0xFF));
  if (Wire.endTransmission() != 0) {
    return false;
  }

  delay(2);

  Wire.beginTransmission(address);
  Wire.write(ADS1115_REG_CONVERSION);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom((int)address, 2) != 2) {
    return false;
  }

  value = (int16_t)((Wire.read() << 8) | Wire.read());
  adsRawInputs[channel] = value;
  return true;
}

// Convert raw ADS1115 counts into a centered -1.0..1.0 control value with a
// deadzone. Calibration data is preferred, but a sane fallback keeps sticks
// usable if calibration storage is missing.
float normalizeStickAxis(int16_t raw, int channel) {
  int16_t center = adsStickCenter[channel];
  int32_t delta = (int32_t)raw - (int32_t)center;
  int32_t minValue = adsStickMin[channel];
  int32_t maxValue = adsStickMax[channel];
  int32_t positiveSpan = max<int32_t>(1, maxValue - center);
  int32_t negativeSpan = max<int32_t>(1, center - minValue);

  if (minValue >= center || maxValue <= center) {
    int32_t estimatedTop =
      constrain((int32_t)center * 2, (int32_t)center + 1, (int32_t)ADS1115_JOYSTICK_MAX_COUNT);
    positiveSpan = max<int32_t>(1, estimatedTop - center);
    negativeSpan = max<int32_t>(1, center);
  }

  int32_t span = (delta >= 0) ? positiveSpan : negativeSpan;

  if (span < 1) span = 1;

  float normalized = (float)delta / (float)span;
  normalized = constrain(normalized, -1.0f, 1.0f);

  float magnitude = fabs(normalized);
  if (magnitude <= STICK_DEADZONE) {
    return 0.0f;
  }

  float scaled = (magnitude - STICK_DEADZONE) / (1.0f - STICK_DEADZONE);
  scaled = constrain(scaled, 0.0f, 1.0f);
  return (normalized < 0.0f) ? -scaled : scaled;
}

// Read non-stick controls from U6 and the GPIO expander, then convert them into
// spare channel values used by the model mixer. U6 AIN4/AIN5 are the preferred
// analog accessory inputs; MCP bits are kept for simple active-low switches.
void updateAccessoryInputs(unsigned long now) {
  accessoryInputChannels[0] = 0.0f;
  accessoryInputChannels[1] = 0.0f;

  if (!ads1115AuxReady && !adsAuxProbeAbandoned) {
    if (now - adsAuxProbeStartMs >= ADS1115_PROBE_TIMEOUT_MS) {
      adsAuxProbeAbandoned = true;
      Serial.println("Auxiliary ADS1115 not detected after 5 seconds; stopping reconnect attempts.");
    } else if (now - lastAdsAuxReconnectAttemptMs >= ADS1115_RECONNECT_INTERVAL_MS) {
      lastAdsAuxReconnectAttemptMs = now;
      ads1115AuxReady = isAds1115Present(ADS1115_AUX_I2C_ADDR);
      if (ads1115AuxReady) adsAuxProbeAbandoned = false;
    }
  }

  if (ads1115AuxReady) {
    int16_t auxRaw0 = 0;
    int16_t auxRaw1 = 0;
    if (readAds1115SingleEnded(4, auxRaw0)) {
      float normalized = constrain((float)auxRaw0 / (float)ADS1115_JOYSTICK_MAX_COUNT, 0.0f, 1.0f);
      adsNormalizedInputs[4] = normalized;
      accessoryInputChannels[0] = (normalized * 2.0f) - 1.0f;
    }
    if (readAds1115SingleEnded(5, auxRaw1)) {
      float normalized = constrain((float)auxRaw1 / (float)ADS1115_JOYSTICK_MAX_COUNT, 0.0f, 1.0f);
      adsNormalizedInputs[5] = normalized;
      accessoryInputChannels[1] = (normalized * 2.0f) - 1.0f;
    }
    for (uint8_t channel = 6; channel < ADS1115_TOTAL_ANALOG_INPUTS; channel++) {
      int16_t rawValue = 0;
      if (readAds1115SingleEnded(channel, rawValue)) {
        adsNormalizedInputs[channel] =
          constrain((float)rawValue / (float)ADS1115_JOYSTICK_MAX_COUNT, 0.0f, 1.0f);
      }
    }
  }

  if (mcp23017Ready) {
    uint16_t mcpValue = mcp23017LastValue;
    bool lowSide = ((mcpValue & (1U << MCP23017_SWITCH_LOW_BIT)) == 0);
    bool highSide = ((mcpValue & (1U << MCP23017_SWITCH_HIGH_BIT)) == 0);
    if (lowSide && !highSide) accessoryInputChannels[1] = -1.0f;
    else if (highSide && !lowSide) accessoryInputChannels[1] = 1.0f;
  }
}

// Main input sampler. Reads the ADS1115 gimbal channels, filters movement,
// applies the physical wiring map, then rebuilds the output channel state.
void updateStickInputs(unsigned long now) {
  if (!ads1115Ready) {
    if (!adsPrimaryProbeAbandoned &&
        now - adsPrimaryProbeStartMs >= ADS1115_PROBE_TIMEOUT_MS) {
      adsPrimaryProbeAbandoned = true;
      Serial.println("Primary ADS1115 not detected after 5 seconds; stopping reconnect attempts.");
    } else if (!adsPrimaryProbeAbandoned &&
               now - lastAdsReconnectAttemptMs >= ADS1115_RECONNECT_INTERVAL_MS) {
      lastAdsReconnectAttemptMs = now;
      initAds1115();
    }

    for (int i = 0; i < CHANNEL_COUNT; i++) {
      inputChannels[i] = 0.0f;
      driveSourceChannels[i] = 0.0f;
      outputChannels[i] = 0.0f;
    }
    leftThrottle = 0.0f;
    rightThrottle = 0.0f;
    leftY = 0.0f;
    rightY = 0.0f;
    return;
  }

  if (now - lastStickSampleTime < STICK_SAMPLE_INTERVAL_MS) {
    inputChannels[DRONE_ROLL_CHANNEL] = -filteredStickAxis[2];
    inputChannels[DRONE_PITCH_CHANNEL] = filteredStickAxis[3];
    inputChannels[DRONE_THROTTLE_CHANNEL] = -filteredStickAxis[1];
    inputChannels[DRONE_YAW_CHANNEL] = filteredStickAxis[0];
    inputChannels[4] = accessoryInputChannels[0];
    inputChannels[5] = accessoryInputChannels[1];
    updateChannelOutputs();
    return;
  }

  int16_t rawValues[STICK_AXIS_COUNT] = { 0, 0, 0, 0 };
  for (uint8_t channel = 0; channel < STICK_AXIS_COUNT; channel++) {
    if (!readAds1115SingleEnded(channel, rawValues[channel])) {
      adsConsecutiveReadFails++;
      if (adsConsecutiveReadFails >= ADS1115_MAX_CONSECUTIVE_READ_FAILS) {
        if (!ads1115StatusLogged || ads1115LastLoggedReady) {
          Serial.println("ADS1115 marked offline; using neutral inputs until reconnect");
        }
        ads1115StatusLogged = true;
        ads1115LastLoggedReady = false;
        ads1115Ready = false;
        adsPrimaryProbeStartMs = now;
        adsPrimaryProbeAbandoned = false;
      }
      return;
    }
  }
  adsConsecutiveReadFails = 0;

  for (uint8_t channel = 0; channel < STICK_AXIS_COUNT; channel++) {
    adsLastRawValues[channel] = rawValues[channel];
    adsRawInputs[channel] = rawValues[channel];
    float normalized = normalizeStickAxis(rawValues[channel], channel);
    adsNormalizedInputs[channel] = normalized;

    if (!stickFilterInitialized) {
      filteredStickAxis[channel] = normalized;
    } else {
      filteredStickAxis[channel] =
        (filteredStickAxis[channel] * (1.0f - STICK_FILTER_ALPHA)) +
        (normalized * STICK_FILTER_ALPHA);
    }
  }

  updateStickCalibrationSweep();
  stickFilterInitialized = true;
  lastStickSampleTime = now;

  // Channel map from the external ADS1115 wiring after the gimbals were
  // swapped physically:
  // CH1 = roll/right X (A2), CH2 = pitch/right Y (A3),
  // CH3 = throttle/left Y (A1), CH4 = yaw/left X (A0).
  inputChannels[DRONE_ROLL_CHANNEL] = -filteredStickAxis[2];
  inputChannels[DRONE_PITCH_CHANNEL] = filteredStickAxis[3];
  inputChannels[DRONE_THROTTLE_CHANNEL] = -filteredStickAxis[1];
  inputChannels[DRONE_YAW_CHANNEL] = filteredStickAxis[0];
  inputChannels[4] = accessoryInputChannels[0];
  inputChannels[5] = accessoryInputChannels[1];
  updateChannelOutputs();
}

bool updateStickDisplayWake(unsigned long now) {
  if (!ads1115Ready || !stickFilterInitialized) {
    displayWakeStickBaselineValid = false;
    return false;
  }

  if (!displayWakeStickBaselineValid) {
    for (uint8_t channel = 0; channel < STICK_AXIS_COUNT; channel++) {
      lastDisplayWakeStickChannels[channel] = inputChannels[channel];
    }
    displayWakeStickBaselineValid = true;
    return false;
  }

  float maxDelta = 0.0f;
  for (uint8_t channel = 0; channel < STICK_AXIS_COUNT; channel++) {
    maxDelta = max(maxDelta, fabs(inputChannels[channel] - lastDisplayWakeStickChannels[channel]));
  }

  if (maxDelta < STICK_DISPLAY_WAKE_DELTA) {
    return false;
  }

  for (uint8_t channel = 0; channel < STICK_AXIS_COUNT; channel++) {
    lastDisplayWakeStickChannels[channel] = inputChannels[channel];
  }

  lastActivityTime = now;
  if (!screenAwake || displayDimmed) {
    screenAwake = true;
    displayDimmed = false;
    screenOffManual = false;
    suppressWakeUntilRelease = false;
    applyDisplayBacklight();
    fullRedraw = true;
    uiNeedsRedraw = true;
    topBarNeedsRedraw = true;
  }
  return true;
}

float getChannelTrimOffset(int channel) {
  switch (channel) {
    case 0:
      return models[activeModel].trimX[1] / 100.0f;
    case 1:
      return models[activeModel].trimY[1] / 100.0f;
    case 2:
      return models[activeModel].trimY[0] / 100.0f;
    case 3:
      return models[activeModel].trimX[0] / 100.0f;
    default:
      return 0.0f;
  }
}

const char* getChannelAxisName(uint8_t channel) {
  switch (channel) {
    case 0: return "RX";
    case 1: return "RY";
    case 2: return "LY";
    case 3: return "LX";
    case 4: return "POT";
    case 5: return "3POS";
    default: return "--";
  }
}

// Input-to-channel pipeline helpers: trim, expo, endpoints, user mixes, and
// per-vehicle output mapping are applied in this order.
float applyExpoCurve(float value, int expoPercent) {
  float x = constrain(value, -1.0f, 1.0f);
  float expo = constrain(expoPercent, -100, 100) / 100.0f;
  float curved = (x * (1.0f - expo)) + (x * x * x * expo);
  return constrain(curved, -1.0f, 1.0f);
}

bool updateEndpointSideLatch(int channel) {
  if (channel < 0 || channel >= CHANNEL_COUNT) return false;
  uint8_t previous = endpointSideLatch[channel];
  float value = inputChannels[channel];
  if (value > 0.12f) endpointSideLatch[channel] = 1;
  else if (value < -0.12f) endpointSideLatch[channel] = 0;
  return previous != endpointSideLatch[channel];
}

bool isEndpointHighSideSelected(int channel) {
  updateEndpointSideLatch(channel);
  if (channel < 0 || channel >= CHANNEL_COUNT) return true;
  return endpointSideLatch[channel] != 0;
}

bool updateEndpointAutoFocus() {
  if (mixNumpadActive) return false;

  int strongestChannel = -1;
  float strongestMagnitude = 0.20f;
  for (int channel = 0; channel < STICK_AXIS_COUNT; channel++) {
    float magnitude = fabs(inputChannels[channel]);
    if (magnitude > strongestMagnitude + 0.02f) {
      strongestMagnitude = magnitude;
      strongestChannel = channel;
    }
  }

  if (strongestChannel < 0) {
    if (!endpointAutoFocusActive) return false;
    endpointAutoFocusActive = false;
    if (focusIndex >= 0 && focusIndex < CHANNEL_COUNT) {
      focusIndex = CHANNEL_COUNT;
      dpadFocusVisible = false;
    }
    return true;
  }

  bool changed = (focusIndex != strongestChannel) || !dpadFocusVisible || !endpointAutoFocusActive;
  focusIndex = strongestChannel;
  dpadFocusVisible = true;
  endpointAutoFocusActive = true;
  endpointSideLatch[strongestChannel] = (inputChannels[strongestChannel] >= 0.0f) ? 1 : 0;
  return changed;
}

float applyEndpointScaling(float value, int channel) {
  if (channel < 0 || channel >= CHANNEL_COUNT) return value;
  float normalized = constrain(value, -1.0f, 1.0f);
  float lowScale = getModelEndpointLowValue(activeModel, channel) / 100.0f;
  float highScale = getModelEndpointHighValue(activeModel, channel) / 100.0f;
  if (normalized >= 0.0f) {
    return constrain(normalized * highScale, -1.0f, 1.0f);
  }
  return constrain(normalized * lowScale, -1.0f, 1.0f);
}

void updateChannelOutputs() {
  float mixSourceChannels[CHANNEL_COUNT];

  for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
    float value = inputChannels[channel] + getChannelTrimOffset(channel);
    value = constrain(value, -1.0f, 1.0f);
    value = applyExpoCurve(value, getModelExpoValue(activeModel, channel));
    value = applyEndpointScaling(value, channel);

    mixSourceChannels[channel] = value;
    driveSourceChannels[channel] = value;
    outputChannels[channel] = value;
  }

  for (int mixIndex = 0; mixIndex < MIX_COUNT; mixIndex++) {
    MixData &mix = models[activeModel].mixes[mixIndex];
    if (!mix.enabled) continue;

    uint8_t source = getMixSource(mix);
    uint8_t destination = getMixDestination(mix);
    if (source >= CHANNEL_COUNT || destination >= CHANNEL_COUNT) continue;

    float mixAmount = mixSourceChannels[source] * (mix.rate / 100.0f);
    mixAmount += mix.offset / 100.0f;
    outputChannels[destination] = constrain(outputChannels[destination] + mixAmount, -1.0f, 1.0f);
  }

  for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
    if (models[activeModel].reverse[channel]) {
      outputChannels[channel] = -outputChannels[channel];
    }
  }

  rightThrottle = outputChannels[0];
  rightY = outputChannels[1];
  leftY = outputChannels[2];
  leftThrottle = outputChannels[3];
}

// Convert UI/model channel state into the actual packet values for the selected
// vehicle preset. This keeps display math and radio output using one source of
// truth while still allowing tank/car/drone-specific mappings.
void buildProtocolOutputChannels(float channels[CHANNEL_COUNT]) {
  for (int i = 0; i < CHANNEL_COUNT; i++) {
    channels[i] = outputChannels[i];
  }

  DriveType driveType = getModelDriveType(activeModel);
  TankControlMode tankMode = getModelTankMode(activeModel);

  if (driveType == DRIVE_TANK) {
    float rightTrack = 0.0f;
    float leftTrack = 0.0f;

    if (tankMode == TANK_MODE_DUAL_STICK) {
      rightTrack = driveSourceChannels[1];  // CH2 = right stick Y
      leftTrack = driveSourceChannels[2];   // CH3 = left stick Y
    } else {
      // One-stick tank control: CH2 is right-stick Y (throttle) and CH1 is
      // right-stick X (turn). Keep this aligned with the ADS/channel map.
      float throttle = driveSourceChannels[1];
      float turn = driveSourceChannels[0];
      rightTrack = constrain(throttle - turn, -1.0f, 1.0f);
      leftTrack = constrain(throttle + turn, -1.0f, 1.0f);
    }

    uint8_t leftChannel = getModelTankLeftEscChannel(activeModel);
    uint8_t rightChannel = getModelTankRightEscChannel(activeModel);
    if (leftChannel >= CHANNEL_COUNT || rightChannel >= CHANNEL_COUNT || leftChannel == rightChannel) {
      leftChannel = 1;
      rightChannel = 0;
    }

    // Respect per-channel reverse settings on the actual ESC output channels.
    if (models[activeModel].reverse[rightChannel]) {
      rightTrack = -rightTrack;
    }
    if (models[activeModel].reverse[leftChannel]) {
      leftTrack = -leftTrack;
    }

    channels[rightChannel] = rightTrack;
    channels[leftChannel] = leftTrack;
  } else if (driveType == DRIVE_CAR) {
    // Car presets keep steering and throttle on CH1/CH2 regardless of whether
    // the user prefers one-stick or split-stick steering.
    channels[0] = getCarSteeringOutput();  // CH1 -> steering
    channels[1] = getCarThrottleOutput();  // CH2 -> throttle
  } else if (driveType == DRIVE_X_DRONE) {
    // Direct flight-controller output: do not mix motors in the transmitter.
    channels[DRONE_ROLL_CHANNEL] = outputChannels[DRONE_ROLL_CHANNEL];
    channels[DRONE_PITCH_CHANNEL] = outputChannels[DRONE_PITCH_CHANNEL];
    channels[DRONE_THROTTLE_CHANNEL] = (!droneArmActive || droneSafetyThrottleLockActive)
      ? DRONE_ARM_DISARMED_VALUE
      : outputChannels[DRONE_THROTTLE_CHANNEL];
    channels[DRONE_YAW_CHANNEL] = outputChannels[DRONE_YAW_CHANNEL];
    channels[DRONE_ARM_CHANNEL] = getDroneArmOutput();
  }
}

float getVehicleDisplayOutput(const float channels[CHANNEL_COUNT], uint8_t channel) {
  if (channel >= CHANNEL_COUNT) return 0.0f;

  // Reverse is a calibration fix for motor/servo wiring. The receiver signal
  // is inverted, but the main panel should show corrected real-world motion.
  float value = channels[channel];
  if (models[activeModel].reverse[channel]) {
    value = -value;
  }
  return constrain(value, -1.0f, 1.0f);
}

// Battery monitor. Prefer the BQ25887 charger ADC/status when present; otherwise
// fall back to the Hosyond battery ADC and a voltage-rise charging heuristic.
// The resulting batteryLevel/batteryCharging values feed the top-bar icon.
void updateBatteryState() {
  unsigned long now = millis();
  if (now - lastBatterySampleTime < BATTERY_SAMPLE_INTERVAL_MS) return;
  lastBatterySampleTime = now;

  bool chargerStatusOk = updateBq25887Status();
  if (!chargerStatusOk && !bq25887Ready) {
    chargerStatusOk = initBq25887Charger();
  }
  bool chargerReportsCharging = chargerStatusOk && isBq25887ChargingState(bq25887ChargeStatus);

  uint32_t totalMv = 0;
  const int sampleCount = 8;
  for (int i = 0; i < sampleCount; i++) {
    totalMv += analogReadMilliVolts(BATTERY_ADC_PIN);
  }

  float adcVoltage = ((float)totalMv / (float)sampleCount) / 1000.0f;
  float measuredBatteryVoltage = adcVoltage * BATTERY_DIVIDER_RATIO;
  float chargerBatteryVoltage = 0.0f;
  bool chargerVoltageOk = readBq25887BatteryVoltage(chargerBatteryVoltage);
  if (chargerVoltageOk) {
    measuredBatteryVoltage = chargerBatteryVoltage;
  }

  batteryUsingBq25887Voltage = chargerVoltageOk;
  batteryPresentMinVoltage = chargerVoltageOk ? BATTERY_2S_PRESENT_MIN_V : BATTERY_PRESENT_MIN_V;
  batteryEmptyVoltage = chargerVoltageOk ? BATTERY_2S_EMPTY_V : BATTERY_EMPTY_V;
  batteryFullVoltage = chargerVoltageOk ? BATTERY_2S_FULL_V : BATTERY_FULL_V;
  batteryLowWarnVoltage = chargerVoltageOk ? BATTERY_2S_LOW_WARN_V : BATTERY_LOW_WARN_V;
  batteryCriticalSleepVoltage = chargerVoltageOk ? BATTERY_2S_CRITICAL_SLEEP_V : BATTERY_CRITICAL_SLEEP_V;

  if (!batteryFilterInitialized) {
    batteryFilteredVoltage = measuredBatteryVoltage;
    batteryChargeWindowVoltage = measuredBatteryVoltage;
    batteryChargeWindowStart = now;
    batteryFilterInitialized = true;
  } else {
    batteryFilteredVoltage = (batteryFilteredVoltage * 0.82f) + (measuredBatteryVoltage * 0.18f);
  }

  transmitterBatteryVoltage = batteryFilteredVoltage;
  batteryPresent = (transmitterBatteryVoltage >= batteryPresentMinVoltage);

  if (!batteryPresent) {
    batteryCharging = chargerReportsCharging;
    batteryChargingHoldUntil = 0;
    batteryCriticalSince = 0;
    lowBatteryWarningVisible = false;
    lowBatteryWarningScreenDrawn = false;
    batteryLevel = 0;
    batteryChargeWindowVoltage = transmitterBatteryVoltage;
    batteryChargeWindowStart = now;
    return;
  }

  batteryLevel = constrain((int)roundf(
    ((transmitterBatteryVoltage - batteryEmptyVoltage) / (batteryFullVoltage - batteryEmptyVoltage)) * 100.0f), 0, 100);

  if (batteryChargeWindowStart == 0) {
    batteryChargeWindowStart = now;
    batteryChargeWindowVoltage = transmitterBatteryVoltage;
  }

  if (now - batteryChargeWindowStart >= BATTERY_CHARGE_WINDOW_MS) {
    float deltaV = transmitterBatteryVoltage - batteryChargeWindowVoltage;
    if (deltaV >= BATTERY_CHARGING_DELTA_V) {
      batteryChargingHoldUntil = now + BATTERY_CHARGING_HOLD_MS;
    }
    batteryChargeWindowStart = now;
    batteryChargeWindowVoltage = transmitterBatteryVoltage;
  }

  batteryCharging = chargerStatusOk ? chargerReportsCharging : (batteryChargingHoldUntil > now);
}

// Low and critical battery protection. Critical voltage takes over the screen
// and then enters deep sleep to protect the pack.
bool updateBatterySafety(unsigned long now, bool userDismiss) {
  if (!batteryPresent || transmitterBatteryVoltage < 0.5f) {
    batteryCriticalSince = 0;
    lowBatteryWarningVisible = false;
    lowBatteryWarningScreenDrawn = false;
    return false;
  }

  bool critical = (transmitterBatteryVoltage <= batteryCriticalSleepVoltage && !batteryCharging);
  if (critical) {
    if (batteryCriticalSince == 0) {
      batteryCriticalSince = now;
    }

    screenAwake = true;
    displayDimmed = false;
    screenOffManual = false;
    suppressWakeUntilRelease = false;
    applyDisplayBacklight();
    drawBatterySafetyScreen(true, now);

    if (now - batteryCriticalSince >= BATTERY_CRITICAL_GRACE_MS) {
      Serial.printf("Battery critical %.2fV: entering deep sleep\n", transmitterBatteryVoltage);
      enterDeepSleepPowerOff();
    }
    return true;
  }

  batteryCriticalSince = 0;

  bool low = (transmitterBatteryVoltage <= batteryLowWarnVoltage && !batteryCharging);
  if (!low) {
    lowBatteryWarningVisible = false;
    lowBatteryWarningScreenDrawn = false;
    return false;
  }

  if (userDismiss && lowBatteryWarningVisible) {
    lowBatteryWarningVisible = false;
    lowBatteryWarningScreenDrawn = false;
    lowBatteryWarningDismissedUntil = now + BATTERY_LOW_WARNING_REPEAT_MS;
    fullRedraw = true;
    uiNeedsRedraw = true;
    return true;
  }

  if (lowBatteryWarningVisible) {
    if (!lowBatteryWarningScreenDrawn) {
      drawBatterySafetyScreen(false, now);
      lowBatteryWarningScreenDrawn = true;
    }
    return true;
  }

  if (now >= lowBatteryWarningDismissedUntil) {
    lowBatteryWarningVisible = true;
    lowBatteryWarningScreenDrawn = false;
    screenAwake = true;
    displayDimmed = false;
    screenOffManual = false;
    suppressWakeUntilRelease = false;
    applyDisplayBacklight();
    drawBatterySafetyScreen(false, now);
    lowBatteryWarningScreenDrawn = true;
    return true;
  }

  return false;
}

void drawBatterySafetyScreen(bool critical, unsigned long now) {
  tft.fillScreen(COLOR_BG);
  uint16_t panelColor = critical ? TFT_RED : COLOR_PANEL;
  uint16_t titleColor = critical ? TFT_WHITE : TFT_ORANGE;

  drawGradientControl(12, 70, 216, 168, 14, COLOR_PANEL, panelColor);
  tft.setTextFont(2);
  tft.setTextColor(titleColor, COLOR_PANEL);
  tft.drawCentreString(critical ? "CRITICAL BATTERY" : "LOW BATTERY", 120, 88, 2);

  char voltageText[32];
  snprintf(voltageText, sizeof(voltageText), "%.2fV", transmitterBatteryVoltage);
  tft.setTextFont(4);
  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  tft.drawCentreString(voltageText, 120, 120, 4);

  tft.setTextFont(2);
  if (critical) {
    unsigned long elapsed = (batteryCriticalSince == 0) ? 0 : (now - batteryCriticalSince);
    unsigned long remainingMs = (elapsed >= BATTERY_CRITICAL_GRACE_MS)
      ? 0
      : (BATTERY_CRITICAL_GRACE_MS - elapsed);
    unsigned int remainingSec = (unsigned int)((remainingMs + 999UL) / 1000UL);
    char sleepText[36];
    snprintf(sleepText, sizeof(sleepText), "Sleeping in %us", remainingSec);
    tft.setTextColor(TFT_WHITE, COLOR_PANEL);
    tft.drawCentreString(sleepText, 120, 164, 2);
    tft.drawCentreString("Protecting LiPo", 120, 190, 2);
  } else {
    tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
    tft.drawCentreString("Charge soon", 120, 164, 2);
    tft.drawCentreString("Press any key to dismiss", 120, 190, 2);
  }
}

// Idle shutdown guard. This is separate from display dim/off so the transmitter
// can fully power down when it has been left untouched.
bool updateAutoDeepSleep(unsigned long now, bool inputDetected, bool powerButtonInput) {
  if (!AUTO_DEEP_SLEEP_ENABLED || powerButtonShutdownPending) return false;
  if (otaUpdateInProgress) return false;

  if (inputDetected) {
    lastActivityTime = now;
    if (autoDeepSleepWarningVisible) {
      autoDeepSleepWarningVisible = false;
      autoDeepSleepWarningScreenDrawn = false;
      autoDeepSleepLastWarningSeconds = -1;
      if (powerButtonInput) {
        powerButtonSuppressShortPressUntilRelease = true;
      }
      screenAwake = true;
      displayDimmed = false;
      screenOffManual = false;
      suppressWakeUntilRelease = false;
      applyDisplayBacklight();
      fullRedraw = true;
      uiNeedsRedraw = true;
      topBarNeedsRedraw = true;
      return true;
    }
    return false;
  }

  unsigned long idleMs = now - lastActivityTime;
  bool blockDeepSleep = isAutoDeepSleepHardwareMissing(now);
  if (idleMs >= AUTO_DEEP_SLEEP_TIMEOUT_MS) {
    if (blockDeepSleep) {
      autoDeepSleepWarningVisible = true;
      screenAwake = true;
      displayDimmed = false;
      screenOffManual = false;
      suppressWakeUntilRelease = false;
      applyDisplayBacklight();
      drawAutoDeepSleepWarningScreen(now);
      return true;
    }
    Serial.println("Auto sleep: inactivity timeout, entering deep sleep");
    enterDeepSleepPowerOff();
    return true;
  }

  unsigned long warningStartMs = (AUTO_DEEP_SLEEP_TIMEOUT_MS > AUTO_DEEP_SLEEP_WARNING_MS)
    ? (AUTO_DEEP_SLEEP_TIMEOUT_MS - AUTO_DEEP_SLEEP_WARNING_MS)
    : 0;
  if (idleMs >= warningStartMs) {
    autoDeepSleepWarningVisible = true;
    screenAwake = true;
    displayDimmed = false;
    screenOffManual = false;
    suppressWakeUntilRelease = false;
    applyDisplayBacklight();
    drawAutoDeepSleepWarningScreen(now);
    return true;
  }

  if (autoDeepSleepWarningVisible) {
    autoDeepSleepWarningVisible = false;
    autoDeepSleepWarningScreenDrawn = false;
    autoDeepSleepLastWarningSeconds = -1;
    fullRedraw = true;
    uiNeedsRedraw = true;
  }
  return false;
}

bool isAutoDeepSleepHardwareMissing(unsigned long now) {
  bool anyDaughterboardI2c =
    ads1115PrimaryReady ||
    ads1115AuxReady ||
    mcp23017Ready ||
    bq25887Ready;

  bool anyUartTraffic =
    elrsInitialized &&
    (elrsModulePresent || lastElrsSerialRxTime > 0 || hasElrsModuleFrames());

  bool anyGpioExpander = mcp23017Ready;
  return !anyDaughterboardI2c && !anyUartTraffic && !anyGpioExpander;
}

void drawAutoDeepSleepWarningScreen(unsigned long now) {
  unsigned long idleMs = now - lastActivityTime;
  unsigned long remainingMs = (idleMs >= AUTO_DEEP_SLEEP_TIMEOUT_MS)
    ? 0
    : (AUTO_DEEP_SLEEP_TIMEOUT_MS - idleMs);
  int remainingSec = (int)((remainingMs + 999UL) / 1000UL);
  bool hardwareMissing = isAutoDeepSleepHardwareMissing(now);
  int warningKey = hardwareMissing ? -2 : remainingSec;
  static bool lastHardwareMissing = false;

  if (autoDeepSleepWarningScreenDrawn && hardwareMissing != lastHardwareMissing) {
    autoDeepSleepWarningScreenDrawn = false;
  }

  if (autoDeepSleepWarningScreenDrawn &&
      warningKey == autoDeepSleepLastWarningSeconds) {
    return;
  }

  if (!autoDeepSleepWarningScreenDrawn) {
    tft.fillScreen(COLOR_BG);
    drawGradientControl(12, 70, 216, 168, 14, COLOR_PANEL, TFT_ORANGE);
    tft.setTextFont(2);
    tft.setTextColor(TFT_ORANGE, COLOR_PANEL);
    tft.drawCentreString("INACTIVITY", 120, 88, 2);
    tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
    tft.drawCentreString(hardwareMissing ? "Shutdown disabled" : "Auto shutdown soon", 120, 126, 2);
    if (hardwareMissing) {
      tft.drawCentreString("Reconnect hardware", 120, 184, 2);
      tft.drawCentreString("or power cycle to exit", 120, 204, 2);
    } else {
      tft.drawCentreString("Touch, D-pad, button,", 120, 184, 2);
      tft.drawCentreString("or move sticks to cancel", 120, 204, 2);
    }
    autoDeepSleepWarningScreenDrawn = true;
  }

  tft.fillRect(28, 140, 184, 42, COLOR_PANEL);
  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, COLOR_PANEL);
  if (hardwareMissing) {
    tft.drawCentreString("NO I2C, UART,", 120, 142, 2);
    tft.drawCentreString("OR GPIO DETECTED", 120, 160, 2);
  } else {
    char countdownText[32];
    snprintf(countdownText, sizeof(countdownText), "Sleeping in %ds", remainingSec);
    tft.drawCentreString(countdownText, 120, 150, 2);
  }
  autoDeepSleepLastWarningSeconds = warningKey;
  lastHardwareMissing = hardwareMissing;
}

// !!!---- DRAWING ----!!!

// Shared drawing primitives. These are intentionally small wrappers around
// TFT_eSPI calls so screen code can stay focused on layout and state.
void drawThickLine(int x1, int y1, int x2, int y2, uint16_t c) {
  tft.drawLine(x1, y1, x2, y2, c);
  tft.drawLine(x1+1, y1, x2+1, y2, c);
}

void drawThickCircle(int x, int y, int r, uint16_t c) {
  tft.drawCircle(x, y, r, c);
  tft.drawCircle(x, y, r-1, c);
}

void drawThickRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
  tft.drawRoundRect(x, y, w, h, r, c);
  tft.drawRoundRect(x+1, y, w-2, h, r-1, c);
}

void drawTopLeftBevelHighlight(int x, int y, int w, int h, int radius,
                               int inset, uint16_t highlightColor) {
  int topRun = w - (2 * radius);
  int sideRun = h - (2 * radius);
  const int highlightLayers = 3;

  for (int layer = 0; layer < highlightLayers; layer++) {
    int localInset = inset + layer;
    int cornerRadius = radius - localInset;
    if (cornerRadius <= 0) {
      break;
    }

    float layerFade = 1.0f - (layer * 0.24f);
    int cornerSteps = max(8, cornerRadius * 5);
    int lastPx = -32768;
    int lastPy = -32768;

    for (int step = 1; step < cornerSteps; step++) {
      float t = (float)step / (float)cornerSteps;
      float angle = PI + (t * (PI * 0.5f));
      int px = x + radius + (int)roundf(cosf(angle) * cornerRadius);
      int py = y + radius + (int)roundf(sinf(angle) * cornerRadius);

      if (px != lastPx || py != lastPy) {
        tft.drawPixel(px, py, fadeColor(highlightColor, layerFade));
        lastPx = px;
        lastPy = py;
      }
    }

    if (topRun > 0) {
      for (int i = 0; i < topRun; i++) {
        float fade = 1.0f - ((float)i / (float)topRun);
        tft.drawPixel(
          x + radius + i,
          y + localInset,
          fadeColor(highlightColor, layerFade * fade)
        );
      }
    }

    if (sideRun > 0) {
      for (int i = 0; i < sideRun; i++) {
        float fade = 1.0f - ((float)i / (float)sideRun);
        tft.drawPixel(
          x + localInset,
          y + radius + i,
          fadeColor(highlightColor, layerFade * fade)
        );
      }
    }
  }
}

// Reusable beveled controls and buttons. Most screens use these helpers for a
// consistent Anubis UI style instead of hand-drawing each panel differently.
void drawGradientControl(int x, int y, int w, int h, int radius,
                         uint16_t baseColor, uint16_t outlineColor) {
  for (int i = 0; i < h; i++) {
    int shade = map(i, 0, h, 18, -18);

    uint8_t r = (baseColor >> 11) & 0x1F;
    uint8_t g = (baseColor >> 5)  & 0x3F;
    uint8_t b = baseColor & 0x1F;

    int nr = constrain(r + shade / 2, 0, 31);
    int ng = constrain(g + shade,     0, 63);
    int nb = constrain(b + shade / 2, 0, 31);

    uint16_t lineColor = (nr << 11) | (ng << 5) | nb;

    int xOffset = 0;
    if (radius > 0) {
      if (i < radius) {
        int dy = radius - i;
        xOffset = radius - sqrt(radius * radius - dy * dy);
      }
      else if (i >= h - radius) {
        int dy = i - (h - radius);
        xOffset = radius - sqrt(radius * radius - dy * dy);
      }
    }

    tft.drawFastHLine(x + xOffset, y + i, w - (2 * xOffset), lineColor);
  }

  tft.drawRoundRect(x, y, w, h, radius, outlineColor);

  int inset = (radius >= 8) ? 5 : 3;
  drawTopLeftBevelHighlight(x, y, w, h, radius, inset, COLOR_ACCENT_HI);

  tft.drawFastHLine(x + radius, y + h - 2, w - (2 * radius), TFT_DARKGREY);
  tft.drawFastVLine(x + w - 2, y + radius, h - (2 * radius), TFT_DARKGREY);
}

// STATIC 3D EFFECT
void drawButtonBubble(int x, int y, int w, int h,
                      const char* label,
                      bool pressed,
                      bool selected,
                      int textOffset = 40)
{
  // Keep button label sizing consistent regardless of prior screen/font state.
  tft.setTextFont(2);

  int drawY = y + (pressed ? 2 : 0);

  bool isBackNavIcon = (strcmp(label, "BACK") == 0);
  bool isCompactMenuIcon =
    (strcmp(label, "Reverse") == 0) ||
    (strcmp(label, "Trim") == 0) ||
    (strcmp(label, "Failsafe") == 0) ||
    (strcmp(label, "End Points") == 0) ||
    (strcmp(label, "Expo") == 0) ||
    (strcmp(label, "Rates") == 0) ||
    (strcmp(label, "Protocol") == 0) ||
    (strcmp(label, "Model Name") == 0) ||
    (strcmp(label, "Drive Type") == 0) ||
    (strcmp(label, "Mixing") == 0);

  int iconScale = 2;   // default
  int iconUnit = 24;

  // ===== SCALE OVERRIDE =====
  if (isBackNavIcon) {
    iconScale = 1;
    iconUnit = 18;
  }
  else if (isCompactMenuIcon) {
    iconScale = 1;
    iconUnit = 24;
  }

  int radius = BTN_RADIUS;
  int bevelInset = 5;
  int iconSafeLeftInset = max(14, radius + 3);
  int iconSafeTopInset = bevelInset + 7;
  int iconSafeBottomInset = 8;
  int iconVerticalNudge = 0;
  int iconSize = iconUnit * iconScale;
  int defaultIconX = isBackNavIcon ? (x + 10) : (x + 20);
  int iconX = max(defaultIconX, x + iconSafeLeftInset);

  if (isBackNavIcon) {
    iconSafeBottomInset = 12;
    iconVerticalNudge = -4;
  }
  else if (isCompactMenuIcon) {
    iconSafeBottomInset = 11;
    iconVerticalNudge = -3;
  }

  int iconMinY = drawY + iconSafeTopInset;
  int iconMaxY = drawY + h - iconSafeBottomInset - iconSize;
  int iconY = drawY + (h / 2) - (iconSize / 2) + iconVerticalNudge;
  if (iconMaxY >= iconMinY) {
    iconY = constrain(iconY, iconMinY, iconMaxY);
  } else {
    iconY = iconMinY;
  }

  uint16_t baseColor;
  uint16_t textColor;
  uint16_t outlineColor;

  if (pressed) {
    baseColor = COLOR_PANEL;
    textColor = COLOR_TEXT;
    outlineColor = COLOR_ACCENT;
  } 
  else if (selected) {
    baseColor = COLOR_ACCENT;
    textColor = COLOR_BG;
    outlineColor = COLOR_ACCENT_HI;
  } 
  else {
    baseColor = COLOR_PANEL;
    textColor = COLOR_TEXT;
    outlineColor = COLOR_ACCENT;
  }

  // ===== GRADIENT BASE =====
  for (int i = 0; i < h; i++) {

    int shade = map(i, 0, h, 18, -18);

    uint8_t r = (baseColor >> 11) & 0x1F;
    uint8_t g = (baseColor >> 5)  & 0x3F;
    uint8_t b = baseColor & 0x1F;

    int nr = constrain(r + shade / 2, 0, 31);
    int ng = constrain(g + shade,     0, 63);
    int nb = constrain(b + shade / 2, 0, 31);

    uint16_t lineColor = (nr << 11) | (ng << 5) | nb;

    int xOffset = 0;

    if (i < radius) {
      int dy = radius - i;
      xOffset = radius - sqrt(radius * radius - dy * dy);
    }
    else if (i >= h - radius) {
      int dy = i - (h - radius);
      xOffset = radius - sqrt(radius * radius - dy * dy);
    }

    tft.drawFastHLine(
      x + xOffset,
      drawY + i,
      w - (2 * xOffset),
      lineColor
    );
  }

  // ===== OUTLINE =====
  tft.drawRoundRect(x, drawY, w, h, radius, outlineColor);

  int inset = 5;
  drawTopLeftBevelHighlight(x, drawY, w, h, radius, inset, COLOR_ACCENT_HI);

  // ===== SHADOW =====
  tft.drawFastHLine(x + radius, drawY + h - 2, w - (2 * radius), TFT_DARKGREY);
  tft.drawFastVLine(x + w - 2, drawY + radius, h - (2 * radius), TFT_DARKGREY);

  uint16_t iconColor = selected ? COLOR_BG : COLOR_ACCENT;

  // ===== ICONS =====
  if (strcmp(label, "Controller") == 0) {
    drawControllerIcon(iconX, iconY, iconScale, iconColor);
  }
  else if (strcmp(label, "Model") == 0) {
    drawCarIcon(iconX, iconY, iconScale, iconColor);
  }
  else if (strcmp(label, "BACK") == 0) {
    if (currentScreen == SCREEN_MENU) {
      drawHomeIcon(iconX, iconY, iconScale, iconColor);
    }
    else {
      drawBackIcon(iconX, iconY, iconScale, iconColor);
    }
  }
  else if (strcmp(label, "Reverse") == 0) {
    drawReverseIcon(iconX, iconY, iconScale, iconColor);
  }
  else if (strcmp(label, "Trim") == 0) {
    drawTrimIcon(iconX, iconY, iconScale, iconColor);
  }
  else if (strcmp(label, "Failsafe") == 0) {
    drawFailsafeIcon(iconX, iconY, iconScale, iconColor);
  }
  else if (strcmp(label, "End Points") == 0) {
    drawEndpointIcon(iconX, iconY, iconScale, iconColor);
  }
  else if (strcmp(label, "Expo") == 0) {
    drawExpoIcon(iconX, iconY, iconScale, iconColor);
  }
  else if (strcmp(label, "Rates") == 0) {
    drawRatesIcon(iconX, iconY, iconScale, iconColor);
  }
  else if (strcmp(label, "Protocol") == 0) {
    drawProtocolIcon(iconX, iconY, iconScale, iconColor);
  }
  else if (strcmp(label, "Model Name") == 0) {
    drawModelNameIcon(iconX, iconY, iconScale, iconColor);
  }
  else if (strcmp(label, "Drive Type") == 0) {
    drawDriveTypeIcon(iconX, iconY, iconScale, iconColor);
  }
  else if (strcmp(label, "Mixing") == 0) {
    drawMixingIcon(iconX, iconY, iconScale, iconColor);
  }

  // ===== TEXT =====
  int16_t tx = x + textOffset;
  int16_t ty = drawY + (h / 2) - 7;

  if (textOffset < 0) {
    tx = x + ((w - tft.textWidth(label)) / 2);
  }

  tft.setTextColor(textColor);

  tft.setCursor(tx, ty);
  tft.print(label);
  tft.setCursor(tx + 1, ty);
  tft.print(label);
}

void drawDroneArmWarning() {
  if (!droneArmWarningVisible) return;

  if (droneArmWarningStaticDirty) {
    drawDroneArmWarningStatic();
    droneArmWarningStaticDirty = false;
    droneArmWarningDynamicDirty = true;
  }

  if (droneArmWarningDynamicDirty) {
    drawDroneArmWarningDynamic();
    droneArmWarningDynamicDirty = false;
  }
}

void drawDroneArmWarningStatic() {
  const int panelX = 12;
  const int panelY = 72;
  const int panelW = 216;
  const int panelH = 174;

  tft.fillRect(0, 56, 240, 204, COLOR_BG);
  drawGradientControl(panelX, panelY, panelW, panelH, 14, COLOR_PANEL, TFT_ORANGE);

  tft.setTextFont(2);
  tft.setTextColor(TFT_ORANGE, COLOR_PANEL);
  tft.drawCentreString("ARM WARNING", 120, 88, 2);

  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  tft.drawCentreString("Props removed?", 120, 118, 2);
  tft.drawCentreString("or ready to fly?", 120, 138, 2);
  tft.drawCentreString("YES arms AUX1/CH5", 120, 164, 2);
  tft.drawCentreString("NO: throttle low", 120, 184, 2);
}

void drawDroneArmWarningDynamic() {
  const int yesX = 30;
  const int noX = 132;
  const int btnY = 204;
  const int btnW = 78;
  const int btnH = 30;

  tft.fillRect(yesX - 2, btnY - 2, btnW + 4, btnH + 4, COLOR_PANEL);
  tft.fillRect(noX - 2, btnY - 2, btnW + 4, btnH + 4, COLOR_PANEL);
  drawButtonBubble(yesX, btnY, btnW, btnH, "YES", false, false, -1);
  drawButtonBubble(noX, btnY, btnW, btnH, "NO", false, false, -1);
}

bool handleDroneArmWarningTouch(bool isTouching, int x, int y) {
  if (!droneArmWarningVisible) return false;

  const int yesX = 30;
  const int noX = 132;
  const int btnY = 204;
  const int btnW = 78;
  const int btnH = 30;

  if (!isTouching) {
    droneArmWarningTouchLocked = false;
    return true;
  }

  if (droneArmWarningTouchLocked) {
    return true;
  }

  if (isInside(x, y, yesX, btnY, btnW, btnH)) {
    droneSafetyThrottleLockActive = false;
    closeDroneArmWarning();
    setDroneArmActive(true);
    waitingForRelease = true;
    return true;
  }

  if (isInside(x, y, noX, btnY, btnW, btnH)) {
    droneSafetyThrottleLockActive = true;
    closeDroneArmWarning();
    setDroneArmActive(false);
    mainOutputPanelNeedsRedraw = true;
    waitingForRelease = true;
    return true;
  }

  return true;
}

void drawMenuScreen() {
  tft.fillScreen(COLOR_BG);

  // ===== BACK BUTTON (BOTTOM LEFT) =====
  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y,
    BACK_BTN_W, BACK_BTN_H,
    "BACK",
    pressedButton == BTN_BACK,
    dpadFocusVisible && selectedButton == BTN_BACK,
    BACK_TEXT_OFFSET);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);

  // ===== MAIN BUTTONS =====
  drawButtonBubble(
    CTRL_BTN_X, 60,
    CTRL_BTN_W, CTRL_BTN_H,
    "Controller",
    pressedButton == BTN_CTRL,
    dpadFocusVisible && selectedButton == BTN_CTRL,
    100);

  drawButtonBubble(
    MODEL_BTN_X, 150,
    MODEL_BTN_W, MODEL_BTN_H,
    "Model",
    pressedButton == BTN_MODEL,
    dpadFocusVisible && selectedButton == BTN_MODEL,
    100);

  drawButtonBubble(
    DISPLAY_MENU_BTN_X, DISPLAY_MENU_BTN_Y,
    DISPLAY_MENU_BTN_W, DISPLAY_MENU_BTN_H,
    "",
    pressedButton == BTN_DISPLAY_SETTINGS,
    dpadFocusVisible && selectedButton == BTN_DISPLAY_SETTINGS,
    -1);
  int gearSize = 22;
  int gearDrawY = DISPLAY_MENU_BTN_Y + (pressedButton == BTN_DISPLAY_SETTINGS ? 2 : 0);
  drawGearIcon(
    DISPLAY_MENU_BTN_X + max(14, BTN_RADIUS + 3),
    gearDrawY + max(12, 5 + 7),
    gearSize,
    (dpadFocusVisible && selectedButton == BTN_DISPLAY_SETTINGS) ? COLOR_BG : COLOR_ACCENT);

  drawButtonBubble(
    TX_UPDATE_MENU_BTN_X, TX_UPDATE_MENU_BTN_Y,
    TX_UPDATE_MENU_BTN_W, TX_UPDATE_MENU_BTN_H,
    "",
    pressedButton == BTN_TX_UPDATE,
    dpadFocusVisible && selectedButton == BTN_TX_UPDATE,
    -1);
  int updateOffset = (pressedButton == BTN_TX_UPDATE) ? 2 : 0;
  uint16_t updateIconColor = (dpadFocusVisible && selectedButton == BTN_TX_UPDATE) ? COLOR_BG : COLOR_ACCENT;
  int cx = TX_UPDATE_MENU_BTN_X + (TX_UPDATE_MENU_BTN_W / 2);
  int cy = TX_UPDATE_MENU_BTN_Y + 24 + updateOffset;
  tft.drawFastHLine(cx - 13, cy - 7, 24, updateIconColor);
  tft.drawFastHLine(cx - 11, cy + 7, 24, updateIconColor);
  tft.fillTriangle(cx + 13, cy - 7, cx + 7, cy - 12, cx + 7, cy - 2, updateIconColor);
  tft.fillTriangle(cx - 13, cy + 7, cx - 7, cy + 2, cx - 7, cy + 12, updateIconColor);
  tft.drawCircle(cx, cy, 4, updateIconColor);

  drawGameMenuButton();
}

void drawGameMenuButton() {
  drawButtonBubble(
    GAME_BTN_X, GAME_BTN_Y,
    GAME_BTN_W, GAME_BTN_H,
    "PLAY",
    pressedButton == BTN_GAME,
    dpadFocusVisible && selectedButton == BTN_GAME,
    -1);
}

// Built-in mini games live in this sketch to keep the transmitter self-contained.
// They still use the shared inputChannels array, so stick calibration/rates stay
// consistent with the rest of the controller.
struct GameMenuEntry {
  const char *label;
  ButtonID button;
};

const GameMenuEntry gameMenuEntries[] = {
  {"Battlezone", BTN_GAME_BATTLEZONE},
  {"20Q", BTN_GAME_20Q},
  {"Asteroids", BTN_GAME_ASTEROIDS},
  {"Invaders", BTN_GAME_INVADERS},
  {"Snake", BTN_GAME_SNAKE},
  {"Maze Chase", BTN_GAME_MAZE_CHASE},
  {"Star Swarm", BTN_GAME_STAR_SWARM},
  {"Missile Defense", BTN_GAME_MISSILE_DEFENSE},
  {"Dungeon Run", BTN_GAME_DUNGEON_RUN},
  {"Scrolling Shooter", BTN_GAME_SCROLL_SHOOTER}
};
const int gameMenuEntryCount = sizeof(gameMenuEntries) / sizeof(gameMenuEntries[0]);
const int gameMenuRowY[GAME_MENU_ITEMS_PER_PAGE] = {
  GAME_MENU_BATTLEZONE_Y,
  GAME_MENU_20Q_Y,
  GAME_MENU_ASTEROIDS_Y,
  GAME_MENU_INVADERS_Y
};

int gamesMenuPageCount() {
  return (gameMenuEntryCount + GAME_MENU_ITEMS_PER_PAGE - 1) / GAME_MENU_ITEMS_PER_PAGE;
}

int getGameMenuIndexForButton(ButtonID button) {
  for (int i = 0; i < gameMenuEntryCount; i++) {
    if (gameMenuEntries[i].button == button) return i;
  }
  return -1;
}

ButtonID getGameMenuButtonForIndex(int index) {
  if (index < 0 || index >= gameMenuEntryCount) return BTN_NONE;
  return gameMenuEntries[index].button;
}

const char* getGameMenuLabelForIndex(int index) {
  if (index < 0 || index >= gameMenuEntryCount) return "";
  return gameMenuEntries[index].label;
}

bool isGameMenuButton(ButtonID button) {
  return getGameMenuIndexForButton(button) >= 0;
}

void selectGameMenuPage(int page) {
  int pageCount = max(1, gamesMenuPageCount());
  gamesMenuPage = page % pageCount;
  if (gamesMenuPage < 0) gamesMenuPage += pageCount;

  int firstIndex = gamesMenuPage * GAME_MENU_ITEMS_PER_PAGE;
  selectedButton = getGameMenuButtonForIndex(firstIndex);
  if (selectedButton == BTN_NONE) selectedButton = BTN_BACK;
  fullRedraw = true;
  uiNeedsRedraw = true;
}

void activateGameMenuButton(ButtonID button) {
  if (button == BTN_BACK) {
    setScreen(SCREEN_MENU);
    selectedButton = BTN_NONE;
    return;
  }

  if (button == BTN_PAGE_NAV) {
    selectGameMenuPage(gamesMenuPage + 1);
    return;
  }

  if (button == BTN_GAME_BATTLEZONE) {
    setScreen(SCREEN_SPACE_GAME);
    selectedButton = BTN_NONE;
    return;
  }

  if (button == BTN_GAME_20Q) {
    setScreen(SCREEN_20Q_GAME);
    selectedButton = BTN_NONE;
    return;
  }

  if (button == BTN_GAME_ASTEROIDS) {
    setScreen(SCREEN_ASTEROIDS_GAME);
    selectedButton = BTN_NONE;
    return;
  }

  if (button == BTN_GAME_STAR_SWARM) {
    setScreen(SCREEN_STAR_SWARM_GAME);
    selectedButton = BTN_NONE;
    return;
  }

  if (button == BTN_GAME_DUNGEON_RUN) {
    setScreen(SCREEN_DUNGEON_RUN);
    selectedButton = BTN_NONE;
    return;
  }

  if (isGameMenuButton(button)) {
    dpadFocusVisible = true;
    fullRedraw = true;
    uiNeedsRedraw = true;
  }
}

void drawGamesMenuScreen() {
  tft.fillScreen(COLOR_BG);

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y,
    BACK_BTN_W, BACK_BTN_H,
    "BACK",
    pressedButton == BTN_BACK,
    dpadFocusVisible && selectedButton == BTN_BACK,
    BACK_TEXT_OFFSET);

  drawButtonBubble(GAME_MENU_PAGE_BTN_X, GAME_MENU_PAGE_BTN_Y,
                   GAME_MENU_PAGE_BTN_W, GAME_MENU_PAGE_BTN_H,
                   "PAGE",
                   pressedButton == BTN_PAGE_NAV,
                   dpadFocusVisible && selectedButton == BTN_PAGE_NAV,
                   40);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  char title[24];
  snprintf(title, sizeof(title), "Games %d/%d", gamesMenuPage + 1, gamesMenuPageCount());
  tft.drawCentreString(title, 120, 30, 2);

  int firstIndex = gamesMenuPage * GAME_MENU_ITEMS_PER_PAGE;
  for (int row = 0; row < GAME_MENU_ITEMS_PER_PAGE; row++) {
    int entryIndex = firstIndex + row;
    if (entryIndex >= gameMenuEntryCount) break;

    const char *label = getGameMenuLabelForIndex(entryIndex);
    ButtonID button = getGameMenuButtonForIndex(entryIndex);
    int textOffset = 86;
    if (strlen(label) > 12) textOffset = 42;
    else if (strlen(label) > 9) textOffset = 58;

    drawButtonBubble(GAME_MENU_BTN_X, gameMenuRowY[row],
                     GAME_MENU_BTN_W, GAME_MENU_BTN_H,
                     label,
                     pressedButton == button,
                     dpadFocusVisible && selectedButton == button,
                     textOffset);
  }

  if (isGameMenuButton(selectedButton) &&
      selectedButton != BTN_GAME_BATTLEZONE &&
      selectedButton != BTN_GAME_20Q &&
      selectedButton != BTN_GAME_ASTEROIDS &&
      selectedButton != BTN_GAME_STAR_SWARM &&
      selectedButton != BTN_GAME_DUNGEON_RUN) {
    tft.setTextColor(COLOR_ACCENT_HI, COLOR_BG);
    tft.drawCentreString("Coming soon", 120, 258, 2);
  }
}

void drawGameExitButton(int x, int y, int w, int h, const char *label, bool pressed, bool selected) {
  int drawY = y + (pressed ? 1 : 0);
  uint16_t fill = selected ? COLOR_ACCENT : COLOR_PANEL;
  uint16_t text = selected ? COLOR_BG : COLOR_TEXT;
  tft.fillRoundRect(x, drawY, w, h, 6, fill);
  tft.drawRoundRect(x, drawY, w, h, 6, COLOR_ACCENT);
  if (selected) tft.drawRoundRect(x - 1, drawY - 1, w + 2, h + 2, 7, COLOR_ACCENT_HI);
  tft.setTextFont(2);
  tft.setTextColor(text, fill);
  tft.drawCentreString(label, x + (w / 2), drawY + 3, 2);
}

const char *twentyQQuestions[TWENTYQ_MAX_QUESTIONS] = {
  "Is it or was it alive?",
  "Is it human?",
  "Is it an animal?",
  "Is it a plant?",
  "Is it known for science or invention?",
  "Is it known for music or performance?",
  "Is it from ancient history?",
  "Is it a non-living natural material?",
  "Is it metallic?",
  "Is it clear, sparkly, or used in jewelry?",
  "Does it live in water?",
  "Can it fly?",
  "Is it commonly kept as a pet?",
  "Does it have flowers?",
  "Is it a tree?",
  "Does it live in dry desert places?",
  "Is it usually larger than a person?",
  "Is it associated with writing or literature?",
  "Is it usually yellow or gold colored?",
  "Is it soft or furry?"
};

const TwentyQCandidate twentyQCandidates[] = {
  {"Albert Einstein", "famous person", { 1, 1,-1,-1, 1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}},
  {"Cleopatra",       "famous person", { 1, 1,-1,-1,-1,-1, 1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}},
  {"William Shakespeare", "famous person", { 1, 1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 1,-1,-1}},
  {"Elvis Presley",   "famous person", { 1, 1,-1,-1,-1, 1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}},
  {"Amelia Earhart",  "famous person", { 1, 1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 1,-1,-1,-1,-1,-1,-1,-1,-1}},
  {"Gold",            "mineral",       {-1,-1,-1,-1,-1,-1,-1, 1, 1, 1,-1,-1,-1,-1,-1,-1,-1,-1, 1,-1}},
  {"Diamond",         "mineral",       {-1,-1,-1,-1,-1,-1,-1, 1,-1, 1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}},
  {"Quartz",          "mineral",       {-1,-1,-1,-1,-1,-1,-1, 1,-1, 1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}},
  {"Copper",          "mineral",       {-1,-1,-1,-1,-1,-1,-1, 1, 1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1}},
  {"Cat",             "animal",        { 1,-1, 1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 1,-1,-1,-1,-1,-1,-1, 1}},
  {"Dolphin",         "animal",        { 1,-1, 1,-1,-1,-1,-1,-1,-1,-1, 1,-1,-1,-1,-1,-1, 0,-1,-1,-1}},
  {"Eagle",           "animal",        { 1,-1, 1,-1,-1,-1,-1,-1,-1,-1,-1, 1,-1,-1,-1,-1,-1,-1,-1,-1}},
  {"Octopus",         "animal",        { 1,-1, 1,-1,-1,-1,-1,-1,-1,-1, 1,-1,-1,-1,-1,-1,-1,-1,-1,-1}},
  {"Rose",            "plant",         { 1,-1,-1, 1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 1,-1,-1,-1,-1,-1,-1}},
  {"Oak tree",        "plant",         { 1,-1,-1, 1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 1,-1, 1,-1,-1,-1}},
  {"Sunflower",       "plant",         { 1,-1,-1, 1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 1,-1,-1,-1,-1, 1,-1}},
  {"Cactus",          "plant",         { 1,-1,-1, 1,-1,-1,-1,-1,-1,-1,-1,-1,-1, 0,-1, 1,-1,-1,-1,-1}}
};

const int twentyQCandidateCount = sizeof(twentyQCandidates) / sizeof(twentyQCandidates[0]);

int getTwentyQTotalCandidateCount() {
  return twentyQCandidateCount + twentyQLearnedCount;
}

const char* getTwentyQCandidateName(int index) {
  if (index < twentyQCandidateCount) return twentyQCandidates[index].name;
  int learnedIndex = index - twentyQCandidateCount;
  if (learnedIndex >= 0 && learnedIndex < twentyQLearnedCount) return twentyQLearnedCandidates[learnedIndex].name;
  return "something mysterious";
}

const char* getTwentyQCandidateCategory(int index) {
  if (index < twentyQCandidateCount) return twentyQCandidates[index].category;
  return "learned answer";
}

int8_t getTwentyQCandidateAnswer(int index, int question) {
  if (question < 0 || question >= TWENTYQ_MAX_QUESTIONS) return 0;
  if (index < twentyQCandidateCount) return twentyQCandidates[index].answers[question];
  int learnedIndex = index - twentyQCandidateCount;
  if (learnedIndex >= 0 && learnedIndex < twentyQLearnedCount) return twentyQLearnedCandidates[learnedIndex].answers[question];
  return 0;
}

bool twentyQNameMatches(const char *a, const char *b) {
  if (a == nullptr || b == nullptr) return false;
  String left = String(a);
  String right = String(b);
  left.trim();
  right.trim();
  left.toLowerCase();
  right.toLowerCase();
  return left.length() > 0 && left == right;
}

bool isTwentyQDuplicateAnswer(const char *name) {
  if (name == nullptr || strlen(name) == 0) return false;
  for (int i = 0; i < getTwentyQTotalCandidateCount(); i++) {
    if (twentyQNameMatches(name, getTwentyQCandidateName(i))) return true;
  }
  return false;
}

void loadTwentyQLearnedCandidates() {
  Preferences preferences;
  twentyQLearnedCount = 0;
  if (!preferences.begin(TWENTYQ_PREF_NAMESPACE, true)) return;

  int storedCount = constrain((int)preferences.getUChar(TWENTYQ_PREF_COUNT_KEY, 0), 0, TWENTYQ_MAX_LEARNED);
  for (int i = 0; i < storedCount; i++) {
    char nameKey[6];
    char answerKey[6];
    snprintf(nameKey, sizeof(nameKey), "n%d", i);
    snprintf(answerKey, sizeof(answerKey), "a%d", i);

    String name = preferences.getString(nameKey, "");
    if (name.length() == 0) continue;

    memset(&twentyQLearnedCandidates[twentyQLearnedCount], 0, sizeof(TwentyQLearnedCandidate));
    strncpy(twentyQLearnedCandidates[twentyQLearnedCount].name, name.c_str(), TWENTYQ_LEARNED_NAME_LEN - 1);
    size_t bytesRead = preferences.getBytes(answerKey, twentyQLearnedCandidates[twentyQLearnedCount].answers, TWENTYQ_MAX_QUESTIONS);
    if (bytesRead < TWENTYQ_MAX_QUESTIONS) {
      memset(twentyQLearnedCandidates[twentyQLearnedCount].answers, 0, TWENTYQ_MAX_QUESTIONS);
    }
    twentyQLearnedCount++;
  }
  preferences.end();
}

void saveTwentyQLearnedCandidates() {
  Preferences preferences;
  if (!preferences.begin(TWENTYQ_PREF_NAMESPACE, false)) return;

  preferences.putUChar(TWENTYQ_PREF_COUNT_KEY, (uint8_t)twentyQLearnedCount);
  for (int i = 0; i < TWENTYQ_MAX_LEARNED; i++) {
    char nameKey[6];
    char answerKey[6];
    snprintf(nameKey, sizeof(nameKey), "n%d", i);
    snprintf(answerKey, sizeof(answerKey), "a%d", i);
    if (i < twentyQLearnedCount) {
      preferences.putString(nameKey, twentyQLearnedCandidates[i].name);
      preferences.putBytes(answerKey, twentyQLearnedCandidates[i].answers, TWENTYQ_MAX_QUESTIONS);
    } else {
      preferences.remove(nameKey);
      preferences.remove(answerKey);
    }
  }
  preferences.end();
}

void saveTwentyQLearnedAnswer(const char *name) {
  if (name == nullptr || strlen(name) == 0) return;
  twentyQLearnedLastRound = false;
  twentyQDuplicateLastRound = false;

  if (isTwentyQDuplicateAnswer(name)) {
    twentyQDuplicateLastRound = true;
    return;
  }

  int target = twentyQLearnedCount;
  if (target >= TWENTYQ_MAX_LEARNED) {
    for (int i = 1; i < TWENTYQ_MAX_LEARNED; i++) {
      twentyQLearnedCandidates[i - 1] = twentyQLearnedCandidates[i];
    }
    target = TWENTYQ_MAX_LEARNED - 1;
  } else {
    twentyQLearnedCount++;
  }

  memset(&twentyQLearnedCandidates[target], 0, sizeof(TwentyQLearnedCandidate));
  strncpy(twentyQLearnedCandidates[target].name, name, TWENTYQ_LEARNED_NAME_LEN - 1);
  for (int i = 0; i < TWENTYQ_MAX_QUESTIONS; i++) {
    twentyQLearnedCandidates[target].answers[i] = twentyQUserAnswers[i];
  }
  saveTwentyQLearnedCandidates();
  twentyQLearnedLastRound = true;
}

void resetTwentyQGame() {
  twentyQQuestionIndex = 0;
  for (int i = 0; i < TWENTYQ_MAX_QUESTIONS; i++) twentyQUserAnswers[i] = 0;
  twentyQGuessReady = false;
  twentyQAnsweredFeedback = false;
  twentyQLearnedLastRound = false;
  twentyQDuplicateLastRound = false;
  twentyQBestCandidate = 0;
  twentyQSecondCandidate = 0;
  twentyQBestScore = 0;
  twentyQSecondScore = 0;
  twentyQNeedsRedraw = true;
  selectedButton = BTN_20Q_YES;
}

void updateTwentyQGuess() {
  twentyQBestCandidate = 0;
  twentyQSecondCandidate = 0;
  twentyQBestScore = -32768;
  twentyQSecondScore = -32768;

  int totalCandidates = max(1, getTwentyQTotalCandidateCount());
  for (int item = 0; item < totalCandidates; item++) {
    int score = 0;
    for (int q = 0; q < twentyQQuestionIndex && q < TWENTYQ_MAX_QUESTIONS; q++) {
      int8_t userAnswer = twentyQUserAnswers[q];
      if (userAnswer == 0) continue;
      int8_t candidateAnswer = getTwentyQCandidateAnswer(item, q);
      if (candidateAnswer == 0) score += 1;
      else if (candidateAnswer == userAnswer) score += 3;
      else score -= 2;
    }

    if (score > twentyQBestScore) {
      twentyQSecondScore = twentyQBestScore;
      twentyQSecondCandidate = twentyQBestCandidate;
      twentyQBestScore = score;
      twentyQBestCandidate = item;
    } else if (score > twentyQSecondScore) {
      twentyQSecondScore = score;
      twentyQSecondCandidate = item;
    }
  }
}

void handleTwentyQAnswer(int8_t answer) {
  if (twentyQGuessReady) return;
  if (twentyQQuestionIndex < TWENTYQ_MAX_QUESTIONS) {
    twentyQUserAnswers[twentyQQuestionIndex] = answer;
    twentyQQuestionIndex++;
  }
  updateTwentyQGuess();
  if (twentyQQuestionIndex >= TWENTYQ_MAX_QUESTIONS ||
      (twentyQQuestionIndex >= 10 && (twentyQBestScore - twentyQSecondScore) >= 14)) {
    twentyQGuessReady = true;
    selectedButton = BTN_20Q_YES;
  }
  twentyQNeedsRedraw = true;
  fullRedraw = true;
  uiNeedsRedraw = true;
}

void drawWrappedCenteredText(const char *text, int x, int y, int w, int lineH, int maxLines, uint16_t fg, uint16_t bg) {
  char buffer[180];
  strncpy(buffer, text, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  tft.setTextFont(2);
  tft.setTextColor(fg, bg);

  char line[80] = "";
  int lineCount = 0;
  char *token = strtok(buffer, " ");
  while (token != nullptr && lineCount < maxLines) {
    char testLine[80];
    if (line[0] == '\0') snprintf(testLine, sizeof(testLine), "%s", token);
    else snprintf(testLine, sizeof(testLine), "%s %s", line, token);

    if (tft.textWidth(testLine) > w && line[0] != '\0') {
      tft.drawCentreString(line, x + (w / 2), y + (lineCount * lineH), 2);
      lineCount++;
      snprintf(line, sizeof(line), "%s", token);
    } else {
      snprintf(line, sizeof(line), "%s", testLine);
    }
    token = strtok(nullptr, " ");
  }

  if (line[0] != '\0' && lineCount < maxLines) {
    tft.drawCentreString(line, x + (w / 2), y + (lineCount * lineH), 2);
  }
}

void drawTwentyQScreen() {
  tft.fillScreen(COLOR_BG);
  drawGameExitButton(TWENTYQ_BACK_X, TWENTYQ_BACK_Y, TWENTYQ_BACK_W, TWENTYQ_BACK_H,
                     "BACK", pressedButton == BTN_BACK, dpadFocusVisible && selectedButton == BTN_BACK);

  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawCentreString("20 QUESTIONS", 120, 32, 2);

  drawGradientControl(TWENTYQ_PANEL_X, TWENTYQ_PANEL_Y, TWENTYQ_PANEL_W, TWENTYQ_PANEL_H,
                      14, COLOR_PANEL, COLOR_ACCENT);

  if (twentyQGuessReady) {
    updateTwentyQGuess();
    tft.setTextColor(COLOR_ACCENT_HI, COLOR_PANEL);
    tft.drawCentreString("I THINK IT IS...", 120, TWENTYQ_PANEL_Y + 16, 2);
    tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
    drawWrappedCenteredText(getTwentyQCandidateName(twentyQBestCandidate),
                            TWENTYQ_PANEL_X + 10, TWENTYQ_PANEL_Y + 50,
                            TWENTYQ_PANEL_W - 20, 22, 3, COLOR_TEXT, COLOR_PANEL);
    tft.setTextColor(COLOR_ACCENT_HI, COLOR_PANEL);
    tft.drawCentreString(getTwentyQCandidateCategory(twentyQBestCandidate), 120, TWENTYQ_PANEL_Y + 116, 2);

    if (twentyQAnsweredFeedback) {
      tft.setTextColor(COLOR_TEXT, COLOR_BG);
      const char *feedbackText = "Tiny oracle victory.";
      if (twentyQDuplicateLastRound) {
        feedbackText = "Oh, darn, I should have guessed that one!";
      } else if (twentyQLearnedLastRound) {
        feedbackText = "I never would have guessed that, but you wont fool me again";
      }
      drawWrappedCenteredText(feedbackText, 10, 210, 220, 18, 3, COLOR_TEXT, COLOR_BG);
      drawButtonBubble(TWENTYQ_RESTART_X, TWENTYQ_RESTART_Y,
                       TWENTYQ_RESTART_W, TWENTYQ_RESTART_H,
                       "PLAY AGAIN",
                       pressedButton == BTN_20Q_RESTART,
                       dpadFocusVisible && selectedButton == BTN_20Q_RESTART,
                       -1);
    } else {
      tft.setTextColor(COLOR_TEXT, COLOR_BG);
      tft.drawCentreString("Was I correct?", 120, 210, 2);
      drawButtonBubble(TWENTYQ_CORRECT_X, TWENTYQ_FEEDBACK_Y,
                       TWENTYQ_FEEDBACK_W, TWENTYQ_FEEDBACK_H,
                       "YES",
                       pressedButton == BTN_20Q_YES,
                       dpadFocusVisible && selectedButton == BTN_20Q_YES,
                       -1);
      drawButtonBubble(TWENTYQ_WRONG_X, TWENTYQ_FEEDBACK_Y,
                       TWENTYQ_FEEDBACK_W, TWENTYQ_FEEDBACK_H,
                       "NO",
                       pressedButton == BTN_20Q_NO,
                       dpadFocusVisible && selectedButton == BTN_20Q_NO,
                       -1);
    }
  } else {
    char progress[28];
    snprintf(progress, sizeof(progress), "Question %d of %d", twentyQQuestionIndex + 1, TWENTYQ_MAX_QUESTIONS);
    tft.setTextColor(COLOR_ACCENT_HI, COLOR_PANEL);
    tft.drawCentreString(progress, 120, TWENTYQ_PANEL_Y + 14, 2);
    drawWrappedCenteredText(twentyQQuestions[twentyQQuestionIndex],
                            TWENTYQ_PANEL_X + 10, TWENTYQ_PANEL_Y + 48,
                            TWENTYQ_PANEL_W - 20, 24, 4, COLOR_TEXT, COLOR_PANEL);

    drawButtonBubble(TWENTYQ_YES_X, TWENTYQ_BTN_Y, TWENTYQ_BTN_W, TWENTYQ_BTN_H,
                     "YES", pressedButton == BTN_20Q_YES,
                     dpadFocusVisible && selectedButton == BTN_20Q_YES, -1);
    drawButtonBubble(TWENTYQ_UNSURE_X, TWENTYQ_BTN_Y, TWENTYQ_BTN_W, TWENTYQ_BTN_H,
                     "UNSURE", pressedButton == BTN_20Q_UNSURE,
                     dpadFocusVisible && selectedButton == BTN_20Q_UNSURE, -1);
    drawButtonBubble(TWENTYQ_NO_X, TWENTYQ_BTN_Y, TWENTYQ_BTN_W, TWENTYQ_BTN_H,
                     "NO", pressedButton == BTN_20Q_NO,
                     dpadFocusVisible && selectedButton == BTN_20Q_NO, -1);
  }

  twentyQNeedsRedraw = false;
}

void handleTwentyQTouch(int x, int y) {
  if (isInside(x, y, TWENTYQ_BACK_X, TWENTYQ_BACK_Y, TWENTYQ_BACK_W, TWENTYQ_BACK_H)) {
    queueScreenButton(BTN_BACK, SCREEN_GAMES_MENU);
    return;
  }
  if (waitingForRelease) return;

  if (twentyQGuessReady) {
    if (twentyQAnsweredFeedback &&
        isInside(x, y, TWENTYQ_RESTART_X, TWENTYQ_RESTART_Y, TWENTYQ_RESTART_W, TWENTYQ_RESTART_H)) {
      pressedButton = BTN_20Q_RESTART;
      waitingForRelease = true;
      resetTwentyQGame();
      fullRedraw = true;
      uiNeedsRedraw = true;
    } else if (!twentyQAnsweredFeedback &&
               isInside(x, y, TWENTYQ_CORRECT_X, TWENTYQ_FEEDBACK_Y, TWENTYQ_FEEDBACK_W, TWENTYQ_FEEDBACK_H)) {
      pressedButton = BTN_20Q_YES;
      selectedButton = BTN_20Q_RESTART;
      waitingForRelease = true;
      twentyQAnsweredFeedback = true;
      twentyQLearnedLastRound = false;
      twentyQNeedsRedraw = true;
      fullRedraw = true;
      uiNeedsRedraw = true;
    } else if (!twentyQAnsweredFeedback &&
               isInside(x, y, TWENTYQ_WRONG_X, TWENTYQ_FEEDBACK_Y, TWENTYQ_FEEDBACK_W, TWENTYQ_FEEDBACK_H)) {
      pressedButton = BTN_20Q_NO;
      waitingForRelease = true;
      keyboardTarget = KEYBOARD_TARGET_20Q_LEARN;
      keyboardBuffer = "";
      keyboardActive = true;
      keyboardLowercase = false;
      keyboardNeedsRedraw = true;
      kbCursorRow = 1;
      kbCursorCol = 0;
      uiNeedsRedraw = true;
    }
    return;
  }

  if (isInside(x, y, TWENTYQ_YES_X, TWENTYQ_BTN_Y, TWENTYQ_BTN_W, TWENTYQ_BTN_H)) {
    pressedButton = BTN_20Q_YES;
    selectedButton = BTN_20Q_YES;
    waitingForRelease = true;
    handleTwentyQAnswer(1);
  } else if (isInside(x, y, TWENTYQ_UNSURE_X, TWENTYQ_BTN_Y, TWENTYQ_BTN_W, TWENTYQ_BTN_H)) {
    pressedButton = BTN_20Q_UNSURE;
    selectedButton = BTN_20Q_UNSURE;
    waitingForRelease = true;
    handleTwentyQAnswer(0);
  } else if (isInside(x, y, TWENTYQ_NO_X, TWENTYQ_BTN_Y, TWENTYQ_BTN_W, TWENTYQ_BTN_H)) {
    pressedButton = BTN_20Q_NO;
    selectedButton = BTN_20Q_NO;
    waitingForRelease = true;
    handleTwentyQAnswer(-1);
  }
}

void drawDisplaySettingsRow(int y, const char* label, const char* valueText,
                            bool minusPressed, bool minusSelected,
                            bool plusPressed, bool plusSelected,
                            uint16_t previewColor, int previewWidth) {
  int panelY = y;
  int panelH = DISPLAY_SETTINGS_ROW_H;
  int contentCenterX = DISPLAY_SETTINGS_ROW_X + (DISPLAY_SETTINGS_ROW_W / 2);

  tft.fillRoundRect(DISPLAY_SETTINGS_ROW_X, panelY, DISPLAY_SETTINGS_ROW_W, panelH, 12, COLOR_PANEL);
  tft.drawRoundRect(DISPLAY_SETTINGS_ROW_X, panelY, DISPLAY_SETTINGS_ROW_W, panelH, 12, COLOR_ACCENT);

  tft.setTextColor(COLOR_TEXT);
  tft.setTextFont(2);
  tft.drawCentreString(label, contentCenterX, panelY + 3, 2);
  tft.drawCentreString(valueText, contentCenterX, panelY + 15, 2);

  int previewX = 72;
  int previewY = panelY + 31;
  int previewH = 5;
  int previewTrackW = 96;
  tft.drawRoundRect(previewX, previewY, previewTrackW, previewH, 3, COLOR_ACCENT_HI);
  if (previewWidth > 0) {
    tft.fillRoundRect(previewX + 1, previewY + 1, min(previewWidth, previewTrackW - 2), previewH - 2, 3, previewColor);
  }

  drawButtonBubble(
    DISPLAY_SETTINGS_MINUS_X, panelY + DISPLAY_SETTINGS_ADJUST_BTN_Y_OFFSET,
    DISPLAY_SETTINGS_ADJUST_BTN_W, DISPLAY_SETTINGS_ADJUST_BTN_H,
    "-",
    minusPressed,
    minusSelected,
    -1);

  drawButtonBubble(
    DISPLAY_SETTINGS_PLUS_X, panelY + DISPLAY_SETTINGS_ADJUST_BTN_Y_OFFSET,
    DISPLAY_SETTINGS_ADJUST_BTN_W, DISPLAY_SETTINGS_ADJUST_BTN_H,
    "+",
    plusPressed,
    plusSelected,
    -1);
}

void drawDisplaySettingsStatic() {
  tft.fillScreen(COLOR_BG);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawCentreString("Display Settings", 120, 38, 2);
  tft.setTextColor(fadeColor(COLOR_TEXT, 0.65f), COLOR_BG);
  tft.drawCentreString("Changes save automatically", 120, 50, 2);
}

void drawDisplaySettingsDynamic() {
  if (!displaySettingsNeedsRedraw) return;

  char dimTimeoutText[16];
  char offTimeoutText[16];
  char brightnessText[16];
  char sleepText[16];
  int brightnessPercent = (displayBrightness * 100) / 255;
  int sleepPercent = (displaySleepBrightness * 100) / 255;
  snprintf(brightnessText, sizeof(brightnessText), "%d%%", brightnessPercent);
  if (displaySleepBrightness == 0) {
    snprintf(sleepText, sizeof(sleepText), "Off");
  } else {
    snprintf(sleepText, sizeof(sleepText), "%d%%", sleepPercent);
  }

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y,
    BACK_BTN_W, BACK_BTN_H,
    "BACK",
    pressedButton == BTN_BACK,
    dpadFocusVisible && selectedButton == BTN_BACK,
    BACK_TEXT_OFFSET);

  drawDisplaySettingsRow(
    DISPLAY_SETTINGS_BRIGHTNESS_Y,
    "Brightness",
    brightnessText,
    pressedButton == BTN_DISPLAY_BRIGHTNESS_DEC,
    dpadFocusVisible && selectedButton == BTN_DISPLAY_BRIGHTNESS_DEC,
    pressedButton == BTN_DISPLAY_BRIGHTNESS_INC,
    dpadFocusVisible && selectedButton == BTN_DISPLAY_BRIGHTNESS_INC,
    COLOR_ACCENT,
    map(displayBrightness, 0, 255, 0, 94));

  drawDisplaySettingsRow(
    DISPLAY_SETTINGS_TIMEOUT_Y,
    "Dim Timeout",
    formatDisplayTimeoutValue(displayTimeoutIndex, displayTimeoutOptionsSec, displayTimeoutOptionCount,
                              dimTimeoutText, sizeof(dimTimeoutText)),
    pressedButton == BTN_DISPLAY_TIMEOUT_DEC,
    dpadFocusVisible && selectedButton == BTN_DISPLAY_TIMEOUT_DEC,
    pressedButton == BTN_DISPLAY_TIMEOUT_INC,
    dpadFocusVisible && selectedButton == BTN_DISPLAY_TIMEOUT_INC,
    COLOR_ACCENT_HI,
    map(min((int)displayTimeoutIndex, (int)displayTimeoutOptionCount - 1), 0, displayTimeoutOptionCount - 1, 12, 94));

  drawDisplaySettingsRow(
    DISPLAY_SETTINGS_OFF_TIMEOUT_Y,
    "Off Delay",
    formatDisplayTimeoutValue(displayOffTimeoutIndex, displayOffTimeoutOptionsSec, displayOffTimeoutOptionCount,
                              offTimeoutText, sizeof(offTimeoutText)),
    pressedButton == BTN_DISPLAY_OFF_TIMEOUT_DEC,
    dpadFocusVisible && selectedButton == BTN_DISPLAY_OFF_TIMEOUT_DEC,
    pressedButton == BTN_DISPLAY_OFF_TIMEOUT_INC,
    dpadFocusVisible && selectedButton == BTN_DISPLAY_OFF_TIMEOUT_INC,
    fadeColor(COLOR_ACCENT_HI, 0.70f),
    map(min((int)displayOffTimeoutIndex, (int)displayOffTimeoutOptionCount - 1), 0, displayOffTimeoutOptionCount - 1, 12, 94));

  drawDisplaySettingsRow(
    DISPLAY_SETTINGS_SLEEP_Y,
    "Dim Level",
    sleepText,
    pressedButton == BTN_DISPLAY_SLEEP_DEC,
    dpadFocusVisible && selectedButton == BTN_DISPLAY_SLEEP_DEC,
    pressedButton == BTN_DISPLAY_SLEEP_INC,
    dpadFocusVisible && selectedButton == BTN_DISPLAY_SLEEP_INC,
    fadeColor(COLOR_ACCENT, 0.70f),
    map(displaySleepBrightness, 0, 255, 0, 94));

  drawButtonBubble(
    DISPLAY_SETTINGS_THEME_X, DISPLAY_SETTINGS_THEME_Y,
    DISPLAY_SETTINGS_THEME_W, DISPLAY_SETTINGS_THEME_H,
    getThemeToggleLabel(),
    pressedButton == BTN_DISPLAY_THEME_TOGGLE,
    dpadFocusVisible && selectedButton == BTN_DISPLAY_THEME_TOGGLE,
    -1);

  displaySettingsNeedsRedraw = false;
}

void drawGearIcon(int x, int y, int size, uint16_t iconColor) {
  int cx = x + (size / 2);
  int cy = y + (size / 2);
  int outerR = max(6, size / 2 - 3);
  int innerR = max(2, size / 4);
  int toothLen = max(2, size / 6);

  for (int i = 0; i < 8; i++) {
    float angle = (float)i * 0.7853982f;
    int sx = cx + (int)(cosf(angle) * (innerR + 2));
    int sy = cy + (int)(sinf(angle) * (innerR + 2));
    int ex = cx + (int)(cosf(angle) * (outerR + toothLen));
    int ey = cy + (int)(sinf(angle) * (outerR + toothLen));
    tft.drawLine(sx, sy, ex, ey, iconColor);
  }

  tft.drawCircle(cx, cy, outerR, iconColor);
  tft.drawCircle(cx, cy, outerR - 1, iconColor);
  tft.fillCircle(cx, cy, innerR, iconColor);
  tft.fillCircle(cx, cy, max(1, innerR - 3), COLOR_PANEL);
}

static float wrapBattleAngle(float angle) {
  const float twoPi = 6.2831853f;
  while (angle < 0.0f) angle += twoPi;
  while (angle >= twoPi) angle -= twoPi;
  return angle;
}

static int countAliveBattleEnemies() {
  int count = 0;
  for (int i = 0; i < BZ_MAX_ENEMIES; i++) {
    if (spaceEnemies[i].alive) count++;
  }
  return count;
}

static bool battleWorldToScreen(float wx, float wy, float &sx, float &scale, float &forward) {
  float dx = wx - spacePlayerX;
  float dy = wy - spacePlayerY;
  float c = cosf(spacePlayerHeading);
  float s = sinf(spacePlayerHeading);

  forward = (c * dx) + (s * dy);
  if (forward <= 1.0f) return false;

  float side = (-s * dx) + (c * dy);
  sx = 120.0f + ((side / forward) * BZ_FOV_SCALE);
  if (sx < -26.0f || sx > 266.0f) return false;

  scale = constrain(220.0f / forward, 4.0f, 92.0f);
  return true;
}

static bool battlePositionBlocked(float x, float y) {
  for (int i = 0; i < BZ_MAX_OBSTACLES; i++) {
    float dx = x - spaceObstacles[i].x;
    float dy = y - spaceObstacles[i].y;
    if ((dx * dx) + (dy * dy) < (spaceObstacles[i].radius * spaceObstacles[i].radius)) {
      return true;
    }
  }
  return false;
}

static void spawnBattleWave(unsigned long now) {
  int targetEnemies = min(2 + (spaceWave / 2), BZ_MAX_ENEMIES);
  for (int i = 0; i < BZ_MAX_ENEMIES; i++) {
    spaceEnemies[i].alive = false;
  }

  for (int i = 0; i < targetEnemies; i++) {
    float angle = random(0, 628) / 100.0f;
    float dist = 28.0f + (random(0, 380) / 10.0f);
    spaceEnemies[i].x = spacePlayerX + (cosf(angle) * dist);
    spaceEnemies[i].y = spacePlayerY + (sinf(angle) * dist);
    spaceEnemies[i].alive = true;
    spaceEnemies[i].type = BZ_ENEMY_TANK;
    if (spaceWave >= 3 && (i == 0) && ((spaceWave % 2) == 1)) {
      spaceEnemies[i].type = BZ_ENEMY_SUPER;
    } else if (spaceWave >= 4 && i == (targetEnemies - 1) && (random(0, 100) < 35)) {
      spaceEnemies[i].type = BZ_ENEMY_UFO;
    }
    spaceEnemies[i].nextShotAt = now + random(1100, 2400);
  }

  spaceNextMissileAt = now + random(6000, 11000);
}

void resetSpaceGame() {
  spacePlayerX = 0.0f;
  spacePlayerY = 0.0f;
  spacePlayerHeading = 0.0f;
  spaceScore = 0;
  spaceLives = 3;
  spaceWave = 1;
  spaceGameOver = false;
  spaceWaveCleared = false;
  spaceGameStarted = false;
  spacePlayerShotActive = false;
  spaceEnemyShotActive = false;
  spaceMissileActive = false;
  spacePlayerShotDistance = 0.0f;
  lastSpaceFrameTime = millis();
  lastSpaceShotTime = 0;
  lastSpaceRespawnAt = 0;
  spaceMuzzleFlashUntil = 0;

  for (int i = 0; i < BZ_MAX_OBSTACLES; i++) {
    spaceObstacles[i].pyramid = (i % 2 == 0);
    spaceObstacles[i].radius = spaceObstacles[i].pyramid ? 3.8f : 3.2f;
    float angle = random(0, 628) / 100.0f;
    float dist = 22.0f + (random(0, 820) / 10.0f);
    spaceObstacles[i].x = cosf(angle) * dist;
    spaceObstacles[i].y = sinf(angle) * dist;
  }

  spawnBattleWave(lastSpaceFrameTime);
}

bool spaceEnemiesRemaining() {
  for (int i = 0; i < BZ_MAX_ENEMIES; i++) {
    if (spaceEnemies[i].alive) return true;
  }
  return false;
}

void moveSpacePlayer(int delta) {
  float direction = (delta < 0) ? -1.0f : 1.0f;
  spacePlayerHeading = wrapBattleAngle(spacePlayerHeading + (direction * BZ_TURN_SPEED));
}

void fireSpacePlayerBullet() {
  if (spaceGameOver) return;
  if (millis() - lastSpaceShotTime < BZ_FIRE_COOLDOWN_MS) return;
  if (spacePlayerShotActive) return;

  spacePlayerShotActive = true;
  spacePlayerShotDistance = 0.0f;
  spacePlayerShotX = spacePlayerX;
  spacePlayerShotY = spacePlayerY;
  spacePlayerShotVX = cosf(spacePlayerHeading) * BZ_SHOT_SPEED;
  spacePlayerShotVY = sinf(spacePlayerHeading) * BZ_SHOT_SPEED;
  lastSpaceShotTime = millis();
  spaceMuzzleFlashUntil = millis() + 90;
}

void fireSpaceEnemyBullet() {
  if (spaceGameOver || !spaceEnemiesRemaining() || spaceEnemyShotActive) return;

  int bestIndex = -1;
  float bestDist2 = 1e9f;
  for (int i = 0; i < BZ_MAX_ENEMIES; i++) {
    if (!spaceEnemies[i].alive) continue;
    if (spaceEnemies[i].type == BZ_ENEMY_UFO) continue;
    float dx = spacePlayerX - spaceEnemies[i].x;
    float dy = spacePlayerY - spaceEnemies[i].y;
    float c = cosf(spacePlayerHeading);
    float s = sinf(spacePlayerHeading);
    float forward = (c * dx) + (s * dy);
    if (forward < 6.0f) continue;
    float d2 = (dx * dx) + (dy * dy);
    if (d2 < bestDist2) {
      bestDist2 = d2;
      bestIndex = i;
    }
  }
  if (bestIndex < 0) return;

  float dx = spacePlayerX - spaceEnemies[bestIndex].x;
  float dy = spacePlayerY - spaceEnemies[bestIndex].y;
  float mag = sqrtf((dx * dx) + (dy * dy));
  if (mag < 0.001f) return;

  spaceEnemyShotActive = true;
  spaceEnemyShotX = spaceEnemies[bestIndex].x;
  spaceEnemyShotY = spaceEnemies[bestIndex].y;
  float shotSpeed = (spaceEnemies[bestIndex].type == BZ_ENEMY_SUPER) ? 1.2f : 0.82f;
  spaceEnemyShotVX = (dx / mag) * shotSpeed;
  spaceEnemyShotVY = (dy / mag) * shotSpeed;
}

void updateSpaceGame(unsigned long now, bool leftPressed, bool rightPressed, bool firePressed, bool exitPressed) {
  if (currentScreen != SCREEN_SPACE_GAME) return;
  if (exitPressed) {
    setScreen(SCREEN_GAMES_MENU);
    return;
  }

  if (now - lastSpaceFrameTime < SPACE_FRAME_INTERVAL_MS) return;
  lastSpaceFrameTime = now;

  if (spaceGameOver) {
    if (firePressed) {
      resetSpaceGame();
      uiNeedsRedraw = true;
    }
    return;
  }

  (void)leftPressed;
  (void)rightPressed;

  if (fabs(inputChannels[0]) > 0.08f ||
      fabs(inputChannels[1]) > 0.08f ||
      firePressed) {
    spaceGameStarted = true;
  }

  // Battlezone always uses raw one-stick tank controls, independent of the
  // active model's drive type, mixes, rates, expo, or reverses.
  float turnInput = inputChannels[0];      // RX = left/right turn
  float throttleInput = inputChannels[1];  // RY = forward/reverse

  turnInput = constrain(turnInput, -1.0f, 1.0f);
  throttleInput = constrain(throttleInput, -1.0f, 1.0f);

  spacePlayerHeading = wrapBattleAngle(spacePlayerHeading + (turnInput * BZ_TURN_SPEED));

  float step = throttleInput * BZ_PLAYER_SPEED;
  float nextX = spacePlayerX + (cosf(spacePlayerHeading) * step);
  float nextY = spacePlayerY + (sinf(spacePlayerHeading) * step);
  if (!battlePositionBlocked(nextX, nextY)) {
    spacePlayerX = nextX;
    spacePlayerY = nextY;
  }

  if (firePressed) fireSpacePlayerBullet();

  if (spacePlayerShotActive) {
    spacePlayerShotX += spacePlayerShotVX;
    spacePlayerShotY += spacePlayerShotVY;
    spacePlayerShotDistance += BZ_SHOT_SPEED;

    if (spaceMissileActive) {
      float mdx = spacePlayerShotX - spaceMissileX;
      float mdy = spacePlayerShotY - spaceMissileY;
      if ((mdx * mdx) + (mdy * mdy) <= 2.6f) {
        spaceMissileActive = false;
        spacePlayerShotActive = false;
        spaceScore += 1000;
      }
    }

    for (int i = 0; i < BZ_MAX_ENEMIES; i++) {
      if (!spacePlayerShotActive) break;
      if (!spaceEnemies[i].alive) continue;
      float dx = spacePlayerShotX - spaceEnemies[i].x;
      float dy = spacePlayerShotY - spaceEnemies[i].y;
      if ((dx * dx) + (dy * dy) <= 3.2f) {
        spaceEnemies[i].alive = false;
        if (spaceEnemies[i].type == BZ_ENEMY_SUPER) spaceScore += 3000;
        else if (spaceEnemies[i].type == BZ_ENEMY_UFO) spaceScore += 5000;
        else spaceScore += 1000;
        spacePlayerShotActive = false;
        break;
      }
    }

    if (spacePlayerShotDistance >= BZ_SHOT_RANGE) {
      spacePlayerShotActive = false;
    }
  }

  for (int i = 0; i < BZ_MAX_ENEMIES; i++) {
    if (!spaceEnemies[i].alive) continue;

    float toPlayerX = spacePlayerX - spaceEnemies[i].x;
    float toPlayerY = spacePlayerY - spaceEnemies[i].y;
    float distSq = (toPlayerX * toPlayerX) + (toPlayerY * toPlayerY);
    float dist = sqrtf(distSq);

    if (spaceEnemies[i].type == BZ_ENEMY_UFO) {
      float orbit = now * 0.0012f + i;
      spaceEnemies[i].x += cosf(orbit) * 0.05f;
      spaceEnemies[i].y += sinf(orbit) * 0.03f;
      if (dist > 55.0f) {
        spaceEnemies[i].x += (toPlayerX / max(dist, 0.001f)) * 0.05f;
        spaceEnemies[i].y += (toPlayerY / max(dist, 0.001f)) * 0.05f;
      }
      continue;
    }

    if (dist > 7.0f) {
      float stepEnemy = (spaceEnemies[i].type == BZ_ENEMY_SUPER)
                          ? (0.028f + (0.004f * min(spaceWave, 6)))
                          : (0.015f + (0.003f * min(spaceWave, 6)));
      float nx = spaceEnemies[i].x + (toPlayerX / max(dist, 0.001f)) * stepEnemy;
      float ny = spaceEnemies[i].y + (toPlayerY / max(dist, 0.001f)) * stepEnemy;
      if (!battlePositionBlocked(nx, ny)) {
        spaceEnemies[i].x = nx;
        spaceEnemies[i].y = ny;
      }
    }

    if (now >= spaceEnemies[i].nextShotAt && dist < 65.0f) {
      fireSpaceEnemyBullet();
      spaceEnemies[i].nextShotAt = now + ((spaceEnemies[i].type == BZ_ENEMY_SUPER)
                                          ? random(700, 1500)
                                          : random(1100, 2400));
    }
  }

  if (spaceEnemyShotActive) {
    spaceEnemyShotX += spaceEnemyShotVX;
    spaceEnemyShotY += spaceEnemyShotVY;
    float hitDx = spaceEnemyShotX - spacePlayerX;
    float hitDy = spaceEnemyShotY - spacePlayerY;
    if ((hitDx * hitDx) + (hitDy * hitDy) <= 2.2f) {
      spaceEnemyShotActive = false;
      if (spaceLives > 0) spaceLives--;
      if (spaceLives <= 0) {
        spaceGameOver = true;
      } else {
        spacePlayerX = 0.0f;
        spacePlayerY = 0.0f;
        spacePlayerHeading = 0.0f;
      }
    } else if ((hitDx * hitDx) + (hitDy * hitDy) > 6400.0f) {
      spaceEnemyShotActive = false;
    }
  }

  if (!spaceMissileActive && now >= spaceNextMissileAt) {
    float angle = random(0, 628) / 100.0f;
    float dist = 85.0f;
    spaceMissileX = spacePlayerX + cosf(angle) * dist;
    spaceMissileY = spacePlayerY + sinf(angle) * dist;
    float dx = spacePlayerX - spaceMissileX;
    float dy = spacePlayerY - spaceMissileY;
    float mag = sqrtf((dx * dx) + (dy * dy));
    if (mag > 0.001f) {
      spaceMissileVX = (dx / mag) * BZ_MISSILE_SPEED;
      spaceMissileVY = (dy / mag) * BZ_MISSILE_SPEED;
      spaceMissileActive = true;
    }
    spaceNextMissileAt = now + random(9000, 15000);
  }

  if (spaceMissileActive) {
    float targetVX = spacePlayerX - spaceMissileX;
    float targetVY = spacePlayerY - spaceMissileY;
    float targetMag = sqrtf((targetVX * targetVX) + (targetVY * targetVY));
    if (targetMag > 0.001f) {
      targetVX = (targetVX / targetMag) * BZ_MISSILE_SPEED;
      targetVY = (targetVY / targetMag) * BZ_MISSILE_SPEED;
      spaceMissileVX += (targetVX - spaceMissileVX) * BZ_MISSILE_TURN;
      spaceMissileVY += (targetVY - spaceMissileVY) * BZ_MISSILE_TURN;
    }

    spaceMissileX += spaceMissileVX;
    spaceMissileY += spaceMissileVY;

    float mdx = spaceMissileX - spacePlayerX;
    float mdy = spaceMissileY - spacePlayerY;
    if ((mdx * mdx) + (mdy * mdy) <= 2.6f) {
      spaceMissileActive = false;
      if (spaceLives > 0) spaceLives--;
      if (spaceLives <= 0) {
        spaceGameOver = true;
      } else {
        spacePlayerX = 0.0f;
        spacePlayerY = 0.0f;
        spacePlayerHeading = 0.0f;
      }
    } else if ((mdx * mdx) + (mdy * mdy) > 18000.0f) {
      spaceMissileActive = false;
    }
  }

  if (!spaceGameOver && countAliveBattleEnemies() == 0 && !spaceWaveCleared) {
    spaceWaveCleared = true;
    lastSpaceRespawnAt = now + BZ_RESPAWN_DELAY_MS;
  }

  if (!spaceGameOver && spaceWaveCleared && now >= lastSpaceRespawnAt) {
    spaceWave++;
    spawnBattleWave(now);
    spaceWaveCleared = false;
    spaceEnemyShotActive = false;
    spacePlayerShotActive = false;
    spaceMissileActive = false;
  }

  uiNeedsRedraw = true;
}

void handleSpaceGameTouch(int x, int y) {
  if (isInside(x, y, SPACE_EXIT_X, SPACE_EXIT_Y, SPACE_EXIT_W, SPACE_EXIT_H)) {
    queueScreenButton(BTN_BACK, SCREEN_GAMES_MENU);
    return;
  }

  if (spaceGameOver) {
    resetSpaceGame();
    waitingForRelease = true;
    uiNeedsRedraw = true;
    return;
  }

  spaceGameStarted = true;
  fireSpacePlayerBullet();
  waitingForRelease = true;

  uiNeedsRedraw = true;
}

void drawSpaceGameStatic() {
  tft.fillScreen(COLOR_BG);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);

  drawGameExitButton(SPACE_EXIT_X, SPACE_EXIT_Y, SPACE_EXIT_W, SPACE_EXIT_H, "EXIT", false, false);

  tft.drawFastHLine(0, SPACE_STATUS_H, 240, COLOR_ACCENT);
  tft.drawString("BATTLEZONE", 72, 8, 2);
}

void drawSpaceGameDynamic() {
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);

  tft.fillRect(96, 6, 136, 16, COLOR_BG);
  tft.drawString(String(spaceScore), 98, 8, 2);
  tft.drawString("L:" + String(spaceLives), 150, 8, 2);
  tft.drawString("W:" + String(spaceWave), 188, 8, 2);

  tft.fillRect(0, BZ_VIEW_TOP, 240, BZ_VIEW_BOTTOM - BZ_VIEW_TOP, COLOR_BG);
  tft.drawFastHLine(0, BZ_HORIZON_Y, 240, TFT_DARKGREY);

  for (int x = 0; x <= 240; x += 30) {
    int peak = (int)(BZ_HORIZON_Y - 6 - 4 * sinf((x + (spaceWave * 7)) * 0.06f));
    tft.drawLine(x, BZ_HORIZON_Y, x + 14, peak, COLOR_PANEL);
    tft.drawLine(x + 14, peak, x + 30, BZ_HORIZON_Y, COLOR_PANEL);
  }

  auto drawWireTarget = [&](float sx, float scale, uint8_t enemyType, bool pyramid) {
    int cx = (int)roundf(sx);
    int h = max(6, (int)roundf(scale));
    int w = max(8, (int)roundf(scale * (pyramid ? 0.9f : 1.2f)));
    int baseY = BZ_HORIZON_Y + (int)roundf(70.0f / max(scale, 1.0f));
    baseY = constrain(baseY, BZ_HORIZON_Y + 8, BZ_VIEW_BOTTOM - 16);
    bool enemyColor = (enemyType == BZ_ENEMY_TANK || enemyType == BZ_ENEMY_SUPER || enemyType == BZ_ENEMY_UFO);
    uint16_t c = enemyColor ? COLOR_ACCENT_HI : COLOR_TEXT;

    if (pyramid) {
      tft.drawLine(cx - w / 2, baseY, cx, baseY - h, c);
      tft.drawLine(cx + w / 2, baseY, cx, baseY - h, c);
      tft.drawLine(cx - w / 2, baseY, cx + w / 2, baseY, c);
      tft.drawLine(cx - w / 3, baseY - h / 2, cx + w / 3, baseY - h / 2, c);
    } else if (enemyType == BZ_ENEMY_UFO) {
      int rw = max(10, (int)roundf(w * 1.1f));
      tft.drawFastHLine(cx - rw / 2, baseY - h / 2, rw, c);
      tft.drawFastHLine(cx - rw / 3, baseY - h / 2 - 4, (rw * 2) / 3, c);
      tft.drawLine(cx - rw / 2, baseY - h / 2, cx - rw / 3, baseY - h / 2 - 4, c);
      tft.drawLine(cx + rw / 2, baseY - h / 2, cx + rw / 3, baseY - h / 2 - 4, c);
      tft.drawLine(cx - rw / 3, baseY - h / 2 + 1, cx + rw / 3, baseY - h / 2 + 1, c);
    } else if (enemyType == BZ_ENEMY_SUPER) {
      tft.drawRect(cx - w / 2, baseY - h, w, h, c);
      tft.drawRect(cx - w / 2 - 2, baseY - h - 2, w + 4, h + 4, c);
      tft.drawLine(cx - w / 2, baseY, cx - w / 3, baseY - h - h / 3, c);
      tft.drawLine(cx + w / 2, baseY, cx + w / 3, baseY - h - h / 3, c);
      tft.drawLine(cx - w / 3, baseY - h - h / 3, cx + w / 3, baseY - h - h / 3, c);
    } else {
      tft.drawRect(cx - w / 2, baseY - h, w, h, c);
      tft.drawLine(cx - w / 2, baseY, cx - w / 3, baseY - h - h / 3, c);
      tft.drawLine(cx + w / 2, baseY, cx + w / 3, baseY - h - h / 3, c);
      tft.drawLine(cx - w / 3, baseY - h - h / 3, cx + w / 3, baseY - h - h / 3, c);
    }
  };

  for (int i = 0; i < BZ_MAX_OBSTACLES; i++) {
    float sx, scale, forward;
    if (!battleWorldToScreen(spaceObstacles[i].x, spaceObstacles[i].y, sx, scale, forward)) continue;
    drawWireTarget(sx, scale * 0.8f, 255, spaceObstacles[i].pyramid);
  }

  for (int i = 0; i < BZ_MAX_ENEMIES; i++) {
    if (!spaceEnemies[i].alive) continue;
    float sx, scale, forward;
    if (!battleWorldToScreen(spaceEnemies[i].x, spaceEnemies[i].y, sx, scale, forward)) continue;
    drawWireTarget(sx, scale, spaceEnemies[i].type, false);
  }

  if (spacePlayerShotActive) {
    float sx, scale, forward;
    if (battleWorldToScreen(spacePlayerShotX, spacePlayerShotY, sx, scale, forward)) {
      int y = BZ_HORIZON_Y + (int)roundf(68.0f / max(scale, 1.0f));
      int ix = (int)sx;
      tft.drawFastVLine(ix, y - 6, 12, COLOR_TEXT);
      tft.drawFastVLine(ix - 1, y - 4, 8, COLOR_ACCENT_HI);
      tft.drawFastVLine(ix + 1, y - 4, 8, COLOR_ACCENT_HI);
    }
  }

  if (spaceEnemyShotActive) {
    float sx, scale, forward;
    if (battleWorldToScreen(spaceEnemyShotX, spaceEnemyShotY, sx, scale, forward)) {
      int y = BZ_HORIZON_Y + (int)roundf(68.0f / max(scale, 1.0f));
      tft.drawFastVLine((int)sx, y - 4, 8, COLOR_ACCENT);
      tft.drawPixel((int)sx - 1, y - 1, COLOR_ACCENT_HI);
      tft.drawPixel((int)sx + 1, y + 1, COLOR_ACCENT_HI);
    }
  }

  if (spaceMissileActive) {
    float sx, scale, forward;
    if (battleWorldToScreen(spaceMissileX, spaceMissileY, sx, scale, forward)) {
      int y = BZ_HORIZON_Y + (int)roundf(68.0f / max(scale, 1.0f));
      int mx = (int)sx;
      tft.drawLine(mx, y, mx - 4, y + 2, COLOR_ACCENT_HI);
      tft.drawLine(mx, y, mx - 4, y - 2, COLOR_ACCENT_HI);
      tft.drawLine(mx, y, mx + 3, y, COLOR_ACCENT_HI);
      tft.drawPixel(mx - 5, y, COLOR_ACCENT);
    }
  }

  tft.drawFastHLine(112, BZ_HORIZON_Y + 2, 16, COLOR_SIG);
  tft.drawFastVLine(120, BZ_HORIZON_Y - 6, 14, COLOR_SIG);
  if (millis() < spaceMuzzleFlashUntil) {
    tft.drawCircle(120, BZ_HORIZON_Y + 1, 4, COLOR_ACCENT_HI);
    tft.drawCircle(120, BZ_HORIZON_Y + 1, 7, COLOR_ACCENT);
  }

  int radarX = 12;
  int radarY = 6;
  int radarW = 52;
  int radarH = 16;
  tft.drawRect(radarX, radarY, radarW, radarH, COLOR_ACCENT);
  tft.fillCircle(radarX + radarW / 2, radarY + radarH / 2, 1, COLOR_SIG);
  for (int i = 0; i < BZ_MAX_ENEMIES; i++) {
    if (!spaceEnemies[i].alive) continue;
    if (spaceEnemies[i].type == BZ_ENEMY_UFO) continue;
    float dx = spaceEnemies[i].x - spacePlayerX;
    float dy = spaceEnemies[i].y - spacePlayerY;
    float rx = constrain(dx * 0.22f, -24.0f, 24.0f);
    float ry = constrain(dy * 0.22f, -7.0f, 7.0f);
    tft.drawPixel(radarX + radarW / 2 + (int)rx, radarY + radarH / 2 + (int)ry, COLOR_ACCENT_HI);
  }
  if (spaceMissileActive) {
    float dx = spaceMissileX - spacePlayerX;
    float dy = spaceMissileY - spacePlayerY;
    float rx = constrain(dx * 0.22f, -24.0f, 24.0f);
    float ry = constrain(dy * 0.22f, -7.0f, 7.0f);
    tft.drawPixel(radarX + radarW / 2 + (int)rx, radarY + radarH / 2 + (int)ry, COLOR_ACCENT);
  }

  if (spaceGameOver) {
    tft.setTextColor(COLOR_ACCENT_HI, COLOR_BG);
    tft.drawCentreString("GAME OVER", 120, 132, 4);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawCentreString("SELECT to restart", 120, 174, 2);
    tft.drawCentreString("DOWN exits to Games", 120, 192, 2);
  } else if (spaceWaveCleared) {
    tft.setTextColor(COLOR_SIG, COLOR_BG);
    tft.drawCentreString("WAVE CLEAR", 120, 132, 4);
  } else if (!spaceGameStarted) {
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawCentreString("RX turn  RY drive", 120, 264, 2);
    tft.drawCentreString("Tap screen to fire", 120, 282, 2);
  }
}

int countActiveStarSwarmEnemies() {
  int count = 0;
  for (int i = 0; i < STAR_SWARM_MAX_ENEMIES; i++) {
    if (starSwarmEnemies[i].active) count++;
  }
  return count;
}

void spawnStarSwarmWave() {
  unsigned long now = millis();
  starSwarmWaveStartTime = now;
  lastStarSwarmDiveTime = now;
  lastStarSwarmEnemyShotTime = now;
  int index = 0;
  for (int row = 0; row < 4; row++) {
    for (int col = 0; col < 6 && index < STAR_SWARM_MAX_ENEMIES; col++, index++) {
      StarSwarmEnemy &enemy = starSwarmEnemies[index];
      enemy.homeX = 32.0f + (col * 35.0f);
      enemy.homeY = 66.0f + (row * 20.0f);
      enemy.path = (row + col + starSwarmWave) % 4;
      if (enemy.path == 0) {
        enemy.startX = -28.0f - (col * 8.0f);
        enemy.startY = 62.0f + (row * 18.0f);
      } else if (enemy.path == 1) {
        enemy.startX = 268.0f + (col * 8.0f);
        enemy.startY = 58.0f + (row * 18.0f);
      } else if (enemy.path == 2) {
        enemy.startX = 40.0f + (col * 26.0f);
        enemy.startY = -34.0f - (row * 12.0f);
      } else {
        enemy.startX = 200.0f - (col * 26.0f);
        enemy.startY = -52.0f - (row * 14.0f);
      }
      enemy.x = enemy.startX;
      enemy.y = enemy.startY;
      enemy.t = 0.0f;
      enemy.oldX = (int)enemy.x;
      enemy.oldY = (int)enemy.y;
      enemy.spawnAt = now + (index * 120UL);
      enemy.active = true;
      enemy.wasDrawn = false;
      enemy.row = row;
      enemy.state = 0;
    }
  }
  starSwarmDir = 1;
}

void resetStarSwarmGame() {
  for (int i = 0; i < STAR_SWARM_MAX_ENEMIES; i++) {
    starSwarmEnemies[i].active = false;
    starSwarmEnemies[i].wasDrawn = false;
  }
  for (int i = 0; i < STAR_SWARM_MAX_PLAYER_SHOTS; i++) {
    starSwarmPlayerShots[i].active = false;
    starSwarmPlayerShots[i].wasDrawn = false;
  }
  for (int i = 0; i < STAR_SWARM_MAX_ENEMY_SHOTS; i++) {
    starSwarmEnemyShots[i].active = false;
    starSwarmEnemyShots[i].wasDrawn = false;
  }
  for (int i = 0; i < STAR_SWARM_MAX_POWERUPS; i++) {
    starSwarmPowerups[i].active = false;
    starSwarmPowerups[i].wasDrawn = false;
  }

  starSwarmPlayerX = 120.0f;
  starSwarmPlayerY = STAR_SWARM_PLAYER_START_Y;
  starSwarmOldPlayerX = 120;
  starSwarmOldPlayerY = STAR_SWARM_PLAYER_START_Y;
  starSwarmPlayerWasDrawn = false;
  starSwarmScore = 0;
  starSwarmLives = 3;
  starSwarmWave = 1;
  starSwarmGameOver = false;
  starSwarmStarted = false;
  starSwarmTouchControl = false;
  starSwarmTouchTargetX = 120;
  starSwarmTouchTargetY = STAR_SWARM_PLAYER_START_Y;
  starSwarmFireQueued = false;
  starSwarmWeaponLevel = 0;
  starSwarmSpeedBoostUntil = 0;
  lastStarSwarmFrameTime = millis();
  lastStarSwarmShotTime = 0;
  lastStarSwarmEnemyShotTime = millis();
  lastStarSwarmDiveTime = millis();
  starSwarmInvulnerableUntil = millis() + STAR_SWARM_RESPAWN_SAFE_MS;
  lastStarSwarmScore = -1;
  lastStarSwarmLives = -1;
  lastStarSwarmWave = -1;
  lastStarSwarmGameOver = false;
  lastStarSwarmStarted = false;
  spawnStarSwarmWave();
}

void fireStarSwarmPlayerShot() {
  if (starSwarmGameOver) return;
  unsigned long now = millis();
  if (now - lastStarSwarmShotTime < STAR_SWARM_FIRE_COOLDOWN_MS) return;

  int needed = (starSwarmWeaponLevel >= 2) ? 3 : (starSwarmWeaponLevel >= 1) ? 2 : 1;
  int freeCount = 0;
  for (int i = 0; i < STAR_SWARM_MAX_PLAYER_SHOTS; i++) {
    if (!starSwarmPlayerShots[i].active) freeCount++;
  }
  if (freeCount < needed) return;

  float offsets[3] = { 0.0f, -6.0f, 6.0f };
  float vx[3] = { 0.0f, -1.15f, 1.15f };
  for (int s = 0; s < needed; s++) {
    for (int i = 0; i < STAR_SWARM_MAX_PLAYER_SHOTS; i++) {
      StarSwarmShot &shot = starSwarmPlayerShots[i];
      if (shot.active) continue;
      int shotIndex = (needed == 1) ? 0 : (needed == 2) ? (s + 1) : s;
      shot.x = starSwarmPlayerX + offsets[shotIndex];
      shot.y = starSwarmPlayerY - 12.0f;
      shot.vx = vx[shotIndex];
      shot.vy = -6.3f;
      shot.oldX = (int)shot.x;
      shot.oldY = (int)shot.y;
      shot.active = true;
      shot.wasDrawn = false;
      break;
    }
  }

  lastStarSwarmShotTime = now;
  starSwarmStarted = true;
}

void spawnStarSwarmPowerup(float x, float y) {
  for (int i = 0; i < STAR_SWARM_MAX_POWERUPS; i++) {
    StarSwarmPowerup &powerup = starSwarmPowerups[i];
    if (powerup.active) continue;
    powerup.x = x;
    powerup.y = y;
    powerup.vy = 1.35f;
    powerup.oldX = (int)x;
    powerup.oldY = (int)y;
    powerup.active = true;
    powerup.wasDrawn = false;
    powerup.type = random(0, 2);
    return;
  }
}

void startStarSwarmDive(unsigned long now) {
  int candidates[STAR_SWARM_MAX_ENEMIES];
  int count = 0;
  for (int i = 0; i < STAR_SWARM_MAX_ENEMIES; i++) {
    if (starSwarmEnemies[i].active && starSwarmEnemies[i].state == 1) {
      candidates[count++] = i;
    }
  }
  if (count <= 0) return;

  StarSwarmEnemy &enemy = starSwarmEnemies[candidates[random(0, count)]];
  enemy.state = 2;
  enemy.t = 0.0f;
  enemy.startX = enemy.x;
  enemy.startY = enemy.y;
  enemy.path = (enemy.path + starSwarmWave + enemy.row + 1) % 4;
  lastStarSwarmDiveTime = now;
}

float starSwarmEase(float t) {
  t = constrain(t, 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

float getStarSwarmSpeedRamp(unsigned long now) {
  unsigned long elapsed = (starSwarmWaveStartTime == 0 || now < starSwarmWaveStartTime)
    ? 0
    : (now - starSwarmWaveStartTime);
  float timeRamp = constrain((float)elapsed / (float)STAR_SWARM_SPEED_RAMP_MS, 0.0f, 1.0f);
  float waveRamp = min(0.55f, (float)(starSwarmWave - 1) * 0.075f);
  return 0.55f + (timeRamp * 0.55f) + waveRamp;
}

void updateStarSwarmEnemyPath(StarSwarmEnemy &enemy, unsigned long now) {
  if (!enemy.active || now < enemy.spawnAt) return;
  float speedRamp = getStarSwarmSpeedRamp(now);

  if (enemy.state == 0) {
    enemy.t += (0.0105f + (starSwarmWave * 0.00075f)) * speedRamp;
    float t = starSwarmEase(enemy.t);
    float arc = sinf(t * 3.14159f);
    float wobble = sinf((t * 6.28318f) + enemy.path) * 34.0f * arc;
    enemy.x = enemy.startX + ((enemy.homeX - enemy.startX) * t) + wobble;
    enemy.y = enemy.startY + ((enemy.homeY - enemy.startY) * t) + (arc * 28.0f);
    if (enemy.t >= 1.0f) {
      enemy.state = 1;
      enemy.t = 0.0f;
      enemy.x = enemy.homeX;
      enemy.y = enemy.homeY;
    }
    return;
  }

  if (enemy.state == 1) {
    float phase = (now * 0.0022f) + (enemy.homeX * 0.035f) + enemy.row;
    enemy.x = enemy.homeX + (sinf(phase) * 9.0f);
    enemy.y = enemy.homeY + (sinf(phase * 0.7f) * 4.0f);
    return;
  }

  enemy.t += (0.0115f + (starSwarmWave * 0.0007f)) * speedRamp;
  float t = enemy.t;
  if (t < 0.70f) {
    float u = t / 0.70f;
    float targetX = starSwarmPlayerX;
    float sideArc = sinf(u * 6.28318f + enemy.path) * (36.0f + enemy.row * 9.0f);
    enemy.x = enemy.startX + ((targetX - enemy.startX) * u) + sideArc;
    float attackY = max(starSwarmPlayerY + 34.0f, (float)STAR_SWARM_VIEW_BOTTOM + 18.0f);
    enemy.y = enemy.startY + ((attackY - enemy.startY) * u);
  } else {
    float u = (t - 0.70f) / 0.30f;
    float topX = (enemy.path & 1) ? 255.0f : -15.0f;
    enemy.x = topX + ((enemy.homeX - topX) * starSwarmEase(u));
    enemy.y = -18.0f + ((enemy.homeY + 2.0f) * starSwarmEase(u));
  }

  if (enemy.t >= 1.0f) {
    enemy.state = 1;
    enemy.t = 0.0f;
    enemy.x = enemy.homeX;
    enemy.y = enemy.homeY;
  }
}

bool starSwarmOnScreen(float x, float y, int margin) {
  return x >= -margin && x <= 240 + margin &&
         y >= STAR_SWARM_VIEW_TOP - margin &&
         y <= STAR_SWARM_VIEW_BOTTOM + margin;
}

bool isStarSwarmPlayerInvulnerable(unsigned long now) {
  return !starSwarmGameOver && now < starSwarmInvulnerableUntil;
}

void destroyStarSwarmPlayer(unsigned long now) {
  if (starSwarmGameOver || isStarSwarmPlayerInvulnerable(now)) return;

  if (starSwarmLives > 0) starSwarmLives--;
  for (int i = 0; i < STAR_SWARM_MAX_ENEMY_SHOTS; i++) {
    starSwarmEnemyShots[i].active = false;
  }

  if (starSwarmLives <= 0) {
    starSwarmGameOver = true;
    return;
  }

  starSwarmPlayerX = 120.0f;
  starSwarmPlayerY = STAR_SWARM_PLAYER_START_Y;
  starSwarmTouchTargetX = (int)starSwarmPlayerX;
  starSwarmTouchTargetY = (int)starSwarmPlayerY;
  starSwarmTouchControl = false;
  starSwarmWeaponLevel = 0;
  starSwarmSpeedBoostUntil = 0;
  starSwarmInvulnerableUntil = now + STAR_SWARM_RESPAWN_SAFE_MS;
}

void fireStarSwarmEnemyShot() {
  if (starSwarmGameOver || countActiveStarSwarmEnemies() == 0) return;

  int openSlot = -1;
  for (int i = 0; i < STAR_SWARM_MAX_ENEMY_SHOTS; i++) {
    if (!starSwarmEnemyShots[i].active) {
      openSlot = i;
      break;
    }
  }
  if (openSlot < 0) return;

  int start = random(0, STAR_SWARM_MAX_ENEMIES);
  for (int tries = 0; tries < STAR_SWARM_MAX_ENEMIES; tries++) {
    int i = (start + tries) % STAR_SWARM_MAX_ENEMIES;
    if (!starSwarmEnemies[i].active || !starSwarmOnScreen(starSwarmEnemies[i].x, starSwarmEnemies[i].y, 16)) continue;
    StarSwarmShot &shot = starSwarmEnemyShots[openSlot];
    shot.x = starSwarmEnemies[i].x;
    shot.y = starSwarmEnemies[i].y + 10;
    float dx = constrain(starSwarmPlayerX - shot.x, -70.0f, 70.0f);
    float speedRamp = getStarSwarmSpeedRamp(millis());
    shot.vx = dx * 0.012f * speedRamp;
    shot.vy = (1.45f + (starSwarmWave * 0.08f)) * speedRamp;
    shot.oldX = (int)shot.x;
    shot.oldY = (int)shot.y;
    shot.active = true;
    shot.wasDrawn = false;
    return;
  }
}

void updateStarSwarmGame(unsigned long now, bool firePressed, bool exitPressed) {
  if (currentScreen != SCREEN_STAR_SWARM_GAME) return;
  if (exitPressed) {
    setScreen(SCREEN_GAMES_MENU);
    return;
  }

  if (now - lastStarSwarmFrameTime < STAR_SWARM_FRAME_INTERVAL_MS) return;
  lastStarSwarmFrameTime = now;
  bool playerInvulnerable = isStarSwarmPlayerInvulnerable(now);

  if (starSwarmGameOver) {
    if (firePressed) {
      resetStarSwarmGame();
      fullRedraw = true;
      uiNeedsRedraw = true;
    }
    return;
  }

  if (starSwarmFireQueued || firePressed) {
    fireStarSwarmPlayerShot();
    starSwarmFireQueued = false;
  }

  float stickMoveX = inputChannels[0];
  float stickMoveY = inputChannels[1];
  float playerSpeed = (now < starSwarmSpeedBoostUntil) ? 7.2f : 4.9f;
  if (fabs(stickMoveX) > 0.08f || fabs(stickMoveY) > 0.08f) {
    starSwarmPlayerX += stickMoveX * playerSpeed;
    starSwarmPlayerY += stickMoveY * playerSpeed;
    starSwarmTouchControl = false;
    starSwarmStarted = true;
  } else if (starSwarmTouchControl) {
    // Touch is direct-control: the ship should stay under the player's finger.
    starSwarmPlayerX = starSwarmTouchTargetX;
    starSwarmPlayerY = starSwarmTouchTargetY;
  }
  starSwarmPlayerX = constrain(starSwarmPlayerX, 16.0f, 224.0f);
  starSwarmPlayerY = constrain(starSwarmPlayerY, (float)STAR_SWARM_PLAYER_MIN_Y, (float)STAR_SWARM_PLAYER_MAX_Y);

  for (int i = 0; i < STAR_SWARM_MAX_ENEMIES; i++) {
    updateStarSwarmEnemyPath(starSwarmEnemies[i], now);
  }

  unsigned long diveInterval = STAR_SWARM_DIVE_INTERVAL_MS;
  unsigned long diveReduction = min(1000UL, (unsigned long)(starSwarmWave - 1) * 90UL);
  diveInterval = max(1100UL, diveInterval - diveReduction);
  bool diveGraceElapsed = starSwarmWaveStartTime > 0 &&
    now - starSwarmWaveStartTime >= STAR_SWARM_FIRST_DIVE_GRACE_MS;
  if (!starSwarmGameOver && diveGraceElapsed && now - lastStarSwarmDiveTime >= diveInterval) {
    startStarSwarmDive(now);
  }

  for (int i = 0; i < STAR_SWARM_MAX_PLAYER_SHOTS; i++) {
    StarSwarmShot &shot = starSwarmPlayerShots[i];
    if (!shot.active) continue;
    shot.x += shot.vx;
    shot.y += shot.vy;
    if (shot.y < STAR_SWARM_VIEW_TOP + 8 || shot.x < -8.0f || shot.x > 248.0f) {
      shot.active = false;
      continue;
    }

    for (int e = 0; e < STAR_SWARM_MAX_ENEMIES; e++) {
      StarSwarmEnemy &enemy = starSwarmEnemies[e];
      if (!enemy.active || !starSwarmOnScreen(enemy.x, enemy.y, 18)) continue;
      if (fabs(shot.x - enemy.x) <= 11.0f && fabs(shot.y - enemy.y) <= 9.0f) {
        if (random(0, 100) < STAR_SWARM_POWERUP_DROP_PCT) {
          spawnStarSwarmPowerup(enemy.x, enemy.y);
        }
        enemy.active = false;
        shot.active = false;
        starSwarmScore += (enemy.state == 2) ? 140 : (50 + (enemy.row * 15));
        break;
      }
    }
  }

  for (int i = 0; i < STAR_SWARM_MAX_ENEMY_SHOTS; i++) {
    StarSwarmShot &shot = starSwarmEnemyShots[i];
    if (!shot.active) continue;
    shot.x += shot.vx;
    shot.y += shot.vy;
    if (shot.y > STAR_SWARM_VIEW_BOTTOM - 8 || shot.x < -8.0f || shot.x > 248.0f) {
      shot.active = false;
      continue;
    }
    if (!playerInvulnerable &&
        fabs(shot.x - starSwarmPlayerX) <= (STAR_SWARM_PLAYER_HIT_W / 2.0f) &&
        fabs(shot.y - starSwarmPlayerY) <= (STAR_SWARM_PLAYER_HIT_H / 2.0f)) {
      shot.active = false;
      destroyStarSwarmPlayer(now);
    }
  }

  for (int i = 0; i < STAR_SWARM_MAX_POWERUPS; i++) {
    StarSwarmPowerup &powerup = starSwarmPowerups[i];
    if (!powerup.active) continue;
    powerup.y += powerup.vy;
    if (powerup.y > STAR_SWARM_VIEW_BOTTOM) {
      powerup.active = false;
      continue;
    }
    if (fabs(powerup.x - starSwarmPlayerX) <= 16.0f &&
        fabs(powerup.y - starSwarmPlayerY) <= 14.0f) {
      if (powerup.type == 0) {
        if (starSwarmWeaponLevel < 2) starSwarmWeaponLevel++;
      } else {
        starSwarmSpeedBoostUntil = now + 9000UL;
      }
      powerup.active = false;
      starSwarmScore += 75;
    }
  }

  for (int i = 0; i < STAR_SWARM_MAX_ENEMIES; i++) {
    StarSwarmEnemy &enemy = starSwarmEnemies[i];
    if (enemy.active &&
        enemy.state == 2 &&
        starSwarmOnScreen(enemy.x, enemy.y, 18) &&
        !playerInvulnerable &&
        fabs(enemy.x - starSwarmPlayerX) <= ((STAR_SWARM_ENEMY_HIT_W + STAR_SWARM_PLAYER_HIT_W) / 2.0f) &&
        fabs(enemy.y - starSwarmPlayerY) <= ((STAR_SWARM_ENEMY_HIT_H + STAR_SWARM_PLAYER_HIT_H) / 2.0f)) {
      enemy.active = false;
      destroyStarSwarmPlayer(now);
      break;
    }
  }

  if (!starSwarmGameOver && now - lastStarSwarmEnemyShotTime >= STAR_SWARM_ENEMY_FIRE_MS) {
    lastStarSwarmEnemyShotTime = now;
    fireStarSwarmEnemyShot();
  }

  if (!starSwarmGameOver && countActiveStarSwarmEnemies() == 0) {
    starSwarmWave++;
    for (int i = 0; i < STAR_SWARM_MAX_PLAYER_SHOTS; i++) starSwarmPlayerShots[i].active = false;
    for (int i = 0; i < STAR_SWARM_MAX_ENEMY_SHOTS; i++) starSwarmEnemyShots[i].active = false;
    for (int i = 0; i < STAR_SWARM_MAX_POWERUPS; i++) starSwarmPowerups[i].active = false;
    spawnStarSwarmWave();
  }

  uiNeedsRedraw = true;
}

void handleStarSwarmTouch(int x, int y) {
  if (isInside(x, y, STAR_SWARM_EXIT_X, STAR_SWARM_EXIT_Y, STAR_SWARM_EXIT_W, STAR_SWARM_EXIT_H)) {
    queueScreenButton(BTN_BACK, SCREEN_GAMES_MENU);
    return;
  }

  if (starSwarmGameOver) {
    resetStarSwarmGame();
    fullRedraw = true;
    waitingForRelease = true;
    uiNeedsRedraw = true;
    return;
  }

  if (y >= STAR_SWARM_PLAYER_MIN_Y - 18) {
    starSwarmTouchTargetX = constrain(x, 16, 224);
    starSwarmTouchTargetY = constrain(y, STAR_SWARM_PLAYER_MIN_Y, STAR_SWARM_PLAYER_MAX_Y);
    starSwarmTouchControl = true;
    starSwarmStarted = true;
  }
  if (y < starSwarmPlayerY - 34) {
    starSwarmFireQueued = true;
  }

  uiNeedsRedraw = true;
}

void drawStarSwarmEnemy(int x, int y, uint8_t row) {
  uint16_t body = (row == 0) ? TFT_MAGENTA : (row == 1) ? TFT_CYAN : (row == 2) ? TFT_ORANGE : TFT_GREEN;
  uint16_t wing = (row == 0) ? TFT_YELLOW : (row == 1) ? TFT_SKYBLUE : (row == 2) ? TFT_RED : COLOR_ACCENT_HI;
  tft.fillRect(x - 4, y - 4, 8, 8, body);
  tft.fillTriangle(x - 10, y + 1, x - 4, y - 4, x - 4, y + 6, wing);
  tft.fillTriangle(x + 10, y + 1, x + 4, y - 4, x + 4, y + 6, wing);
  tft.drawPixel(x - 2, y - 1, TFT_BLACK);
  tft.drawPixel(x + 2, y - 1, TFT_BLACK);
}

void drawStarSwarmPlayer(int x, int y) {
  tft.fillTriangle(x, y - 11, x - 11, y + 10, x + 11, y + 10, COLOR_SIG);
  tft.fillTriangle(x, y - 6, x - 6, y + 8, x + 6, y + 8, COLOR_ACCENT_HI);
  tft.fillRect(x - 2, y + 7, 5, 4, TFT_ORANGE);
}

void clearStarSwarmEnemyRect(int x, int y) {
  tft.fillRect(x - ((STAR_SWARM_ENEMY_W / 2) + 2),
               y - ((STAR_SWARM_ENEMY_H / 2) + 2),
               STAR_SWARM_ENEMY_W + 4,
               STAR_SWARM_ENEMY_H + 5,
               TFT_BLACK);
}

void clearStarSwarmPlayerRect(int x, int y) {
  tft.fillRect(x - ((STAR_SWARM_PLAYER_W / 2) + 3),
               y - ((STAR_SWARM_PLAYER_H / 2) + 3),
               STAR_SWARM_PLAYER_W + 6,
               STAR_SWARM_PLAYER_H + 7,
               TFT_BLACK);
}

void clearStarSwarmShotRect(int x, int y) {
  tft.fillRect(x - (STAR_SWARM_SHOT_W / 2),
               y - (STAR_SWARM_SHOT_H / 2),
               STAR_SWARM_SHOT_W,
               STAR_SWARM_SHOT_H,
               TFT_BLACK);
}

void clearStarSwarmPowerupRect(int x, int y) {
  tft.fillRect(x - 8, y - 8, 17, 17, TFT_BLACK);
}

void drawStarSwarmShot(const StarSwarmShot &shot, bool playerShot) {
  int x = (int)roundf(shot.x);
  int y = (int)roundf(shot.y);
  uint16_t color = playerShot ? TFT_YELLOW : TFT_RED;
  tft.fillRect(x - 1, y - 5, 3, 10, color);
  tft.drawPixel(x, y - 6, playerShot ? TFT_WHITE : TFT_ORANGE);
}

void drawStarSwarmPowerup(const StarSwarmPowerup &powerup) {
  int x = (int)roundf(powerup.x);
  int y = (int)roundf(powerup.y);
  uint16_t color = (powerup.type == 0) ? TFT_CYAN : TFT_GREEN;
  tft.fillCircle(x, y, 6, color);
  tft.drawCircle(x, y, 7, TFT_WHITE);
  tft.setTextFont(1);
  tft.setTextColor(TFT_BLACK, color);
  tft.drawCentreString((powerup.type == 0) ? "W" : "S", x, y - 3, 1);
}

void drawStarSwarmStatic() {
  tft.fillScreen(TFT_BLACK);
  drawGameExitButton(STAR_SWARM_EXIT_X, STAR_SWARM_EXIT_Y,
                     STAR_SWARM_EXIT_W, STAR_SWARM_EXIT_H,
                     "EXIT", false, false);
  tft.drawFastHLine(0, STAR_SWARM_STATUS_H, 240, COLOR_ACCENT);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, TFT_BLACK);
  tft.drawString("STAR SWARM", 76, 8, 2);
  lastStarSwarmScore = -1;
  lastStarSwarmLives = -1;
  lastStarSwarmWave = -1;
  lastStarSwarmGameOver = !starSwarmGameOver;
  lastStarSwarmStarted = !starSwarmStarted;
  starSwarmPlayerWasDrawn = false;
  for (int i = 0; i < STAR_SWARM_MAX_ENEMIES; i++) starSwarmEnemies[i].wasDrawn = false;
  for (int i = 0; i < STAR_SWARM_MAX_PLAYER_SHOTS; i++) starSwarmPlayerShots[i].wasDrawn = false;
  for (int i = 0; i < STAR_SWARM_MAX_ENEMY_SHOTS; i++) starSwarmEnemyShots[i].wasDrawn = false;
  for (int i = 0; i < STAR_SWARM_MAX_POWERUPS; i++) starSwarmPowerups[i].wasDrawn = false;
}

void drawStarSwarmDynamic() {
  if (starSwarmScore != lastStarSwarmScore ||
      starSwarmLives != lastStarSwarmLives ||
      starSwarmWave != lastStarSwarmWave) {
    tft.fillRect(70, 4, 168, 20, TFT_BLACK);
    tft.setTextFont(2);
    tft.setTextColor(COLOR_TEXT, TFT_BLACK);
    tft.drawString(String(starSwarmScore), 76, 8, 2);
    tft.drawString("L:" + String(starSwarmLives), 142, 8, 2);
    tft.drawString("W:" + String(starSwarmWave), 186, 8, 2);
    lastStarSwarmScore = starSwarmScore;
    lastStarSwarmLives = starSwarmLives;
    lastStarSwarmWave = starSwarmWave;
  }

  if (starSwarmPlayerWasDrawn) clearStarSwarmPlayerRect(starSwarmOldPlayerX, starSwarmOldPlayerY);
  for (int i = 0; i < STAR_SWARM_MAX_ENEMIES; i++) {
    if (starSwarmEnemies[i].wasDrawn) clearStarSwarmEnemyRect(starSwarmEnemies[i].oldX, starSwarmEnemies[i].oldY);
  }
  for (int i = 0; i < STAR_SWARM_MAX_PLAYER_SHOTS; i++) {
    if (starSwarmPlayerShots[i].wasDrawn) clearStarSwarmShotRect(starSwarmPlayerShots[i].oldX, starSwarmPlayerShots[i].oldY);
  }
  for (int i = 0; i < STAR_SWARM_MAX_ENEMY_SHOTS; i++) {
    if (starSwarmEnemyShots[i].wasDrawn) clearStarSwarmShotRect(starSwarmEnemyShots[i].oldX, starSwarmEnemyShots[i].oldY);
  }
  for (int i = 0; i < STAR_SWARM_MAX_POWERUPS; i++) {
    if (starSwarmPowerups[i].wasDrawn) clearStarSwarmPowerupRect(starSwarmPowerups[i].oldX, starSwarmPowerups[i].oldY);
  }

  if (lastStarSwarmGameOver != starSwarmGameOver) {
    tft.fillRect(26, 112, 188, 82, TFT_BLACK);
    lastStarSwarmGameOver = starSwarmGameOver;
  }

  if (lastStarSwarmStarted != starSwarmStarted) {
    tft.fillRect(28, 240, 184, 42, TFT_BLACK);
    lastStarSwarmStarted = starSwarmStarted;
  }

  for (int i = 0; i < STAR_SWARM_MAX_ENEMIES; i++) {
    StarSwarmEnemy &enemy = starSwarmEnemies[i];
    if (enemy.active &&
        starSwarmOnScreen(enemy.x, enemy.y, 12) &&
        enemy.y >= STAR_SWARM_VIEW_TOP + (STAR_SWARM_ENEMY_H / 2)) {
      int x = (int)roundf(enemy.x);
      int y = (int)roundf(enemy.y);
      drawStarSwarmEnemy(x, y, enemy.row);
      enemy.oldX = x;
      enemy.oldY = y;
      enemy.wasDrawn = true;
    } else {
      enemy.wasDrawn = false;
    }
  }

  for (int i = 0; i < STAR_SWARM_MAX_PLAYER_SHOTS; i++) {
    StarSwarmShot &shot = starSwarmPlayerShots[i];
    if (shot.active) {
      drawStarSwarmShot(shot, true);
      shot.oldX = (int)roundf(shot.x);
      shot.oldY = (int)roundf(shot.y);
      shot.wasDrawn = true;
    } else {
      shot.wasDrawn = false;
    }
  }

  for (int i = 0; i < STAR_SWARM_MAX_ENEMY_SHOTS; i++) {
    StarSwarmShot &shot = starSwarmEnemyShots[i];
    if (shot.active) {
      drawStarSwarmShot(shot, false);
      shot.oldX = (int)roundf(shot.x);
      shot.oldY = (int)roundf(shot.y);
      shot.wasDrawn = true;
    } else {
      shot.wasDrawn = false;
    }
  }

  for (int i = 0; i < STAR_SWARM_MAX_POWERUPS; i++) {
    StarSwarmPowerup &powerup = starSwarmPowerups[i];
    if (powerup.active) {
      drawStarSwarmPowerup(powerup);
      powerup.oldX = (int)roundf(powerup.x);
      powerup.oldY = (int)roundf(powerup.y);
      powerup.wasDrawn = true;
    } else {
      powerup.wasDrawn = false;
    }
  }

  bool drawPlayer = !isStarSwarmPlayerInvulnerable(millis()) ||
    (((millis() / STAR_SWARM_FLASH_MS) & 1UL) == 0);
  if (drawPlayer) {
    drawStarSwarmPlayer((int)roundf(starSwarmPlayerX), (int)roundf(starSwarmPlayerY));
  }
  starSwarmOldPlayerX = (int)roundf(starSwarmPlayerX);
  starSwarmOldPlayerY = (int)roundf(starSwarmPlayerY);
  starSwarmPlayerWasDrawn = drawPlayer;

  if (starSwarmGameOver) {
    tft.fillRoundRect(28, 116, 184, 72, 10, TFT_BLACK);
    tft.drawRoundRect(28, 116, 184, 72, 10, TFT_RED);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawCentreString("GAME OVER", 120, 126, 4);
    tft.setTextColor(COLOR_TEXT, TFT_BLACK);
    tft.drawCentreString("Tap or SELECT restart", 120, 166, 2);
  } else if (!starSwarmStarted) {
    tft.setTextFont(2);
    tft.setTextColor(COLOR_TEXT, TFT_BLACK);
    tft.drawCentreString("Drag to move", 120, 244, 2);
    tft.drawCentreString("Tap high to fire", 120, 262, 2);
  }
}

void wrapAsteroidsPoint(float &x, float &y) {
  if (x < 0.0f) x += 240.0f;
  if (x >= 240.0f) x -= 240.0f;
  float h = ASTEROIDS_VIEW_BOTTOM - ASTEROIDS_VIEW_TOP;
  if (y < ASTEROIDS_VIEW_TOP) y += h;
  if (y >= ASTEROIDS_VIEW_BOTTOM) y -= h;
}

int findFreeAsteroidsRock() {
  for (int i = 0; i < ASTEROIDS_MAX_ROCKS; i++) {
    if (!asteroidsRocks[i].active) return i;
  }
  return -1;
}

int countActiveAsteroidsRocks() {
  int count = 0;
  for (int i = 0; i < ASTEROIDS_MAX_ROCKS; i++) {
    if (asteroidsRocks[i].active) count++;
  }
  return count;
}

void addAsteroidsRock(float x, float y, uint8_t size) {
  int slot = findFreeAsteroidsRock();
  if (slot < 0) return;
  AsteroidsRock &rock = asteroidsRocks[slot];
  rock.active = true;
  rock.x = x;
  rock.y = y;
  rock.size = size;
  rock.radius = (size == 3) ? 20.0f : ((size == 2) ? 14.0f : 8.0f);
  float angle = random(0, 628) / 100.0f;
  float speed = ((size == 3) ? 0.62f : ((size == 2) ? 0.88f : 1.15f)) + (asteroidsLevel * 0.035f);
  rock.vx = cosf(angle) * speed;
  rock.vy = sinf(angle) * speed;
}

void spawnAsteroidsWave() {
  for (int i = 0; i < ASTEROIDS_MAX_ROCKS; i++) asteroidsRocks[i].active = false;
  for (int i = 0; i < ASTEROIDS_MAX_BULLETS; i++) asteroidsBullets[i].active = false;

  int rockCount = min(ASTEROIDS_MAX_ROCKS, asteroidsLevel + 3);
  for (int i = 0; i < rockCount; i++) {
    float x = (random(0, 2) == 0) ? random(0, 45) : random(195, 240);
    float y = random(ASTEROIDS_VIEW_TOP + 12, ASTEROIDS_VIEW_BOTTOM - 12);
    addAsteroidsRock(x, y, 3);
  }
}

void resetAsteroidsShip(bool safeRespawn) {
  asteroidsShipX = 120.0f;
  asteroidsShipY = (ASTEROIDS_VIEW_TOP + ASTEROIDS_VIEW_BOTTOM) * 0.5f;
  asteroidsShipVX = 0.0f;
  asteroidsShipVY = 0.0f;
  asteroidsShipHeading = -1.5707963f;
  asteroidsInvulnerableUntil = safeRespawn ? (millis() + ASTEROIDS_RESPAWN_MS) : 0;
}

void resetAsteroidsGame() {
  asteroidsScore = 0;
  asteroidsLives = 3;
  asteroidsLevel = 1;
  asteroidsGameOver = false;
  asteroidsStarted = false;
  lastAsteroidsFrameTime = millis();
  lastAsteroidsShotTime = 0;
  resetAsteroidsShip(false);
  spawnAsteroidsWave();
}

void fireAsteroidsBullet() {
  unsigned long now = millis();
  if (now - lastAsteroidsShotTime < ASTEROIDS_FIRE_COOLDOWN_MS) return;
  for (int i = 0; i < ASTEROIDS_MAX_BULLETS; i++) {
    if (!asteroidsBullets[i].active) {
      AsteroidsBullet &bullet = asteroidsBullets[i];
      bullet.active = true;
      bullet.x = asteroidsShipX + cosf(asteroidsShipHeading) * 9.0f;
      bullet.y = asteroidsShipY + sinf(asteroidsShipHeading) * 9.0f;
      bullet.vx = asteroidsShipVX + cosf(asteroidsShipHeading) * ASTEROIDS_BULLET_SPEED;
      bullet.vy = asteroidsShipVY + sinf(asteroidsShipHeading) * ASTEROIDS_BULLET_SPEED;
      bullet.expiresAt = now + ASTEROIDS_BULLET_TTL_MS;
      lastAsteroidsShotTime = now;
      return;
    }
  }
}

void splitAsteroidsRock(int index) {
  if (index < 0 || index >= ASTEROIDS_MAX_ROCKS || !asteroidsRocks[index].active) return;
  AsteroidsRock rock = asteroidsRocks[index];
  asteroidsRocks[index].active = false;
  asteroidsScore += (rock.size == 3) ? 20 : ((rock.size == 2) ? 50 : 100);
  if (rock.size > 1) {
    addAsteroidsRock(rock.x + 3.0f, rock.y - 3.0f, rock.size - 1);
    addAsteroidsRock(rock.x - 3.0f, rock.y + 3.0f, rock.size - 1);
  }
}

void updateAsteroidsGame(unsigned long now, bool firePressed, bool exitPressed) {
  if (currentScreen != SCREEN_ASTEROIDS_GAME) return;
  if (exitPressed) {
    setScreen(SCREEN_GAMES_MENU);
    return;
  }

  if (asteroidsGameOver) {
    if (firePressed) resetAsteroidsGame();
    uiNeedsRedraw = true;
    return;
  }

  if (firePressed) {
    asteroidsStarted = true;
    fireAsteroidsBullet();
  }

  if (now - lastAsteroidsFrameTime < ASTEROIDS_FRAME_INTERVAL_MS) return;
  lastAsteroidsFrameTime = now;

  float turnInput = constrain(inputChannels[0], -1.0f, 1.0f);
  float thrustInput = constrain(inputChannels[1], -1.0f, 1.0f);
  if (fabs(turnInput) > 0.10f || fabs(thrustInput) > 0.10f) asteroidsStarted = true;

  asteroidsShipHeading += turnInput * ASTEROIDS_TURN_SPEED;
  if (thrustInput > 0.12f) {
    asteroidsShipVX += cosf(asteroidsShipHeading) * ASTEROIDS_THRUST * thrustInput;
    asteroidsShipVY += sinf(asteroidsShipHeading) * ASTEROIDS_THRUST * thrustInput;
  }
  asteroidsShipVX = constrain(asteroidsShipVX * ASTEROIDS_DRAG, -3.2f, 3.2f);
  asteroidsShipVY = constrain(asteroidsShipVY * ASTEROIDS_DRAG, -3.2f, 3.2f);
  asteroidsShipX += asteroidsShipVX;
  asteroidsShipY += asteroidsShipVY;
  wrapAsteroidsPoint(asteroidsShipX, asteroidsShipY);

  for (int i = 0; i < ASTEROIDS_MAX_BULLETS; i++) {
    if (!asteroidsBullets[i].active) continue;
    AsteroidsBullet &bullet = asteroidsBullets[i];
    bullet.x += bullet.vx;
    bullet.y += bullet.vy;
    wrapAsteroidsPoint(bullet.x, bullet.y);
    if (now >= bullet.expiresAt) bullet.active = false;
  }

  for (int i = 0; i < ASTEROIDS_MAX_ROCKS; i++) {
    if (!asteroidsRocks[i].active) continue;
    asteroidsRocks[i].x += asteroidsRocks[i].vx;
    asteroidsRocks[i].y += asteroidsRocks[i].vy;
    wrapAsteroidsPoint(asteroidsRocks[i].x, asteroidsRocks[i].y);
  }

  for (int b = 0; b < ASTEROIDS_MAX_BULLETS; b++) {
    if (!asteroidsBullets[b].active) continue;
    for (int r = 0; r < ASTEROIDS_MAX_ROCKS; r++) {
      if (!asteroidsRocks[r].active) continue;
      float dx = asteroidsBullets[b].x - asteroidsRocks[r].x;
      float dy = asteroidsBullets[b].y - asteroidsRocks[r].y;
      float hitR = asteroidsRocks[r].radius + 2.0f;
      if ((dx * dx) + (dy * dy) <= hitR * hitR) {
        asteroidsBullets[b].active = false;
        splitAsteroidsRock(r);
        break;
      }
    }
  }

  if (now > asteroidsInvulnerableUntil) {
    for (int r = 0; r < ASTEROIDS_MAX_ROCKS; r++) {
      if (!asteroidsRocks[r].active) continue;
      float dx = asteroidsShipX - asteroidsRocks[r].x;
      float dy = asteroidsShipY - asteroidsRocks[r].y;
      float hitR = asteroidsRocks[r].radius + ASTEROIDS_SHIP_RADIUS;
      if ((dx * dx) + (dy * dy) <= hitR * hitR) {
        asteroidsLives--;
        if (asteroidsLives <= 0) {
          asteroidsGameOver = true;
        } else {
          resetAsteroidsShip(true);
        }
        break;
      }
    }
  }

  if (!asteroidsGameOver && countActiveAsteroidsRocks() == 0) {
    asteroidsLevel++;
    resetAsteroidsShip(true);
    spawnAsteroidsWave();
  }

  uiNeedsRedraw = true;
}

void handleAsteroidsTouch(int x, int y) {
  if (isInside(x, y, ASTEROIDS_EXIT_X, ASTEROIDS_EXIT_Y, ASTEROIDS_EXIT_W, ASTEROIDS_EXIT_H)) {
    queueScreenButton(BTN_BACK, SCREEN_GAMES_MENU);
    return;
  }
  if (asteroidsGameOver) {
    resetAsteroidsGame();
  } else {
    asteroidsStarted = true;
    fireAsteroidsBullet();
  }
  waitingForRelease = true;
  uiNeedsRedraw = true;
}

void drawAsteroidsStatic() {
  tft.fillScreen(COLOR_BG);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  drawGameExitButton(ASTEROIDS_EXIT_X, ASTEROIDS_EXIT_Y, ASTEROIDS_EXIT_W, ASTEROIDS_EXIT_H, "EXIT", false, false);
  tft.drawFastHLine(0, ASTEROIDS_STATUS_H, 240, COLOR_ACCENT);
  tft.drawString("ASTEROIDS", 76, 8, 2);
}

void drawAsteroidsRock(const AsteroidsRock &rock, uint16_t color) {
  const int points = 9;
  int px[points];
  int py[points];
  for (int i = 0; i < points; i++) {
    float angle = (6.2831853f * i) / points;
    float wobble = 0.78f + (0.22f * ((i % 3) + 1));
    px[i] = (int)roundf(rock.x + cosf(angle) * rock.radius * wobble);
    py[i] = (int)roundf(rock.y + sinf(angle) * rock.radius * wobble);
  }
  for (int i = 0; i < points; i++) {
    int j = (i + 1) % points;
    tft.drawLine(px[i], py[i], px[j], py[j], color);
  }
}

void drawAsteroidsShip(uint16_t color) {
  float noseX = asteroidsShipX + cosf(asteroidsShipHeading) * 11.0f;
  float noseY = asteroidsShipY + sinf(asteroidsShipHeading) * 11.0f;
  float leftA = asteroidsShipHeading + 2.42f;
  float rightA = asteroidsShipHeading - 2.42f;
  float leftX = asteroidsShipX + cosf(leftA) * 9.0f;
  float leftY = asteroidsShipY + sinf(leftA) * 9.0f;
  float rightX = asteroidsShipX + cosf(rightA) * 9.0f;
  float rightY = asteroidsShipY + sinf(rightA) * 9.0f;
  tft.drawLine((int)noseX, (int)noseY, (int)leftX, (int)leftY, color);
  tft.drawLine((int)noseX, (int)noseY, (int)rightX, (int)rightY, color);
  tft.drawLine((int)leftX, (int)leftY, (int)rightX, (int)rightY, color);

  if (inputChannels[1] > 0.20f && (millis() / 80) % 2 == 0) {
    float tailX = asteroidsShipX - cosf(asteroidsShipHeading) * 12.0f;
    float tailY = asteroidsShipY - sinf(asteroidsShipHeading) * 12.0f;
    tft.drawLine((int)asteroidsShipX, (int)asteroidsShipY, (int)tailX, (int)tailY, COLOR_ACCENT_HI);
  }
}

void drawAsteroidsDynamic() {
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.fillRect(64, 6, 172, 16, COLOR_BG);
  tft.drawString(String(asteroidsScore), 68, 8, 2);
  tft.drawString("L:" + String(asteroidsLives), 150, 8, 2);
  tft.drawString("W:" + String(asteroidsLevel), 190, 8, 2);

  tft.fillRect(0, ASTEROIDS_VIEW_TOP, 240, ASTEROIDS_VIEW_BOTTOM - ASTEROIDS_VIEW_TOP, COLOR_BG);

  for (int i = 0; i < 18; i++) {
    int sx = (i * 47 + asteroidsLevel * 11) % 240;
    int sy = ASTEROIDS_VIEW_TOP + ((i * 83 + asteroidsScore / 10) % (ASTEROIDS_VIEW_BOTTOM - ASTEROIDS_VIEW_TOP));
    tft.drawPixel(sx, sy, COLOR_PANEL);
  }

  for (int i = 0; i < ASTEROIDS_MAX_ROCKS; i++) {
    if (asteroidsRocks[i].active) drawAsteroidsRock(asteroidsRocks[i], COLOR_ACCENT);
  }

  for (int i = 0; i < ASTEROIDS_MAX_BULLETS; i++) {
    if (!asteroidsBullets[i].active) continue;
    tft.fillCircle((int)asteroidsBullets[i].x, (int)asteroidsBullets[i].y, 2, COLOR_ACCENT_HI);
  }

  if (!asteroidsGameOver && (millis() > asteroidsInvulnerableUntil || (millis() / 120) % 2 == 0)) {
    drawAsteroidsShip(COLOR_TEXT);
  }

  if (asteroidsGameOver) {
    tft.setTextColor(TFT_RED, COLOR_BG);
    tft.drawCentreString("GAME OVER", 120, 132, 4);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawCentreString("SELECT or tap to restart", 120, 180, 2);
    tft.drawCentreString("DOWN exits to Games", 120, 200, 2);
  } else if (!asteroidsStarted) {
    tft.setTextColor(COLOR_ACCENT_HI, COLOR_BG);
    tft.drawCentreString("RX rotate  RY thrust", 120, 254, 2);
    tft.drawCentreString("Tap or SELECT to fire", 120, 274, 2);
  }
}

const char dungeonMap[DUNGEON_MAP_H][DUNGEON_MAP_W + 1] = {
  "############",
  "#..........#",
  "#..AA..B...#",
  "#......B...#",
  "#.CCC..BBB.#",
  "#.C........#",
  "#.C..DD..A.#",
  "#....D...A.#",
  "###..D.....#",
  "#....BBB.C.#",
  "#..........#",
  "############"
};

char dungeonWallTileAt(int mx, int my) {
  if (mx < 0 || mx >= DUNGEON_MAP_W || my < 0 || my >= DUNGEON_MAP_H) return '#';
  return dungeonMap[my][mx];
}

bool isDungeonWallAt(float x, float y) {
  int mx = (int)floorf(x);
  int my = (int)floorf(y);
  return dungeonWallTileAt(mx, my) != '.';
}

uint16_t dungeonWallColor(char tile, float distance, float textureX, int y, int wallTop, int wallBottom, bool sideHit) {
  int wallHeight = max(1, wallBottom - wallTop);
  int textureColumn = constrain((int)(textureX * 16.0f), 0, 15);
  int textureRow = constrain(((y - wallTop) * 24) / wallHeight, 0, 23);
  int shade = constrain(224 - (int)(distance * 24.0f), 48, 224);

  if (sideHit) shade = (shade * 3) / 4;

  int red = shade;
  int green = constrain((shade * 58) / 100, 24, 160);
  int blue = constrain((shade * 42) / 100, 18, 130);

  if (tile == 'A' || tile == '#') {
    bool mortar = (textureColumn == 0 || textureColumn == 15 || textureRow == 0 || textureRow == 11 || textureRow == 23);
    bool offsetSeam = ((textureRow / 6) & 1) && (textureColumn == 7 || textureColumn == 8);
    if (mortar) shade = (shade * 5) / 8;
    if (offsetSeam) shade = (shade * 3) / 4;
    red = shade;
    green = constrain((shade * 58) / 100, 24, 160);
    blue = constrain((shade * 42) / 100, 18, 130);
  } else if (tile == 'B') {
    bool rib = (textureColumn == 2 || textureColumn == 13 || textureRow == 5 || textureRow == 18);
    bool bolt = ((textureColumn == 4 || textureColumn == 11) && (textureRow == 3 || textureRow == 20));
    if (rib) shade = min(240, shade + 22);
    if (bolt) shade = max(36, shade - 48);
    red = constrain((shade * 42) / 100, 20, 120);
    green = constrain((shade * 72) / 100, 28, 170);
    blue = shade;
  } else if (tile == 'C') {
    bool crack = ((textureColumn + textureRow * 3) % 11) == 0;
    bool block = (textureColumn == 0 || textureColumn == 15 || textureRow == 7 || textureRow == 16);
    if (crack) shade = max(34, shade - 70);
    if (block) shade = (shade * 7) / 10;
    red = constrain((shade * 52) / 100, 22, 130);
    green = constrain((shade * 76) / 100, 28, 178);
    blue = constrain((shade * 48) / 100, 18, 120);
  } else if (tile == 'D') {
    bool stripe = ((textureColumn + textureRow) % 6) < 2;
    bool seam = textureColumn == 0 || textureColumn == 15;
    if (stripe) shade = min(240, shade + 28);
    if (seam) shade = max(38, shade - 44);
    red = shade;
    green = constrain((shade * 72) / 100, 26, 180);
    blue = constrain((shade * 28) / 100, 12, 90);
  }

  return tft.color565(red, green, blue);
}

uint16_t dungeonCeilingColor(int y) {
  int depth = constrain(y - DUNGEON_VIEW_TOP, 0, DUNGEON_VIEW_H / 2);
  int shade = 18 + depth / 8;
  return tft.color565(shade, shade + 3, shade + 12);
}

uint16_t dungeonFloorColor(int y, int x) {
  int depth = constrain(y - (DUNGEON_VIEW_TOP + DUNGEON_VIEW_H / 2), 0, DUNGEON_VIEW_H / 2);
  bool seam = ((y + (x / 2)) % 18) == 0;
  int shade = 34 + depth / 6;
  if (seam) shade = max(24, shade - 12);
  return tft.color565(shade + 5, shade, shade - 4);
}

bool dungeonSpriteOpaqueAt(int screenX, int screenY, int spriteX, int spriteY,
                           int spriteW, int spriteH, const uint16_t *data) {
  int sx = screenX - spriteX;
  int sy = screenY - spriteY;
  if (sx < 0 || sx >= spriteW || sy < 0 || sy >= spriteH) return false;
  return data[(sy * spriteW) + sx] != DUNGEON_RUN_TRANSPARENT_RGB565;
}

int dungeonPistolXForWidth(int spriteW) {
  return 120 - DUNGEON_PISTOL_MUZZLE_SRC_X;
}

bool dungeonGunMaskAt(int x, int y) {
  int idleX = dungeonPistolXForWidth(DUNGEON_RUN_PISTOL_IDLE_W);
  int idleY = DUNGEON_VIEW_BOTTOM - DUNGEON_RUN_PISTOL_IDLE_H;
  return dungeonSpriteOpaqueAt(x, y, idleX, idleY, DUNGEON_RUN_PISTOL_IDLE_W, DUNGEON_RUN_PISTOL_IDLE_H, DUNGEON_RUN_PISTOL_IDLE);
}

void buildDungeonIdlePistolMask() {
  if (dungeonIdlePistolMaskReady) return;
  for (int y = 0; y < DUNGEON_RUN_PISTOL_IDLE_H; y++) {
    int minX = DUNGEON_RUN_PISTOL_IDLE_W;
    int maxX = -1;
    for (int x = 0; x < DUNGEON_RUN_PISTOL_IDLE_W; x++) {
      uint16_t color = DUNGEON_RUN_PISTOL_IDLE[(y * DUNGEON_RUN_PISTOL_IDLE_W) + x];
      if (color == DUNGEON_RUN_TRANSPARENT_RGB565) continue;
      if (x < minX) minX = x;
      if (x > maxX) maxX = x;
    }
    dungeonIdlePistolMinX[y] = minX;
    dungeonIdlePistolMaxX[y] = maxX;
  }
  dungeonIdlePistolMaskReady = true;
}

void dungeonFillWorldStrip(int x, int y, int w, int h, uint16_t color) {
  if (h <= 0 || w <= 0) return;
  if (!dungeonPreserveIdlePistol) {
    tft.fillRect(x, y, w, h, color);
    return;
  }

  buildDungeonIdlePistolMask();
  int idleX = dungeonPistolXForWidth(DUNGEON_RUN_PISTOL_IDLE_W);
  int idleY = DUNGEON_VIEW_BOTTOM - DUNGEON_RUN_PISTOL_IDLE_H;
  int rectRight = x + w - 1;
  int rectBottom = y + h - 1;
  if (rectRight < idleX || x >= idleX + DUNGEON_RUN_PISTOL_IDLE_W ||
      rectBottom < idleY || y >= idleY + DUNGEON_RUN_PISTOL_IDLE_H) {
    tft.fillRect(x, y, w, h, color);
    return;
  }

  for (int py = y; py < y + h; py++) {
    int row = py - idleY;
    if (row < 0 || row >= DUNGEON_RUN_PISTOL_IDLE_H ||
        dungeonIdlePistolMaxX[row] < dungeonIdlePistolMinX[row]) {
      tft.drawFastHLine(x, py, w, color);
      continue;
    }

    int blockLeft = idleX + dungeonIdlePistolMinX[row];
    int blockRight = idleX + dungeonIdlePistolMaxX[row];
    if (rectRight < blockLeft || x > blockRight) {
      tft.drawFastHLine(x, py, w, color);
      continue;
    }

    if (x < blockLeft) {
      int leftW = min(rectRight, blockLeft - 1) - x + 1;
      if (leftW > 0) tft.drawFastHLine(x, py, leftW, color);
    }
    if (rectRight > blockRight) {
      int rightX = max(x, blockRight + 1);
      int rightW = rectRight - rightX + 1;
      if (rightW > 0) tft.drawFastHLine(rightX, py, rightW, color);
    }
  }
}

void drawDungeonImpactMark(int x, int y) {
  uint16_t dark = tft.color565(26, 18, 12);
  uint16_t hot = tft.color565(250, 208, 92);
  tft.drawLine(x - 7, y - 1, x + 7, y + 1, dark);
  tft.drawLine(x - 1, y - 7, x + 1, y + 7, dark);
  tft.drawLine(x - 5, y + 5, x + 5, y - 5, dark);
  tft.drawPixel(x, y, hot);
  tft.drawPixel(x - 1, y, hot);
  tft.drawPixel(x + 1, y, hot);
}

void drawDungeonGunOverlay(bool muzzleVisible) {
  unsigned long age = (lastDungeonShotTime == 0) ? DUNGEON_FIRE_ANIM_MS : (millis() - lastDungeonShotTime);
  const uint16_t *pistol = DUNGEON_RUN_PISTOL_IDLE;
  int pistolW = DUNGEON_RUN_PISTOL_IDLE_W;
  int pistolH = DUNGEON_RUN_PISTOL_IDLE_H;

  if (age < DUNGEON_FIRE_ANIM_MS) {
    if (age < 70) {
      pistol = DUNGEON_RUN_PISTOL_FIRE0;
      pistolW = DUNGEON_RUN_PISTOL_FIRE0_W;
      pistolH = DUNGEON_RUN_PISTOL_FIRE0_H;
    } else if (age < 140) {
      pistol = DUNGEON_RUN_PISTOL_FIRE1;
      pistolW = DUNGEON_RUN_PISTOL_FIRE1_W;
      pistolH = DUNGEON_RUN_PISTOL_FIRE1_H;
    } else {
      pistol = DUNGEON_RUN_PISTOL_FIRE2;
      pistolW = DUNGEON_RUN_PISTOL_FIRE2_W;
      pistolH = DUNGEON_RUN_PISTOL_FIRE2_H;
    }
  }

  int pistolX = dungeonPistolXForWidth(pistolW);
  int pistolY = DUNGEON_VIEW_BOTTOM - pistolH;
  if (muzzleVisible && age < 70) {
    drawDungeonRunSpriteKeyed(
      120 - DUNGEON_PISTOL_FLASH_CENTER_X,
      pistolY - 17,
      DUNGEON_RUN_PISTOL_FLASH_W,
      DUNGEON_RUN_PISTOL_FLASH_H,
      DUNGEON_RUN_PISTOL_FLASH);
  }
  drawDungeonRunSpriteKeyed(pistolX, pistolY, pistolW, pistolH, pistol);
}

void drawDungeonRunSpriteKeyed(int x, int y, int w, int h, const uint16_t *data) {
  for (int sy = 0; sy < h; sy++) {
    int dy = y + sy;
    if (dy < 0 || dy >= 320) continue;
    for (int sx = 0; sx < w; sx++) {
      int dx = x + sx;
      if (dx < 0 || dx >= 240) continue;
      uint16_t color = data[(sy * w) + sx];
      if (color == DUNGEON_RUN_TRANSPARENT_RGB565) continue;
      tft.drawPixel(dx, dy, color);
    }
  }
}

void drawDungeonRunHudNumber(int value, int x, int y, int digits) {
  value = constrain(value, 0, 999);
  int divisor = 1;
  for (int i = 1; i < digits; i++) divisor *= 10;

  for (int i = 0; i < digits; i++) {
    int digit = (value / divisor) % 10;
    drawDungeonRunSpriteKeyed(
      x + (i * (DUNGEON_RUN_HUD_DIGIT_W + 1)), y,
      DUNGEON_RUN_HUD_DIGIT_W, DUNGEON_RUN_HUD_DIGIT_H,
      DUNGEON_RUN_HUD_DIGITS[digit]);
    divisor /= 10;
  }
}

void drawDungeonRunHud() {
  tft.fillRect(0, DUNGEON_HUD_Y, 240, DUNGEON_HUD_H, COLOR_BG);
  drawDungeonRunSpriteKeyed(
    DUNGEON_HUD_FACE_X, DUNGEON_HUD_FACE_Y,
    DUNGEON_RUN_HUD_FACE_W, DUNGEON_RUN_HUD_FACE_H,
    DUNGEON_RUN_HUD_FACE);
  drawDungeonRunSpriteKeyed(
    0, DUNGEON_HUD_Y,
    DUNGEON_RUN_HUD_BAR_W, DUNGEON_RUN_HUD_BAR_H,
    DUNGEON_RUN_HUD_BAR);
  drawDungeonRunHudNumber(dungeonAmmo, 3, DUNGEON_HUD_Y + 4, 3);
  drawDungeonRunHudNumber(dungeonHealth, 48, DUNGEON_HUD_Y + 4, 3);
}

void resetDungeonRun() {
  dungeonPlayerX = 2.5f;
  dungeonPlayerY = 2.5f;
  dungeonPlayerHeading = 1.5707963f;
  dungeonHealth = 100;
  dungeonAmmo = 50;
  dungeonStarted = false;
  lastDungeonFrameTime = millis();
  lastDungeonShotTime = 0;
  dungeonMuzzleFlashUntil = 0;
  dungeonFrameDirty = true;
  dungeonStatusDirty = true;
  dungeonGunDirty = true;
  dungeonMuzzleWasVisible = false;
  dungeonWeaponAnimWasVisible = false;
  dungeonRestoreWeaponBackdrop = false;
  dungeonImpactWasVisible = false;
  dungeonImpactUntil = 0;
  lastDungeonDrawnHealth = -1;
  lastDungeonDrawnAmmo = -1;
}

void fireDungeonWeapon() {
  if (millis() - lastDungeonShotTime < DUNGEON_FIRE_COOLDOWN_MS) return;
  if (dungeonAmmo <= 0) return;
  dungeonStarted = true;
  dungeonAmmo--;
  lastDungeonShotTime = millis();
  dungeonMuzzleFlashUntil = lastDungeonShotTime + DUNGEON_MUZZLE_FLASH_MS;
  dungeonImpactUntil = lastDungeonShotTime + DUNGEON_IMPACT_MS;
  dungeonStatusDirty = true;
  dungeonGunDirty = true;
  dungeonRestoreWeaponBackdrop = true;
  dungeonFrameDirty = true;
  uiNeedsRedraw = true;
}

void updateDungeonRun(unsigned long now, bool firePressed, bool exitPressed) {
  if (currentScreen != SCREEN_DUNGEON_RUN) return;
  if (exitPressed) {
    setScreen(SCREEN_GAMES_MENU);
    return;
  }

  if (firePressed) fireDungeonWeapon();
  bool weaponAnimating = lastDungeonShotTime > 0 && (now - lastDungeonShotTime) < DUNGEON_FIRE_ANIM_MS;
  if (weaponAnimating || dungeonWeaponAnimWasVisible) {
    dungeonFrameDirty = true;
    dungeonRestoreWeaponBackdrop = true;
  }
  if (dungeonWeaponAnimWasVisible && !weaponAnimating) {
    dungeonGunDirty = true;
  }
  if (dungeonMuzzleWasVisible && now >= dungeonMuzzleFlashUntil) {
    dungeonFrameDirty = true;
  }
  if (dungeonImpactWasVisible && now >= dungeonImpactUntil) {
    dungeonFrameDirty = true;
  }
  if (weaponAnimating || dungeonWeaponAnimWasVisible ||
      now < dungeonMuzzleFlashUntil || dungeonMuzzleWasVisible ||
      now < dungeonImpactUntil || dungeonImpactWasVisible) {
    uiNeedsRedraw = true;
  }
  if (now - lastDungeonFrameTime < DUNGEON_FRAME_INTERVAL_MS) return;
  lastDungeonFrameTime = now;

  float turnInput = constrain(inputChannels[0], -1.0f, 1.0f);      // CH1 / right stick X
  float moveInput = constrain(inputChannels[1], -1.0f, 1.0f);      // CH2 / right stick Y
  float strafeInput = constrain(inputChannels[3], -1.0f, 1.0f);    // CH4 / left stick X
  bool strafing = fabs(strafeInput) > 0.08f;
  bool moving = fabs(turnInput) > 0.08f || fabs(moveInput) > 0.08f || strafing;
  bool wasStarted = dungeonStarted;
  if (moving) {
    dungeonStarted = true;
  }
  if (!wasStarted && dungeonStarted) {
    dungeonRestoreWeaponBackdrop = true;
  }

  if (fabs(turnInput) > 0.08f) {
    dungeonPlayerHeading += turnInput * DUNGEON_TURN_SPEED;
    while (dungeonPlayerHeading < 0.0f) dungeonPlayerHeading += 6.2831853f;
    while (dungeonPlayerHeading >= 6.2831853f) dungeonPlayerHeading -= 6.2831853f;
  }

  if (fabs(moveInput) > 0.08f) {
    float step = moveInput * DUNGEON_MOVE_SPEED;
    float nx = dungeonPlayerX + cosf(dungeonPlayerHeading) * step;
    float ny = dungeonPlayerY + sinf(dungeonPlayerHeading) * step;
    if (!isDungeonWallAt(nx, dungeonPlayerY)) dungeonPlayerX = nx;
    if (!isDungeonWallAt(dungeonPlayerX, ny)) dungeonPlayerY = ny;
  }

  if (strafing) {
    float step = strafeInput * DUNGEON_STRAFE_SPEED;
    float strafeAngle = dungeonPlayerHeading + 1.5707963f;
    float nx = dungeonPlayerX + cosf(strafeAngle) * step;
    float ny = dungeonPlayerY + sinf(strafeAngle) * step;
    if (!isDungeonWallAt(nx, dungeonPlayerY)) dungeonPlayerX = nx;
    if (!isDungeonWallAt(dungeonPlayerX, ny)) dungeonPlayerY = ny;
  }

  if (moving) {
    dungeonFrameDirty = true;
    uiNeedsRedraw = true;
  }
}

void handleDungeonRunTouch(int x, int y) {
  if (isInside(x, y, DUNGEON_EXIT_X, DUNGEON_EXIT_Y, DUNGEON_EXIT_W, DUNGEON_EXIT_H)) {
    queueScreenButton(BTN_BACK, SCREEN_GAMES_MENU);
    return;
  }

  fireDungeonWeapon();
  waitingForRelease = true;
  uiNeedsRedraw = true;
}

void drawDungeonRunStatic() {
  tft.fillScreen(COLOR_BG);
  tft.setTextFont(2);
  drawGameExitButton(DUNGEON_EXIT_X, DUNGEON_EXIT_Y, DUNGEON_EXIT_W, DUNGEON_EXIT_H, "EXIT", false, false);
  tft.drawFastHLine(0, DUNGEON_STATUS_H, 240, COLOR_ACCENT);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawString("DUNGEON RUN", 72, 8, 2);
  drawDungeonRunHud();
  dungeonFrameDirty = true;
  dungeonStatusDirty = true;
  dungeonGunDirty = true;
  lastDungeonDrawnHealth = -1;
  lastDungeonDrawnAmmo = -1;
}

void drawDungeonRunDynamic() {
  unsigned long now = millis();
  bool muzzleVisible = now < dungeonMuzzleFlashUntil;
  bool weaponAnimating = lastDungeonShotTime > 0 && (now - lastDungeonShotTime) < DUNGEON_FIRE_ANIM_MS;
  bool impactVisible = now < dungeonImpactUntil;
  bool redrawFrame = dungeonFrameDirty;
  bool gunOverlayDynamic = weaponAnimating || dungeonWeaponAnimWasVisible || muzzleVisible || dungeonMuzzleWasVisible;

  tft.setTextFont(2);
  if (dungeonStatusDirty ||
      dungeonHealth != lastDungeonDrawnHealth ||
      dungeonAmmo != lastDungeonDrawnAmmo) {
    tft.fillRect(150, 6, 84, 16, COLOR_BG);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString("H:" + String(dungeonHealth), 150, 8, 2);
    tft.drawString("A:" + String(dungeonAmmo), 190, 8, 2);
    lastDungeonDrawnHealth = dungeonHealth;
    lastDungeonDrawnAmmo = dungeonAmmo;
    drawDungeonRunHud();
    dungeonStatusDirty = false;
  }

  if (!redrawFrame) {
    if (impactVisible) {
      drawDungeonImpactMark(120, DUNGEON_VIEW_TOP + (DUNGEON_VIEW_H / 2));
      uiNeedsRedraw = true;
    }
    dungeonImpactWasVisible = impactVisible;

    if (dungeonGunDirty || weaponAnimating || dungeonWeaponAnimWasVisible ||
        muzzleVisible || dungeonMuzzleWasVisible) {
      drawDungeonGunOverlay(muzzleVisible);
      dungeonGunDirty = false;
      dungeonMuzzleWasVisible = muzzleVisible;
      dungeonWeaponAnimWasVisible = weaponAnimating;
      if (muzzleVisible || weaponAnimating) uiNeedsRedraw = true;
    }
    return;
  }

  dungeonPreserveIdlePistol = !gunOverlayDynamic && !dungeonGunDirty;
  int horizon = DUNGEON_VIEW_TOP + (DUNGEON_VIEW_H / 2);

  for (int ray = 0; ray < DUNGEON_RAY_COUNT; ray++) {
    int screenX = ray * DUNGEON_RAY_STEP;
    float camera = ((ray + 0.5f) / (float)DUNGEON_RAY_COUNT) - 0.5f;
    float rayAngle = dungeonPlayerHeading + (camera * DUNGEON_FOV);
    float rayDirX = cosf(rayAngle);
    float rayDirY = sinf(rayAngle);

    int mapX = (int)floorf(dungeonPlayerX);
    int mapY = (int)floorf(dungeonPlayerY);
    float deltaDistX = (fabs(rayDirX) < 0.0001f) ? 1000000.0f : fabs(1.0f / rayDirX);
    float deltaDistY = (fabs(rayDirY) < 0.0001f) ? 1000000.0f : fabs(1.0f / rayDirY);
    float sideDistX;
    float sideDistY;
    int stepX;
    int stepY;
    bool sideHit = false;
    bool hit = false;

    if (rayDirX < 0.0f) {
      stepX = -1;
      sideDistX = (dungeonPlayerX - mapX) * deltaDistX;
    } else {
      stepX = 1;
      sideDistX = (mapX + 1.0f - dungeonPlayerX) * deltaDistX;
    }

    if (rayDirY < 0.0f) {
      stepY = -1;
      sideDistY = (dungeonPlayerY - mapY) * deltaDistY;
    } else {
      stepY = 1;
      sideDistY = (mapY + 1.0f - dungeonPlayerY) * deltaDistY;
    }

    for (int stepCount = 0; stepCount < 32; stepCount++) {
      if (sideDistX < sideDistY) {
        sideDistX += deltaDistX;
        mapX += stepX;
        sideHit = false;
      } else {
        sideDistY += deltaDistY;
        mapY += stepY;
        sideHit = true;
      }

      if (mapX < 0 || mapX >= DUNGEON_MAP_W || mapY < 0 || mapY >= DUNGEON_MAP_H ||
          dungeonWallTileAt(mapX, mapY) != '.') {
        hit = true;
        break;
      }
    }

    float corrected = hit
      ? (sideHit ? (sideDistY - deltaDistY) : (sideDistX - deltaDistX))
      : 12.0f;
    corrected = max(corrected, 0.12f);
    int wallH = constrain((int)(DUNGEON_VIEW_H / corrected), 8, DUNGEON_VIEW_H);
    int wallTop = horizon - (wallH / 2);
    int wallBottom = wallTop + wallH;
    wallTop = max(DUNGEON_VIEW_TOP, wallTop);
    wallBottom = min(DUNGEON_VIEW_BOTTOM, wallBottom);

    float wallX = sideHit
      ? dungeonPlayerX + corrected * rayDirX
      : dungeonPlayerY + corrected * rayDirY;
    wallX -= floorf(wallX);
    char wallTile = hit ? dungeonWallTileAt(mapX, mapY) : '#';

    int ceilingY = DUNGEON_VIEW_TOP;
    while (ceilingY < wallTop) {
      int segmentEnd = min(wallTop, ceilingY + 8);
      dungeonFillWorldStrip(screenX, ceilingY, DUNGEON_RAY_STEP, segmentEnd - ceilingY, dungeonCeilingColor(ceilingY));
      ceilingY = segmentEnd;
    }

    int segmentStart = wallTop;
    while (segmentStart < wallBottom) {
      int segmentEnd = min(wallBottom, segmentStart + 4);
      uint16_t wallColor = dungeonWallColor(wallTile, corrected, wallX, segmentStart, wallTop, wallBottom, sideHit);
      dungeonFillWorldStrip(screenX, segmentStart, DUNGEON_RAY_STEP, segmentEnd - segmentStart, wallColor);
      segmentStart = segmentEnd;
    }

    int floorY = wallBottom;
    while (floorY < DUNGEON_VIEW_BOTTOM) {
      int segmentEnd = min(DUNGEON_VIEW_BOTTOM, floorY + 8);
      dungeonFillWorldStrip(screenX, floorY, DUNGEON_RAY_STEP, segmentEnd - floorY, dungeonFloorColor(floorY, screenX));
      floorY = segmentEnd;
    }
  }

  dungeonPreserveIdlePistol = false;

  if (impactVisible) drawDungeonImpactMark(120, horizon);

  if (!dungeonStarted) {
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawCentreString("RX turn  RY move", 120, 186, 2);
    tft.drawCentreString("LX strafe  Tap fire", 120, 204, 2);
  }

  if (dungeonGunDirty || gunOverlayDynamic) {
    drawDungeonGunOverlay(muzzleVisible);
    dungeonGunDirty = false;
  }

  dungeonFrameDirty = false;
  dungeonRestoreWeaponBackdrop = false;
  dungeonMuzzleWasVisible = muzzleVisible;
  dungeonWeaponAnimWasVisible = weaponAnimating;
  dungeonImpactWasVisible = impactVisible;
  if (muzzleVisible || weaponAnimating || impactVisible) uiNeedsRedraw = true;
}

  void drawControllerSettings() {
  tft.fillScreen(COLOR_BG);

  const char* navLabel = (controllerSettingsPage == 0) ? "NEXT" : "PREV";

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    pressedButton == BTN_BACK,
    dpadFocusVisible && selectedButton == BTN_BACK,
    BACK_TEXT_OFFSET);

    tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);

  drawButtonBubble(130, BACK_BTN_Y, 100, BACK_BTN_H, navLabel,
                   pressedButton == BTN_PAGE_NAV, dpadFocusVisible && selectedButton == BTN_PAGE_NAV, 40);

  if (controllerSettingsPage == 0) {
    drawButtonBubble(20, 50, 200, BTN_HEIGHT, "Trim",
                     pressedButton == BTN_TRIM, dpadFocusVisible && selectedButton == BTN_TRIM, 100);
    drawButtonBubble(20, 120, 200, BTN_HEIGHT, "Failsafe",
                     pressedButton == BTN_FAILSAFE, dpadFocusVisible && selectedButton == BTN_FAILSAFE, 100);
    drawButtonBubble(20, 190, 200, BTN_HEIGHT, "End Points",
                     pressedButton == BTN_ENDPOINTS, dpadFocusVisible && selectedButton == BTN_ENDPOINTS, -1);
  } else if (controllerSettingsPage == 1) {
    drawButtonBubble(20, 50, 200, BTN_HEIGHT, "Expo",
                     pressedButton == BTN_EXPO, dpadFocusVisible && selectedButton == BTN_EXPO, 100);
    drawButtonBubble(20, 120, 200, BTN_HEIGHT, "Rates",
                     pressedButton == BTN_RATES, dpadFocusVisible && selectedButton == BTN_RATES, 100);
    drawButtonBubble(20, 190, 200, BTN_HEIGHT, "Protocol",
                     pressedButton == BTN_PROTOCOL, dpadFocusVisible && selectedButton == BTN_PROTOCOL, 100);
  } else if (STICK_CAL_SCREEN_ENABLED) {
    drawButtonBubble(20, 85, 200, BTN_HEIGHT, "Stick Calibration",
                     pressedButton == BTN_STICK_CAL, dpadFocusVisible && selectedButton == BTN_STICK_CAL, -1);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.setTextFont(2);
    tft.drawCentreString("Factory setup only", 120, 176, 2);
    tft.drawCentreString("Stores min / center / max", 120, 196, 2);
  }
  }

// Controller tuning screens. Static functions draw labels/chrome; dynamic
// functions refresh live values, focus, and selection state.
void drawEndpointStatic() {
  tft.fillScreen(COLOR_BG);

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    false,
    (dpadFocusVisible && focusIndex == CHANNEL_COUNT),
    BACK_TEXT_OFFSET);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT);
  tft.drawCentreString("End Points", 120, 38, 2);
  tft.drawCentreString("Move stick -/+ to pick side", 120, 58, 2);

  int startY = 78;
  int spacing = 33;
  for (int i = 0; i < CHANNEL_COUNT; i++) {
    int y = startY + (i * spacing);
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(16, y + 10);
    tft.print("CH");
    tft.print(i + 1);
    tft.setCursor(52, y + 10);
    tft.print(getChannelAxisName(i));
    tft.drawRoundRect(88, y, 136, 30, 10, COLOR_ACCENT);
  }
}

void drawEndpointDynamic() {
  if (!endpointNeedsRedraw) return;

  int startY = 78;
  int spacing = 33;

  for (int i = 0; i < CHANNEL_COUNT; i++) {
    int y = startY + (i * spacing);
    int low = getModelEndpointLowValue(activeModel, i);
    int high = getModelEndpointHighValue(activeModel, i);
    bool rowFocused = (dpadFocusVisible && focusIndex == i);
    bool highSide = isEndpointHighSideSelected(i);

    tft.fillRect(84, y - 4, 146, 40, COLOR_BG);
    tft.fillRoundRect(90, y + 2, 132, 26, 8, COLOR_PANEL);
    tft.drawRoundRect(88, y, 136, 30, 10, COLOR_ACCENT);

    if (rowFocused) {
      tft.drawRoundRect(86, y - 2, 140, 34, 10, COLOR_ACCENT_HI);
    }

    char lowText[8];
    char highText[8];
    snprintf(lowText, sizeof(lowText), "%3d", low);
    snprintf(highText, sizeof(highText), "%3d", high);

    int lowBoxX = 94;
    int lowBoxW = 46;
    int valueBoxY = y + 4;
    int valueBoxH = 22;
    int highBoxX = 170;
    int highBoxW = 46;

    tft.fillRoundRect(lowBoxX, valueBoxY, lowBoxW, valueBoxH, 6, COLOR_BG);
    tft.drawRoundRect(lowBoxX, valueBoxY, lowBoxW, valueBoxH, 6,
                      (rowFocused && !highSide) ? COLOR_ACCENT_HI : COLOR_ACCENT);
    tft.fillRoundRect(highBoxX, valueBoxY, highBoxW, valueBoxH, 6, COLOR_BG);
    tft.drawRoundRect(highBoxX, valueBoxY, highBoxW, valueBoxH, 6,
                      (rowFocused && highSide) ? COLOR_ACCENT_HI : COLOR_ACCENT);

    tft.setTextColor((rowFocused && !highSide) ? COLOR_ACCENT_HI : COLOR_TEXT);
    tft.drawCentreString(lowText, lowBoxX + (lowBoxW / 2), y + 9, 2);

    tft.setTextColor(COLOR_TEXT);
    tft.drawString(">", 153, y + 9, 2);

    tft.setTextColor((rowFocused && highSide) ? COLOR_ACCENT_HI : COLOR_TEXT);
    tft.drawCentreString(highText, highBoxX + (highBoxW / 2), y + 9, 2);
  }

  endpointNeedsRedraw = false;
}

  void drawControllerPlaceholderScreen(const char* title, const char* line1, const char* line2) {
  tft.fillScreen(COLOR_BG);

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    pressedButton == BTN_BACK,
    dpadFocusVisible && selectedButton == BTN_BACK,
    BACK_TEXT_OFFSET);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT);
  tft.drawCentreString(title, 120, 52, 2);
  tft.drawCentreString(line1, 120, 112, 2);
  tft.drawCentreString(line2, 120, 136, 2);
  }

  void drawStickCalibrationScreen() {
  tft.fillScreen(COLOR_BG);

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    pressedButton == BTN_BACK,
    dpadFocusVisible && focusIndex == 1,
    BACK_TEXT_OFFSET);

  const char *actionLabel =
    (stickCalibrationState == STICK_CAL_STATE_CAPTURE_CENTER)
      ? "CAPTURE"
      : "SAVE CAL";

  drawButtonBubble(
    130, BACK_BTN_Y, 100, BACK_BTN_H,
    actionLabel,
    pressedButton == BTN_STICK_CAL,
    dpadFocusVisible && focusIndex == 0,
    8);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawCentreString("Stick Calibration", 120, 24, 2);

  if (stickCalibrationState == STICK_CAL_STATE_CAPTURE_CENTER) {
    tft.drawCentreString("Center sticks, then CAPTURE.", 120, 46, 2);
  } else {
    tft.drawCentreString("Sweep full travel, then SAVE.", 120, 46, 2);
  }

  int startY = 78;
  int rowH = 40;
  for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
    int y = startY + (channel * rowH);
    int16_t minValue = (stickCalibrationState == STICK_CAL_STATE_SWEEP_RANGE)
      ? stickCalibrationSweepMin[channel]
      : adsStickMin[channel];
    int16_t maxValue = (stickCalibrationState == STICK_CAL_STATE_SWEEP_RANGE)
      ? stickCalibrationSweepMax[channel]
      : adsStickMax[channel];

    tft.setCursor(8, y);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.printf("A%d L%5d C%5d", channel, minValue, adsStickCenter[channel]);
    tft.setCursor(8, y + 16);
    tft.printf("   H%5d RAW%5d", maxValue, adsLastRawValues[channel]);
  }
  }

  void drawExpoStatic() {
  tft.fillScreen(COLOR_BG);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT);
  tft.drawCentreString("Expo", 120, 38, 2);
  tft.drawCentreString("Value", 120, EXPO_VALUE_LABEL_Y, 2);
  }

  void drawExpoDynamic() {
  if (!expoNeedsRedraw) return;

  int currentValue = getModelExpoValue(activeModel, selectedExpoChannel);
  char valueText[12];

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    false,
    (dpadFocusVisible && focusIndex == 0),
    BACK_TEXT_OFFSET);

  tft.fillRect(0, RATE_TAB_Y - 4, 240, RATE_TAB_H + 10, COLOR_BG);
  tft.fillRect(EXPO_GRAPH_X - 4, EXPO_GRAPH_Y - 4, EXPO_GRAPH_W + 8, EXPO_GRAPH_H + 8, COLOR_BG);
  tft.fillRect(RATE_VALUE_BOX_X - 4, EXPO_VALUE_BOX_Y - 4, RATE_VALUE_BOX_W + 8, RATE_VALUE_BOX_H + 8, COLOR_BG);

  for (int i = 0; i < CHANNEL_COUNT; i++) {
    bool isSelected = (i == selectedExpoChannel);
    int tabX = RATE_TAB_X(i);

    drawGradientControl(
      tabX, RATE_TAB_Y, RATE_TAB_W, RATE_TAB_H, 8,
      isSelected ? COLOR_ACCENT : COLOR_PANEL, COLOR_ACCENT);
    if (dpadFocusVisible && focusIndex == i + 1) {
      tft.drawRoundRect(tabX - 2, RATE_TAB_Y - 2, RATE_TAB_W + 4, RATE_TAB_H + 4, 8, COLOR_ACCENT_HI);
    }
    tft.setTextColor(isSelected ? COLOR_BG : COLOR_TEXT);
    snprintf(valueText, sizeof(valueText), "CH%d", i + 1);
    tft.drawCentreString(valueText, tabX + (RATE_TAB_W / 2), RATE_TAB_Y + 7, 2);
  }

  drawGradientControl(EXPO_GRAPH_X, EXPO_GRAPH_Y, EXPO_GRAPH_W, EXPO_GRAPH_H, 10, COLOR_PANEL, COLOR_ACCENT);

  int graphPad = 12;
  int gx = EXPO_GRAPH_X + graphPad;
  int gy = EXPO_GRAPH_Y + graphPad;
  int gw = EXPO_GRAPH_W - (graphPad * 2);
  int gh = EXPO_GRAPH_H - (graphPad * 2);
  int cx = gx + (gw / 2);
  int cy = gy + (gh / 2);

  tft.drawRect(gx, gy, gw, gh, TFT_DARKGREY);
  tft.drawFastVLine(cx, gy, gh, TFT_DARKGREY);
  tft.drawFastHLine(gx, cy, gw, TFT_DARKGREY);

  tft.drawLine(gx, gy + gh - 1, gx + gw - 1, gy, tft.color565(100, 100, 100));

  int prevX = gx;
  int prevY = gy + gh - 1;
  for (int i = 1; i < gw; i++) {
    float input = ((float)i / (float)(gw - 1)) * 2.0f - 1.0f;
    float output = applyExpoCurve(input, currentValue);
    int py = gy + (gh - 1) - (int)roundf(((output + 1.0f) * 0.5f) * (gh - 1));
    int px = gx + i;
    tft.drawLine(prevX, prevY, px, py, COLOR_ACCENT_HI);
    prevX = px;
    prevY = py;
  }

  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(EXPO_GRAPH_X + 12, EXPO_GRAPH_Y + 8);
  tft.print("Input");
  tft.setCursor(EXPO_GRAPH_X + EXPO_GRAPH_W - 56, EXPO_GRAPH_Y + 8);
  tft.print("Output");

  drawGradientControl(
    RATE_VALUE_BOX_X, EXPO_VALUE_BOX_Y, RATE_VALUE_BOX_W, RATE_VALUE_BOX_H, 10,
    COLOR_PANEL, COLOR_ACCENT);
  if (dpadFocusVisible && focusIndex == CHANNEL_COUNT + 1) {
    tft.drawRoundRect(RATE_VALUE_BOX_X - 2, EXPO_VALUE_BOX_Y - 2,
                      RATE_VALUE_BOX_W + 4, RATE_VALUE_BOX_H + 4, 10, COLOR_ACCENT_HI);
  }
  if (mixNumpadActive && mixNumpadTarget == NUMPAD_TARGET_EXPO_VALUE) {
    tft.drawRoundRect(RATE_VALUE_BOX_X - 2, EXPO_VALUE_BOX_Y - 2,
                      RATE_VALUE_BOX_W + 4, RATE_VALUE_BOX_H + 4, 10, COLOR_ACCENT_HI);
  }
  snprintf(valueText, sizeof(valueText), "%d", currentValue);
  tft.setTextColor(COLOR_ACCENT_HI);
  tft.drawCentreString(valueText, RATE_VALUE_BOX_X + (RATE_VALUE_BOX_W / 2), EXPO_VALUE_BOX_Y + 10, 2);

  expoNeedsRedraw = false;
  }

  void drawRatesStatic() {
  tft.fillScreen(COLOR_BG);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT);
  tft.drawCentreString("Rates", 120, 38, 2);
  tft.drawCentreString("Rate Amount", 120, 146, 2);
  tft.drawCentreString("Value", 120, RATE_VALUE_BOX_Y - 20, 2);

  tft.fillRoundRect(RATE_SLIDER_X, RATE_SLIDER_Y + 7, RATE_SLIDER_W, 8, 4, COLOR_PANEL);
  tft.drawRoundRect(RATE_SLIDER_X - 1, RATE_SLIDER_Y + 6, RATE_SLIDER_W + 2, 10, 4, COLOR_ACCENT);

  const int notchValues[3] = {RATE_LOW_VALUE, RATE_NORMAL_VALUE, RATE_HIGH_VALUE};
  for (int i = 0; i < 3; i++) {
    int notchX = RATE_SLIDER_X + ((notchValues[i] - 1) * (RATE_SLIDER_W - 1)) / 99;
    tft.drawFastVLine(notchX, RATE_SLIDER_Y + 2, 18, COLOR_ACCENT_HI);
  }
  }

  void drawRatesDynamic() {
  if (!ratesNeedsRedraw) return;

  int currentValue = getModelRateValue(activeModel, selectedRateChannel);
  int sliderKnobX = RATE_SLIDER_X + ((currentValue - 1) * (RATE_SLIDER_W - 1)) / 99;
  char valueText[12];

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    false,
    (dpadFocusVisible && focusIndex == 0),
    BACK_TEXT_OFFSET);

  tft.fillRect(0, RATE_TAB_Y - 4, 240, RATE_TAB_H + 10, COLOR_BG);
  tft.fillRect(0, RATE_TOGGLE_Y - 4, 240, RATE_TOGGLE_H + 10, COLOR_BG);
  tft.fillRect(
    RATE_SLIDER_X - RATE_SLIDER_KNOB_RADIUS - 4,
    RATE_SLIDER_Y - 6,
    RATE_SLIDER_W + (RATE_SLIDER_KNOB_RADIUS * 2) + 8,
    RATE_SLIDER_H + 16,
    COLOR_BG
  );
  tft.fillRoundRect(RATE_SLIDER_X, RATE_SLIDER_Y + 7, RATE_SLIDER_W, 8, 4, COLOR_PANEL);
  tft.drawRoundRect(RATE_SLIDER_X - 1, RATE_SLIDER_Y + 6, RATE_SLIDER_W + 2, 10, 4, COLOR_ACCENT);
  const int notchValues[3] = {RATE_LOW_VALUE, RATE_NORMAL_VALUE, RATE_HIGH_VALUE};
  for (int i = 0; i < 3; i++) {
    int notchX = RATE_SLIDER_X + ((notchValues[i] - 1) * (RATE_SLIDER_W - 1)) / 99;
    tft.drawFastVLine(notchX, RATE_SLIDER_Y + 2, 18, COLOR_ACCENT_HI);
  }
  tft.fillRect(RATE_VALUE_BOX_X - 4, RATE_VALUE_BOX_Y - 4, RATE_VALUE_BOX_W + 8, RATE_VALUE_BOX_H + 8, COLOR_BG);

  for (int i = 0; i < CHANNEL_COUNT; i++) {
    bool isSelected = (i == selectedRateChannel);
    int tabX = RATE_TAB_X(i);

    drawGradientControl(
      tabX, RATE_TAB_Y, RATE_TAB_W, RATE_TAB_H, 8,
      isSelected ? COLOR_ACCENT : COLOR_PANEL, COLOR_ACCENT);
    if (dpadFocusVisible && focusIndex == i + 1) {
      tft.drawRoundRect(tabX - 2, RATE_TAB_Y - 2, RATE_TAB_W + 4, RATE_TAB_H + 4, 8, COLOR_ACCENT_HI);
    }
    tft.setTextColor(isSelected ? COLOR_BG : COLOR_TEXT);
    snprintf(valueText, sizeof(valueText), "CH%d", i + 1);
    tft.drawCentreString(valueText, tabX + (RATE_TAB_W / 2), RATE_TAB_Y + 7, 2);
  }

  const char* toggleLabels[3] = {"LOW", "NORMAL", "HIGH"};
  const int toggleValues[3] = {RATE_LOW_VALUE, RATE_NORMAL_VALUE, RATE_HIGH_VALUE};
  for (int i = 0; i < 3; i++) {
    bool isSelected = (currentValue == toggleValues[i]);
    int toggleX = RATE_TOGGLE_X(i);
    int toggleW = (i == 1) ? 70 : RATE_TOGGLE_W;
    if (i == 1) toggleX -= 5;

    drawGradientControl(
      toggleX, RATE_TOGGLE_Y, toggleW, RATE_TOGGLE_H, 8,
      isSelected ? COLOR_ACCENT : COLOR_PANEL, COLOR_ACCENT);
    if (dpadFocusVisible && focusIndex == i + CHANNEL_COUNT + 1) {
      tft.drawRoundRect(toggleX - 2, RATE_TOGGLE_Y - 2, toggleW + 4, RATE_TOGGLE_H + 4, 8, COLOR_ACCENT_HI);
    }
    tft.setTextColor(isSelected ? COLOR_BG : COLOR_TEXT);
    tft.drawCentreString(toggleLabels[i], toggleX + (toggleW / 2), RATE_TOGGLE_Y + 7, 2);
  }

  tft.setTextColor(COLOR_TEXT);
  if (dpadFocusVisible && focusIndex == CHANNEL_COUNT + 4) {
    tft.drawRoundRect(RATE_SLIDER_X - 4, RATE_SLIDER_Y - 2, RATE_SLIDER_W + 8, RATE_SLIDER_H + 8, 8, COLOR_ACCENT_HI);
  }

  tft.fillCircle(sliderKnobX, RATE_SLIDER_Y + 11, RATE_SLIDER_KNOB_RADIUS, COLOR_ACCENT);
  tft.drawCircle(sliderKnobX, RATE_SLIDER_Y + 11, RATE_SLIDER_KNOB_RADIUS, COLOR_ACCENT_HI);
  tft.fillCircle(sliderKnobX, RATE_SLIDER_Y + 11, 4, COLOR_BG);

  drawGradientControl(
    RATE_VALUE_BOX_X, RATE_VALUE_BOX_Y, RATE_VALUE_BOX_W, RATE_VALUE_BOX_H, 10,
    COLOR_PANEL, COLOR_ACCENT);
  if (mixNumpadActive && mixNumpadTarget == NUMPAD_TARGET_RATE_VALUE) {
    tft.drawRoundRect(RATE_VALUE_BOX_X - 2, RATE_VALUE_BOX_Y - 2,
                      RATE_VALUE_BOX_W + 4, RATE_VALUE_BOX_H + 4, 10, COLOR_ACCENT_HI);
  }
  snprintf(valueText, sizeof(valueText), "%d", currentValue);
  tft.setTextColor(COLOR_ACCENT_HI);
  tft.drawCentreString(valueText, RATE_VALUE_BOX_X + (RATE_VALUE_BOX_W / 2), RATE_VALUE_BOX_Y + 10, 2);

  ratesNeedsRedraw = false;
  }

// Protocol settings screens share status composition helpers so ELRS, ESP-NOW,
// binding, and OTA state appear consistently in the UI and top bar.
void composeProtocolStatusText(char *bindStatus, size_t bindStatusSize, uint16_t *statusColor) {
  if (bindStatus == nullptr || bindStatusSize == 0) return;

  if (otaModeActive) {
    uint16_t resolvedColor = COLOR_ACCENT_HI;
    if (otaUpdateInProgress) {
      snprintf(bindStatus, bindStatusSize, "OTA update in progress");
    } else if (otaStaConnectPending && otaUsingSoftAP) {
      IPAddress ip = WiFi.softAPIP();
      snprintf(bindStatus, bindStatusSize, "OTA AP %u.%u.%u.%u + STA...",
               ip[0], ip[1], ip[2], ip[3]);
    } else if (otaStaConnectPending) {
      snprintf(bindStatus, bindStatusSize, "OTA connecting STA...");
    } else if (otaServiceReady) {
      IPAddress ip = otaUsingSoftAP ? WiFi.softAPIP() : WiFi.localIP();
      if ((uint32_t)ip == 0) {
        ip = WiFi.softAPIP();
      }
      snprintf(bindStatus, bindStatusSize, "OTA %u.%u.%u.%u",
               ip[0], ip[1], ip[2], ip[3]);
    } else {
      snprintf(bindStatus, bindStatusSize, "OTA unavailable");
      resolvedColor = TFT_RED;
    }
    if (statusColor != nullptr) *statusColor = resolvedColor;
    return;
  }

  bool elrsActive = (getModelProtocol(activeModel) == PROTOCOL_ELRS);
  bool espNowActive = !elrsActive;

  uint16_t resolvedColor = COLOR_TEXT;
  if (elrsActive) {
    snprintf(bindStatus, bindStatusSize, "ELRS selected");
    resolvedColor = COLOR_ACCENT_HI;
  } else if (espNowBindingMode) {
    unsigned long remainingMs = 0;
    unsigned long now = millis();
    if (now - espNowBindingStartTime < ESPNOW_BIND_TIMEOUT_MS) {
      remainingMs = ESPNOW_BIND_TIMEOUT_MS - (now - espNowBindingStartTime);
    }
    snprintf(bindStatus, bindStatusSize, "Listening %lus", (remainingMs + 999) / 1000);
    if (espNowActive) resolvedColor = COLOR_ACCENT_HI;
  } else if (millis() - espNowBindSuccessTime < 2500) {
    snprintf(bindStatus, bindStatusSize, "Bind complete %s",
             getModelEspNowOutputMode(activeModel) == ESPNOW_OUTPUT_IBUS ? "iBUS" : "PWM");
  } else if (millis() - espNowUnbindTime < 2500) {
    snprintf(bindStatus, bindStatusSize, "Receiver unbound");
    resolvedColor = COLOR_ACCENT_HI;
  } else {
    char macText[32];
    formatReceiverMacShort(getBoundReceiverMac(activeModel), macText, sizeof(macText));
    snprintf(bindStatus, bindStatusSize, "%s %s",
             macText,
             getModelEspNowOutputMode(activeModel) == ESPNOW_OUTPUT_IBUS ? "iBUS" : "PWM");
  }

  if (statusColor != nullptr) *statusColor = resolvedColor;
}

const char* protocolBindButtonLabel(bool elrsActive) {
  if (otaModeActive) return "LOCK";
  if (elrsActive) return "ELRS";
  if (espNowBindingMode) return "CANCEL";
  if (modelHasBoundReceiver(activeModel)) return "UNBIND";
  return "BIND";
}

void composeElrsStatusText(char *statusText, size_t statusTextSize, uint16_t *statusColor) {
  if (statusText == nullptr || statusTextSize == 0) return;

  unsigned long now = millis();
  uint16_t resolvedColor = COLOR_TEXT;
  bool haveRawRx = (elrsRxByteCount > 0);
  bool haveModuleFrames = hasElrsModuleFrames();
  if (now - elrsBindSuccessTime < 3000) {
    snprintf(statusText, statusTextSize, "ELRS bind successful");
    resolvedColor = COLOR_SIG;
  } else if (elrsBindAwaitingResult) {
    snprintf(statusText, statusTextSize, "ELRS binding...");
    resolvedColor = COLOR_ACCENT_HI;
  } else if (now - elrsBindCommandSentTime < 2500) {
    snprintf(statusText, statusTextSize, "ELRS bind command sent");
    resolvedColor = COLOR_ACCENT_HI;
  } else if (!elrsInitialized) {
    snprintf(statusText, statusTextSize, "ELRS uart offline");
  } else if (elrsLinkActive) {
    snprintf(statusText, statusTextSize, "ELRS link active LQ %u", (unsigned int)elrsUplinkLq);
    resolvedColor = COLOR_SIG;
  } else if (haveModuleFrames || elrsModulePresent) {
    snprintf(statusText, statusTextSize, "ELRS module online");
    resolvedColor = COLOR_ACCENT_HI;
  } else if (haveRawRx) {
    snprintf(statusText, statusTextSize, "ELRS raw uart rx only");
    resolvedColor = TFT_ORANGE;
  } else {
    snprintf(statusText, statusTextSize, "ELRS no uart rx");
  }

  if (statusColor != nullptr) *statusColor = resolvedColor;
}

void drawProtocolScreen() {
  tft.fillScreen(COLOR_BG);

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    pressedButton == BTN_BACK,
    dpadFocusVisible && selectedButton == BTN_BACK,
    BACK_TEXT_OFFSET);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT);
  tft.drawCentreString("Protocol", 120, 38, 2);

  bool elrsActive = (getModelProtocol(activeModel) == PROTOCOL_ELRS);
  bool espNowActive = !elrsActive;
  char bindStatus[56];
  uint16_t statusColor = COLOR_TEXT;
  composeProtocolStatusText(bindStatus, sizeof(bindStatus), &statusColor);

  drawButtonBubble(PROTOCOL_MODE_BTN_X, PROTOCOL_ELRS_BTN_Y, PROTOCOL_MODE_BTN_W, PROTOCOL_MODE_BTN_H, "ELRS",
                   pressedButton == BTN_PROTOCOL_ELRS,
                   elrsActive || (dpadFocusVisible && selectedButton == BTN_PROTOCOL_ELRS),
                   -1);
  drawButtonBubble(PROTOCOL_MODE_BTN_X, PROTOCOL_ESPNOW_BTN_Y, PROTOCOL_MODE_BTN_W, PROTOCOL_MODE_BTN_H, "ESP-NOW",
                   pressedButton == BTN_PROTOCOL_ESPNOW,
                   espNowActive || (dpadFocusVisible && selectedButton == BTN_PROTOCOL_ESPNOW),
                   -1);

  // Clear status area so protocol switches never leave stale text behind.
  tft.fillRect(PROTOCOL_STATUS_X, PROTOCOL_STATUS_Y, PROTOCOL_STATUS_W, PROTOCOL_STATUS_H, COLOR_BG);
  tft.setTextFont(2);
  tft.setTextColor(statusColor);
  tft.drawString(bindStatus, PROTOCOL_STATUS_X + 2, PROTOCOL_STATUS_Y + 2, 2);

  drawButtonBubble(PROTOCOL_BIND_BTN_X, PROTOCOL_BIND_BTN_Y,
                   PROTOCOL_BIND_BTN_W, PROTOCOL_BIND_BTN_H,
                   protocolBindButtonLabel(elrsActive),
                   pressedButton == BTN_PROTOCOL_BIND,
                   dpadFocusVisible && selectedButton == BTN_PROTOCOL_BIND,
                   -1);
}

void drawProtocolDynamic() {
  static char lastStatus[56] = "";
  static uint16_t lastStatusColor = 0;
  static bool lastBindingMode = false;
  static bool lastElrsActive = false;
  static bool lastOtaMode = false;
  static bool lastBoundReceiver = false;

  char bindStatus[56];
  uint16_t statusColor = COLOR_TEXT;
  composeProtocolStatusText(bindStatus, sizeof(bindStatus), &statusColor);
  bool elrsActive = (getModelProtocol(activeModel) == PROTOCOL_ELRS);
  bool boundReceiver = modelHasBoundReceiver(activeModel);

  if (strcmp(bindStatus, lastStatus) != 0 || statusColor != lastStatusColor) {
    tft.fillRect(PROTOCOL_STATUS_X, PROTOCOL_STATUS_Y, PROTOCOL_STATUS_W, PROTOCOL_STATUS_H, COLOR_BG);
    tft.setTextFont(2);
    tft.setTextColor(statusColor, COLOR_BG);
    tft.drawString(bindStatus, PROTOCOL_STATUS_X + 2, PROTOCOL_STATUS_Y + 2, 2);
    strncpy(lastStatus, bindStatus, sizeof(lastStatus) - 1);
    lastStatus[sizeof(lastStatus) - 1] = '\0';
    lastStatusColor = statusColor;
  }

  if (lastBindingMode != espNowBindingMode ||
      lastElrsActive != elrsActive ||
      lastOtaMode != otaModeActive ||
      lastBoundReceiver != boundReceiver) {
    drawButtonBubble(PROTOCOL_BIND_BTN_X, PROTOCOL_BIND_BTN_Y,
                     PROTOCOL_BIND_BTN_W, PROTOCOL_BIND_BTN_H,
                     protocolBindButtonLabel(elrsActive),
                     pressedButton == BTN_PROTOCOL_BIND,
                     dpadFocusVisible && selectedButton == BTN_PROTOCOL_BIND,
                     -1);
    lastBindingMode = espNowBindingMode;
    lastElrsActive = elrsActive;
    lastOtaMode = otaModeActive;
    lastBoundReceiver = boundReceiver;
  }
}

void drawEspNowOutputModePrompt() {
  if (!espNowOutputModePromptVisible) return;

  const int panelX = 14;
  const int panelY = 74;
  const int panelW = 212;
  const int panelH = 156;
  const int btnW = 86;
  const int btnH = 32;
  const int pwmX = 28;
  const int ibusX = 126;
  const int modeY = 154;
  const int cancelX = 77;
  const int cancelY = 198;

  tft.fillRect(0, 56, 240, 190, COLOR_BG);
  drawGradientControl(panelX, panelY, panelW, panelH, 14, COLOR_PANEL, COLOR_ACCENT);

  tft.setTextFont(2);
  tft.setTextColor(COLOR_ACCENT_HI, COLOR_PANEL);
  tft.drawCentreString("Receiver Output", 120, 90, 2);

  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  tft.drawCentreString("How is this receiver", 120, 116, 2);
  tft.drawCentreString("connected to the model?", 120, 134, 2);

  EspNowReceiverOutputMode currentMode = getModelEspNowOutputMode(activeModel);
  drawButtonBubble(pwmX, modeY, btnW, btnH, "PWM",
                   false, currentMode == ESPNOW_OUTPUT_PWM, -1);
  drawButtonBubble(ibusX, modeY, btnW, btnH, "iBUS",
                   false, currentMode == ESPNOW_OUTPUT_IBUS, -1);
  drawButtonBubble(cancelX, cancelY, btnW, btnH, "CANCEL", false, false, -1);

  espNowOutputModePromptDirty = false;
}

bool handleEspNowOutputModePromptTouch(bool isTouching, int x, int y) {
  if (!espNowOutputModePromptVisible) return false;
  if (!isTouching) {
    waitingForRelease = false;
    return true;
  }
  if (waitingForRelease) return true;

  const int btnW = 86;
  const int btnH = 32;
  const int pwmX = 28;
  const int ibusX = 126;
  const int modeY = 154;
  const int cancelX = 77;
  const int cancelY = 198;

  if (isInside(x, y, pwmX, modeY, btnW, btnH)) {
    pressedButton = BTN_OPTION_2;
    selectedButton = BTN_OPTION_2;
    waitingForRelease = true;
    startEspNowBindingWithOutputMode(ESPNOW_OUTPUT_PWM);
    return true;
  }

  if (isInside(x, y, ibusX, modeY, btnW, btnH)) {
    pressedButton = BTN_OPTION_3;
    selectedButton = BTN_OPTION_3;
    waitingForRelease = true;
    startEspNowBindingWithOutputMode(ESPNOW_OUTPUT_IBUS);
    return true;
  }

  if (isInside(x, y, cancelX, cancelY, btnW, btnH)) {
    espNowOutputModePromptVisible = false;
    espNowOutputModePromptDirty = false;
    waitingForRelease = true;
    fullRedraw = true;
    uiNeedsRedraw = true;
    return true;
  }

  return true;
}

void drawTxUpdateScreen() {
  tft.fillScreen(COLOR_BG);
  txUpdateNeedsRedraw = true;

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    pressedButton == BTN_BACK,
    dpadFocusVisible && selectedButton == BTN_BACK,
    BACK_TEXT_OFFSET);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawCentreString("Transmitter Update", 120, 38, 2);
  tft.drawCentreString("Send firmware to this TX", 120, 58, 2);

  drawTxUpdateDynamic();
}

void drawTxUpdateDynamic() {
  static char lastStatus[56] = "";
  static uint16_t lastStatusColor = 0;
  static bool lastOtaMode = false;
  static bool lastOtaPressed = false;
  static bool lastOtaFocus = false;
  static bool lastCfgPressed = false;
  static bool lastCfgFocus = false;
  static bool cacheValid = false;

  char statusText[56];
  uint16_t statusColor = COLOR_TEXT;
  composeProtocolStatusText(statusText, sizeof(statusText), &statusColor);

  bool otaFocus = dpadFocusVisible && selectedButton == BTN_PROTOCOL_OTA;
  bool cfgFocus = dpadFocusVisible && selectedButton == BTN_PROTOCOL_OTA_CFG;
  bool otaPressed = pressedButton == BTN_PROTOCOL_OTA;
  bool cfgPressed = pressedButton == BTN_PROTOCOL_OTA_CFG;

  if (!cacheValid || txUpdateNeedsRedraw ||
      strcmp(lastStatus, statusText) != 0 || lastStatusColor != statusColor) {
    tft.fillRect(TX_UPDATE_STATUS_X, TX_UPDATE_STATUS_Y, TX_UPDATE_STATUS_W, TX_UPDATE_STATUS_H, COLOR_BG);
    tft.setTextFont(2);
    tft.setTextColor(statusColor, COLOR_BG);
    tft.drawCentreString(statusText, 120, TX_UPDATE_STATUS_Y + 4, 2);
    strncpy(lastStatus, statusText, sizeof(lastStatus) - 1);
    lastStatus[sizeof(lastStatus) - 1] = '\0';
    lastStatusColor = statusColor;
  }

  if (!cacheValid || txUpdateNeedsRedraw ||
      lastOtaMode != otaModeActive ||
      lastOtaPressed != otaPressed ||
      lastOtaFocus != otaFocus) {
    drawButtonBubble(TX_UPDATE_OTA_BTN_X, TX_UPDATE_OTA_BTN_Y,
                     TX_UPDATE_OTA_BTN_W, TX_UPDATE_OTA_BTN_H,
                     otaModeActive ? "OTA ON" : "OTA OFF",
                     otaPressed,
                     otaFocus,
                     -1);
    lastOtaMode = otaModeActive;
    lastOtaPressed = otaPressed;
    lastOtaFocus = otaFocus;
  }

  if (!cacheValid || txUpdateNeedsRedraw ||
      lastCfgPressed != cfgPressed ||
      lastCfgFocus != cfgFocus) {
    drawButtonBubble(TX_UPDATE_CFG_BTN_X, TX_UPDATE_CFG_BTN_Y,
                     TX_UPDATE_CFG_BTN_W, TX_UPDATE_CFG_BTN_H,
                     "CFG",
                     cfgPressed,
                     cfgFocus,
                     -1);
    lastCfgPressed = cfgPressed;
    lastCfgFocus = cfgFocus;
  }

  if (!cacheValid || txUpdateNeedsRedraw) {
    tft.setTextColor(fadeColor(COLOR_TEXT, 0.72f), COLOR_BG);
    tft.drawCentreString("OTA AP password: anubisota", 120, 206, 2);
    tft.drawCentreString("Use CFG for WiFi/AP names", 120, 228, 2);
  }

  cacheValid = true;
  txUpdateNeedsRedraw = false;
}

void drawOtaSettingsStatic() {
  tft.fillScreen(COLOR_BG);

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    pressedButton == BTN_BACK,
    (dpadFocusVisible && focusIndex == 0),
    BACK_TEXT_OFFSET);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT);
  tft.drawCentreString("OTA Settings", 120, 38, 2);
  tft.drawCentreString("Select field, then type", 120, 58, 2);
  tft.drawCentreString("AP password: anubisota", 120, 72, 2);

  const char* labels[3] = {"Home SSID", "Home Pass", "AP Name"};
  int rowY = 96;
  for (int i = 0; i < 3; i++) {
    tft.setTextColor(COLOR_TEXT);
    tft.drawString(labels[i], 12, rowY + 10, 2);
    tft.drawRoundRect(98, rowY, 130, 30, 8, COLOR_ACCENT);
    rowY += 48;
  }
}

void drawOtaSettingsDynamic() {
  if (!otaSettingsNeedsRedraw) return;

  char ssidText[OTA_STA_SSID_STORAGE_BYTES + 1];
  char passText[OTA_STA_PASSWORD_STORAGE_BYTES + 1];
  char apText[OTA_AP_SSID_STORAGE_BYTES + 1];

  strncpy(ssidText, otaStaSsid, OTA_STA_SSID_STORAGE_BYTES);
  ssidText[OTA_STA_SSID_STORAGE_BYTES] = '\0';
  strncpy(apText, otaApSsid, OTA_AP_SSID_STORAGE_BYTES);
  apText[OTA_AP_SSID_STORAGE_BYTES] = '\0';

  if (keyboardActive) {
    if (keyboardTarget == KEYBOARD_TARGET_OTA_STA_SSID) {
      strncpy(ssidText, keyboardBuffer.c_str(), OTA_STA_SSID_STORAGE_BYTES);
      ssidText[OTA_STA_SSID_STORAGE_BYTES] = '\0';
    } else if (keyboardTarget == KEYBOARD_TARGET_OTA_AP_SSID) {
      strncpy(apText, keyboardBuffer.c_str(), OTA_AP_SSID_STORAGE_BYTES);
      apText[OTA_AP_SSID_STORAGE_BYTES] = '\0';
    }
  }

  const char* passwordSource = otaStaPassword;
  if (keyboardActive && keyboardTarget == KEYBOARD_TARGET_OTA_STA_PASSWORD) {
    passwordSource = keyboardBuffer.c_str();
  }

  int passLen = (int)strlen(passwordSource);
  if (passLen > OTA_STA_PASSWORD_STORAGE_BYTES) passLen = OTA_STA_PASSWORD_STORAGE_BYTES;
  bool showPassword = (keyboardActive && keyboardTarget == KEYBOARD_TARGET_OTA_STA_PASSWORD);
  for (int i = 0; i < passLen; i++) passText[i] = showPassword ? passwordSource[i] : '*';
  passText[passLen] = '\0';

  const char* values[3] = {
    (strlen(ssidText) > 0) ? ssidText : "<empty>",
    (passLen > 0) ? passText : "<empty>",
    (strlen(apText) > 0) ? apText : "<empty>"
  };

  int rowY = 96;
  for (int i = 0; i < 3; i++) {
    tft.fillRoundRect(100, rowY + 2, 126, 26, 6, COLOR_PANEL);
    tft.drawRoundRect(98, rowY, 130, 30, 8, COLOR_ACCENT);
    if (dpadFocusVisible && focusIndex == i + 1) {
      tft.drawRoundRect(96, rowY - 2, 134, 34, 8, COLOR_ACCENT_HI);
    }

    tft.setTextColor(COLOR_ACCENT_HI);
    tft.drawString(values[i], 104, rowY + 9, 2);
    rowY += 48;
  }

  otaSettingsNeedsRedraw = false;
}

void drawElrsPowerControl(bool selectedSlider, bool selectedValue, bool forceRedraw) {
  static int lastDrawnPower = -1;
  static bool lastSliderSelected = false;
  static bool lastValueSelected = false;
  static bool lastFieldKnown = false;
  static bool lastNumpadActive = false;

  bool numpadActive = (mixNumpadActive && mixNumpadTarget == NUMPAD_TARGET_ELRS_TX_POWER);
  if (!forceRedraw &&
      lastDrawnPower == (int)elrsTxPowerMw &&
      lastSliderSelected == selectedSlider &&
      lastValueSelected == selectedValue &&
      lastFieldKnown == elrsTxPowerFieldKnown &&
      lastNumpadActive == numpadActive) {
    return;
  }

  tft.fillRect(8, 206, 224, 70, COLOR_BG);

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawCentreString("TX Power", 120, 206, 2);

  tft.fillRoundRect(ELRS_POWER_SLIDER_X, ELRS_POWER_SLIDER_Y + 5, ELRS_POWER_SLIDER_W, 4, 2, COLOR_PANEL);
  tft.drawRoundRect(ELRS_POWER_SLIDER_X - 1, ELRS_POWER_SLIDER_Y + 4, ELRS_POWER_SLIDER_W + 2, 6, 2, COLOR_ACCENT);
  if (selectedSlider) {
    tft.drawRoundRect(ELRS_POWER_SLIDER_X - 3, ELRS_POWER_SLIDER_Y + 2, ELRS_POWER_SLIDER_W + 6, 10, 4, COLOR_ACCENT_HI);
  }

  int clampedMw = constrain((int)elrsTxPowerMw, ELRS_TX_POWER_MIN_MW, ELRS_TX_POWER_MAX_MW);
  int knobX = ELRS_POWER_SLIDER_X + (clampedMw * (ELRS_POWER_SLIDER_W - 1)) / ELRS_TX_POWER_MAX_MW;
  tft.fillCircle(knobX, ELRS_POWER_SLIDER_Y + 7, ELRS_POWER_SLIDER_KNOB_RADIUS, COLOR_ACCENT);
  tft.drawCircle(knobX, ELRS_POWER_SLIDER_Y + 7, ELRS_POWER_SLIDER_KNOB_RADIUS, COLOR_ACCENT_HI);
  tft.fillCircle(knobX, ELRS_POWER_SLIDER_Y + 7, 3, COLOR_BG);

  tft.setTextColor(fadeColor(COLOR_TEXT, 0.7f), COLOR_BG);
  tft.drawString("0", ELRS_POWER_SLIDER_X - 2, ELRS_POWER_SLIDER_Y + 16, 2);
  tft.drawRightString("150", ELRS_POWER_SLIDER_X + ELRS_POWER_SLIDER_W + 2, ELRS_POWER_SLIDER_Y + 16, 2);

  drawGradientControl(
    ELRS_POWER_VALUE_BOX_X, ELRS_POWER_VALUE_BOX_Y,
    ELRS_POWER_VALUE_BOX_W, ELRS_POWER_VALUE_BOX_H, 8,
    COLOR_PANEL, COLOR_ACCENT);
  if (selectedValue || numpadActive) {
    tft.drawRoundRect(ELRS_POWER_VALUE_BOX_X - 2, ELRS_POWER_VALUE_BOX_Y - 2,
                      ELRS_POWER_VALUE_BOX_W + 4, ELRS_POWER_VALUE_BOX_H + 4, 8, COLOR_ACCENT_HI);
  }

  char powerText[24];
  snprintf(powerText, sizeof(powerText), "%u mW", (unsigned int)clampedMw);
  tft.setTextColor(elrsTxPowerFieldKnown ? COLOR_ACCENT_HI : COLOR_TEXT, COLOR_PANEL);
  tft.drawCentreString(powerText, ELRS_POWER_VALUE_BOX_X + (ELRS_POWER_VALUE_BOX_W / 2), ELRS_POWER_VALUE_BOX_Y + 8, 2);

  lastDrawnPower = (int)elrsTxPowerMw;
  lastSliderSelected = selectedSlider;
  lastValueSelected = selectedValue;
  lastFieldKnown = elrsTxPowerFieldKnown;
  lastNumpadActive = numpadActive;
}

void drawElrsScreen() {
  tft.fillScreen(COLOR_BG);

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    pressedButton == BTN_BACK,
    dpadFocusVisible && selectedButton == BTN_BACK,
    BACK_TEXT_OFFSET);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT);
  tft.drawCentreString("ELRS", 120, 38, 2);

  char statusText[56];
  uint16_t statusColor = COLOR_TEXT;
  composeElrsStatusText(statusText, sizeof(statusText), &statusColor);
  tft.fillRect(14, 82, 212, 20, COLOR_BG);
  tft.setTextColor(statusColor, COLOR_BG);
  tft.drawString(statusText, 16, 84, 2);

  drawButtonBubble(ELRS_RX_CONFIG_BTN_X, ELRS_RX_CONFIG_BTN_Y,
                   ELRS_RX_CONFIG_BTN_W, ELRS_RX_CONFIG_BTN_H,
                   "RECEIVER CONFIG",
                   pressedButton == BTN_ELRS_RX_CONFIG,
                   dpadFocusVisible && selectedButton == BTN_ELRS_RX_CONFIG,
                   -1);

  drawButtonBubble(ELRS_BIND_BTN_X, ELRS_BIND_BTN_Y,
                   ELRS_BIND_BTN_W, ELRS_BIND_BTN_H,
                   "BIND",
                   pressedButton == BTN_ELRS_BIND,
                   dpadFocusVisible && selectedButton == BTN_ELRS_BIND,
                   -1);
  drawElrsPowerControl(
    dpadFocusVisible && selectedButton == BTN_ELRS_TX_POWER_SLIDER,
    dpadFocusVisible && selectedButton == BTN_ELRS_TX_POWER_VALUE,
    true
  );
}

void drawElrsDynamic() {
  static char lastStatus[56] = "";
  static uint16_t lastStatusColor = 0;

  char statusText[56];
  uint16_t statusColor = COLOR_TEXT;
  composeElrsStatusText(statusText, sizeof(statusText), &statusColor);

  if (strcmp(lastStatus, statusText) != 0 || lastStatusColor != statusColor) {
    tft.fillRect(14, 82, 212, 20, COLOR_BG);
    tft.setTextColor(statusColor, COLOR_BG);
    tft.drawString(statusText, 16, 84, 2);
    strncpy(lastStatus, statusText, sizeof(lastStatus) - 1);
    lastStatus[sizeof(lastStatus) - 1] = '\0';
    lastStatusColor = statusColor;
  }

  drawButtonBubble(ELRS_RX_CONFIG_BTN_X, ELRS_RX_CONFIG_BTN_Y,
                   ELRS_RX_CONFIG_BTN_W, ELRS_RX_CONFIG_BTN_H,
                   "RECEIVER CONFIG",
                   pressedButton == BTN_ELRS_RX_CONFIG,
                   dpadFocusVisible && selectedButton == BTN_ELRS_RX_CONFIG,
                   -1);

  drawElrsPowerControl(
    dpadFocusVisible && selectedButton == BTN_ELRS_TX_POWER_SLIDER,
    dpadFocusVisible && selectedButton == BTN_ELRS_TX_POWER_VALUE,
    false
  );
}

void drawElrsReceiverConfigScreen() {
  tft.fillScreen(COLOR_BG);
  elrsReceiverConfigStaticDirty = true;

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    pressedButton == BTN_BACK,
    dpadFocusVisible && selectedButton == BTN_BACK,
    BACK_TEXT_OFFSET);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawCentreString("Receiver Config", 120, 38, 2);
}

void drawElrsReceiverConfigDynamic() {
  static bool cacheValid = false;
  static ElrsReceiverConfigState lastState = RX_CONFIG_IDLE;
  static char lastStatus[72] = "";
  static char lastDetail[96] = "";
  static char lastEndpoint[24] = "";
  static char lastLuaName[18] = "";
  static unsigned long lastWaitSeconds = 0xFFFFFFFFUL;
  static int lastModelId = -999;
  static bool lastModelFocus = false;
  static int lastPwmCount = -1;
  static int lastPwmFailsafe[4] = {-999, -999, -999, -999};
  static bool lastPwmFocus[4] = {false, false, false, false};
  static bool lastSaveDirty = false;
  static bool lastSavePressed = false;
  static bool lastSaveFocus = false;
  static bool lastPushPressed = false;
  static bool lastPushFocus = false;
  static bool lastBackPressed = false;
  static bool lastBackFocus = false;

  uint16_t statusColor = COLOR_TEXT;
  if (elrsReceiverConfigState == RX_CONFIG_CONNECTING ||
      elrsReceiverConfigState == RX_CONFIG_FETCHING) {
    statusColor = COLOR_ACCENT_HI;
  } else if (elrsReceiverConfigState == RX_CONFIG_DONE) {
    statusColor = COLOR_SIG;
  } else if (elrsReceiverConfigState == RX_CONFIG_FAILED) {
    statusColor = TFT_ORANGE;
  }

  bool stateChanged = !cacheValid ||
                      elrsReceiverConfigStaticDirty ||
                      lastState != elrsReceiverConfigState;

  if (stateChanged) {
    tft.fillRect(10, 68, 220, 204, COLOR_BG);

    lastStatus[0] = '\0';
    lastDetail[0] = '\0';
    lastEndpoint[0] = '\0';
    lastLuaName[0] = '\0';
    lastWaitSeconds = 0xFFFFFFFFUL;
    lastModelId = -999;
    lastModelFocus = false;
    lastPwmCount = -1;
    for (int i = 0; i < 4; i++) {
      lastPwmFailsafe[i] = -999;
      lastPwmFocus[i] = false;
    }
    lastSaveDirty = !elrsReceiverConfigDirty;
    lastSavePressed = pressedButton == BTN_ELRS_RX_SAVE;
    lastSaveFocus = dpadFocusVisible && selectedButton == BTN_ELRS_RX_SAVE;
    lastPushPressed = pressedButton == BTN_ELRS_RX_PUSH_FAILSAFE;
    lastPushFocus = dpadFocusVisible && selectedButton == BTN_ELRS_RX_PUSH_FAILSAFE;

    tft.setTextFont(2);
    tft.setTextColor(fadeColor(COLOR_TEXT, 0.72f), COLOR_BG);
    if (elrsReceiverConfigState != RX_CONFIG_DONE) {
      tft.drawString("Default: ExpressLRS RX", 16, 166, 2);
      tft.drawString("Pass: expresslrs", 16, 188, 2);
      tft.drawString("Serial prints page/API probe", 16, 222, 2);
    } else {
      tft.setTextColor(COLOR_TEXT, COLOR_BG);
      if (elrsReceiverHasModelId) {
        tft.drawString("Model ID", 16, ELRS_RX_MODEL_ID_Y + 5, 2);
      }
      if (elrsReceiverPwmCount > 0) {
        tft.drawString("PWM failsafe us", 16, 172, 2);
      } else if (!elrsReceiverHasModelId) {
        tft.setTextColor(fadeColor(COLOR_TEXT, 0.72f), COLOR_BG);
        tft.drawString("No editable fields found", 16, 172, 2);
      }

      int maxRows = min((int)elrsReceiverPwmCount, 4);
      for (int i = 0; i < maxRows; i++) {
        bool rightCol = (i & 1);
        int rowY = (i < 2) ? ELRS_RX_PWM_ROW1_Y : ELRS_RX_PWM_ROW2_Y;
        int labelX = rightCol ? ELRS_RX_PWM_RIGHT_LABEL_X : ELRS_RX_PWM_LEFT_LABEL_X;
        char line[8];
        snprintf(line, sizeof(line), "O%d", i + 1);
        tft.drawString(line, labelX, rowY + 5, 2);
      }
    }

    lastState = elrsReceiverConfigState;
    elrsReceiverConfigStaticDirty = false;
    cacheValid = true;
  }

  bool backPressed = pressedButton == BTN_BACK;
  bool backFocus = dpadFocusVisible && selectedButton == BTN_BACK;
  if (stateChanged || backPressed != lastBackPressed || backFocus != lastBackFocus) {
    drawButtonBubble(
      BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
      "BACK",
      backPressed,
      backFocus,
      BACK_TEXT_OFFSET);
    lastBackPressed = backPressed;
    lastBackFocus = backFocus;
  }

  if (stateChanged ||
      strcmp(lastStatus, elrsReceiverConfigStatus) != 0) {
    tft.fillRect(14, 70, 214, 22, COLOR_BG);
    tft.setTextFont(2);
    tft.setTextColor(statusColor, COLOR_BG);
    tft.drawString(elrsReceiverConfigStatus, 16, 72, 2);
    strncpy(lastStatus, elrsReceiverConfigStatus, sizeof(lastStatus) - 1);
    lastStatus[sizeof(lastStatus) - 1] = '\0';
  }

  if (stateChanged ||
      strcmp(lastDetail, elrsReceiverConfigDetail) != 0) {
    tft.fillRect(14, 96, 214, 22, COLOR_BG);
    tft.setTextFont(2);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString(elrsReceiverConfigDetail, 16, 98, 2);
    strncpy(lastDetail, elrsReceiverConfigDetail, sizeof(lastDetail) - 1);
    lastDetail[sizeof(lastDetail) - 1] = '\0';
  }

  if (elrsReceiverConfigState == RX_CONFIG_DONE) {
    char line[64];

    if (stateChanged || strcmp(lastLuaName, elrsReceiverLuaName) != 0) {
      tft.fillRect(14, 120, 214, 20, COLOR_BG);
      tft.setTextColor(COLOR_TEXT, COLOR_BG);
      tft.drawString(elrsReceiverLuaName, 16, 122, 2);
      strncpy(lastLuaName, elrsReceiverLuaName, sizeof(lastLuaName) - 1);
      lastLuaName[sizeof(lastLuaName) - 1] = '\0';
    }

    bool modelFocus = dpadFocusVisible && selectedButton == BTN_ELRS_RX_MODEL_ID;
    if (elrsReceiverHasModelId) {
      if (stateChanged ||
          lastModelId != elrsReceiverEditModelId ||
          lastModelFocus != modelFocus) {
        tft.fillRect(ELRS_RX_FIELD_X - 3, ELRS_RX_MODEL_ID_Y - 3,
                     ELRS_RX_FIELD_W + 6, ELRS_RX_FIELD_H + 6, COLOR_BG);
        drawGradientControl(ELRS_RX_FIELD_X, ELRS_RX_MODEL_ID_Y,
                            ELRS_RX_FIELD_W, ELRS_RX_FIELD_H, 7,
                            COLOR_PANEL, COLOR_ACCENT);
        if (modelFocus) {
          tft.drawRoundRect(ELRS_RX_FIELD_X - 2, ELRS_RX_MODEL_ID_Y - 2,
                            ELRS_RX_FIELD_W + 4, ELRS_RX_FIELD_H + 4, 7, COLOR_ACCENT_HI);
        }
        snprintf(line, sizeof(line), "%d", constrain(elrsReceiverEditModelId, 0, 255));
        tft.setTextColor(elrsReceiverEditModelId != elrsReceiverModelId ? COLOR_ACCENT_HI : COLOR_TEXT, COLOR_PANEL);
        tft.drawCentreString(line, ELRS_RX_FIELD_X + (ELRS_RX_FIELD_W / 2), ELRS_RX_MODEL_ID_Y + 5, 2);
        lastModelId = elrsReceiverEditModelId;
        lastModelFocus = modelFocus;
      }
    } else if (stateChanged) {
      tft.fillRect(ELRS_RX_FIELD_X - 3, ELRS_RX_MODEL_ID_Y - 3,
                   ELRS_RX_FIELD_W + 6, ELRS_RX_FIELD_H + 6, COLOR_BG);
    }

    int maxRows = min((int)elrsReceiverPwmCount, 4);
    if (stateChanged || lastPwmCount != maxRows) {
      lastPwmCount = maxRows;
      for (int i = 0; i < 4; i++) {
        lastPwmFailsafe[i] = -999;
        lastPwmFocus[i] = false;
      }
    }

    for (int i = 0; i < maxRows; i++) {
      bool rightCol = (i & 1);
      int rowY = (i < 2) ? ELRS_RX_PWM_ROW1_Y : ELRS_RX_PWM_ROW2_Y;
      int fieldX = rightCol ? ELRS_RX_PWM_RIGHT_FIELD_X : ELRS_RX_PWM_LEFT_FIELD_X;
      ButtonID button = getElrsReceiverPwmButton(i);
      bool pwmFocus = dpadFocusVisible && selectedButton == button;

      if (stateChanged ||
          lastPwmFailsafe[i] != elrsReceiverPwmEditFailsafeUs[i] ||
          lastPwmFocus[i] != pwmFocus) {
        tft.fillRect(fieldX - 3, rowY - 3,
                     ELRS_RX_PWM_FIELD_W + 6, ELRS_RX_FIELD_H + 6, COLOR_BG);
        drawGradientControl(fieldX, rowY,
                            ELRS_RX_PWM_FIELD_W, ELRS_RX_FIELD_H, 7,
                            COLOR_PANEL, COLOR_ACCENT);
        if (pwmFocus) {
          tft.drawRoundRect(fieldX - 2, rowY - 2,
                            ELRS_RX_PWM_FIELD_W + 4, ELRS_RX_FIELD_H + 4, 7, COLOR_ACCENT_HI);
        }
        snprintf(line, sizeof(line), "%d", elrsReceiverPwmEditFailsafeUs[i]);
        tft.setTextColor(isElrsReceiverPwmConfigEdited(i)
                           ? COLOR_ACCENT_HI
                           : COLOR_TEXT,
                         COLOR_PANEL);
        tft.drawCentreString(line, fieldX + (ELRS_RX_PWM_FIELD_W / 2), rowY + 5, 2);
        lastPwmFailsafe[i] = elrsReceiverPwmEditFailsafeUs[i];
        lastPwmFocus[i] = pwmFocus;
      }
    }

    bool savePressed = pressedButton == BTN_ELRS_RX_SAVE;
    bool saveFocus = dpadFocusVisible && selectedButton == BTN_ELRS_RX_SAVE;
    bool pushPressed = pressedButton == BTN_ELRS_RX_PUSH_FAILSAFE;
    bool pushFocus = dpadFocusVisible && selectedButton == BTN_ELRS_RX_PUSH_FAILSAFE;
    if (elrsReceiverPwmCount > 0 &&
        (stateChanged ||
         lastPushPressed != pushPressed ||
         lastPushFocus != pushFocus)) {
      drawButtonBubble(ELRS_RX_PUSH_FS_X, ELRS_RX_PUSH_FS_Y,
                       ELRS_RX_PUSH_FS_W, ELRS_RX_PUSH_FS_H,
                       "TX FS",
                       pushPressed,
                       pushFocus,
                       -1);
      lastPushPressed = pushPressed;
      lastPushFocus = pushFocus;
    } else if (stateChanged && elrsReceiverPwmCount == 0) {
      tft.fillRect(ELRS_RX_PUSH_FS_X - 2, ELRS_RX_PUSH_FS_Y - 2,
                   ELRS_RX_PUSH_FS_W + 4, ELRS_RX_PUSH_FS_H + 4, COLOR_BG);
    }

    if (stateChanged ||
        lastSaveDirty != elrsReceiverConfigDirty ||
        lastSavePressed != savePressed ||
        lastSaveFocus != saveFocus) {
      drawButtonBubble(ELRS_RX_SAVE_X, ELRS_RX_SAVE_Y,
                       ELRS_RX_SAVE_W, ELRS_RX_SAVE_H,
                       elrsReceiverConfigDirty ? "SAVE*" : "SAVE",
                       savePressed,
                       saveFocus,
                       -1);
      lastSaveDirty = elrsReceiverConfigDirty;
      lastSavePressed = savePressed;
      lastSaveFocus = saveFocus;
    }
  } else if (elrsReceiverConfigState == RX_CONFIG_CONNECTING) {
    unsigned long elapsed = (millis() - elrsReceiverConfigStartTime) / 1000UL;
    if (stateChanged || elapsed != lastWaitSeconds) {
      char waitText[48];
      snprintf(waitText, sizeof(waitText), "Waiting %lus for RX AP", elapsed);
      tft.fillRect(14, 120, 214, 22, COLOR_BG);
      tft.setTextColor(COLOR_TEXT, COLOR_BG);
      tft.drawString(waitText, 16, 122, 2);
      lastWaitSeconds = elapsed;
    }
  } else if (elrsReceiverConfigLastEndpoint[0] != '\0') {
    if (stateChanged || strcmp(lastEndpoint, elrsReceiverConfigLastEndpoint) != 0) {
      char endpointText[48];
      snprintf(endpointText, sizeof(endpointText), "Endpoint: %s", elrsReceiverConfigLastEndpoint);
      tft.fillRect(14, 120, 214, 22, COLOR_BG);
      tft.setTextColor(COLOR_TEXT, COLOR_BG);
      tft.drawString(endpointText, 16, 122, 2);
      strncpy(lastEndpoint, elrsReceiverConfigLastEndpoint, sizeof(lastEndpoint) - 1);
      lastEndpoint[sizeof(lastEndpoint) - 1] = '\0';
    }
  }
}

  void drawModelSettings() {
  tft.fillScreen(COLOR_BG);

  const char* navLabel = (modelSettingsPage == 0) ? "NEXT" : "PREV";

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    pressedButton == BTN_BACK,
    dpadFocusVisible && selectedButton == BTN_BACK,
    BACK_TEXT_OFFSET);

    tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);

  drawButtonBubble(130, BACK_BTN_Y, 100, BACK_BTN_H, navLabel,
                   pressedButton == BTN_PAGE_NAV, dpadFocusVisible && selectedButton == BTN_PAGE_NAV, 40);

  if (modelSettingsPage == 0) {
    drawButtonBubble(20, 50, 200, BTN_HEIGHT, "Model Name",
                     pressedButton == BTN_MODEL_NAME, dpadFocusVisible && selectedButton == BTN_MODEL_NAME, 100);
    drawButtonBubble(20, 120, 200, BTN_HEIGHT, "Drive Type",
                     pressedButton == BTN_DRIVE_TYPE, dpadFocusVisible && selectedButton == BTN_DRIVE_TYPE, 100);
    drawButtonBubble(20, 190, 200, BTN_HEIGHT, "Mixing",
                     pressedButton == BTN_MIXING, dpadFocusVisible && selectedButton == BTN_MIXING, 100);
  }
  else {
    drawButtonBubble(20, 120, 200, BTN_HEIGHT, "Reverse",
                     pressedButton == BTN_REVERSE, dpadFocusVisible && selectedButton == BTN_REVERSE, 100);
  }
}

void drawDriveTypeScreen() {
  tft.fillScreen(COLOR_BG);

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    pressedButton == BTN_BACK,
    dpadFocusVisible && selectedButton == BTN_BACK,
    BACK_TEXT_OFFSET);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);

  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT);
  tft.drawCentreString("Drive Type", 120, 38, 2);

  drawDriveTypeOption(DRIVE_BTN_X1, DRIVE_BTN_Y1, DRIVE_BTN_W, DRIVE_BTN_H,
                      "Tank", DRIVE_TANK, BTN_DRIVE_TANK);
  drawDriveTypeOption(DRIVE_BTN_X2, DRIVE_BTN_Y1, DRIVE_BTN_W, DRIVE_BTN_H,
                      "Car", DRIVE_CAR, BTN_DRIVE_CAR);
  drawDriveTypeOption(DRIVE_BTN_X1, DRIVE_BTN_Y2, DRIVE_BTN_W, DRIVE_BTN_H,
                      "Omni", DRIVE_OMNI, BTN_DRIVE_OMNI);
  drawDriveTypeOption(DRIVE_BTN_X2, DRIVE_BTN_Y2, DRIVE_BTN_W, DRIVE_BTN_H,
                      "X-Drone", DRIVE_X_DRONE, BTN_DRIVE_X_DRONE);
}

void drawTankModeScreen() {
  tft.fillScreen(COLOR_BG);

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    pressedButton == BTN_BACK,
    dpadFocusVisible && selectedButton == BTN_BACK,
    BACK_TEXT_OFFSET);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT);
  tft.drawCentreString(currentDrive == DRIVE_CAR ? "Car Control" : "Tank Control", 120, 38, 2);

  bool dualActive = (getModelTankMode(activeModel) == TANK_MODE_DUAL_STICK);
  bool singleActive = !dualActive;

  drawButtonBubble(20, 70, 200, 72, "2 Stick",
                   pressedButton == BTN_TANK_DUAL,
                   dualActive || (dpadFocusVisible && selectedButton == BTN_TANK_DUAL),
                   102);
  drawButtonBubble(20, 160, 200, 72, "1 Stick",
                   pressedButton == BTN_TANK_SINGLE,
                   singleActive || (dpadFocusVisible && selectedButton == BTN_TANK_SINGLE),
                   102);

  uint16_t dualColor = dualActive ? COLOR_BG : COLOR_ACCENT;
  uint16_t singleColor = singleActive ? COLOR_BG : COLOR_ACCENT;

  // simple 2-stick icon
  tft.drawRoundRect(34, 89, 34, 26, 6, dualColor);
  tft.drawRoundRect(76, 89, 34, 26, 6, dualColor);
  tft.drawCircle(51, 102, 5, dualColor);
  tft.drawCircle(93, 102, 5, dualColor);

  // simple 1-stick icon
  tft.drawRoundRect(55, 179, 34, 26, 6, singleColor);
  tft.drawCircle(72, 192, 5, singleColor);
  tft.drawLine(72, 171, 72, 179, singleColor);
  tft.drawLine(72, 213, 72, 205, singleColor);
  tft.drawLine(47, 192, 55, 192, singleColor);
  tft.drawLine(89, 192, 97, 192, singleColor);

  char mapLine[40];
  snprintf(mapLine, sizeof(mapLine), "L:CH%u  R:CH%u",
           (unsigned)(getModelTankLeftEscChannel(activeModel) + 1),
           (unsigned)(getModelTankRightEscChannel(activeModel) + 1));
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT);
  tft.drawCentreString(mapLine, 120, 246, 2);
}

void drawEscChannelSetupScreen() {
  tft.fillScreen(COLOR_BG);

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    pressedButton == BTN_BACK,
    dpadFocusVisible && selectedButton == BTN_BACK,
    BACK_TEXT_OFFSET);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);

  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT);
  bool carSetup = (getModelDriveType(activeModel) == DRIVE_CAR);
  const char* setupTitle = carSetup ? "Car Channel Setup" : "ESC Channel Setup";
  const char* firstOutputLabel = carSetup ? "Steering Servo" : "Left ESC";
  const char* secondOutputLabel = carSetup ? "Throttle ESC" : "Right ESC";
  char prompt[40];
  snprintf(prompt, sizeof(prompt), "Select %s Channel",
           (escSetupStep == 0) ? firstOutputLabel : secondOutputLabel);
  tft.drawCentreString(setupTitle, 120, 34, 2);
  tft.drawCentreString(prompt, 120, 56, 2);

  const int bx[4] = {20, 130, 20, 130};
  const int by[4] = {86, 86, 158, 158};
  const ButtonID ids[4] = {BTN_ESC_CH1, BTN_ESC_CH2, BTN_ESC_CH3, BTN_ESC_CH4};
  char label[8];
  uint8_t currentLeft = getModelTankLeftEscChannel(activeModel);
  uint8_t currentRight = getModelTankRightEscChannel(activeModel);

  for (int i = 0; i < 4; i++) {
    snprintf(label, sizeof(label), "CH%d", i + 1);
    bool selected =
      (escSetupStep == 0 && escSetupPendingLeftChannel == (uint8_t)i) ||
      (escSetupStep == 1 && currentRight == (uint8_t)i) ||
      (escSetupStep == 0 && currentLeft == (uint8_t)i);
    bool blocked = (escSetupStep == 1 && escSetupPendingLeftChannel == (uint8_t)i);
    bool focused = dpadFocusVisible && selectedButton == ids[i];
    drawButtonBubble(
      bx[i], by[i], 90, 54, label,
      pressedButton == ids[i],
      (!blocked && selected) || focused,
      44);
    if (blocked) {
      tft.setTextFont(1);
      tft.setTextColor(COLOR_ACCENT_HI);
      tft.drawCentreString("USED", bx[i] + 45, by[i] + 40, 1);
    }
  }

  char mapLine[48];
  if (escSetupStep == 0) {
    snprintf(mapLine, sizeof(mapLine), carSetup ? "Servo:CH%u ESC:CH%u" : "Current L:CH%u R:CH%u",
             (unsigned)(currentLeft + 1), (unsigned)(currentRight + 1));
  } else {
    snprintf(mapLine, sizeof(mapLine), "%s -> CH%u",
             firstOutputLabel,
             (unsigned)(escSetupPendingLeftChannel + 1));
  }
  tft.setTextFont(2);
  tft.setTextColor(COLOR_TEXT);
  tft.drawCentreString(mapLine, 120, 246, 2);
}

void drawDriveTypeOption(int x, int y, int w, int h, const char* label,
                         DriveType drive, ButtonID button) {

  bool active = (currentDrive == drive);
  bool pressed = (pressedButton == button);
  bool focused = (dpadFocusVisible && selectedButton == button);

  uint16_t baseColor = active ? COLOR_ACCENT : COLOR_PANEL;
  uint16_t textColor = active ? COLOR_BG : COLOR_TEXT;
  uint16_t iconColor = active ? COLOR_BG : COLOR_ACCENT;
  uint16_t outlineColor = active ? COLOR_ACCENT_HI : (focused ? COLOR_ACCENT_HI : COLOR_ACCENT);

  int drawY = pressed ? y + 2 : y;
  int radius = 10;

  for (int i = 0; i < h; i++) {
    int shade = map(i, 0, h, 18, -18);

    uint8_t r = (baseColor >> 11) & 0x1F;
    uint8_t g = (baseColor >> 5)  & 0x3F;
    uint8_t b = baseColor & 0x1F;

    int nr = constrain(r + shade / 2, 0, 31);
    int ng = constrain(g + shade,     0, 63);
    int nb = constrain(b + shade / 2, 0, 31);

    uint16_t lineColor = (nr << 11) | (ng << 5) | nb;

    int xOffset = 0;

    if (i < radius) {
      int dy = radius - i;
      xOffset = radius - sqrt(radius * radius - dy * dy);
    }
    else if (i >= h - radius) {
      int dy = i - (h - radius);
      xOffset = radius - sqrt(radius * radius - dy * dy);
    }

    tft.drawFastHLine(x + xOffset, drawY + i, w - (2 * xOffset), lineColor);
  }

  tft.drawRoundRect(x, drawY, w, h, radius, outlineColor);
  tft.drawFastHLine(x + radius, drawY + h - 2, w - (2 * radius), TFT_DARKGREY);
  tft.drawFastVLine(x + w - 2, drawY + radius, h - (2 * radius), TFT_DARKGREY);

  for (int i = 0; i < 5; i++) {
    uint16_t c = fadeColor(COLOR_ACCENT_HI, 1.0 - (i * 0.16));
    tft.drawFastHLine(x + radius + i, drawY + 5 + i, w - (2 * radius) - (2 * i), c);
  }

  if (active) {
    tft.drawRoundRect(x + 2, drawY + 2, w - 4, h - 4, radius - 2, COLOR_ACCENT_HI);
  }
  else if (focused) {
    tft.drawRoundRect(x + 2, drawY + 2, w - 4, h - 4, radius - 2, COLOR_TEXT);
  }

  tft.setTextFont(2);
  tft.setTextColor(textColor);
  tft.drawCentreString(label, x + w / 2, drawY + 8, 2);

  drawDriveTypeOptionIcon(x + 12, drawY + 30, w - 24, h - 38, drive, iconColor);
}

void drawDriveTypeOptionIcon(int x, int y, int w, int h, DriveType drive, uint16_t iconColor) {
  if (drive == DRIVE_TANK) {
    drawTankIcon(x, y, w, h, iconColor);
  }
  else if (drive == DRIVE_X_DRONE) {
    drawQuadXIcon(x, y, w, h, iconColor);
  }
  else if (drive == DRIVE_CAR) {
    int iconX = x + (w / 2) - 24;
    int iconY = y + (h / 2) - 24;
    drawCarIcon(iconX, iconY, 2, iconColor);
  }
  else if (drive == DRIVE_OMNI) {
    drawOmniIcon(x, y, w, h, iconColor);
  }
}

void drawOmniIcon(int x, int y, int w, int h, uint16_t iconColor) {
  int cx = x + w / 2;
  int cy = y + h / 2;
  int r = min(w, h) / 4;

  tft.drawCircle(cx, cy, r, iconColor);
  tft.drawCircle(cx, cy, r + 4, iconColor);

  tft.drawLine(cx, y + 2, cx, cy - r - 5, iconColor);
  tft.drawLine(cx, y + 2, cx - 4, y + 8, iconColor);
  tft.drawLine(cx, y + 2, cx + 4, y + 8, iconColor);

  tft.drawLine(cx, y + h - 2, cx, cy + r + 5, iconColor);
  tft.drawLine(cx, y + h - 2, cx - 4, y + h - 8, iconColor);
  tft.drawLine(cx, y + h - 2, cx + 4, y + h - 8, iconColor);

  tft.drawLine(x + 2, cy, cx - r - 5, cy, iconColor);
  tft.drawLine(x + 2, cy, x + 8, cy - 4, iconColor);
  tft.drawLine(x + 2, cy, x + 8, cy + 4, iconColor);

  tft.drawLine(x + w - 2, cy, cx + r + 5, cy, iconColor);
  tft.drawLine(x + w - 2, cy, x + w - 8, cy - 4, iconColor);
  tft.drawLine(x + w - 2, cy, x + w - 8, cy + 4, iconColor);
}

void closeKeyboard() {
  if (!keyboardActive) return;

  keyboardActive = false;
  keyboardTarget = KEYBOARD_TARGET_MODEL_NAME;
  keyboardNeedsRedraw = true;

  // force full redraw of underlying screen
  fullRedraw = true;
  uiNeedsRedraw = true;
}

void openMixNumpad(NumpadTarget target) {
  mixNumpadTarget = target;
  mixNumpadEditingRate = (target == NUMPAD_TARGET_MIX_RATE);

  if (target == NUMPAD_TARGET_MIX_RATE || target == NUMPAD_TARGET_MIX_OFFSET) {
    mixingNeedsRedraw = true;
  }
  else if (target == NUMPAD_TARGET_RATE_VALUE) {
    ratesNeedsRedraw = true;
  }
  else if (target == NUMPAD_TARGET_ENDPOINT_VALUE) {
    endpointNeedsRedraw = true;
  }
  else if (target == NUMPAD_TARGET_ELRS_TX_POWER) {
    uiNeedsRedraw = true;
  }
  else if (target == NUMPAD_TARGET_ELRS_RX_MODEL_ID) {
    uiNeedsRedraw = true;
  }
  else if (target == NUMPAD_TARGET_ELRS_RX_FAILSAFE) {
    uiNeedsRedraw = true;
  }
  else if (target == NUMPAD_TARGET_ELRS_RX_PWM_FAILSAFE) {
    uiNeedsRedraw = true;
  }
  else if (target == NUMPAD_TARGET_TIMER_VALUE) {
    timerNeedsRedraw = true;
    uiNeedsRedraw = true;
  }
  else {
    expoNeedsRedraw = true;
  }

  mixNumpadBuffer = "";
  mixNumpadActive = true;
  mixNumpadNeedsRedraw = true;
  mixNumpadCursorRow = 0;
  mixNumpadCursorCol = 0;
  uiNeedsRedraw = true;
}

void openElrsReceiverPwmNumpad(int pwmIndex) {
  if (elrsReceiverConfigState != RX_CONFIG_DONE) return;
  if (pwmIndex < 0 || pwmIndex >= elrsReceiverPwmCount) return;

  elrsReceiverEditPwmIndex = pwmIndex;
  elrsReceiverPwmNumpadArmed = true;
  elrsReceiverPwmNumpadArmedIndex = pwmIndex;
  elrsReceiverPwmNumpadOriginalUs = constrain(elrsReceiverPwmEditFailsafeUs[pwmIndex],
                                              ELRS_RX_PWM_FAILSAFE_MIN_US,
                                              ELRS_RX_PWM_FAILSAFE_MAX_US);
  Serial.printf("ELRS RX PWM edit armed out%d original=%d gen=%lu\n",
                pwmIndex + 1,
                elrsReceiverPwmNumpadOriginalUs,
                (unsigned long)elrsReceiverConfigGeneration);
  openMixNumpad(NUMPAD_TARGET_ELRS_RX_PWM_FAILSAFE);
}

void closeMixNumpad(bool commitValue) {
  if (!mixNumpadActive) return;

  if (commitValue) {
    if (mixNumpadBuffer.length() == 0 || mixNumpadBuffer == "-") {
      commitValue = false;
    }
  }

  if (commitValue) {
    int parsedValue = 0;

    parsedValue = mixNumpadBuffer.toInt();

    if (mixNumpadTarget == NUMPAD_TARGET_MIX_RATE || mixNumpadTarget == NUMPAD_TARGET_MIX_OFFSET) {
      parsedValue = constrain(parsedValue, -100, 100);
      MixData &mix = models[activeModel].mixes[selectedMixIndex];
      if (mixNumpadTarget == NUMPAD_TARGET_MIX_RATE) {
        mix.rate = parsedValue;
      } else {
        mix.offset = parsedValue;
      }

      saveModels();
      mixingNeedsRedraw = true;
    }
    else if (mixNumpadTarget == NUMPAD_TARGET_RATE_VALUE) {
      parsedValue = constrain(parsedValue, 1, 100);
      setModelRateValue(activeModel, selectedRateChannel, parsedValue);
      saveRateValues();
      ratesNeedsRedraw = true;
    }
    else if (mixNumpadTarget == NUMPAD_TARGET_ENDPOINT_VALUE) {
      parsedValue = constrain(parsedValue, 1, 120);
      int channel = constrain(endpointNumpadChannel, 0, CHANNEL_COUNT - 1);
      if (endpointNumpadHighSide) {
        setModelEndpointHighValue(activeModel, channel, parsedValue);
      } else {
        setModelEndpointLowValue(activeModel, channel, parsedValue);
      }
      saveEndpointValues();
      endpointNeedsRedraw = true;
    }
    else if (mixNumpadTarget == NUMPAD_TARGET_ELRS_TX_POWER) {
      parsedValue = constrain(parsedValue, ELRS_TX_POWER_MIN_MW, ELRS_TX_POWER_MAX_MW);
      sendElrsTxPowerWrite((uint16_t)parsedValue);
      uiNeedsRedraw = true;
    }
    else if (mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_MODEL_ID) {
      elrsReceiverEditModelId = constrain(parsedValue, 0, 255);
      elrsReceiverSaveArmed = true;
      elrsReceiverConfigDirty = isElrsReceiverConfigEdited();
      uiNeedsRedraw = true;
    }
    else if (mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_FAILSAFE) {
      elrsReceiverEditFailsafe = constrain(parsedValue, 0, 255);
      elrsReceiverSaveArmed = true;
      elrsReceiverConfigDirty = isElrsReceiverConfigEdited();
      uiNeedsRedraw = true;
    }
    else if (mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_PWM_FAILSAFE) {
      int pwmIndex = constrain(elrsReceiverEditPwmIndex, 0, max(0, (int)elrsReceiverPwmCount - 1));
      int nextFailsafeUs = constrain(parsedValue,
                                     ELRS_RX_PWM_FAILSAFE_MIN_US,
                                     ELRS_RX_PWM_FAILSAFE_MAX_US);
      bool armedForThisField = elrsReceiverPwmNumpadArmed &&
                               pwmIndex == elrsReceiverPwmNumpadArmedIndex &&
                               elrsReceiverConfigState == RX_CONFIG_DONE;

      if (armedForThisField && nextFailsafeUs != elrsReceiverPwmNumpadOriginalUs) {
        elrsReceiverPwmEditFailsafeUs[pwmIndex] = nextFailsafeUs;
        elrsReceiverPwmEditTouched[pwmIndex] = true;
        elrsReceiverPwmEditGeneration[pwmIndex] = elrsReceiverConfigGeneration;
        elrsReceiverPwmEditRaw[pwmIndex] =
          encodeElrsPwmFailsafeUs(elrsReceiverPwmRaw[pwmIndex], elrsReceiverPwmEditFailsafeUs[pwmIndex]);
        elrsReceiverSaveArmed = true;
        Serial.printf("ELRS RX PWM edit committed out%d value=%d gen=%lu\n",
                      pwmIndex + 1,
                      nextFailsafeUs,
                      (unsigned long)elrsReceiverConfigGeneration);
      } else {
        Serial.printf("ELRS RX PWM edit ignored armed=%d index=%d/%d value=%d original=%d state=%d\n",
                      armedForThisField ? 1 : 0,
                      pwmIndex,
                      elrsReceiverPwmNumpadArmedIndex,
                      nextFailsafeUs,
                      elrsReceiverPwmNumpadOriginalUs,
                      (int)elrsReceiverConfigState);
      }
      elrsReceiverConfigDirty = isElrsReceiverConfigEdited();
      uiNeedsRedraw = true;
    }
    else if (mixNumpadTarget == NUMPAD_TARGET_TIMER_VALUE) {
      parsedValue = constrain(parsedValue, 0, 9959);
      if ((parsedValue % 100) > 59) {
        parsedValue = (parsedValue / 100) * 100 + 59;
      }
      if (parsedValue == 0) {
        timerInputBuffer = "";
        timerTargetSeconds = 0;
      } else {
        timerInputBuffer = String(parsedValue);
        timerTargetSeconds = getTimerInputSeconds();
      }
      resetTimerRuntime();
      timerNeedsRedraw = true;
      uiNeedsRedraw = true;
    }
    else {
      parsedValue = constrain(parsedValue, -100, 100);
      setModelExpoValue(activeModel, selectedExpoChannel, parsedValue);
      saveExpoValues();
      expoNeedsRedraw = true;
    }
  }

  mixNumpadActive = false;
  if (mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_PWM_FAILSAFE) {
    elrsReceiverPwmNumpadArmed = false;
    elrsReceiverPwmNumpadArmedIndex = -1;
    elrsReceiverPwmNumpadOriginalUs = 0;
  }
  endpointNumpadChannel = -1;
  mixNumpadNeedsRedraw = false;
  fullRedraw = true;
  uiNeedsRedraw = true;
}

// ===== FUNCTIONS =====
int mapTouch(int val, int in_min, int in_max, int out_min, int out_max) {
  return (val - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
bool isInside(int x, int y, int bx, int by, int bw, int bh) {
  return (x > bx && x < bx + bw && y > by && y < by + bh);
}

int cycleFocusIndex(int current, int count, bool forward) {
  if (count <= 0) return 0;
  if (current < 0 || current >= count) return forward ? 0 : count - 1;
  return forward ? ((current + 1) % count) : ((current + count - 1) % count);
}

ButtonID cycleButtonOrder(ButtonID current, const ButtonID *order, int count, bool forward) {
  if (order == nullptr || count <= 0) return BTN_NONE;

  int index = -1;
  for (int i = 0; i < count; i++) {
    if (order[i] == current) {
      index = i;
      break;
    }
  }

  if (index < 0) return forward ? order[0] : order[count - 1];
  int next = forward ? ((index + 1) % count) : ((index + count - 1) % count);
  return order[next];
}

// Centralized screen transition reset. Put per-screen "enter" cleanup here so
// both touch and D-pad navigation land in the same state.
void setScreen(Screen screen) {
  Screen previousScreen = currentScreen;
  if (previousScreen == screen) return;

#if AUDIO_ENABLED
  if (screen != SCREEN_MAIN) {
    audioVolumePanelVisible = false;
  }
#endif

  if (previousScreen == SCREEN_TX_UPDATE &&
      screen != SCREEN_TX_UPDATE &&
      screen != SCREEN_OTA_SETTINGS &&
      otaModeActive &&
      !otaUpdateInProgress) {
    stopOtaMode();
  }
  if (previousScreen == SCREEN_OTA_SETTINGS &&
      screen != SCREEN_TX_UPDATE &&
      screen != SCREEN_OTA_SETTINGS &&
      otaModeActive &&
      !otaUpdateInProgress) {
    stopOtaMode();
  }
  if (previousScreen == SCREEN_ELRS_RX_CONFIG &&
      screen != SCREEN_ELRS_RX_CONFIG) {
    stopElrsReceiverConfig();
  }
  if (previousScreen == SCREEN_MODEL_NAME &&
      screen != SCREEN_MODEL_NAME &&
      keyboardActive) {
    closeKeyboard();
    inputBoxSelected = false;
    kbPressedRow = -1;
    kbPressedCol = -1;
  }
  if (previousScreen == SCREEN_20Q_GAME &&
      screen != SCREEN_20Q_GAME &&
      keyboardActive &&
      keyboardTarget == KEYBOARD_TARGET_20Q_LEARN) {
    closeKeyboard();
    kbPressedRow = -1;
    kbPressedCol = -1;
  }

  currentScreen = screen;
  lastScreen = screen;
  dpadFocusVisible = false;
  selectedButton = BTN_NONE;
  focusIndex = 0;

  if (screen == SCREEN_MENU) {
    selectedButton = BTN_CTRL;
  }
  else if (screen == SCREEN_GAMES_MENU) {
    selectGameMenuPage(gamesMenuPage);
  }
  else if (screen == SCREEN_DISPLAY_SETTINGS) {
    selectedButton = BTN_DISPLAY_BRIGHTNESS_DEC;
    displaySettingsNeedsRedraw = true;
  }
  else if (screen == SCREEN_TIMER) {
    selectedButton = BTN_TIMER_START_PAUSE;
    timerStaticNeedsRedraw = true;
    timerNeedsRedraw = true;
    lastTimerDisplayText[0] = '\0';
  }
  else if (screen == SCREEN_CONTROLLER_SETTINGS) {
    selectedButton = getControllerSettingsDefaultButtonForPage(controllerSettingsPage);
  }
  else if (screen == SCREEN_STICK_CALIBRATION) {
    focusIndex = 0;
    stickCalibrationState = STICK_CAL_STATE_CAPTURE_CENTER;
    stickCalibrationNeedsRedraw = true;
  }
  else if (screen == SCREEN_ENDPOINTS) {
    focusIndex = 0;
    endpointNeedsRedraw = true;
  }
  else if (screen == SCREEN_EXPO) {
    focusIndex = 0;
    expoNeedsRedraw = true;
  }
  else if (screen == SCREEN_RATES) {
    focusIndex = 0;
    ratesNeedsRedraw = true;
  }
  else if (screen == SCREEN_PROTOCOL) {
    if (espNowBindingMode) selectedButton = BTN_PROTOCOL_BIND;
    else {
      selectedButton = (getModelProtocol(activeModel) == PROTOCOL_ESPNOW)
        ? BTN_PROTOCOL_ESPNOW
        : BTN_PROTOCOL_ELRS;
    }
  }
  else if (screen == SCREEN_TX_UPDATE) {
    selectedButton = BTN_PROTOCOL_OTA;
    txUpdateNeedsRedraw = true;
  }
  else if (screen == SCREEN_OTA_SETTINGS) {
    focusIndex = 1;
    otaSettingsNeedsRedraw = true;
  }
  else if (screen == SCREEN_ELRS) {
    selectedButton = BTN_ELRS_BIND;
  }
  else if (screen == SCREEN_ELRS_RX_CONFIG) {
    if (elrsReceiverHasModelId) selectedButton = BTN_ELRS_RX_MODEL_ID;
    else if (elrsReceiverPwmCount > 0) selectedButton = BTN_ELRS_RX_PWM1;
    else selectedButton = BTN_ELRS_RX_SAVE;
  }
  else if (screen == SCREEN_MODEL_SETTINGS) {
    selectedButton = (modelSettingsPage == 0) ? BTN_MODEL_NAME : BTN_REVERSE;
  }
  else if (screen == SCREEN_MIXING) {
    setSelectedMixPage(selectedMixPage);
    mixingNeedsRedraw = true;
  }
  else if (screen == SCREEN_REVERSE) {
    reverseReturnScreen = (previousScreen == SCREEN_MODEL_SETTINGS)
      ? SCREEN_MODEL_SETTINGS
      : SCREEN_CONTROLLER_SETTINGS;
  }
  else if (screen == SCREEN_DRIVE_TYPE) {
    if (currentDrive == DRIVE_TANK) selectedButton = BTN_DRIVE_TANK;
    else if (currentDrive == DRIVE_CAR) selectedButton = BTN_DRIVE_CAR;
    else if (currentDrive == DRIVE_OMNI) selectedButton = BTN_DRIVE_OMNI;
    else selectedButton = BTN_DRIVE_X_DRONE;
  }
  else if (screen == SCREEN_TANK_MODE) {
    selectedButton = (getModelTankMode(activeModel) == TANK_MODE_RIGHT_STICK)
      ? BTN_TANK_SINGLE
      : BTN_TANK_DUAL;
  }
  else if (screen == SCREEN_ESC_CHANNEL_SETUP) {
    selectedButton = BTN_ESC_CH1;
  }
  else if (screen == SCREEN_SPACE_GAME) {
    resetSpaceGame();
  }
  else if (screen == SCREEN_20Q_GAME) {
    resetTwentyQGame();
  }
  else if (screen == SCREEN_ASTEROIDS_GAME) {
    resetAsteroidsGame();
  }
  else if (screen == SCREEN_STAR_SWARM_GAME) {
    resetStarSwarmGame();
  }
  else if (screen == SCREEN_DUNGEON_RUN) {
    resetDungeonRun();
  }

  fullRedraw = true;
  uiNeedsRedraw = true;
  topBarNeedsRedraw = true;
  screenChangePending = false;
}

void queueScreenButton(ButtonID button, Screen screen) {
  if (waitingForRelease) return;

  if (screen == SCREEN_REVERSE) {
    if (currentScreen == SCREEN_MODEL_SETTINGS) {
      reverseReturnScreen = SCREEN_MODEL_SETTINGS;
    } else if (currentScreen == SCREEN_CONTROLLER_SETTINGS) {
      reverseReturnScreen = SCREEN_CONTROLLER_SETTINGS;
    }
  }

  pressedButton = button;
  nextScreen = screen;
  waitingForRelease = true;
  screenChangePending = true;
  uiNeedsRedraw = true;
}

bool isBackHoldEligibleScreen(Screen screen) {
  return screen != SCREEN_SPLASH &&
         screen != SCREEN_MAIN &&
         screen != SCREEN_MENU &&
         screen != SCREEN_SPACE_GAME &&
         screen != SCREEN_20Q_GAME &&
         screen != SCREEN_ASTEROIDS_GAME &&
         screen != SCREEN_STAR_SWARM_GAME &&
         screen != SCREEN_DUNGEON_RUN;
}

Screen getBackButtonShortTargetForScreen(Screen screen) {
  switch (screen) {
    case SCREEN_CONTROLLER_SETTINGS:
    case SCREEN_MODEL_SETTINGS:
    case SCREEN_GAMES_MENU:
      return SCREEN_MENU;
    case SCREEN_SPACE_GAME:
    case SCREEN_20Q_GAME:
    case SCREEN_ASTEROIDS_GAME:
    case SCREEN_STAR_SWARM_GAME:
    case SCREEN_DUNGEON_RUN:
      return SCREEN_GAMES_MENU;
    case SCREEN_PROTOCOL:
      return SCREEN_CONTROLLER_SETTINGS;
    case SCREEN_TX_UPDATE:
      return SCREEN_MENU;
    case SCREEN_ELRS:
      return SCREEN_PROTOCOL;
    case SCREEN_ELRS_RX_CONFIG:
      return SCREEN_ELRS;
    case SCREEN_OTA_SETTINGS:
      return SCREEN_TX_UPDATE;
    case SCREEN_DRIVE_TYPE:
    case SCREEN_MODEL_NAME:
      return SCREEN_MODEL_SETTINGS;
    case SCREEN_TANK_MODE:
      return SCREEN_DRIVE_TYPE;
    case SCREEN_ESC_CHANNEL_SETUP:
      return SCREEN_TANK_MODE;
    case SCREEN_MIXING:
      return SCREEN_MODEL_SETTINGS;
    case SCREEN_REVERSE:
      return reverseReturnScreen;
    case SCREEN_TRIM:
    case SCREEN_ENDPOINTS:
    case SCREEN_FAILSAFE:
    case SCREEN_EXPO:
    case SCREEN_RATES:
    case SCREEN_STICK_CALIBRATION:
      return SCREEN_CONTROLLER_SETTINGS;
    case SCREEN_DISPLAY_SETTINGS:
      return SCREEN_MENU;
    case SCREEN_TIMER:
      return SCREEN_MAIN;
    default:
      return SCREEN_MENU;
  }
}

void cancelBackHold() {
  backHoldActive = false;
  backHoldTriggered = false;
  backHoldStartTime = 0;
}

bool handleBackHoldTouch(bool isTouching, int x, int y) {
  if (backHoldSuppressUntilRelease) {
    if (!isTouching) {
      backHoldSuppressUntilRelease = false;
      cancelBackHold();
      waitingForRelease = false;
      pressedButton = BTN_NONE;
    }
    return true;
  }

  if (!isBackHoldEligibleScreen(currentScreen)) {
    cancelBackHold();
    return false;
  }

  bool onBackButton = isTouching && isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H);

  if (onBackButton) {
    if (!backHoldActive) {
      backHoldActive = true;
      backHoldTriggered = false;
      backHoldStartTime = millis();
      backHoldShortTarget = getBackButtonShortTargetForScreen(currentScreen);
      pressedButton = BTN_BACK;
      waitingForRelease = true;
      uiNeedsRedraw = true;
    } else if (!backHoldTriggered && millis() - backHoldStartTime >= BACK_HOLD_HOME_MS) {
      backHoldTriggered = true;
      screenChangePending = false;
      waitingForRelease = false;
      pressedButton = BTN_NONE;
      backHoldSuppressUntilRelease = true;
      setScreen(SCREEN_MAIN);
    }
    return true;
  }

  if (backHoldActive && !isTouching) {
    if (!backHoldTriggered) {
      Screen target = backHoldShortTarget;
      cancelBackHold();
      waitingForRelease = false;
      pressedButton = BTN_NONE;
      queueScreenButton(BTN_BACK, target);
    } else {
      cancelBackHold();
      waitingForRelease = false;
      pressedButton = BTN_NONE;
    }
    return true;
  }

  if (backHoldActive && isTouching) {
    cancelBackHold();
    waitingForRelease = false;
    pressedButton = BTN_NONE;
  }
  return false;
}

void saveKeyboardBufferToModelSlot() {
  if (keyboardBuffer.length() == 0) return;

  int targetModel = selectedModelIndex;
  if (targetModel < 0 || targetModel >= MAX_MODELS) {
    targetModel = activeModel;
  }

  bool creatingNewModel = modelNames[targetModel].length() == 0;
  if (creatingNewModel) {
    initModelDefaults(targetModel);
    clearBoundReceiver(targetModel);
  }

  strncpy(models[targetModel].name, keyboardBuffer.c_str(), 19);
  models[targetModel].name[19] = '\0';
  modelNames[targetModel] = keyboardBuffer;

  activeModel = targetModel;
  selectedModelIndex = targetModel;
  clearActiveEspNowSessionTelemetry();
  currentDrive = getModelDriveType(targetModel);
  droneArmWarningVisible = false;
  droneArmWarningTouchLocked = false;
  droneArmWarningStaticDirty = false;
  droneArmWarningDynamicDirty = false;
  droneSafetyThrottleLockActive = false;
  setDroneArmActive(false);
  currentModelName = String(models[targetModel].name);

  trimRenderX = models[targetModel].trimX[currentTrimPage];
  trimRenderY = models[targetModel].trimY[currentTrimPage];
  selectedMixIndex = 0;
  mixingNeedsRedraw = true;

  saveModels();
  saveActiveModelIndex();
  if (creatingNewModel) {
    saveReceiverBindings();
  }
}

bool modelSlotUninitialized(int i) {
  for (int c = 0; c < 20; c++) {
    if ((uint8_t)models[i].name[c] != 0xFF) return false;
  }

  return true;
}

void saveModels() {
  int addr = 0;

  for (int i = 0; i < MAX_MODELS; i++) {
    EEPROM.put(addr, models[i]);
    addr += sizeof(ModelData);
  }

  uint8_t packed0 = 0;
  uint8_t packed1 = 0;
  for (int i = 0; i < MAX_MODELS; i++) {
    sanitizeModelTankEscChannels(i);
    uint8_t nibble =
      (modelTankLeftEscChannel[i] & 0x03) |
      ((modelTankRightEscChannel[i] & 0x03) << 2);
    if (i < 2) {
      packed0 |= (nibble << (i * 4));
    } else {
      packed1 |= (nibble << ((i - 2) * 4));
    }
  }
  EEPROM.write(EEPROM_ESC_MAP_DATA_ADDR, packed0);
  EEPROM.write(EEPROM_ESC_MAP_DATA_ADDR + 1, packed1);
  EEPROM.write(EEPROM_ESC_MAP_VERSION_ADDR, ESC_MAP_STORAGE_VERSION);

  EEPROM.write(EEPROM_MIX_VERSION_ADDR, MIX_STORAGE_VERSION);
  EEPROM.commit();
}

void updateMainModelPanelBounds() {
  const int stickY = 50;
  const int stickSize = 80;
  const int padding = 10;

  const int panelY = stickY - padding;
  const int panelH = stickSize + (padding * 2);

  modelPanelX = 10;
  modelPanelW = 220;
  modelPanelY = panelY + panelH + 10;
  modelPanelH = (MENU_BTN_Y - 10) - modelPanelY;
}

void getDronePanelBounds(int iconX, int iconY, int iconW, int iconH,
                         int &droneIconH, int &armX, int &armY, int &armW, int &armH) {
  armH = 24;
  int gap = 4;
  droneIconH = max(28, iconH - armH - gap);
  armX = iconX + 6;
  armY = iconY + droneIconH + gap;
  armW = iconW - 12;
  if (armY + armH > iconY + iconH) {
    armH = max(18, (iconY + iconH) - armY);
  }
}

void loadModels() {
  int addr = 0;

  for (int i = 0; i < MAX_MODELS; i++) {
    EEPROM.get(addr, models[i]);
    addr += sizeof(ModelData);
  }
}

// !!!!==== TOUCH HANDELING ====!!!!
// Touch dispatcher. It routes coordinates to modal editors first, then to the
// active screen. D-pad handling lives in loop(), but both paths eventually
// mutate the same selectedButton/currentScreen state.
void handleTouch(int x, int y) {

  if (keyboardActive) {
    handleKeyboardTouch(x, y);
    return;
  }

  if (mixNumpadActive) {
    handleMixNumpadTouch(x, y);
    return;
  }

  if (handleMainVolumeTouch(x, y)) {
    return;
  }

  // ===== MAIN =====
  if (currentScreen == SCREEN_MAIN) {
    updateMainModelPanelBounds();

    const int mainStickY = 50;
    const int mainStickSize = 80;
    const int leftStickX = 20;
    const int rightStickX = 140;
    const int iconX = modelPanelX + 10;
    const int iconY = modelPanelY + 10;
    const int iconW = (modelPanelW - 20) / 2;
    const int iconH = modelPanelH - 20;
    int droneIconH = 0;
    int droneArmX = 0;
    int droneArmY = 0;
    int droneArmW = 0;
    int droneArmH = 0;
    getDronePanelBounds(iconX, iconY, iconW, iconH,
                        droneIconH, droneArmX, droneArmY, droneArmW, droneArmH);
    const int rightPanelX = modelPanelX + (modelPanelW / 2);
    const int rightPanelW = modelPanelW / 2;

    if (isInside(x, y, MENU_BTN_X, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H)) {
      queueScreenButton(BTN_MENU, SCREEN_MENU);
      return;
    }

    // Quick-trim shortcut: touch left/right gimbal to jump straight to that trim page.
    if (isInside(x, y, leftStickX, mainStickY, mainStickSize, mainStickSize)) {
      if (!waitingForRelease) {
        currentTrimPage = 0;
        trimRenderX = models[activeModel].trimX[currentTrimPage];
        trimRenderY = models[activeModel].trimY[currentTrimPage];
        trimNeedsRedraw = true;
        trimDirty = true;
        fullRedraw = true;
        uiNeedsRedraw = true;
      }
      queueScreenButton(BTN_TRIM, SCREEN_TRIM);
      return;
    }

    if (isInside(x, y, rightStickX, mainStickY, mainStickSize, mainStickSize)) {
      if (!waitingForRelease) {
        currentTrimPage = 1;
        trimRenderX = models[activeModel].trimX[currentTrimPage];
        trimRenderY = models[activeModel].trimY[currentTrimPage];
        trimNeedsRedraw = true;
        trimDirty = true;
        fullRedraw = true;
        uiNeedsRedraw = true;
      }
      queueScreenButton(BTN_TRIM, SCREEN_TRIM);
      return;
    }

    if (currentDrive == DRIVE_X_DRONE && droneArmActive &&
        isInside(x, y, iconX, iconY, iconW, iconH)) {
      if (!waitingForRelease) {
        pressedButton = BTN_DRONE_ARM;
        selectedButton = BTN_DRONE_ARM;
        setDroneArmActive(false);
        waitingForRelease = true;
      }
      return;
    }

    if (currentDrive == DRIVE_X_DRONE &&
        isInside(x, y, droneArmX, droneArmY, droneArmW, droneArmH)) {
      if (!waitingForRelease) {
        pressedButton = BTN_DRONE_ARM;
        selectedButton = BTN_DRONE_ARM;
        showDroneArmWarning();
        waitingForRelease = true;
      }
      return;
    }

    // Touching the drive icon area opens Drive Type directly.
    if (isInside(x, y, iconX, iconY, iconW, iconH)) {
      queueScreenButton(BTN_DRIVE_TYPE, SCREEN_DRIVE_TYPE);
      return;
    }

    // Touching the right info panel opens Protocol directly.
    if (isInside(x, y, rightPanelX, modelPanelY, rightPanelW, modelPanelH)) {
      queueScreenButton(BTN_PROTOCOL, SCREEN_PROTOCOL);
      return;
    }

    if (isInside(x, y, modelPanelX, modelPanelY, modelPanelW, modelPanelH)) {
      if (!waitingForRelease) selectedButton = BTN_DRIVE_TYPE;
      queueScreenButton(BTN_DRIVE_TYPE, SCREEN_MODEL_SETTINGS);
      return;
    }
  }

  else if (currentScreen == SCREEN_TIMER) {
    handleTimerTouch(x, y);
    return;
  }

  // ===== MENU =====
  else if (currentScreen == SCREEN_MENU) {

    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, SCREEN_MAIN);
      return;
    }

    else if (isInside(x, y, CTRL_BTN_X, 60, CTRL_BTN_W, CTRL_BTN_H)) {
      queueScreenButton(BTN_CTRL, SCREEN_CONTROLLER_SETTINGS);
      return;
    }

    else if (isInside(x, y, MODEL_BTN_X, 150, MODEL_BTN_W, MODEL_BTN_H)) {
      queueScreenButton(BTN_MODEL, SCREEN_MODEL_SETTINGS);
      return;
    }

    else if (isInside(x, y, DISPLAY_MENU_BTN_X, DISPLAY_MENU_BTN_Y, DISPLAY_MENU_BTN_W, DISPLAY_MENU_BTN_H)) {
      queueScreenButton(BTN_DISPLAY_SETTINGS, SCREEN_DISPLAY_SETTINGS);
      return;
    }

    else if (isInside(x, y, TX_UPDATE_MENU_BTN_X, TX_UPDATE_MENU_BTN_Y, TX_UPDATE_MENU_BTN_W, TX_UPDATE_MENU_BTN_H)) {
      queueScreenButton(BTN_TX_UPDATE, SCREEN_TX_UPDATE);
      return;
    }

    else if (isInside(x, y, GAME_BTN_X, GAME_BTN_Y, GAME_BTN_W, GAME_BTN_H)) {
      queueScreenButton(BTN_GAME, SCREEN_GAMES_MENU);
      return;
    }
  }

  else if (currentScreen == SCREEN_GAMES_MENU) {
    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, SCREEN_MENU);
      return;
    }

    if (isInside(x, y, GAME_MENU_PAGE_BTN_X, GAME_MENU_PAGE_BTN_Y,
                 GAME_MENU_PAGE_BTN_W, GAME_MENU_PAGE_BTN_H)) {
      if (!waitingForRelease) {
        pressedButton = BTN_PAGE_NAV;
        selectedButton = BTN_PAGE_NAV;
        waitingForRelease = true;
        selectGameMenuPage(gamesMenuPage + 1);
      }
      return;
    }

    if (waitingForRelease) return;

    int firstIndex = gamesMenuPage * GAME_MENU_ITEMS_PER_PAGE;
    for (int row = 0; row < GAME_MENU_ITEMS_PER_PAGE; row++) {
      int entryIndex = firstIndex + row;
      ButtonID button = getGameMenuButtonForIndex(entryIndex);
      if (button == BTN_NONE) break;
      if (isInside(x, y, GAME_MENU_BTN_X, gameMenuRowY[row],
                   GAME_MENU_BTN_W, GAME_MENU_BTN_H)) {
        pressedButton = button;
        selectedButton = button;
        waitingForRelease = true;
        if (button == BTN_GAME_BATTLEZONE) {
          nextScreen = SCREEN_SPACE_GAME;
          screenChangePending = true;
          uiNeedsRedraw = true;
        } else if (button == BTN_GAME_20Q) {
          nextScreen = SCREEN_20Q_GAME;
          screenChangePending = true;
          uiNeedsRedraw = true;
        } else if (button == BTN_GAME_ASTEROIDS) {
          nextScreen = SCREEN_ASTEROIDS_GAME;
          screenChangePending = true;
          uiNeedsRedraw = true;
        } else if (button == BTN_GAME_STAR_SWARM) {
          nextScreen = SCREEN_STAR_SWARM_GAME;
          screenChangePending = true;
          uiNeedsRedraw = true;
        } else if (button == BTN_GAME_DUNGEON_RUN) {
          nextScreen = SCREEN_DUNGEON_RUN;
          screenChangePending = true;
          uiNeedsRedraw = true;
        } else {
          dpadFocusVisible = true;
          fullRedraw = true;
          uiNeedsRedraw = true;
        }
        return;
      }
    }
  }

  else if (currentScreen == SCREEN_DISPLAY_SETTINGS) {
    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, SCREEN_MENU);
      return;
    }

    if (waitingForRelease) return;

    if (isInside(x, y, DISPLAY_SETTINGS_MINUS_X, DISPLAY_SETTINGS_BRIGHTNESS_Y + DISPLAY_SETTINGS_ADJUST_BTN_Y_OFFSET,
                 DISPLAY_SETTINGS_ADJUST_BTN_W, DISPLAY_SETTINGS_ADJUST_BTN_H)) {
      pressedButton = BTN_DISPLAY_BRIGHTNESS_DEC;
      selectedButton = BTN_DISPLAY_BRIGHTNESS_DEC;
      stepDisplayOption(displayBrightness, displayBrightnessOptions, displayBrightnessOptionCount, -1);
      displaySleepBrightness = min(displaySleepBrightness, displayBrightness);
      saveDisplaySettings();
      applyDisplayBacklight();
      displaySettingsNeedsRedraw = true;
      uiNeedsRedraw = true;
      waitingForRelease = true;
      return;
    }

    if (isInside(x, y, DISPLAY_SETTINGS_PLUS_X, DISPLAY_SETTINGS_BRIGHTNESS_Y + DISPLAY_SETTINGS_ADJUST_BTN_Y_OFFSET,
                 DISPLAY_SETTINGS_ADJUST_BTN_W, DISPLAY_SETTINGS_ADJUST_BTN_H)) {
      pressedButton = BTN_DISPLAY_BRIGHTNESS_INC;
      selectedButton = BTN_DISPLAY_BRIGHTNESS_INC;
      stepDisplayOption(displayBrightness, displayBrightnessOptions, displayBrightnessOptionCount, 1);
      saveDisplaySettings();
      applyDisplayBacklight();
      displaySettingsNeedsRedraw = true;
      uiNeedsRedraw = true;
      waitingForRelease = true;
      return;
    }

    if (isInside(x, y, DISPLAY_SETTINGS_MINUS_X, DISPLAY_SETTINGS_TIMEOUT_Y + DISPLAY_SETTINGS_ADJUST_BTN_Y_OFFSET,
                 DISPLAY_SETTINGS_ADJUST_BTN_W, DISPLAY_SETTINGS_ADJUST_BTN_H)) {
      pressedButton = BTN_DISPLAY_TIMEOUT_DEC;
      selectedButton = BTN_DISPLAY_TIMEOUT_DEC;
      displayTimeoutIndex = constrain((int)displayTimeoutIndex - 1, 0, displayTimeoutOptionCount - 1);
      saveDisplaySettings();
      displaySettingsNeedsRedraw = true;
      uiNeedsRedraw = true;
      waitingForRelease = true;
      return;
    }

    if (isInside(x, y, DISPLAY_SETTINGS_PLUS_X, DISPLAY_SETTINGS_TIMEOUT_Y + DISPLAY_SETTINGS_ADJUST_BTN_Y_OFFSET,
                 DISPLAY_SETTINGS_ADJUST_BTN_W, DISPLAY_SETTINGS_ADJUST_BTN_H)) {
      pressedButton = BTN_DISPLAY_TIMEOUT_INC;
      selectedButton = BTN_DISPLAY_TIMEOUT_INC;
      displayTimeoutIndex = constrain((int)displayTimeoutIndex + 1, 0, displayTimeoutOptionCount - 1);
      saveDisplaySettings();
      displaySettingsNeedsRedraw = true;
      uiNeedsRedraw = true;
      waitingForRelease = true;
      return;
    }

    if (isInside(x, y, DISPLAY_SETTINGS_MINUS_X, DISPLAY_SETTINGS_OFF_TIMEOUT_Y + DISPLAY_SETTINGS_ADJUST_BTN_Y_OFFSET,
                 DISPLAY_SETTINGS_ADJUST_BTN_W, DISPLAY_SETTINGS_ADJUST_BTN_H)) {
      pressedButton = BTN_DISPLAY_OFF_TIMEOUT_DEC;
      selectedButton = BTN_DISPLAY_OFF_TIMEOUT_DEC;
      displayOffTimeoutIndex = constrain((int)displayOffTimeoutIndex - 1, 0, displayOffTimeoutOptionCount - 1);
      saveDisplaySettings();
      displaySettingsNeedsRedraw = true;
      uiNeedsRedraw = true;
      waitingForRelease = true;
      return;
    }

    if (isInside(x, y, DISPLAY_SETTINGS_PLUS_X, DISPLAY_SETTINGS_OFF_TIMEOUT_Y + DISPLAY_SETTINGS_ADJUST_BTN_Y_OFFSET,
                 DISPLAY_SETTINGS_ADJUST_BTN_W, DISPLAY_SETTINGS_ADJUST_BTN_H)) {
      pressedButton = BTN_DISPLAY_OFF_TIMEOUT_INC;
      selectedButton = BTN_DISPLAY_OFF_TIMEOUT_INC;
      displayOffTimeoutIndex = constrain((int)displayOffTimeoutIndex + 1, 0, displayOffTimeoutOptionCount - 1);
      saveDisplaySettings();
      displaySettingsNeedsRedraw = true;
      uiNeedsRedraw = true;
      waitingForRelease = true;
      return;
    }

    if (isInside(x, y, DISPLAY_SETTINGS_MINUS_X, DISPLAY_SETTINGS_SLEEP_Y + DISPLAY_SETTINGS_ADJUST_BTN_Y_OFFSET,
                 DISPLAY_SETTINGS_ADJUST_BTN_W, DISPLAY_SETTINGS_ADJUST_BTN_H)) {
      pressedButton = BTN_DISPLAY_SLEEP_DEC;
      selectedButton = BTN_DISPLAY_SLEEP_DEC;
      stepDisplayOption(displaySleepBrightness, displaySleepBrightnessOptions, displaySleepBrightnessOptionCount, -1);
      saveDisplaySettings();
      applyDisplayBacklight();
      displaySettingsNeedsRedraw = true;
      uiNeedsRedraw = true;
      waitingForRelease = true;
      return;
    }

    if (isInside(x, y, DISPLAY_SETTINGS_PLUS_X, DISPLAY_SETTINGS_SLEEP_Y + DISPLAY_SETTINGS_ADJUST_BTN_Y_OFFSET,
                 DISPLAY_SETTINGS_ADJUST_BTN_W, DISPLAY_SETTINGS_ADJUST_BTN_H)) {
      pressedButton = BTN_DISPLAY_SLEEP_INC;
      selectedButton = BTN_DISPLAY_SLEEP_INC;
      stepDisplayOption(displaySleepBrightness, displaySleepBrightnessOptions, displaySleepBrightnessOptionCount, 1);
      saveDisplaySettings();
      applyDisplayBacklight();
      displaySettingsNeedsRedraw = true;
      uiNeedsRedraw = true;
      waitingForRelease = true;
      return;
    }

    if (isInside(x, y, DISPLAY_SETTINGS_THEME_X, DISPLAY_SETTINGS_THEME_Y,
                 DISPLAY_SETTINGS_THEME_W, DISPLAY_SETTINGS_THEME_H)) {
      pressedButton = BTN_DISPLAY_THEME_TOGGLE;
      selectedButton = BTN_DISPLAY_THEME_TOGGLE;
      themeMode = (themeMode == THEME_DARK) ? THEME_LIGHT : THEME_DARK;
      applyThemePalette();
      saveDisplaySettings();
      fullRedraw = true;
      uiNeedsRedraw = true;
      topBarNeedsRedraw = true;
      waitingForRelease = true;
      return;
    }
  }

  // ===== SPACE GAME =====
  else if (currentScreen == SCREEN_SPACE_GAME) {
    handleSpaceGameTouch(x, y);
    return;
  }

  // ===== 20 QUESTIONS =====
  else if (currentScreen == SCREEN_20Q_GAME) {
    handleTwentyQTouch(x, y);
    return;
  }

  // ===== ASTEROIDS =====
  else if (currentScreen == SCREEN_ASTEROIDS_GAME) {
    handleAsteroidsTouch(x, y);
    return;
  }

  // ===== STAR SWARM =====
  else if (currentScreen == SCREEN_STAR_SWARM_GAME) {
    handleStarSwarmTouch(x, y);
    return;
  }

  // ===== DUNGEON RUN =====
  else if (currentScreen == SCREEN_DUNGEON_RUN) {
    handleDungeonRunTouch(x, y);
    return;
  }

  // ===== CONTROLLER SETTINGS =====
  else if (currentScreen == SCREEN_CONTROLLER_SETTINGS) {

    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, SCREEN_MENU);
      return;
    }

    else if (isInside(x, y, 130, BACK_BTN_Y, 100, BACK_BTN_H)) {
      if (!waitingForRelease) {
        controllerSettingsPage = (controllerSettingsPage + 1) % controllerSettingsPageCount;
        selectedButton = getControllerSettingsDefaultButtonForPage(controllerSettingsPage);
        pressedButton = BTN_PAGE_NAV;
        fullRedraw = true;
        uiNeedsRedraw = true;
        waitingForRelease = true;
      }
      return;
    }

    else if (controllerSettingsPage == 0) {
      if (isInside(x, y, 20, 50, 200, BTN_HEIGHT)) {
        queueScreenButton(BTN_TRIM, SCREEN_TRIM);
        return;
      }

      else if (isInside(x, y, 20, 120, 200, BTN_HEIGHT)) {
        queueScreenButton(BTN_FAILSAFE, SCREEN_FAILSAFE);
        return;
      }

      else if (isInside(x, y, 20, 190, 200, BTN_HEIGHT)) {
        queueScreenButton(BTN_ENDPOINTS, SCREEN_ENDPOINTS);
        return;
      }
    }
    else if (controllerSettingsPage == 1) {
      if (isInside(x, y, 20, 50, 200, BTN_HEIGHT)) {
        queueScreenButton(BTN_EXPO, SCREEN_EXPO);
        return;
      }

      else if (isInside(x, y, 20, 120, 200, BTN_HEIGHT)) {
        queueScreenButton(BTN_RATES, SCREEN_RATES);
        return;
      }

      else if (isInside(x, y, 20, 190, 200, BTN_HEIGHT)) {
        queueScreenButton(BTN_PROTOCOL, SCREEN_PROTOCOL);
        return;
      }
    }
    else if (STICK_CAL_SCREEN_ENABLED) {
      if (isInside(x, y, 20, 85, 200, BTN_HEIGHT)) {
        queueScreenButton(BTN_STICK_CAL, SCREEN_STICK_CALIBRATION);
        return;
      }
    }
  }

  else if (currentScreen == SCREEN_STICK_CALIBRATION) {
    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, SCREEN_CONTROLLER_SETTINGS);
      return;
    }

    if (isInside(x, y, 130, BACK_BTN_Y, 100, BACK_BTN_H) && !waitingForRelease) {
      pressedButton = BTN_STICK_CAL;
      if (stickCalibrationState == STICK_CAL_STATE_CAPTURE_CENTER) {
        captureStickCalibrationCenter();
      } else if (finalizeStickCalibration()) {
        setScreen(SCREEN_CONTROLLER_SETTINGS);
      }
      fullRedraw = true;
      uiNeedsRedraw = true;
      waitingForRelease = true;
      return;
    }
  }

  else if (currentScreen == SCREEN_EXPO || currentScreen == SCREEN_RATES) {
    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, SCREEN_CONTROLLER_SETTINGS);
      return;
    }

    if (currentScreen == SCREEN_EXPO) {
      if (!waitingForRelease) {
        for (int i = 0; i < CHANNEL_COUNT; i++) {
          if (isInside(x, y, RATE_TAB_X(i), RATE_TAB_Y, RATE_TAB_W, RATE_TAB_H)) {
            selectedExpoChannel = i;
            focusIndex = i + 1;
            expoNeedsRedraw = true;
            uiNeedsRedraw = true;
            waitingForRelease = true;
            return;
          }
        }
      }

      if (isInside(x, y, RATE_VALUE_BOX_X, EXPO_VALUE_BOX_Y, RATE_VALUE_BOX_W, RATE_VALUE_BOX_H)) {
        openMixNumpad(NUMPAD_TARGET_EXPO_VALUE);
        focusIndex = CHANNEL_COUNT + 1;
        waitingForRelease = true;
        return;
      }
    }
    else if (currentScreen == SCREEN_RATES) {
      if (!waitingForRelease) {
        for (int i = 0; i < CHANNEL_COUNT; i++) {
          if (isInside(x, y, RATE_TAB_X(i), RATE_TAB_Y, RATE_TAB_W, RATE_TAB_H)) {
            selectedRateChannel = i;
            focusIndex = i + 1;
            ratesNeedsRedraw = true;
            uiNeedsRedraw = true;
            waitingForRelease = true;
            return;
          }
        }

        if (isInside(x, y, RATE_TOGGLE_X(0), RATE_TOGGLE_Y, RATE_TOGGLE_W, RATE_TOGGLE_H)) {
          setModelRateValue(activeModel, selectedRateChannel, RATE_LOW_VALUE);
          ratesValueDirty = true;
          ratesNeedsRedraw = true;
          uiNeedsRedraw = true;
          focusIndex = CHANNEL_COUNT + 1;
          waitingForRelease = true;
          return;
        }

        if (isInside(x, y, RATE_TOGGLE_X(1), RATE_TOGGLE_Y, RATE_TOGGLE_W, RATE_TOGGLE_H)) {
          setModelRateValue(activeModel, selectedRateChannel, RATE_NORMAL_VALUE);
          ratesValueDirty = true;
          ratesNeedsRedraw = true;
          uiNeedsRedraw = true;
          focusIndex = CHANNEL_COUNT + 2;
          waitingForRelease = true;
          return;
        }

        if (isInside(x, y, RATE_TOGGLE_X(2), RATE_TOGGLE_Y, RATE_TOGGLE_W, RATE_TOGGLE_H)) {
          setModelRateValue(activeModel, selectedRateChannel, RATE_HIGH_VALUE);
          ratesValueDirty = true;
          ratesNeedsRedraw = true;
          uiNeedsRedraw = true;
          focusIndex = CHANNEL_COUNT + 3;
          waitingForRelease = true;
          return;
        }
      }

      if (isInside(x, y, RATE_SLIDER_X, RATE_SLIDER_Y - 8, RATE_SLIDER_W, RATE_SLIDER_H + 16)) {
        int sliderValue = mapTouch(x, RATE_SLIDER_X, RATE_SLIDER_X + RATE_SLIDER_W, 1, 100);
        setModelRateValue(activeModel, selectedRateChannel, sliderValue);
        ratesValueDirty = true;
        ratesNeedsRedraw = true;
        uiNeedsRedraw = true;
        focusIndex = CHANNEL_COUNT + 4;
        return;
      }

      if (isInside(x, y, RATE_VALUE_BOX_X, RATE_VALUE_BOX_Y, RATE_VALUE_BOX_W, RATE_VALUE_BOX_H)) {
        openMixNumpad(NUMPAD_TARGET_RATE_VALUE);
        focusIndex = CHANNEL_COUNT + 4;
        waitingForRelease = true;
        return;
      }
    }
  }

  else if (currentScreen == SCREEN_ENDPOINTS) {
    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, SCREEN_CONTROLLER_SETTINGS);
      return;
    }

    int startY = 78;
    int spacing = 33;
    for (int i = 0; i < CHANNEL_COUNT; i++) {
      int rowY = startY + (i * spacing);
      if (isInside(x, y, 88, rowY, 136, 30)) {
        focusIndex = i;
        endpointNeedsRedraw = true;
        uiNeedsRedraw = true;

        if (!waitingForRelease) {
          bool highSide = isEndpointHighSideSelected(i);
          bool tappedLow = isInside(x, y, 94, rowY + 4, 46, 22);
          bool tappedHigh = isInside(x, y, 170, rowY + 4, 46, 22);

          if ((tappedLow && !highSide) || (tappedHigh && highSide)) {
            endpointNumpadChannel = i;
            endpointNumpadHighSide = highSide;
            openMixNumpad(NUMPAD_TARGET_ENDPOINT_VALUE);
          }
        }

        waitingForRelease = true;
        return;
      }
    }
  }

  else if (currentScreen == SCREEN_PROTOCOL) {
    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      if (!otaUpdateInProgress) {
        queueScreenButton(BTN_BACK, SCREEN_CONTROLLER_SETTINGS);
      }
      return;
    }

    if (!waitingForRelease) {
      if (isInside(x, y, PROTOCOL_MODE_BTN_X, PROTOCOL_ELRS_BTN_Y, PROTOCOL_MODE_BTN_W, PROTOCOL_MODE_BTN_H)) {
        if (!otaModeActive) {
          cancelEspNowBinding(true);
          setModelProtocol(activeModel, PROTOCOL_ELRS);
          elrsTxPowerUserOverrideActive = false;
          elrsLowPowerFallbackActive = true;
          elrsTxPowerMw = clampElrsTxPowerMw(ELRS_LOW_POWER_FALLBACK_MW);
          initElrsUart();
          sendElrsTxPowerWrite(ELRS_LOW_POWER_FALLBACK_MW, false);
          pressedButton = BTN_PROTOCOL_ELRS;
          selectedButton = BTN_PROTOCOL_ELRS;
          saveModels();
          fullRedraw = true;
          uiNeedsRedraw = true;
          waitingForRelease = true;
        }
        return;
      }
      else if (isInside(x, y, PROTOCOL_MODE_BTN_X, PROTOCOL_ESPNOW_BTN_Y, PROTOCOL_MODE_BTN_W, PROTOCOL_MODE_BTN_H)) {
        if (!otaModeActive) {
          setModelProtocol(activeModel, PROTOCOL_ESPNOW);
          stopElrsUart();
          pressedButton = BTN_PROTOCOL_ESPNOW;
          selectedButton = BTN_PROTOCOL_ESPNOW;
          saveModels();
          fullRedraw = true;
          uiNeedsRedraw = true;
          waitingForRelease = true;
        }
        return;
      }
      else if (isInside(x, y, PROTOCOL_BIND_BTN_X, PROTOCOL_BIND_BTN_Y,
                        PROTOCOL_BIND_BTN_W, PROTOCOL_BIND_BTN_H)) {
        if (otaModeActive) {
          return;
        }

        pressedButton = BTN_PROTOCOL_BIND;
        selectedButton = BTN_PROTOCOL_BIND;
        if (getModelProtocol(activeModel) == PROTOCOL_ELRS) {
          setScreen(SCREEN_ELRS);
        } else {
          if (espNowBindingMode) {
            cancelEspNowBinding(true);
          } else if (modelHasBoundReceiver(activeModel)) {
            sendEspNowUnbindCommand();
          } else {
            showEspNowOutputModePrompt();
          }
        }

        fullRedraw = true;
        uiNeedsRedraw = true;
        waitingForRelease = true;
        return;
      }
      else {
        return;
      }
    }

    return;
  }
  else if (currentScreen == SCREEN_TX_UPDATE) {
    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      if (!otaUpdateInProgress) {
        queueScreenButton(BTN_BACK, SCREEN_MENU);
      }
      return;
    }

    if (!waitingForRelease) {
      if (isInside(x, y, TX_UPDATE_OTA_BTN_X, TX_UPDATE_OTA_BTN_Y,
                   TX_UPDATE_OTA_BTN_W, TX_UPDATE_OTA_BTN_H)) {
        pressedButton = BTN_PROTOCOL_OTA;
        selectedButton = BTN_PROTOCOL_OTA;

        if (otaModeActive) stopOtaMode();
        else startOtaMode();

        txUpdateNeedsRedraw = true;
        fullRedraw = true;
        uiNeedsRedraw = true;
        waitingForRelease = true;
        return;
      }

      if (isInside(x, y, TX_UPDATE_CFG_BTN_X, TX_UPDATE_CFG_BTN_Y,
                   TX_UPDATE_CFG_BTN_W, TX_UPDATE_CFG_BTN_H)) {
        pressedButton = BTN_PROTOCOL_OTA_CFG;
        selectedButton = BTN_PROTOCOL_OTA_CFG;

        if (!otaUpdateInProgress) {
          setScreen(SCREEN_OTA_SETTINGS);
          fullRedraw = true;
          uiNeedsRedraw = true;
          waitingForRelease = true;
        }
        return;
      }
    }
  }
  else if (currentScreen == SCREEN_OTA_SETTINGS) {
    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, SCREEN_TX_UPDATE);
      return;
    }

    if (!waitingForRelease) {
      if (isInside(x, y, 98, 88, 130, 30)) {
        focusIndex = 1;
        beginOtaFieldEdit(KEYBOARD_TARGET_OTA_STA_SSID);
        waitingForRelease = true;
        return;
      }
      if (isInside(x, y, 98, 136, 130, 30)) {
        focusIndex = 2;
        beginOtaFieldEdit(KEYBOARD_TARGET_OTA_STA_PASSWORD);
        waitingForRelease = true;
        return;
      }
      if (isInside(x, y, 98, 184, 130, 30)) {
        focusIndex = 3;
        beginOtaFieldEdit(KEYBOARD_TARGET_OTA_AP_SSID);
        waitingForRelease = true;
        return;
      }
    }
  }
  else if (currentScreen == SCREEN_ELRS) {
    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, SCREEN_PROTOCOL);
      return;
    }

    if (!waitingForRelease) {
      if (isInside(x, y, ELRS_BIND_BTN_X, ELRS_BIND_BTN_Y, ELRS_BIND_BTN_W, ELRS_BIND_BTN_H)) {
        pressedButton = BTN_ELRS_BIND;
        selectedButton = BTN_ELRS_BIND;
        sendElrsBindCommand();
        fullRedraw = true;
        uiNeedsRedraw = true;
        waitingForRelease = true;
        return;
      }

      if (isInside(x, y, ELRS_RX_CONFIG_BTN_X, ELRS_RX_CONFIG_BTN_Y,
                   ELRS_RX_CONFIG_BTN_W, ELRS_RX_CONFIG_BTN_H)) {
        pressedButton = BTN_ELRS_RX_CONFIG;
        selectedButton = BTN_ELRS_RX_CONFIG;
        beginElrsReceiverConfig();
        waitingForRelease = true;
        return;
      }

      if (isInside(x, y, ELRS_POWER_SLIDER_X, ELRS_POWER_SLIDER_Y - 8, ELRS_POWER_SLIDER_W, ELRS_POWER_SLIDER_H + 16)) {
        int sliderMw = mapTouch(x, ELRS_POWER_SLIDER_X, ELRS_POWER_SLIDER_X + ELRS_POWER_SLIDER_W, ELRS_TX_POWER_MIN_MW, ELRS_TX_POWER_MAX_MW);
        sliderMw = constrain(sliderMw, ELRS_TX_POWER_MIN_MW, ELRS_TX_POWER_MAX_MW);
        pressedButton = BTN_ELRS_TX_POWER_SLIDER;
        selectedButton = BTN_ELRS_TX_POWER_SLIDER;
        sendElrsTxPowerWrite((uint16_t)sliderMw);
        uiNeedsRedraw = true;
        waitingForRelease = true;
        return;
      }

      if (isInside(x, y, ELRS_POWER_VALUE_BOX_X, ELRS_POWER_VALUE_BOX_Y, ELRS_POWER_VALUE_BOX_W, ELRS_POWER_VALUE_BOX_H)) {
        pressedButton = BTN_ELRS_TX_POWER_VALUE;
        selectedButton = BTN_ELRS_TX_POWER_VALUE;
        openMixNumpad(NUMPAD_TARGET_ELRS_TX_POWER);
        uiNeedsRedraw = true;
        waitingForRelease = true;
        return;
      }
    }
    return;
  }
  else if (currentScreen == SCREEN_ELRS_RX_CONFIG) {
    if (elrsReceiverConfigTouchLocked) {
      if (!waitingForRelease) {
        Serial.printf("ELRS RX config: touch ignored while locked x=%d y=%d until=%lu quietSince=%lu now=%lu\n",
                      x,
                      y,
                      elrsReceiverConfigTouchUnlockAt,
                      elrsReceiverConfigNoTouchSince,
                      millis());
      }
      elrsReceiverConfigNoTouchSince = 0;
      waitingForRelease = true;
      return;
    }

    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, SCREEN_ELRS);
      return;
    }
    if (!waitingForRelease && elrsReceiverConfigState == RX_CONFIG_DONE) {
      if (elrsReceiverHasModelId &&
          isInside(x, y, ELRS_RX_FIELD_X, ELRS_RX_MODEL_ID_Y, ELRS_RX_FIELD_W, ELRS_RX_FIELD_H)) {
        pressedButton = BTN_ELRS_RX_MODEL_ID;
        selectedButton = BTN_ELRS_RX_MODEL_ID;
        openMixNumpad(NUMPAD_TARGET_ELRS_RX_MODEL_ID);
        waitingForRelease = true;
        return;
      }
      int maxRows = min((int)elrsReceiverPwmCount, 4);
      for (int i = 0; i < maxRows; i++) {
        bool rightCol = (i & 1);
        int rowY = (i < 2) ? ELRS_RX_PWM_ROW1_Y : ELRS_RX_PWM_ROW2_Y;
        int fieldX = rightCol ? ELRS_RX_PWM_RIGHT_FIELD_X : ELRS_RX_PWM_LEFT_FIELD_X;
        if (isInside(x, y, fieldX, rowY, ELRS_RX_PWM_FIELD_W, ELRS_RX_FIELD_H)) {
          pressedButton = getElrsReceiverPwmButton(i);
          selectedButton = pressedButton;
          openElrsReceiverPwmNumpad(i);
          waitingForRelease = true;
          return;
        }
      }
      if (isInside(x, y, ELRS_RX_SAVE_X, ELRS_RX_SAVE_Y, ELRS_RX_SAVE_W, ELRS_RX_SAVE_H)) {
        pressedButton = BTN_ELRS_RX_SAVE;
        selectedButton = BTN_ELRS_RX_SAVE;
        saveElrsReceiverConfig();
        waitingForRelease = true;
        return;
      }
      if (elrsReceiverPwmCount > 0 &&
          isInside(x, y, ELRS_RX_PUSH_FS_X, ELRS_RX_PUSH_FS_Y, ELRS_RX_PUSH_FS_W, ELRS_RX_PUSH_FS_H)) {
        pressedButton = BTN_ELRS_RX_PUSH_FAILSAFE;
        selectedButton = BTN_ELRS_RX_PUSH_FAILSAFE;
        applyModelFailsafesToElrsReceiverPwm();
        waitingForRelease = true;
        return;
      }
    }
    return;
  }

  // ===== MODEL SETTINGS =====
  else if (currentScreen == SCREEN_MODEL_SETTINGS) {

  // ===== BACK =====
  if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    queueScreenButton(BTN_BACK, SCREEN_MENU);
    return;
  }

  // ===== PAGE NAV =====
  else if (isInside(x, y, 130, BACK_BTN_Y, 100, BACK_BTN_H)) {
    if (!waitingForRelease) {
      modelSettingsPage = 1 - modelSettingsPage;
      selectedButton = (modelSettingsPage == 0) ? BTN_MODEL_NAME : BTN_REVERSE;
      pressedButton = BTN_PAGE_NAV;
      fullRedraw = true;
      uiNeedsRedraw = true;
      waitingForRelease = true;
    }
    return;
  }

  if (modelSettingsPage == 0) {
    // ===== MODEL NAME =====
    if (isInside(x, y, 20, 50, 200, BTN_HEIGHT)) {
      queueScreenButton(BTN_MODEL_NAME, SCREEN_MODEL_NAME);
      return;
    }

    // ===== DRIVE TYPE =====
    else if (isInside(x, y, 20, 120, 200, BTN_HEIGHT)) {
      queueScreenButton(BTN_DRIVE_TYPE, SCREEN_DRIVE_TYPE);
      return;
    }

    // ===== MIXING =====
    else if (isInside(x, y, 20, 190, 200, BTN_HEIGHT)) {
      queueScreenButton(BTN_MIXING, SCREEN_MIXING);
      return;
    }
  }
  else {
    // ===== REVERSE =====
    if (isInside(x, y, 20, 120, 200, BTN_HEIGHT)) {
      queueScreenButton(BTN_REVERSE, SCREEN_REVERSE);
      return;
    }
  }
}

  // ===== MIXING =====
  else if (currentScreen == SCREEN_MIXING) {

    if (waitingForRelease) return;

    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, SCREEN_MODEL_SETTINGS);
      return;
    }
    if (isInside(x, y, MIX_PAGE_BTN_X, MIX_PAGE_BTN_Y, MIX_PAGE_BTN_W, MIX_PAGE_BTN_H)) {
      toggleSelectedMixPageDebounced(millis());
      waitingForRelease = true;
      return;
    }

    int pageBase = getMixPageBase(selectedMixPage);
    for (int i = 0; i < MIXES_PER_PAGE; i++) {
      if (isInside(x, y, MIX_TAB_X(i), MIX_TAB_Y, MIX_TAB_W, MIX_TAB_H)) {
        selectedMixIndex = pageBase + i;
        mixingNeedsRedraw = true;
        uiNeedsRedraw = true;
        waitingForRelease = true;
        return;
      }
    }

    MixData &mix = models[activeModel].mixes[selectedMixIndex];
    uint8_t source = getMixSource(mix);
    uint8_t destination = getMixDestination(mix);

    if (isInside(x, y, MIX_FIELD_X, MIX_ROW_ENABLE_Y, MIX_FIELD_W, MIX_FIELD_H)) {
      mix.enabled = !mix.enabled;
    }
    else if (isInside(x, y, MIX_FIELD_X, MIX_ROW_SOURCE_Y, MIX_FIELD_W, MIX_FIELD_H)) {
      source = (source + 1) % CHANNEL_COUNT;
      setMixSource(mix, source);
      if (source == destination) {
        destination = (destination + 1) % CHANNEL_COUNT;
        setMixDestination(mix, destination);
      }
    }
    else if (isInside(x, y, MIX_FIELD_X, MIX_ROW_DEST_Y, MIX_FIELD_W, MIX_FIELD_H)) {
      destination = (destination + 1) % CHANNEL_COUNT;
      setMixDestination(mix, destination);
      if (destination == source) {
        destination = (destination + 1) % CHANNEL_COUNT;
        setMixDestination(mix, destination);
      }
    }
    else if (isInside(x, y, MIX_FIELD_X, MIX_ROW_LINK_Y, MIX_FIELD_W, MIX_FIELD_H)) {
      setMixReverseLinked(mix, !isMixReverseLinked(mix));
    }
    else if (isInside(x, y, MIX_VALUE_X, MIX_ROW_RATE_Y, MIX_VALUE_W, MIX_FIELD_H)) {
      openMixNumpad(NUMPAD_TARGET_MIX_RATE);
      waitingForRelease = true;
      return;
    }
    else if (isInside(x, y, MIX_VALUE_X, MIX_ROW_OFFS_Y, MIX_VALUE_W, MIX_FIELD_H)) {
      openMixNumpad(NUMPAD_TARGET_MIX_OFFSET);
      waitingForRelease = true;
      return;
    }
    else if (isInside(x, y, MIX_MINUS_X, MIX_ROW_RATE_Y, MIX_ADJUST_W, MIX_FIELD_H)) {
      mix.rate = constrain(mix.rate - 1, -100, 100);
    }
    else if (isInside(x, y, MIX_PLUS_X, MIX_ROW_RATE_Y, MIX_ADJUST_W, MIX_FIELD_H)) {
      mix.rate = constrain(mix.rate + 1, -100, 100);
    }
    else if (isInside(x, y, MIX_MINUS_X, MIX_ROW_OFFS_Y, MIX_ADJUST_W, MIX_FIELD_H)) {
      mix.offset = constrain(mix.offset - 1, -100, 100);
    }
    else if (isInside(x, y, MIX_PLUS_X, MIX_ROW_OFFS_Y, MIX_ADJUST_W, MIX_FIELD_H)) {
      mix.offset = constrain(mix.offset + 1, -100, 100);
    }
    else {
      return;
    }

    saveModels();
    mixingNeedsRedraw = true;
    uiNeedsRedraw = true;
    waitingForRelease = true;
    return;
  }

  // ===== DRIVE TYPE =====
  else if (currentScreen == SCREEN_DRIVE_TYPE) {

    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, SCREEN_MODEL_SETTINGS);
      return;
    }

    if (!waitingForRelease) {

      if (isInside(x, y,
                   DRIVE_BTN_X1 - DRIVE_TOUCH_PAD,
                   DRIVE_BTN_Y1 - DRIVE_TOUCH_PAD,
                   DRIVE_BTN_W + (DRIVE_TOUCH_PAD * 2),
                   DRIVE_BTN_H + (DRIVE_TOUCH_PAD * 2))) {
        currentDrive = DRIVE_TANK;
        setModelDriveType(activeModel, DRIVE_TANK);
        applyDrivePresetMixes(activeModel);
        pressedButton = BTN_DRIVE_TANK;
        selectedButton = BTN_DRIVE_TANK;
        nextScreen = SCREEN_TANK_MODE;
        screenChangePending = true;
      }
      else if (isInside(x, y,
                        DRIVE_BTN_X2 - DRIVE_TOUCH_PAD,
                        DRIVE_BTN_Y1 - DRIVE_TOUCH_PAD,
                        DRIVE_BTN_W + (DRIVE_TOUCH_PAD * 2),
                        DRIVE_BTN_H + (DRIVE_TOUCH_PAD * 2))) {
        currentDrive = DRIVE_CAR;
        setModelDriveType(activeModel, DRIVE_CAR);
        applyDrivePresetMixes(activeModel);
        pressedButton = BTN_DRIVE_CAR;
        selectedButton = BTN_DRIVE_CAR;
        nextScreen = SCREEN_TANK_MODE;
        screenChangePending = true;
      }
      else if (isInside(x, y,
                        DRIVE_BTN_X1 - DRIVE_TOUCH_PAD,
                        DRIVE_BTN_Y2 - DRIVE_TOUCH_PAD,
                        DRIVE_BTN_W + (DRIVE_TOUCH_PAD * 2),
                        DRIVE_BTN_H + (DRIVE_TOUCH_PAD * 2))) {
        currentDrive = DRIVE_OMNI;
        setModelDriveType(activeModel, DRIVE_OMNI);
        applyDrivePresetMixes(activeModel);
        pressedButton = BTN_DRIVE_OMNI;
        selectedButton = BTN_DRIVE_OMNI;
        screenChangePending = false;
      }
      else if (isInside(x, y,
                        DRIVE_BTN_X2 - DRIVE_TOUCH_PAD,
                        DRIVE_BTN_Y2 - DRIVE_TOUCH_PAD,
                        DRIVE_BTN_W + (DRIVE_TOUCH_PAD * 2),
                        DRIVE_BTN_H + (DRIVE_TOUCH_PAD * 2))) {
        currentDrive = DRIVE_X_DRONE;
        setModelDriveType(activeModel, DRIVE_X_DRONE);
        applyDrivePresetMixes(activeModel);
        pressedButton = BTN_DRIVE_X_DRONE;
        selectedButton = BTN_DRIVE_X_DRONE;
        screenChangePending = false;
      }
      else {
        return;
      }

      fullRedraw = true;
      uiNeedsRedraw = true;
      saveModels();
      waitingForRelease = true;
    }

    return;
  }

  // ===== TANK MODE =====
  else if (currentScreen == SCREEN_TANK_MODE) {
    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, SCREEN_DRIVE_TYPE);
      return;
    }

    if (!waitingForRelease) {
      if (isInside(x, y, 20, 70, 200, 72)) {
        setModelTankMode(activeModel, TANK_MODE_DUAL_STICK);
        applyDrivePresetMixes(activeModel);
        pressedButton = BTN_TANK_DUAL;
        selectedButton = BTN_TANK_DUAL;
        escSetupStep = 0;
        escSetupPendingLeftChannel = getModelTankLeftEscChannel(activeModel);
        nextScreen = SCREEN_ESC_CHANNEL_SETUP;
        screenChangePending = true;
      }
      else if (isInside(x, y, 20, 160, 200, 72)) {
        setModelTankMode(activeModel, TANK_MODE_RIGHT_STICK);
        applyDrivePresetMixes(activeModel);
        pressedButton = BTN_TANK_SINGLE;
        selectedButton = BTN_TANK_SINGLE;
        escSetupStep = 0;
        escSetupPendingLeftChannel = getModelTankLeftEscChannel(activeModel);
        nextScreen = SCREEN_ESC_CHANNEL_SETUP;
        screenChangePending = true;
      }
      else {
        return;
      }

      saveModels();
      fullRedraw = true;
      uiNeedsRedraw = true;
      waitingForRelease = true;
    }

    return;
  }

  else if (currentScreen == SCREEN_ESC_CHANNEL_SETUP) {
    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, SCREEN_TANK_MODE);
      return;
    }

    if (!waitingForRelease) {
      const int bx[4] = {20, 130, 20, 130};
      const int by[4] = {86, 86, 158, 158};
      const ButtonID ids[4] = {BTN_ESC_CH1, BTN_ESC_CH2, BTN_ESC_CH3, BTN_ESC_CH4};

      int selectedChannel = -1;
      for (int i = 0; i < 4; i++) {
        if (isInside(x, y, bx[i], by[i], 90, 54)) {
          selectedChannel = i;
          pressedButton = ids[i];
          selectedButton = ids[i];
          break;
        }
      }

      if (selectedChannel < 0) {
        return;
      }

      if (escSetupStep == 0) {
        escSetupPendingLeftChannel = (uint8_t)selectedChannel;
        escSetupStep = 1;
        fullRedraw = true;
        uiNeedsRedraw = true;
        waitingForRelease = true;
        return;
      }

      if ((uint8_t)selectedChannel == escSetupPendingLeftChannel) {
        waitingForRelease = true;
        return;
      }

      setModelTankEscChannels(activeModel, escSetupPendingLeftChannel, (uint8_t)selectedChannel);
      applyDrivePresetMixes(activeModel);
      saveModels();
      nextScreen = SCREEN_TANK_MODE;
      screenChangePending = true;
      waitingForRelease = true;
      fullRedraw = true;
      uiNeedsRedraw = true;
    }

    return;
  }

  // ===== REVERSE =====
  else if (currentScreen == SCREEN_REVERSE) {

    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, reverseReturnScreen);
      return;
    }

    int startY = 42;
    int spacing = 38;

    for (int i = 0; i < CHANNEL_COUNT; i++) {

      int ty = startY + (i * spacing);
      int tx = 120;
      int tw = 80;
      int th = 30;

      if (isInside(x, y, tx, ty, tw, th)) {

        if (!waitingForRelease) {
          bool linked[CHANNEL_COUNT] = {false, false, false, false};
          getLinkedReverseChannels(activeModel, i, linked);
          bool newState = !models[activeModel].reverse[i];

          for (int ch = 0; ch < CHANNEL_COUNT; ch++) {
            if (!linked[ch]) continue;
            models[activeModel].reverse[ch] = newState;
            reverseChannelDirty[ch] = true;
          }

          saveModels();
          reverseNeedsRedraw = true;
          uiNeedsRedraw = true;
          waitingForRelease = true;
        }

        return;
      }
    }
  }

  // ===== TRIM =====
  else if (currentScreen == SCREEN_TRIM) {

  int cx = TRIM_CENTER_X;
  int cy = TRIM_CENTER_Y;
  int s  = TRIM_SIZE;
  int sideBtnY = cy - (TRIM_BTN_SIZE / 2);
  int centerBtnX = cx - (TRIM_BTN_SIZE / 2);

  int graphLeft   = cx - s;
  int graphRight  = cx + s;
  int graphTop    = cy - s;
  int graphBottom = cy + s;

  bool didPress = false;

  // ===== BACK =====
  if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {

    if (!waitingForRelease) {

      queueScreenButton(BTN_BACK, SCREEN_CONTROLLER_SETTINGS);
    }

    return;
  }

  // ===== NEXT =====
  if (isInside(x, y, 130, BACK_BTN_Y, 100, BACK_BTN_H)) {

    if (!waitingForRelease) {

      currentTrimPage = !currentTrimPage;

      // snap render to correct position for new gimbal
      trimRenderX = models[activeModel].trimX[currentTrimPage];
      trimRenderY = models[activeModel].trimY[currentTrimPage];

      trimNeedsRedraw = true;
      trimDirty = true;
      uiNeedsRedraw = true;
      fullRedraw = true;

      waitingForRelease = true;
    }

    return;
  }

  // ===== RESET =====
  if (isInside(x, y, TRIM_RESET_BTN_X, TRIM_RESET_BTN_Y, TRIM_RESET_BTN_W, TRIM_RESET_BTN_H)) {
    if (!waitingForRelease) {
      models[activeModel].trimX[currentTrimPage] = 0;
      models[activeModel].trimY[currentTrimPage] = 0;
      saveModels();

      trimRenderX = 0;
      trimRenderY = 0;
      trimDirty = true;
      trimNeedsRedraw = true;
      uiNeedsRedraw = true;
      waitingForRelease = true;
    }
    return;
  }

  // ===== GRAPH TOUCH =====
  if (isInside(x, y, graphLeft, graphTop,
             graphRight - graphLeft,
             graphBottom - graphTop)) {

  int newX = map(x, graphLeft, graphRight, -50, 50);
  int newY = map(y, graphBottom, graphTop, -50, 50);

  newX = constrain(newX, -50, 50);
  newY = constrain(newY, -50, 50);

  models[activeModel].trimX[currentTrimPage] = newX;
  models[activeModel].trimY[currentTrimPage] = newY;
  saveModels();

  trimRenderX = newX;
  trimRenderY = newY;

  trimDirty = true;
  uiNeedsRedraw = true;

  waitingForRelease = true;
  return;
  }

  // ===== TRIM BUTTONS =====
  if (!waitingForRelease) {

    // X-
    if (isInside(x, y, trimBtnLeftX, sideBtnY, TRIM_BTN_SIZE, TRIM_BTN_SIZE)) {
      models[activeModel].trimX[currentTrimPage] =
      constrain(models[activeModel].trimX[currentTrimPage] - 1, -50, 50);
      saveModels();
      didPress = true;
    }

    // X+
    else if (isInside(x, y, trimBtnRightX, sideBtnY, TRIM_BTN_SIZE, TRIM_BTN_SIZE)) {
      models[activeModel].trimX[currentTrimPage] =
      constrain(models[activeModel].trimX[currentTrimPage] + 1, -50, 50);
      saveModels();
      didPress = true;
    }

    // Y+
    else if (isInside(x, y, centerBtnX, trimBtnTopY, TRIM_BTN_SIZE, TRIM_BTN_SIZE)) {
      models[activeModel].trimY[currentTrimPage] =
      constrain(models[activeModel].trimY[currentTrimPage] + 1, -50, 50);
      saveModels();
      didPress = true;
    }

    // Y-
    else if (isInside(x, y, centerBtnX, trimBtnBottomY, TRIM_BTN_SIZE, TRIM_BTN_SIZE)) {
      models[activeModel].trimY[currentTrimPage] =
      constrain(models[activeModel].trimY[currentTrimPage] - 1, -50, 50);
      saveModels();
      didPress = true;
    }

      if (didPress) {
        trimDirty = true;
        uiNeedsRedraw = true;
        waitingForRelease = true;
      }
    }
  }
  // ==== FAILSAFE ====
  else if (currentScreen == SCREEN_FAILSAFE) {

  if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {

    queueScreenButton(BTN_BACK, SCREEN_CONTROLLER_SETTINGS);
    return;
  }

  int startY = 85;
  int spacing = 34;

  for (int i = 0; i < CHANNEL_COUNT; i++) {

    int ty = startY + (i * spacing);
    int tx = 120;
    int tw = 80;
    int th = 30;

    if (isInside(x, y, tx, ty, tw, th)) {

      if (!waitingForRelease) {

        bool newState = !models[activeModel].failsafe[i];
        models[activeModel].failsafe[i] = newState;
        if (newState) {
          setModelFailsafeValue(activeModel, i,
                                (int)roundf(outputChannels[i] * 100.0f));
          saveFailsafeValues();
        }
        saveModels();
        failsafeDirty[i] = true;
        uiNeedsRedraw = true;

        waitingForRelease = true;
      }

      return;
    }
  }
}
  else if (currentScreen == SCREEN_MODEL_NAME) {

    // ===== BACK =====
    if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
      queueScreenButton(BTN_BACK, SCREEN_MODEL_SETTINGS);
      return;
    }

    // ===== INPUT BOX =====
    if (isInside(x, y, MODEL_NAME_INPUT_X, MODEL_NAME_INPUT_Y, MODEL_NAME_INPUT_W, MODEL_NAME_INPUT_H)) {
      if (!waitingForRelease) {
        keyboardBuffer = "";
        keyboardTarget = KEYBOARD_TARGET_MODEL_NAME;
        keyboardActive = true;
        keyboardLowercase = false;
        keyboardNeedsRedraw = true;
        inputBoxSelected = true;
        waitingForRelease = true;
      }
      return;
    }

    // ===== SELECT MODEL =====
    int listY = MODEL_NAME_LIST_Y;

    for (int i = 0; i < 4; i++) {

      int bx = MODEL_NAME_BOX_X;
      int by = listY - 2;
      int bw = MODEL_NAME_BOX_W;
      int bh = MODEL_NAME_ROW_H - 6;

      if (isInside(x, y, bx, by, bw, bh)) {

        if (!waitingForRelease) {

          selectedModelIndex = i;

          if (modelNames[i].length() == 0) {
            keyboardBuffer = "";
            keyboardTarget = KEYBOARD_TARGET_MODEL_NAME;
            keyboardActive = true;
            keyboardLowercase = false;
            keyboardNeedsRedraw = true;
            inputBoxSelected = true;
            modelNameDirty = true;
            modelNameNeedsRedraw = true;
            uiNeedsRedraw = true;
            waitingForRelease = true;
            return;
          }

          pendingModelHoldIndex = i;
          pendingModelHoldStart = millis();
          pendingModelHoldTriggered = false;
          waitingForRelease = true;
        }

        return;
      }

      listY += MODEL_NAME_ROW_H;
    }

    // ===== DELETE BUTTONS =====
    int rowY = MODEL_NAME_LIST_Y;

    for (int i = 0; i < 4; i++) {

    int bx = MODEL_NAME_DELETE_X;
    int by = rowY - 2;
    int bw = MODEL_NAME_DELETE_W;
    int bh = MODEL_NAME_ROW_H - 6;

      if (isInside(x, y, bx, by, bw, bh)) {
        if (!waitingForRelease) {

          // ===== SHIFT BOTH NAME + DATA =====
          for (int j = i; j < 3; j++) {
            modelNames[j] = modelNames[j + 1];
            models[j] = models[j + 1];
            modelTankLeftEscChannel[j] = modelTankLeftEscChannel[j + 1];
            modelTankRightEscChannel[j] = modelTankRightEscChannel[j + 1];
            memcpy(boundReceiverMacs[j], boundReceiverMacs[j + 1], ESP_NOW_ETH_ALEN);
            for (int channel = 0; channel < CHANNEL_COUNT; channel++) {
              modelFailsafeValues[j][channel] = modelFailsafeValues[j + 1][channel];
              modelRateValues[j][channel] = modelRateValues[j + 1][channel];
              modelExpoValues[j][channel] = modelExpoValues[j + 1][channel];
              modelEndpointLowValues[j][channel] = modelEndpointLowValues[j + 1][channel];
              modelEndpointHighValues[j][channel] = modelEndpointHighValues[j + 1][channel];
            }
          }

          // clear last slot
          modelNames[3] = "";
          initModelDefaults(3);
          clearBoundReceiver(3);
          clearModelFailsafeValues(3);
          clearModelRateValues(3);
          clearModelExpoValues(3);
          clearModelEndpointValues(3);

          saveModels();
          saveReceiverBindings();
          saveFailsafeValues();
          saveRateValues();
          saveExpoValues();
          saveEndpointValues();

        modelNameDirty = true;
        modelNameNeedsRedraw = true;
        uiNeedsRedraw = true;
        waitingForRelease = true;
        }
      return;
      }
      rowY += MODEL_NAME_ROW_H;
    }

  }
}

bool handleKeyboardTouch(int x, int y) {

  if (!keyboardActive) return false;
  int adjustedX = adjustKeyboardTouchX(x);

  if (y < kbY) {
    if (!waitingForRelease) {
      closeKeyboard();
      inputBoxSelected = false;
      waitingForRelease = true;
    }

    return true;
  }

  for (int r = 0; r < KB_ROWS; r++) {
    for (int c = 0; c < KB_COLS; c++) {

      const char* key = getKeyboardKey(r, c);
      if (strlen(key) == 0) continue;

      int kx, ky, kw, kh;
      getKeyboardKeyRect(r, c, kx, ky, kw, kh);

      if (isInside(adjustedX, y, kx, ky, kw, kh)) {

        if (!waitingForRelease) {

          kbPressedRow = r;
          kbPressedCol = c;

          processKeyboardKey(key);
          waitingForRelease = true;
        }

        return true;
      }
    }
  }

  return true;
}

bool handleMixNumpadTouch(int x, int y) {
  if (!mixNumpadActive) return false;

  if (!isInside(x, y, MIX_NUMPAD_X, MIX_NUMPAD_Y, MIX_NUMPAD_W, MIX_NUMPAD_H)) {
    if (!waitingForRelease) {
      closeMixNumpad(false);
      waitingForRelease = true;
    }

    return true;
  }

  const char* mixPadLayout[4][3] = {
    {"1", "2", "3"},
    {"4", "5", "6"},
    {"7", "8", "9"},
    {"<", "0", "-"}
  };

  if (isInside(x, y, MIX_NUMPAD_OK_X, MIX_NUMPAD_OK_Y, MIX_NUMPAD_OK_W, MIX_NUMPAD_OK_H)) {
    if (!waitingForRelease) {
      closeMixNumpad(true);
      waitingForRelease = true;
    }

    return true;
  }

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 3; c++) {
      int keyX = MIX_NUMPAD_GRID_X + (c * (MIX_NUMPAD_KEY_W + MIX_NUMPAD_KEY_GAP));
      int keyY = MIX_NUMPAD_GRID_Y + (r * (MIX_NUMPAD_KEY_H + MIX_NUMPAD_KEY_GAP));

      if (!isInside(x, y, keyX, keyY, MIX_NUMPAD_KEY_W, MIX_NUMPAD_KEY_H)) continue;

      if (waitingForRelease) return true;

      const char* key = mixPadLayout[r][c];
      bool positiveOnlyTarget = (mixNumpadTarget == NUMPAD_TARGET_RATE_VALUE ||
                                 mixNumpadTarget == NUMPAD_TARGET_ENDPOINT_VALUE ||
                                 mixNumpadTarget == NUMPAD_TARGET_ELRS_TX_POWER ||
                                 mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_MODEL_ID ||
                                 mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_FAILSAFE ||
                                 mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_PWM_FAILSAFE ||
                                 mixNumpadTarget == NUMPAD_TARGET_TIMER_VALUE);

      if (strcmp(key, "<") == 0) {
        if (mixNumpadBuffer.length() > 0) {
          mixNumpadBuffer.remove(mixNumpadBuffer.length() - 1);

          if (mixNumpadBuffer == "-") {
            mixNumpadBuffer = "";
          }

          mixNumpadNeedsRedraw = true;
        }
      }
      else if (strcmp(key, "-") == 0) {
        if (!positiveOnlyTarget) {
          if (mixNumpadBuffer.startsWith("-")) {
            mixNumpadBuffer.remove(0, 1);
          }
          else {
            mixNumpadBuffer = "-" + mixNumpadBuffer;
          }

          if (mixNumpadBuffer.length() == 0) {
            mixNumpadBuffer = "-";
          }

          mixNumpadNeedsRedraw = true;
        }
      }
      else {
        String candidate = mixNumpadBuffer;

        if (candidate == "0") candidate = "";
        if (candidate == "-0") candidate = "-";

        candidate += key;

        bool validLength = true;
        int digitCount = 0;
        for (int i = 0; i < candidate.length(); i++) {
          if (candidate[i] >= '0' && candidate[i] <= '9') digitCount++;
        }

        int maxDigits = (mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_PWM_FAILSAFE ||
                         mixNumpadTarget == NUMPAD_TARGET_TIMER_VALUE) ? 4 : 3;
        if (digitCount > maxDigits) validLength = false;

        if (validLength) {
          int parsedValue = candidate.toInt();
          bool validCandidate = false;
          if (positiveOnlyTarget) {
            int maxValue = 100;
            if (mixNumpadTarget == NUMPAD_TARGET_ENDPOINT_VALUE) maxValue = 120;
            else if (mixNumpadTarget == NUMPAD_TARGET_ELRS_TX_POWER) maxValue = ELRS_TX_POWER_MAX_MW;
            else if (mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_MODEL_ID ||
                     mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_FAILSAFE) maxValue = 255;
            else if (mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_PWM_FAILSAFE) maxValue = ELRS_RX_PWM_FAILSAFE_MAX_US;
            else if (mixNumpadTarget == NUMPAD_TARGET_TIMER_VALUE) maxValue = 9959;
            validCandidate = (digitCount <= maxDigits && parsedValue >= 0 && parsedValue <= maxValue);
            if (mixNumpadTarget == NUMPAD_TARGET_TIMER_VALUE) {
              validCandidate = validCandidate && ((parsedValue % 100) <= 59);
            }
          } else {
            validCandidate = ((candidate == "-") || (parsedValue >= -100 && parsedValue <= 100));
          }

          if (validCandidate) {
            mixNumpadBuffer = candidate;
            mixNumpadNeedsRedraw = true;
          }
        }
      }

      waitingForRelease = true;
      return true;
    }
  }

  return true;
}

// ==== SPLASH SCREEN ====
// Startup throttle lockout prevents a model from receiving an unexpected drive
// command immediately after boot.
bool isStartupThrottleSafe() {
  if (!STARTUP_THROTTLE_SAFETY_ENABLED) return true;
  if (!ads1115Ready) return false;
  return inputChannels[STARTUP_THROTTLE_SAFETY_CHANNEL] <= STARTUP_THROTTLE_SAFE_THRESHOLD;
}

bool handleStartupThrottleSafetyBypass(uint8_t touchCount, int rawX, int rawY, unsigned long now) {
  if (ads1115Ready) {
    startupThrottleBypassTapCount = 0;
    startupThrottleBypassTouchActive = false;
    return false;
  }

  if (touchCount == 0) {
    startupThrottleBypassTouchActive = false;
    return false;
  }

  if (startupThrottleBypassTouchActive) return false;
  startupThrottleBypassTouchActive = true;

  int x = mapTouch(rawX, 0, 240, 0, 240);
  int y = mapTouch(rawY, 320, 0, 320, 0);
  if (x < 0 || x > 239 || y < STARTUP_THROTTLE_BYPASS_Y_MIN || y > 319) {
    return false;
  }

  if (startupThrottleBypassTapCount < STARTUP_THROTTLE_BYPASS_TAPS) {
    startupThrottleBypassTapCount++;
  }
  lastStartupThrottleSafetyDrawTime = 0;

  if (startupThrottleBypassTapCount >= STARTUP_THROTTLE_BYPASS_TAPS) {
    Serial.println("Startup throttle safety bypassed: stick input offline service tap sequence.");
    return true;
  }

  return false;
}

void updateStartupThrottleSafetyScreen(unsigned long now) {
  if (startupThrottleSafetyScreenDrawn &&
      now - lastStartupThrottleSafetyDrawTime < STARTUP_THROTTLE_SCREEN_REFRESH_MS) {
    return;
  }
  lastStartupThrottleSafetyDrawTime = now;

  if (!startupThrottleSafetyScreenDrawn) {
    tft.fillScreen(COLOR_BG);
    drawGradientControl(14, 78, 212, 150, 14, COLOR_PANEL, COLOR_ACCENT);
    tft.setTextFont(2);
    tft.setTextColor(COLOR_ACCENT_HI, COLOR_PANEL);
    tft.drawCentreString("THROTTLE SAFETY", 120, 96, 2);
    tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
    tft.drawCentreString("Lower CH3 to zero", 120, 128, 2);
    tft.drawCentreString("before arming transmitter.", 120, 148, 2);
    tft.drawCentreString("Weapon channel locked.", 120, 188, 2);
    startupThrottleSafetyScreenDrawn = true;
  }

  tft.fillRect(45, 166, 150, 18, COLOR_PANEL);
  tft.setTextFont(2);
  if (!ads1115Ready) {
    tft.setTextColor(TFT_ORANGE, COLOR_PANEL);
    tft.drawCentreString("Stick input offline", 120, 166, 2);
    tft.fillRect(20, 235, 200, 20, COLOR_BG);
    if (startupThrottleBypassTapCount > 0) {
      char bypassText[28];
      snprintf(bypassText, sizeof(bypassText), "Service bypass: %u/%u",
               startupThrottleBypassTapCount, STARTUP_THROTTLE_BYPASS_TAPS);
      tft.setTextColor(COLOR_TEXT, COLOR_BG);
      tft.drawCentreString(bypassText, 120, 236, 2);
    }
  } else {
    int throttlePercent = (int)roundf(constrain((inputChannels[STARTUP_THROTTLE_SAFETY_CHANNEL] + 1.0f) * 50.0f, 0.0f, 100.0f));
    char throttleText[24];
    snprintf(throttleText, sizeof(throttleText), "CH3: %d%%", throttlePercent);
    tft.setTextColor(TFT_ORANGE, COLOR_PANEL);
    tft.drawCentreString(throttleText, 120, 166, 2);
  }
}

// Boot splash and main screen. The splash keeps startup work from looking like a
// freeze; the main screen is split into static/dynamic passes for speed.
void drawSplash() {

  static bool drawn = false;

  // draw ONCE only
  if (!drawn) {
    splashStartTime = millis();
    tft.fillScreen(COLOR_BG);

    tft.setSwapBytes(true);
    tft.pushImage(0, 0, 240, 320, anubisLogo);

    startStartupAudioClip();
    drawn = true;
  }

  updateStartupAudioClip();

  // wait, then switch screens
  if (!startupAudioPlaying &&
      millis() - splashStartTime > STARTUP_SPLASH_DURATION_MS) {
    setScreen(SCREEN_MAIN);
  }
}

int getThreePosSwitchPosition() {
  if (inputChannels[5] > 0.35f) return 1;
  if (inputChannels[5] < -0.35f) return -1;
  return 0;
}

void drawThreePosSwitchIndicator(int cx, int y, int position) {
  const int triW = 24;
  const int triH = 17;
  const int square = 18;
  const int gap = 5;
  const int clearX = cx - 17;
  const int clearY = y - 3;
  const int clearW = 34;
  const int clearH = (triH * 2) + square + (gap * 2) + 8;
  const int topY = y;
  const int squareY = topY + triH + gap;
  const int bottomY = squareY + square + gap;

  tft.fillRoundRect(clearX, clearY, clearW, clearH, 7, COLOR_BG);

  uint16_t inactive = fadeColor(COLOR_ACCENT, 0.55f);
  uint16_t topColor = (position > 0) ? COLOR_ACCENT_HI : inactive;
  uint16_t midColor = (position == 0) ? COLOR_ACCENT_HI : inactive;
  uint16_t bottomColor = (position < 0) ? COLOR_ACCENT_HI : inactive;
  uint16_t activeFill = COLOR_ACCENT;

  if (position > 0) {
    tft.fillTriangle(cx, topY, cx - triW / 2, topY + triH, cx + triW / 2, topY + triH, activeFill);
  }
  tft.drawTriangle(cx, topY, cx - triW / 2, topY + triH, cx + triW / 2, topY + triH, topColor);

  if (position == 0) {
    tft.fillRoundRect(cx - square / 2, squareY, square, square, 4, activeFill);
  }
  tft.drawRoundRect(cx - square / 2, squareY, square, square, 4, midColor);

  if (position < 0) {
    tft.fillTriangle(cx - triW / 2, bottomY, cx + triW / 2, bottomY, cx, bottomY + triH, activeFill);
  }
  tft.drawTriangle(cx - triW / 2, bottomY, cx + triW / 2, bottomY, cx, bottomY + triH, bottomColor);
}

void runMainToTimerSwipeTransition() {
  const int steps = 12;
  for (int i = 1; i <= steps; i++) {
    int wipeW = (240 * i) / steps;
    tft.fillRect(240 - wipeW, 0, wipeW, 320, COLOR_BG);
    delay(8);
  }
}

bool handleMainSwipeGesture(bool isTouching, int x, int y, unsigned long now) {
  if (currentScreen != SCREEN_MAIN || keyboardActive || mixNumpadActive) {
    mainSwipeActive = false;
    mainSwipeConsumedTouch = false;
    return false;
  }

  if (isTouching && !mainSwipeActive) {
    mainSwipeActive = true;
    mainSwipeConsumedTouch = false;
    mainSwipeStartX = x;
    mainSwipeStartY = y;
    mainSwipeLastX = x;
    mainSwipeLastY = y;
    mainSwipeStartTime = now;
    return false;
  }

  if (isTouching && mainSwipeActive) {
    mainSwipeLastX = x;
    mainSwipeLastY = y;
    int dx = mainSwipeStartX - mainSwipeLastX;
    int dy = abs(mainSwipeLastY - mainSwipeStartY);
    if (dx >= 24 && dy <= MAIN_SWIPE_MAX_DY) {
      mainSwipeConsumedTouch = true;
      waitingForRelease = false;
      pressedButton = BTN_NONE;
      screenChangePending = false;
      return true;
    }
    return false;
  }

  if (!isTouching && mainSwipeActive) {
    int dx = mainSwipeStartX - mainSwipeLastX;
    int dy = abs(mainSwipeLastY - mainSwipeStartY);
    unsigned long dt = now - mainSwipeStartTime;
    mainSwipeActive = false;

    if (dx >= MAIN_SWIPE_MIN_DX && dy <= MAIN_SWIPE_MAX_DY && dt <= MAIN_SWIPE_MAX_MS) {
      runMainToTimerSwipeTransition();
      setScreen(SCREEN_TIMER);
      waitingForRelease = false;
      pressedButton = BTN_NONE;
      screenChangePending = false;
      mainSwipeConsumedTouch = false;
      return true;
    }

    bool consumed = mainSwipeConsumedTouch;
    mainSwipeConsumedTouch = false;
    return consumed;
  }

  return false;
}

uint32_t getTimerInputSeconds() {
  uint32_t value = (uint32_t)timerInputBuffer.toInt();
  uint32_t seconds = value % 100UL;
  uint32_t minutes = value / 100UL;
  if (seconds > 59UL) seconds = 59UL;
  uint32_t total = (minutes * 60UL) + seconds;
  return min(total, (uint32_t)TIMER_MAX_SECONDS);
}

unsigned long getTimerElapsedMs(unsigned long now) {
  unsigned long elapsed = timerAccumulatedMs;
  if (timerRunning) {
    elapsed += now - timerRunStartedAt;
  }
  return elapsed;
}

bool isTimerElapsed(unsigned long now) {
  if (timerTargetSeconds == 0) return false;
  unsigned long targetMs = (unsigned long)timerTargetSeconds * 1000UL;
  return getTimerElapsedMs(now) >= targetMs;
}

unsigned long getTimerDisplayMs(unsigned long now) {
  unsigned long elapsed = getTimerElapsedMs(now);
  if (timerMode == TIMER_MODE_COUNT_UP) {
    if (timerTargetSeconds > 0) {
      unsigned long targetMs = (unsigned long)timerTargetSeconds * 1000UL;
      if (elapsed >= targetMs) {
        if (timerRunning) {
          timerRunning = false;
          timerAccumulatedMs = targetMs;
          timerNeedsRedraw = true;
        }
        return targetMs;
      }
    }
    return elapsed;
  }

  unsigned long targetMs = (unsigned long)timerTargetSeconds * 1000UL;
  if (elapsed >= targetMs) {
    if (timerRunning) {
      timerRunning = false;
      timerAccumulatedMs = targetMs;
      timerNeedsRedraw = true;
    }
    return 0;
  }
  return targetMs - elapsed;
}

void formatTimerDisplay(unsigned long displayMs, char *out, size_t outSize) {
  unsigned long totalSeconds = displayMs / 1000UL;
  unsigned int minutes = (unsigned int)(totalSeconds / 60UL);
  unsigned int seconds = (unsigned int)(totalSeconds % 60UL);
  snprintf(out, outSize, "%02u:%02u", minutes, seconds);
}

void formatTimerTargetBuffer(char *out, size_t outSize) {
  if (timerTargetSeconds == 0) {
    snprintf(out, outSize, "tap time to set");
    return;
  }

  unsigned int minutes = (unsigned int)(timerTargetSeconds / 60UL);
  unsigned int seconds = (unsigned int)(timerTargetSeconds % 60UL);
  snprintf(out, outSize, "target %02u:%02u", minutes, seconds);
}

void resetTimerRuntime() {
  timerRunning = false;
  timerRunStartedAt = 0;
  timerAccumulatedMs = 0;
  lastTimerAlarmActive = false;
  lastTimerAlarmVisible = true;
  timerNeedsRedraw = true;
}

void drawTimerScreen() {
  tft.fillScreen(COLOR_BG);
  drawButtonBubble(BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H, "BACK",
                   pressedButton == BTN_BACK, selectedButton == BTN_BACK, -1);
  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.setTextFont(4);
  tft.drawCentreString("Timer", 120, 37, 4);

  drawGradientControl(TIMER_DISPLAY_X, TIMER_DISPLAY_Y, TIMER_DISPLAY_W, TIMER_DISPLAY_H, 12, COLOR_PANEL, COLOR_ACCENT);

  drawButtonBubble(TIMER_COUNT_DOWN_X, TIMER_MODE_Y, TIMER_MODE_W, TIMER_MODE_H, "COUNT DN",
                   pressedButton == BTN_TIMER_COUNT_DOWN,
                   timerMode == TIMER_MODE_COUNT_DOWN || selectedButton == BTN_TIMER_COUNT_DOWN,
                   -1);
  drawButtonBubble(TIMER_COUNT_UP_X, TIMER_MODE_Y, TIMER_MODE_W, TIMER_MODE_H, "COUNT UP",
                   pressedButton == BTN_TIMER_COUNT_UP,
                   timerMode == TIMER_MODE_COUNT_UP || selectedButton == BTN_TIMER_COUNT_UP,
                   -1);
  drawButtonBubble(TIMER_START_X, TIMER_ACTION_Y, TIMER_ACTION_W, TIMER_ACTION_H, timerRunning ? "PAUSE" : "START",
                   pressedButton == BTN_TIMER_START_PAUSE,
                   selectedButton == BTN_TIMER_START_PAUSE,
                   -1);
  drawButtonBubble(TIMER_RESET_X, TIMER_ACTION_Y, TIMER_ACTION_W, TIMER_ACTION_H, "RESET",
                   pressedButton == BTN_TIMER_RESET,
                   selectedButton == BTN_TIMER_RESET,
                   -1);

  lastTimerDisplayText[0] = '\0';
  lastTimerDrawMode = timerMode;
  lastTimerDrawRunning = timerRunning;
  lastTimerAlarmActive = false;
  lastTimerAlarmVisible = true;
  timerNeedsRedraw = true;
}

void drawTimerDynamic() {
  unsigned long now = millis();
  bool alarmActive = isTimerElapsed(now);
  bool alarmVisible = !alarmActive || ((now / TIMER_ALARM_BLINK_MS) % 2 == 0);
  char displayText[12];
  formatTimerDisplay(getTimerDisplayMs(now), displayText, sizeof(displayText));

  bool buttonsChanged = (lastTimerDrawMode != timerMode || lastTimerDrawRunning != timerRunning);
  if (buttonsChanged) {
    tft.fillRect(TIMER_COUNT_DOWN_X - 4, TIMER_MODE_Y - 4, 216, 78, COLOR_BG);
    drawButtonBubble(TIMER_COUNT_DOWN_X, TIMER_MODE_Y, TIMER_MODE_W, TIMER_MODE_H, "COUNT DN",
                     false, timerMode == TIMER_MODE_COUNT_DOWN, -1);
    drawButtonBubble(TIMER_COUNT_UP_X, TIMER_MODE_Y, TIMER_MODE_W, TIMER_MODE_H, "COUNT UP",
                     false, timerMode == TIMER_MODE_COUNT_UP, -1);
    drawButtonBubble(TIMER_START_X, TIMER_ACTION_Y, TIMER_ACTION_W, TIMER_ACTION_H, timerRunning ? "PAUSE" : "START",
                     false, selectedButton == BTN_TIMER_START_PAUSE, -1);
    drawButtonBubble(TIMER_RESET_X, TIMER_ACTION_Y, TIMER_ACTION_W, TIMER_ACTION_H, "RESET",
                     false, selectedButton == BTN_TIMER_RESET, -1);
    lastTimerDrawMode = timerMode;
    lastTimerDrawRunning = timerRunning;
  }

  if (strcmp(displayText, lastTimerDisplayText) != 0 ||
      alarmActive != lastTimerAlarmActive ||
      alarmVisible != lastTimerAlarmVisible ||
      timerNeedsRedraw) {
    tft.fillRoundRect(TIMER_DISPLAY_X + 4, TIMER_DISPLAY_Y + 4,
                      TIMER_DISPLAY_W - 8, TIMER_DISPLAY_H - 8, 10, COLOR_PANEL);
    if (alarmVisible) {
      tft.setTextColor(alarmActive ? TFT_RED : COLOR_ACCENT_HI, COLOR_PANEL);
      tft.drawCentreString(displayText, 120, TIMER_DISPLAY_Y + 10, 7);
    }

    tft.setTextFont(2);
    tft.setTextColor(fadeColor(COLOR_TEXT, 0.72f), COLOR_PANEL);
    char targetText[18];
    formatTimerTargetBuffer(targetText, sizeof(targetText));
    tft.drawCentreString(targetText, 120, TIMER_DISPLAY_Y + 62, 2);

    strncpy(lastTimerDisplayText, displayText, sizeof(lastTimerDisplayText) - 1);
    lastTimerDisplayText[sizeof(lastTimerDisplayText) - 1] = '\0';
    lastTimerAlarmActive = alarmActive;
    lastTimerAlarmVisible = alarmVisible;
  }

  timerNeedsRedraw = false;
}

void handleTimerTouch(int x, int y) {
  if (isInside(x, y, BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H)) {
    queueScreenButton(BTN_BACK, SCREEN_MAIN);
    return;
  }

  if (waitingForRelease) return;

  if (isInside(x, y, TIMER_DISPLAY_X, TIMER_DISPLAY_Y, TIMER_DISPLAY_W, TIMER_DISPLAY_H)) {
    openMixNumpad(NUMPAD_TARGET_TIMER_VALUE);
    waitingForRelease = true;
    return;
  }

  if (isInside(x, y, TIMER_COUNT_DOWN_X, TIMER_MODE_Y, TIMER_MODE_W, TIMER_MODE_H)) {
    pressedButton = BTN_TIMER_COUNT_DOWN;
    selectedButton = BTN_TIMER_COUNT_DOWN;
    timerMode = TIMER_MODE_COUNT_DOWN;
    resetTimerRuntime();
    waitingForRelease = true;
    uiNeedsRedraw = true;
    return;
  }

  if (isInside(x, y, TIMER_COUNT_UP_X, TIMER_MODE_Y, TIMER_MODE_W, TIMER_MODE_H)) {
    pressedButton = BTN_TIMER_COUNT_UP;
    selectedButton = BTN_TIMER_COUNT_UP;
    timerMode = TIMER_MODE_COUNT_UP;
    resetTimerRuntime();
    waitingForRelease = true;
    uiNeedsRedraw = true;
    return;
  }

  if (isInside(x, y, TIMER_START_X, TIMER_ACTION_Y, TIMER_ACTION_W, TIMER_ACTION_H)) {
    pressedButton = BTN_TIMER_START_PAUSE;
    selectedButton = BTN_TIMER_START_PAUSE;
    if (timerRunning) {
      timerAccumulatedMs = getTimerElapsedMs(millis());
      timerRunning = false;
    } else {
      timerRunStartedAt = millis();
      timerRunning = true;
    }
    timerNeedsRedraw = true;
    waitingForRelease = true;
    uiNeedsRedraw = true;
    return;
  }

  if (isInside(x, y, TIMER_RESET_X, TIMER_ACTION_Y, TIMER_ACTION_W, TIMER_ACTION_H)) {
    pressedButton = BTN_TIMER_RESET;
    selectedButton = BTN_TIMER_RESET;
    timerInputBuffer = "";
    timerTargetSeconds = 0;
    resetTimerRuntime();
    waitingForRelease = true;
    uiNeedsRedraw = true;
    return;
  }

}

void drawSpeakerGlyph(int x, int y, bool muted, uint16_t color) {
  tft.fillRect(x, y + 7, 5, 8, color);
  tft.fillTriangle(x + 5, y + 7, x + 12, y + 2, x + 12, y + 20, color);

  if (muted) {
    tft.drawLine(x + 15, y + 5, x + 22, y + 17, TFT_RED);
    tft.drawLine(x + 22, y + 5, x + 15, y + 17, TFT_RED);
  } else {
    tft.drawLine(x + 16, y + 7, x + 19, y + 10, color);
    tft.drawLine(x + 19, y + 10, x + 16, y + 14, color);
    tft.drawLine(x + 19, y + 4, x + 23, y + 10, color);
    tft.drawLine(x + 23, y + 10, x + 19, y + 17, color);
  }
}

void drawMainVolumeButton() {
#if AUDIO_ENABLED
  uint16_t fill = audioVolumePanelVisible ? COLOR_ACCENT : COLOR_PANEL;
  uint16_t border = audioUserMuted ? TFT_RED : COLOR_ACCENT_HI;
  tft.fillRoundRect(VOLUME_BTN_X, VOLUME_BTN_Y, VOLUME_BTN_W, VOLUME_BTN_H, 6, fill);
  tft.drawRoundRect(VOLUME_BTN_X, VOLUME_BTN_Y, VOLUME_BTN_W, VOLUME_BTN_H, 6, border);
  drawSpeakerGlyph(VOLUME_BTN_X + 8, VOLUME_BTN_Y + 4,
                   audioUserMuted || audioUserVolumePercent == 0,
                   audioVolumePanelVisible ? COLOR_BG : COLOR_TEXT);
#endif
}

void drawMainVolumePanel() {
#if AUDIO_ENABLED
  if (!audioVolumePanelVisible) return;

  tft.fillRoundRect(VOLUME_PANEL_X, VOLUME_PANEL_Y,
                    VOLUME_PANEL_W, VOLUME_PANEL_H, 7, COLOR_PANEL);
  tft.drawRoundRect(VOLUME_PANEL_X, VOLUME_PANEL_Y,
                    VOLUME_PANEL_W, VOLUME_PANEL_H, 7, COLOR_ACCENT);

  uint16_t muteFill = audioUserMuted ? TFT_RED : COLOR_BG;
  tft.fillRoundRect(VOLUME_MUTE_X, VOLUME_MUTE_Y,
                    VOLUME_MUTE_W, VOLUME_MUTE_H, 5, muteFill);
  tft.drawRoundRect(VOLUME_MUTE_X, VOLUME_MUTE_Y,
                    VOLUME_MUTE_W, VOLUME_MUTE_H, 5,
                    audioUserMuted ? TFT_ORANGE : COLOR_ACCENT_HI);
  drawSpeakerGlyph(VOLUME_MUTE_X + 5, VOLUME_MUTE_Y + 6,
                   audioUserMuted, audioUserMuted ? COLOR_TEXT : COLOR_ACCENT_HI);

  tft.fillRoundRect(VOLUME_SLIDER_X, VOLUME_SLIDER_Y,
                    VOLUME_SLIDER_W, VOLUME_SLIDER_H, 6, TFT_DARKGREY);
  int fillW = map(audioUserVolumePercent, 0, 100, 0, VOLUME_SLIDER_W);
  if (fillW > 0) {
    tft.fillRoundRect(VOLUME_SLIDER_X, VOLUME_SLIDER_Y,
                      fillW, VOLUME_SLIDER_H, 6,
                      audioUserMuted ? TFT_DARKGREY : COLOR_ACCENT);
  }
  int knobX = VOLUME_SLIDER_X + map(audioUserVolumePercent, 0, 100, 0, VOLUME_SLIDER_W - 1);
  tft.fillCircle(knobX, VOLUME_SLIDER_Y + (VOLUME_SLIDER_H / 2), 7, COLOR_TEXT);
  tft.drawCircle(knobX, VOLUME_SLIDER_Y + (VOLUME_SLIDER_H / 2), 7, COLOR_ACCENT_HI);

  char volumeText[8];
  snprintf(volumeText, sizeof(volumeText), "%u%%", audioUserVolumePercent);
  tft.setTextFont(1);
  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  tft.drawRightString(volumeText, VOLUME_SLIDER_X + VOLUME_SLIDER_W,
                      VOLUME_SLIDER_Y + 17, 1);
#endif
}

bool handleMainVolumeTouch(int x, int y) {
#if AUDIO_ENABLED
  if (currentScreen != SCREEN_MAIN) return false;

  if (isInside(x, y, VOLUME_BTN_X, VOLUME_BTN_Y, VOLUME_BTN_W, VOLUME_BTN_H)) {
    if (!touchActive) {
      audioVolumePanelVisible = !audioVolumePanelVisible;
      if (!audioVolumePanelVisible) fullRedraw = true;
      uiNeedsRedraw = true;
    }
    return true;
  }

  if (!audioVolumePanelVisible) return false;

  if (isInside(x, y, VOLUME_MUTE_X, VOLUME_MUTE_Y, VOLUME_MUTE_W, VOLUME_MUTE_H)) {
    if (!touchActive) {
      if (audioUserMuted || audioUserVolumePercent == 0) {
        audioUserMuted = false;
        if (audioUserVolumePercent == 0) {
          audioUserVolumePercent = (audioVolumeBeforeMutePercent > 0)
            ? audioVolumeBeforeMutePercent
            : 1;
        }
      } else {
        audioVolumeBeforeMutePercent = audioUserVolumePercent;
        audioUserMuted = true;
      }
      applyAudioUserMuteState();
      audioSettingsDirty = true;
      uiNeedsRedraw = true;
    }
    return true;
  }

  if (isInside(x, y,
               VOLUME_SLIDER_X - 8, VOLUME_SLIDER_Y - 14,
               VOLUME_SLIDER_W + 16, VOLUME_SLIDER_H + 28)) {
    int sliderX = constrain(x, VOLUME_SLIDER_X, VOLUME_SLIDER_X + VOLUME_SLIDER_W);
    uint8_t nextVolume = (uint8_t)map(sliderX,
                                     VOLUME_SLIDER_X,
                                     VOLUME_SLIDER_X + VOLUME_SLIDER_W,
                                     0, 100);
    if (nextVolume != audioUserVolumePercent || audioUserMuted) {
      if (nextVolume == 0 && audioUserVolumePercent > 0) {
        audioVolumeBeforeMutePercent = audioUserVolumePercent;
      } else if (nextVolume > 0) {
        audioVolumeBeforeMutePercent = nextVolume;
      }
      audioUserVolumePercent = nextVolume;
      audioUserMuted = (nextVolume == 0);
      applyAudioUserMuteState();
      audioSettingsDirty = true;
      uiNeedsRedraw = true;
    }
    return true;
  }

  if (!touchActive) {
    audioVolumePanelVisible = false;
    fullRedraw = true;
    uiNeedsRedraw = true;
  }
  return true;
#else
  (void)x;
  (void)y;
  return false;
#endif
}

// ==== STATIC MAIN SCREEN ====
void drawMainScreenStatic() {
  updateMainModelPanelBounds();

  int stickY = 50;
  int stickSize = 80;
  int padding = 10;

  int panelX = 10;
  int panelW = 220;
  int panelY = stickY - padding;
  int panelH = stickSize + (padding * 2);

  int modelY = panelY + panelH + 10;
  int modelBottom = MENU_BTN_Y - 10;
  int modelH = modelBottom - modelY;

  // panel background + border
  tft.fillRoundRect(panelX, modelY, panelW, modelH, 10, COLOR_STICK_PANEL);
  tft.drawRoundRect(panelX, modelY, panelW, modelH, 10, COLOR_ACCENT);
  tft.drawRoundRect(panelX+2, modelY+2, panelW-4, modelH-4, 10, TFT_DARKGREY);

  // divider
  tft.drawFastVLine(panelX + panelW / 2, modelY + 10, modelH - 20, COLOR_ACCENT);

  drawStickBase(20, stickY, stickSize);
  drawStickBase(140, stickY, stickSize);
  drawThreePosSwitchIndicator(120, stickY + 7, getThreePosSwitchPosition());

  drawButtonBubble(
    MENU_BTN_X, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H,
    "MENU",
    pressedButton == BTN_MENU,
    selectedButton == BTN_MENU,
    -1
  );
  drawMainVolumeButton();
}

void drawModelPanelSemiStatic() {
updateMainModelPanelBounds();
  mainOutputPanelNeedsRedraw = true;
  
  int stickY = 50;
  int stickSize = 80;
  int padding = 10;

  int panelX = 10;
  int panelW = 220;
  int panelY = stickY - padding;
  int panelH = stickSize + (padding * 2);

  int modelY = panelY + panelH + 10;
  int modelBottom = MENU_BTN_Y - 10;
  int modelH = modelBottom - modelY;

  int iconX = panelX + 10;
  int iconY = modelY + 10;
  int iconW = (panelW - 20) / 2;
  int iconH = modelH - 20;

  // clear ONLY icon area
  tft.fillRect(iconX - 2, iconY, iconW + 2, iconH, COLOR_STICK_PANEL);

  if (currentDrive == DRIVE_TANK) {
    drawLegacyTankIcon(iconX, iconY, iconW, iconH, COLOR_ACCENT);
  } 
  else if (currentDrive == DRIVE_X_DRONE) {
    int droneIconH = 0;
    int armX = 0;
    int armY = 0;
    int armW = 0;
    int armH = 0;
    getDronePanelBounds(iconX, iconY, iconW, iconH, droneIconH, armX, armY, armW, armH);
    drawQuadXIcon(iconX, iconY, iconW, droneIconH, COLOR_ACCENT);
    drawButtonBubble(armX, armY, armW, armH,
                     droneArmActive ? "DISARM" : "ARM",
                     pressedButton == BTN_DRONE_ARM,
                     dpadFocusVisible && selectedButton == BTN_DRONE_ARM,
                     -1);
  }
  else if (currentDrive == DRIVE_CAR) {
    drawCarIcon(iconX + (iconW / 2) - 36, iconY + (iconH / 2) - 36, 3, COLOR_ACCENT);
  }
  else if (currentDrive == DRIVE_OMNI) {
    drawOmniIcon(iconX + 10, iconY + 8, iconW - 20, iconH - 16, COLOR_ACCENT);
  }

  drawRightPanel(panelX + panelW / 2, modelY, panelW / 2, modelH);
}

// ==== DYNAMIC MAIN SCREEN ====
void drawMainScreenDynamic() {
updateMainModelPanelBounds();

int stickY = 50;
int stickSize = 80;
int padding = 10;

int panelX = 10;
int panelW = 220;
int panelY = stickY - padding;
int panelH = stickSize + (padding * 2);

int modelY = panelY + panelH + 10;
int modelBottom = MENU_BTN_Y - 10;
int modelH = modelBottom - modelY;

int range = 25;  // max stick travel (tweak this)

// Show physical stick position on the gimbals, not reversed/mixed action output.
int lx = inputChannels[3] * range;
int ly = -inputChannels[2] * range;

int rx = inputChannels[0] * range;
int ry = -inputChannels[1] * range;

static int lastLX = 0, lastLY = 0;
static int lastRX = 0, lastRY = 0;
static int lastThreePos = 99;
static int lastLatency = -1;
static float lastTelemetryVoltage = -1;
static String lastModel = "";
static ProtocolType lastRightPanelProtocol = PROTOCOL_ESPNOW;
static bool lastRightPanelElrsLink = false;
static uint8_t lastRightPanelLq = 255;
static int8_t lastRightPanelSnr = 127;
static uint8_t lastRightPanelRssi1 = 255;
static uint8_t lastRightPanelRssi2 = 255;

ProtocolType rightPanelProtocol = getModelProtocol(activeModel);
bool rightPanelChanged =
    currentModelName != lastModel ||
    rightPanelProtocol != lastRightPanelProtocol ||
    espNowLatency != lastLatency ||
    abs(telemetryVoltage - lastTelemetryVoltage) > 0.01;

if (rightPanelProtocol == PROTOCOL_ELRS) {
  rightPanelChanged = rightPanelChanged ||
    elrsLinkActive != lastRightPanelElrsLink ||
    elrsUplinkLq != lastRightPanelLq ||
    elrsUplinkSnr != lastRightPanelSnr ||
    elrsUplinkRssi1 != lastRightPanelRssi1 ||
    elrsUplinkRssi2 != lastRightPanelRssi2;
}

if (rightPanelChanged) {

  int rightX = panelX + panelW / 2;
  int rightW = panelW / 2;

  drawRightPanelDynamic(rightX, modelY, rightW, modelH);

  lastLatency = espNowLatency;
  lastTelemetryVoltage = telemetryVoltage;
  lastModel = currentModelName;
  lastRightPanelProtocol = rightPanelProtocol;
  lastRightPanelElrsLink = elrsLinkActive;
  lastRightPanelLq = elrsUplinkLq;
  lastRightPanelSnr = elrsUplinkSnr;
  lastRightPanelRssi1 = elrsUplinkRssi1;
  lastRightPanelRssi2 = elrsUplinkRssi2;
}

  // ===== ERASE OLD KNOBS =====
  if (lx != lastLX || ly != lastLY) {
  drawStickBase(20, stickY, stickSize);
}

if (rx != lastRX || ry != lastRY) {
  drawStickBase(140, stickY, stickSize);
}

  drawStickKnob(20, stickY, stickSize, lx, ly);
  drawStickKnob(140, stickY, stickSize, rx, ry);

int threePos = getThreePosSwitchPosition();
if (threePos != lastThreePos || fullRedraw) {
  drawThreePosSwitchIndicator(120, stickY + 7, threePos);
  lastThreePos = threePos;
}

lastLX = lx;
lastLY = ly;
lastRX = rx;
lastRY = ry;

// ===== REDRAW ICON =====
int iconX = panelX + 10;
int iconY = modelY + 10;
int iconW = (panelW - 20) / 2;
int iconH = modelH - 20;

float protocolChannels[CHANNEL_COUNT];
buildProtocolOutputChannels(protocolChannels);

if (currentDrive == DRIVE_TANK) {

static float lastLeft = 0;
static float lastRight = 0;
static float lastTurret = 0;
static float lastWeapon = 0;
static bool lastShowTurret = true;
static bool lastShowWeapon = true;
uint8_t leftChannel = getModelTankLeftEscChannel(activeModel);
uint8_t rightChannel = getModelTankRightEscChannel(activeModel);
if (leftChannel >= CHANNEL_COUNT || rightChannel >= CHANNEL_COUNT || leftChannel == rightChannel) {
  leftChannel = 1;
  rightChannel = 0;
}
float tankLeftOutput = getVehicleDisplayOutput(protocolChannels, leftChannel);
float tankRightOutput = getVehicleDisplayOutput(protocolChannels, rightChannel);
bool tankCh3IsDrive = (leftChannel == 2 || rightChannel == 2);
bool tankCh4IsDrive = (leftChannel == 3 || rightChannel == 3);
bool tankShowTurretArc = !tankCh4IsDrive;
bool tankShowWeaponBar = (getModelTankMode(activeModel) == TANK_MODE_RIGHT_STICK) && !tankCh3IsDrive;
float tankTurretOutput = tankShowTurretArc
  ? getVehicleDisplayOutput(protocolChannels, 3)  // CH4
  : 0.0f;
float tankWeaponOutput = tankShowWeaponBar
  ? getVehicleDisplayOutput(protocolChannels, 2)  // CH3
  : -1.0f;

if (mainOutputPanelNeedsRedraw ||
    abs(tankLeftOutput - lastLeft) > 0.01 ||
    abs(tankRightOutput - lastRight) > 0.01 ||
    abs(tankTurretOutput - lastTurret) > 0.01 ||
    abs(tankWeaponOutput - lastWeapon) > 0.01 ||
    tankShowTurretArc != lastShowTurret ||
    tankShowWeaponBar != lastShowWeapon) {

  tft.fillRect(iconX - 2, iconY, iconW + 4, iconH, COLOR_STICK_PANEL);

  // ✅ CLEAR FIRST

  // ✅ THEN redraw icon (restores tracks)
  drawLegacyTankIcon(iconX, iconY, iconW, iconH, COLOR_ACCENT);

  // ✅ THEN bars on top
  drawTankBars(iconX, iconY, iconW, iconH, tankLeftOutput, tankRightOutput);
  drawTankAuxBars(iconX, iconY, iconW, iconH, tankTurretOutput, tankWeaponOutput);

  lastLeft = tankLeftOutput;
  lastRight = tankRightOutput;
  lastTurret = tankTurretOutput;
  lastWeapon = tankWeaponOutput;
  lastShowTurret = tankShowTurretArc;
  lastShowWeapon = tankShowWeaponBar;
  mainOutputPanelNeedsRedraw = false;
}
}
else if (currentDrive == DRIVE_CAR) {

static float lastSteering = 0.0f;
static float lastThrottle = 0.0f;
float carSteeringOutput = getVehicleDisplayOutput(protocolChannels, 0);
float carThrottleOutput = getVehicleDisplayOutput(protocolChannels, 1);

if (mainOutputPanelNeedsRedraw ||
    abs(carSteeringOutput - lastSteering) > 0.01 ||
    abs(carThrottleOutput - lastThrottle) > 0.01) {

  tft.fillRect(iconX - 2, iconY, iconW + 2, iconH, COLOR_STICK_PANEL);
  drawCarIcon(iconX + (iconW / 2) - 36, iconY + (iconH / 2) - 36, 3, COLOR_ACCENT);
  drawCarBars(iconX, iconY, iconW, iconH, carSteeringOutput, carThrottleOutput);

  lastSteering = carSteeringOutput;
  lastThrottle = carThrottleOutput;
  mainOutputPanelNeedsRedraw = false;
}
}
else if (currentDrive == DRIVE_OMNI) {

static float lastOmniArc = 0.0f;
static float lastOmniX = 0.0f;
static float lastOmniY = 0.0f;
float omniArcOutput = getVehicleDisplayOutput(protocolChannels, 3);
float omniXOutput = getVehicleDisplayOutput(protocolChannels, 0);
float omniYOutput = getVehicleDisplayOutput(protocolChannels, 1);

if (mainOutputPanelNeedsRedraw ||
    abs(omniArcOutput - lastOmniArc) > 0.01 ||
    abs(omniXOutput - lastOmniX) > 0.01 ||
    abs(omniYOutput - lastOmniY) > 0.01) {

  tft.fillRect(iconX - 2, iconY, iconW + 2, iconH, COLOR_STICK_PANEL);
  drawOmniIcon(iconX + 10, iconY + 8, iconW - 20, iconH - 16, COLOR_ACCENT);
  drawOmniBars(iconX + 10, iconY + 8, iconW - 20, iconH - 16, omniArcOutput, omniXOutput, omniYOutput);

  lastOmniArc = omniArcOutput;
  lastOmniX = omniXOutput;
  lastOmniY = omniYOutput;
  mainOutputPanelNeedsRedraw = false;
}
}
else if (currentDrive == DRIVE_X_DRONE) {

static float lastDroneYaw = 0.0f;
static float lastQuad1 = -1.0f;
static float lastQuad2 = -1.0f;
static float lastQuad3 = -1.0f;
static float lastQuad4 = -1.0f;
static bool lastDroneArmActive = false;
static ButtonID lastDroneArmPressed = BTN_NONE;
static ButtonID lastDroneArmSelected = BTN_NONE;

int droneIconH = 0;
int armX = 0;
int armY = 0;
int armW = 0;
int armH = 0;
getDronePanelBounds(iconX, iconY, iconW, iconH, droneIconH, armX, armY, armW, armH);

// Drone visualization shows the command being sent to a flight controller,
// not literal motor outputs. Use processed stick values before drive mixing.
float droneYaw = driveSourceChannels[DRONE_YAW_CHANNEL];       // Left stick X
float droneThrottle = (driveSourceChannels[DRONE_THROTTLE_CHANNEL] + 1.0f) * 0.5f;  // Left stick Y
float droneRoll = driveSourceChannels[DRONE_ROLL_CHANNEL];     // Right stick X
float dronePitch = driveSourceChannels[DRONE_PITCH_CHANNEL];   // Right stick Y
const float cyclicScale = 0.5f;

// Motor order: 1 front-left, 2 front-right, 3 rear-right, 4 rear-left.
float quad1Output = droneThrottle - (dronePitch * cyclicScale) + (droneRoll * cyclicScale);
float quad2Output = droneThrottle - (dronePitch * cyclicScale) - (droneRoll * cyclicScale);
float quad3Output = droneThrottle + (dronePitch * cyclicScale) - (droneRoll * cyclicScale);
float quad4Output = droneThrottle + (dronePitch * cyclicScale) + (droneRoll * cyclicScale);

quad1Output = constrain(quad1Output, 0.0f, 1.0f);
quad2Output = constrain(quad2Output, 0.0f, 1.0f);
quad3Output = constrain(quad3Output, 0.0f, 1.0f);
quad4Output = constrain(quad4Output, 0.0f, 1.0f);

if (mainOutputPanelNeedsRedraw ||
    abs(droneYaw - lastDroneYaw) > 0.01 ||
    abs(quad1Output - lastQuad1) > 0.01 ||
    abs(quad2Output - lastQuad2) > 0.01 ||
    abs(quad3Output - lastQuad3) > 0.01 ||
    abs(quad4Output - lastQuad4) > 0.01 ||
    droneArmActive != lastDroneArmActive ||
    pressedButton != lastDroneArmPressed ||
    selectedButton != lastDroneArmSelected) {

  tft.fillRect(iconX - 2, iconY, iconW + 2, iconH, COLOR_STICK_PANEL);
  drawQuadXIcon(iconX, iconY, iconW, droneIconH, COLOR_ACCENT);
  drawDroneCommandBars(iconX, iconY, iconW, droneIconH, droneYaw, quad1Output, quad2Output, quad3Output, quad4Output);
  drawButtonBubble(armX, armY, armW, armH,
                   droneArmActive ? "DISARM" : "ARM",
                   pressedButton == BTN_DRONE_ARM,
                   dpadFocusVisible && selectedButton == BTN_DRONE_ARM,
                   -1);

  lastDroneYaw = droneYaw;
  lastQuad1 = quad1Output;
  lastQuad2 = quad2Output;
  lastQuad3 = quad3Output;
  lastQuad4 = quad4Output;
  lastDroneArmActive = droneArmActive;
  lastDroneArmPressed = pressedButton;
  lastDroneArmSelected = selectedButton;
  mainOutputPanelNeedsRedraw = false;
}
}

  // ===== MENU BUTTON =====
  static ButtonID lastMenuPressed = BTN_NONE;
  static ButtonID lastMenuSelected = BTN_NONE;

  if (pressedButton != lastMenuPressed || selectedButton != lastMenuSelected) {
    drawButtonBubble(
      MENU_BTN_X, MENU_BTN_Y, MENU_BTN_W, MENU_BTN_H,
      "MENU",
      pressedButton == BTN_MENU,
      dpadFocusVisible && selectedButton == BTN_MENU,
      -1
    );

    lastMenuPressed = pressedButton;
    lastMenuSelected = selectedButton;
  }
}

void drawLegacyTankIcon(int x, int y, int w, int h, uint16_t iconColor) {

  int cx = x + w / 2;

  int bodyW = w / 2;
  int bodyH = bodyW + 14;

  int bodyX = cx - bodyW / 2;
  int bodyY = y + (h - bodyH) / 2;

  tft.drawRect(bodyX, bodyY, bodyW, bodyH, iconColor);

  int ty = bodyY + bodyH / 2;
  tft.drawCircle(cx, ty, 6, iconColor);

  tft.drawLine(cx, ty - 6, cx, bodyY - 6, iconColor);

  int trackOffset = bodyW / 2 + 8;

  for (int i = 0; i < bodyH; i += 8) {
    tft.drawRect(cx - trackOffset, bodyY + i, 6, 4, iconColor);
    tft.drawRect(cx + trackOffset - 6, bodyY + i, 6, 4, iconColor);
  }
}

void drawLegacyQuadXIcon(int x, int y, int w, int h, uint16_t iconColor) {

  int cx = x + w / 2;
  int cy = y + h / 2;

  int armX = w / 2 - 10;
  int armY = h / 3;

  tft.drawLine(cx, cy, cx - armX, cy - armY, iconColor);
  tft.drawLine(cx, cy, cx + armX, cy - armY, iconColor);
  tft.drawLine(cx, cy, cx - armX, cy + armY, iconColor);
  tft.drawLine(cx, cy, cx + armX, cy + armY, iconColor);

  tft.drawCircle(cx - armX, cy - armY, 5, iconColor);
  tft.drawCircle(cx + armX, cy - armY, 5, iconColor);
  tft.drawCircle(cx - armX, cy + armY, 5, iconColor);
  tft.drawCircle(cx + armX, cy + armY, 5, iconColor);

  tft.drawCircle(cx, cy, 4, iconColor);
}

void drawLegacyCarIcon(int x, int y, int s, uint16_t iconColor) {

  int ax = x + 4*s;
  int baseY = y + 8*s;
  int tipY  = baseY - 6*s;

  drawThickRoundRect(x + 2*s, y + 8*s, 20*s, 10*s, 3*s, iconColor);

  drawThickLine(x + 6*s, y + 8*s, x + 10*s, y + 4*s, iconColor);
  drawThickLine(x + 10*s, y + 4*s, x + 16*s, y + 4*s, iconColor);
  drawThickLine(x + 16*s, y + 4*s, x + 20*s, y + 8*s, iconColor);

  drawThickCircle(x + 6*s, y + 20*s, 3*s, iconColor);
  drawThickCircle(x + 18*s, y + 20*s, 3*s, iconColor);

  drawThickLine(ax, baseY, ax, tipY, iconColor);
  drawThickCircle(ax, tipY, 2*s, iconColor);
}

void drawTankIcon(int x, int y, int w, int h, uint16_t iconColor) {
  drawRgb565IconTinted(x, y, w, h, icon_TANK, ICON_TANK_W, ICON_TANK_H, iconColor);
}

void drawQuadXIcon(int x, int y, int w, int h, uint16_t iconColor) {
  drawRgb565IconTinted(x, y, w, h, icon_X_DRONE, ICON_X_DRONE_W, ICON_X_DRONE_H, iconColor);
}

void drawTankBars(int x, int y, int w, int h, float left, float right) {

  int barW = 6;

  // ===== LEFT HALF OF PANEL =====
  int cx = x + w / 2;

  int bodyW = w / 2;
  int trackOffset = bodyW / 2 + 8;

  int leftX  = cx - trackOffset;
  int rightX = cx + trackOffset - barW;

  // ===== MATCH ICON VERTICAL AREA =====
  int top = y + 10;
  int height = h - 20;

  drawCenteredBar(leftX,  top, barW, height, left);
  drawCenteredBar(rightX, top, barW, height, right);
}

void drawTankAuxBars(int x, int y, int w, int h, float turret, float weapon) {
  int cx = x + w / 2;
  int bodyW = w / 2;
  int bodyH = bodyW + 14;
  int bodyY = y + (h - bodyH) / 2;
  int turretCy = bodyY + bodyH / 2;

  turret = constrain(turret, -1.0f, 1.0f);
  weapon = constrain(weapon, -1.0f, 1.0f);

  int turretSpan = roundf(abs(turret) * 90.0f);
  if (turretSpan > 2) {
    int startDeg = (turret < 0.0f) ? (-90 - turretSpan) : -90;
    int endDeg = (turret < 0.0f) ? -90 : (-90 + turretSpan);
    uint16_t turretColor = throttleColor(abs(turret) * 2.0f - 1.0f);
    drawThickArc(cx, turretCy, 9, 12, startDeg, endDeg, turretColor);
  }

  int barX = x + 12;
  int barY = min(y + h - 8, bodyY + bodyH + 8);
  int barW = w - 24;
  int barH = 5;
  float weaponLevel = constrain((weapon + 1.0f) * 0.5f, 0.0f, 1.0f);
  int fillW = roundf(weaponLevel * barW);

  for (int i = 0; i < fillW; i++) {
    float gradient = (barW <= 1) ? 0.0f : (float)i / (barW - 1);
    uint16_t c = throttleColor(gradient * 2.0f - 1.0f);
    tft.drawFastVLine(barX + i, barY, barH, c);
  }
}

float getCarSteeringOutput() {
  if (getModelTankMode(activeModel) == TANK_MODE_DUAL_STICK) {
    return outputChannels[3];  // CH4 = left X
  }

  return outputChannels[0];    // CH1 = right X
}

float getCarThrottleOutput() {
  return outputChannels[1];    // CH2 = right Y
}

void drawCarBars(int x, int y, int w, int h, float steering, float throttle) {
  int carX = x + (w / 2) - 36;
  int carY = y + (h / 2) - 36;
  int carW = 72;
  int carH = 72;

  int vertBarX = carX - 12;
  int vertBarY = carY + 8;
  int vertBarW = 6;
  int vertBarH = carH - 16;

  int horizBarX = carX + 10;
  int horizBarY = carY + carH + 2;
  int horizBarW = carW - 20;
  int horizBarH = 6;

  int vertCenterY = vertBarY + (vertBarH / 2);
  int vertMaxLen = max(1, (vertBarH / 2) - 2);
  int vertLen = throttle * vertMaxLen;
  if (vertLen > 0) {
    for (int i = 0; i < vertLen; i++) {
      int drawY = vertCenterY - i;
      if (drawY >= vertBarY + 2) {
        uint16_t c = tankThrottleColor((float)i / vertMaxLen * throttle);
        tft.drawFastHLine(vertBarX, drawY, vertBarW, c);
      }
    }
  } else {
    for (int i = 0; i < -vertLen; i++) {
      int drawY = vertCenterY + i;
      if (drawY <= vertBarY + vertBarH - 2) {
        uint16_t c = tankThrottleColor((float)i / vertMaxLen * throttle);
        tft.drawFastHLine(vertBarX, drawY, vertBarW, c);
      }
    }
  }

  int centerX = horizBarX + (horizBarW / 2);
  int maxLen = horizBarW / 2;
  int len = steering * maxLen;
  if (len > 0) {
    for (int i = 0; i < len; i++) {
      int drawX = centerX + i;
      if (drawX <= horizBarX + horizBarW) {
        uint16_t c = tankThrottleColor((float)i / maxLen * steering);
        tft.drawFastVLine(drawX, horizBarY, horizBarH, c);
      }
    }
  } else {
    for (int i = 0; i < -len; i++) {
      int drawX = centerX - i;
      if (drawX >= horizBarX) {
        uint16_t c = tankThrottleColor((float)i / maxLen * steering);
        tft.drawFastVLine(drawX, horizBarY, horizBarH, c);
      }
    }
  }
}

void drawOmniBars(int x, int y, int w, int h, float ch4, float ch1, float ch2) {
  int cx = x + w / 2;
  int cy = y + h / 2;
  int innerR = min(w, h) / 4;
  int outerR = innerR + 4;

  int arcHalfSpanDeg = 110;
  int arcCenterDeg = -90;
  int arcOffsetDeg = (int)roundf(ch4 * arcHalfSpanDeg);
  int arcStart = min(arcCenterDeg, arcCenterDeg + arcOffsetDeg);
  int arcEnd = max(arcCenterDeg, arcCenterDeg + arcOffsetDeg);

  if (arcOffsetDeg != 0) {
    for (int angle = arcStart; angle <= arcEnd; angle += 2) {
      float radians = angle * 0.0174532925f;
      float intensity = (arcHalfSpanDeg == 0) ? 0.0f : fabs((float)(angle - arcCenterDeg) / arcHalfSpanDeg);
      uint16_t c = tankThrottleColor((ch4 >= 0.0f ? 1.0f : -1.0f) * intensity);

      for (int radius = innerR + 1; radius < outerR; radius++) {
        int px = cx + (int)roundf(cosf(radians) * radius);
        int py = cy + (int)roundf(sinf(radians) * radius);
        tft.drawPixel(px, py, c);
      }
    }
  }

  int crossRange = max(6, innerR - 6);
  int markerX = cx + (int)roundf(ch1 * crossRange);
  int markerY = cy - (int)roundf(ch2 * crossRange);

  tft.drawFastHLine(cx - crossRange, cy, crossRange * 2, fadeColor(COLOR_ACCENT, 0.45f));
  tft.drawFastVLine(cx, cy - crossRange, crossRange * 2, fadeColor(COLOR_ACCENT, 0.45f));

  tft.drawFastHLine(markerX - 5, markerY, 11, COLOR_ACCENT_HI);
  tft.drawFastVLine(markerX, markerY - 5, 11, COLOR_ACCENT_HI);
  tft.drawPixel(markerX, markerY, COLOR_TEXT);
}

void drawThickArc(int cx, int cy, int innerR, int outerR, int startDeg, int endDeg, uint16_t color) {
  if (endDeg < startDeg) {
    int temp = startDeg;
    startDeg = endDeg;
    endDeg = temp;
  }

  for (int angle = startDeg; angle <= endDeg; angle += 2) {
    float radians = angle * 0.0174532925f;
    float arcCos = cosf(radians);
    float arcSin = sinf(radians);
    for (int radius = innerR; radius <= outerR; radius++) {
      int px = cx + (int)roundf(arcCos * radius);
      int py = cy + (int)roundf(arcSin * radius);
      tft.drawPixel(px, py, color);
    }
  }
}

void restoreQuadXIconClip(int iconX, int iconY, int iconW, int iconH, int clipX, int clipY, int clipW, int clipH) {
  int left = max(iconX, clipX);
  int top = max(iconY, clipY);
  int right = min(iconX + iconW, clipX + clipW);
  int bottom = min(iconY + iconH, clipY + clipH);
  if (left >= right || top >= bottom) return;

  for (int py = top; py < bottom; py++) {
    int dy = py - iconY;
    int sy = (dy * ICON_X_DRONE_H) / iconH;
    int srcRow = sy * ICON_X_DRONE_W;

    for (int px = left; px < right; px++) {
      int dx = px - iconX;
      int sx = (dx * ICON_X_DRONE_W) / iconW;
      uint16_t srcColor = icon_X_DRONE[srcRow + sx];
      if (srcColor == ICON_TRANSPARENT_RGB565) continue;
      tft.drawPixel(px, py, tintRgb565Pixel(srcColor, COLOR_ACCENT));
    }
  }
}

void clearDroneIndicatorRegion(int iconX, int iconY, int iconW, int iconH, int cx, int cy, int radius) {
  int pad = 5;
  int clearX = cx - radius - pad;
  int clearY = cy - radius - pad;
  int clearW = (radius + pad) * 2 + 1;
  int clearH = clearW;

  tft.fillRect(clearX, clearY, clearW, clearH, COLOR_STICK_PANEL);
  restoreQuadXIconClip(iconX, iconY, iconW, iconH, clearX, clearY, clearW, clearH);
}

void drawDroneThrottleArc(int cx, int cy, int radius, float value, bool topHalf) {
  value = constrain(value, 0.0f, 1.0f);
  if (value < 0.03f) {
    return;
  }

  int innerR = max(2, radius - 3);
  int outerR = radius;

  int arcStart = topHalf ? 180 : 0;
  int activeEnd = arcStart + (int)roundf(value * 180.0f);
  if (activeEnd > arcStart + 4) {
    for (int angle = arcStart; angle <= activeEnd; angle += 2) {
      float radians = angle * 0.0174532925f;
      float arcCos = cosf(radians);
      float arcSin = sinf(radians);
      float progress = constrain((float)(angle - arcStart) / 180.0f, 0.0f, 1.0f);
      uint16_t c = throttleColor((progress * 2.0f) - 1.0f);

      for (int radiusStep = innerR; radiusStep <= outerR; radiusStep++) {
        int px = cx + (int)roundf(arcCos * radiusStep);
        int py = cy + (int)roundf(arcSin * radiusStep);
        tft.drawPixel(px, py, c);
      }
    }
  }
}

void drawDroneCommandBars(int x, int y, int w, int h, float yaw, float m1, float m2, float m3, float m4) {
  int cx = x + w / 2;
  int cy = y + h / 2;
  int motorR = max(6, min(w, h) / 9);

  int m1x = x + (w * 24) / 100;
  int m1y = y + (h * 16) / 100;
  int m2x = x + (w * 76) / 100;
  int m2y = y + (h * 16) / 100;
  int m3x = x + (w * 76) / 100;
  int m3y = y + (h * 84) / 100;
  int m4x = x + (w * 24) / 100;
  int m4y = y + (h * 84) / 100;

  clearDroneIndicatorRegion(x, y, w, h, m1x, m1y, motorR);
  clearDroneIndicatorRegion(x, y, w, h, m2x, m2y, motorR);
  clearDroneIndicatorRegion(x, y, w, h, m3x, m3y, motorR);
  clearDroneIndicatorRegion(x, y, w, h, m4x, m4y, motorR);

  drawDroneThrottleArc(m1x, m1y, motorR, m1, true);
  drawDroneThrottleArc(m2x, m2y, motorR, m2, true);
  drawDroneThrottleArc(m3x, m3y, motorR, m3, false);
  drawDroneThrottleArc(m4x, m4y, motorR, m4, false);

  int yawInnerR = max(8, min(w, h) / 5);
  int yawOuterR = yawInnerR + 4;
  int yawClearR = yawOuterR + 2;
  clearDroneIndicatorRegion(x, y, w, h, cx, cy, yawClearR);

  int yawSpan = (int)roundf(constrain(yaw, -1.0f, 1.0f) * 90.0f);
  if (yawSpan != 0) {
    int yawCenter = 270;
    int yawStart = min(yawCenter, yawCenter + yawSpan);
    int yawEnd = max(yawCenter, yawCenter + yawSpan);
    drawThickArc(cx, cy, yawInnerR, yawOuterR, yawStart, yawEnd, COLOR_ACCENT_HI);
  }
}

  // ==== RIGHT PANEL INFO ====
void drawRightPanel(int x, int y, int w, int h) {

  // ===== CLEAR ONLY RIGHT SIDE =====
  tft.fillRect(x + 2, y + 2, w - 4, h - 4, COLOR_STICK_PANEL);

  // ===== TEXT STYLING =====
  tft.setTextColor(COLOR_TEXT);
  tft.setTextFont(2);

  int lineY = y + 5;

  // ===== MODEL NAME =====
  tft.setCursor(x + 10, lineY);
  tft.print("Model:");

  ProtocolType protocol = getModelProtocol(activeModel);
  if (protocol == PROTOCOL_ELRS) {
    // ===== ELRS LINK =====
    lineY += 30;
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(x + 10, lineY);
    tft.print("Link:");

    // ===== ELRS RSSI =====
    lineY += 45;
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(x + 10, lineY);
    tft.print("RSSI:");
  } else {
    // ===== ESP-NOW LATENCY =====
    lineY += 30;

    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(x + 10, lineY);
    tft.print("Latency:");

    // ===== ESP-NOW VOLTAGE =====
    lineY += 30;

    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(x + 10, lineY);
    tft.print("Voltage:");
  }

  drawRightPanelDynamic(x, y, w, h);
}

void drawRightPanelDynamic(int x, int y, int w, int h) {
  int lineY = y + 5;
  int valueX = x + 14;
  int valueW = max(20, w - 24);
  char valueText[32];

  tft.setTextFont(2);

  tft.fillRect(valueX, lineY + 15, valueW, 16, COLOR_STICK_PANEL);
  tft.setTextColor(COLOR_ACCENT, COLOR_STICK_PANEL);
  tft.setCursor(valueX, lineY + 15);
  tft.print(currentModelName);

  ProtocolType protocol = getModelProtocol(activeModel);
  if (protocol == PROTOCOL_ELRS) {
    lineY += 30;
    tft.fillRect(valueX, lineY + 15, valueW, 31, COLOR_STICK_PANEL);
    tft.setTextColor(COLOR_ACCENT, COLOR_STICK_PANEL);
    if (elrsLinkActive) {
      snprintf(valueText, sizeof(valueText), "LQ %u", (unsigned int)elrsUplinkLq);
      tft.setCursor(valueX, lineY + 15);
      tft.print(valueText);
      snprintf(valueText, sizeof(valueText), "SNR %d", (int)elrsUplinkSnr);
      tft.setCursor(valueX, lineY + 30);
      tft.print(valueText);
    } else {
      tft.setCursor(valueX, lineY + 15);
      tft.print("No Link");
    }

    lineY += 45;
    tft.fillRect(valueX, lineY + 15, valueW, 16, COLOR_STICK_PANEL);
    tft.setTextColor(COLOR_ACCENT, COLOR_STICK_PANEL);
    tft.setCursor(valueX, lineY + 15);
    if (elrsLinkActive) {
      snprintf(valueText, sizeof(valueText), "%u / %u",
               (unsigned int)elrsUplinkRssi1,
               (unsigned int)elrsUplinkRssi2);
      tft.print(valueText);
    } else {
      tft.print("-- / --");
    }
  } else {
    lineY += 30;
    tft.fillRect(valueX, lineY + 15, valueW, 16, COLOR_STICK_PANEL);
    tft.setTextColor(COLOR_ACCENT, COLOR_STICK_PANEL);
    tft.setCursor(valueX, lineY + 15);
    if (hasEspNowHeaderSignal(millis())) {
      int latencySnapshot = espNowLatency;
      snprintf(valueText, sizeof(valueText), "%d ms", max(0, latencySnapshot));
      tft.print(valueText);
    } else {
      tft.print("-- ms");
    }

    lineY += 30;
    tft.fillRect(valueX, lineY + 15, valueW, 16, COLOR_STICK_PANEL);
    tft.setTextColor(COLOR_ACCENT, COLOR_STICK_PANEL);
    tft.setCursor(valueX, lineY + 15);
    if (telemetryVoltage > 0.05f) {
      tft.print(telemetryVoltage, 2);
      tft.print(" V");
    } else {
      tft.print("--.-- V");
    }
  }
}

void drawCenteredBar(int x, int y, int w, int h, float value) {

  int top = y + 10;          // match tank body top
  int bottom = y + h - 10;   // match tank body bottom

  int centerY = (top + bottom) / 2;
  int maxLen = (bottom - top) / 2;

  int len = value * maxLen;

  if (len > 0) {
    for (int i = 0; i < len; i++) {
      int drawY = centerY - i;
      if (drawY >= top) {
        uint16_t c = tankThrottleColor((float)i / maxLen * value);
        tft.drawFastHLine(x, drawY, w, c);
      }
    }
  } else {
    for (int i = 0; i < -len; i++) {
      int drawY = centerY + i;
      if (drawY <= bottom) {
        uint16_t c = tankThrottleColor((float)i / maxLen * value);
        tft.drawFastHLine(x, drawY, w, c);
      }
    }
  }
}

void drawQuadBars(int x, int y, int w, int h,
                  float m1, float m2, float m3, float m4) {

  int barW = 5;

  drawBottomBar(x, y, barW, h, m1);
  drawBottomBar(x + w - barW, y, barW, h, m2);

  drawBottomBar(x, y + h/2, barW, h/2, m3);
  drawBottomBar(x + w - barW, y + h/2, barW, h/2, m4);
}

void drawBottomBar(int x, int y, int w, int h, float value) {
  value = constrain(value, 0.0, 1.0);

  int len = value * h;

  for (int i = 0; i < len; i++) {
    float t = (float)i / h;
    uint16_t c = throttleColor(t * 2 - 1); // reuse gradient
    tft.drawFastHLine(x, y + h - i, w, c);
  }
}
    void drawSignalBars(int x, int y, int strength) {
    // strength = 0–4

    int barWidth = 6;
    int spacing  = 3;

    int heights[4] = {6, 10, 14, 18};

    for (int i = 0; i < 4; i++) {

      int bx = x + i * (barWidth + spacing);
      int by = y + (13 - heights[i]);  // align bottoms

      uint16_t color = (i < strength) ? COLOR_SIG : COLOR_PANEL;

      tft.fillRect(bx, by, barWidth, heights[i], color);
    }
  }

  void drawBattery(int x, int y, int level, bool present, bool charging) {
  // level = 0–100

  int w = 32;
  int h = 16;
  int padding = 2;

  // ===== OUTLINE =====
  tft.drawRoundRect(x, y, w, h, 2, COLOR_TEXT);

  // terminal
  tft.fillRect(x + w, y + (h / 3), 4, h / 3, COLOR_TEXT);

  if (!present) {
    tft.drawLine(x + 4, y + 3, x + w - 4, y + h - 3, COLOR_ACCENT_HI);
    tft.drawLine(x + w - 4, y + 3, x + 4, y + h - 3, COLOR_ACCENT_HI);
    return;
  }

  // ===== FILL WIDTH =====
  int fillWidth = map(level, 0, 100, 0, w - (padding * 2));

  uint16_t color;

  if (level > 60) color = COLOR_SIG;
  else if (level > 30) color = COLOR_ACCENT;
  else color = TFT_RED;

  // ===== FILL =====
  tft.fillRoundRect(
    x + padding,
    y + padding,
    fillWidth,
    h - (padding * 2),
    2,
    color
  );

  if (charging) {
    int boltX = x + 13;
    int boltY = y + 2;
    tft.fillTriangle(boltX + 4, boltY, boltX, boltY + 7, boltX + 5, boltY + 7, COLOR_TEXT);
    tft.fillTriangle(boltX + 4, boltY + 12, boltX + 3, boltY + 7, boltX + 8, boltY + 7, COLOR_TEXT);
  }
}

// Header/top bar indicators are small but important: battery, charge state,
// link quality, latency, and protocol health all update here.
void drawTopBarStatic() {
  // ===== CLEAR AREA =====
  tft.fillRect(0, 0, 240, 35, COLOR_BG);

  // ===== TITLE =====
  tft.setTextFont(4);
  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(10, 5);
  tft.print("Anubis");
  tft.setTextFont(1);
  tft.setTextSize(1);

  topBarForceRedraw = true;
}

void drawTopBarDynamic() {

  static bool lastBatteryPresent = true;
  static bool lastBatteryCharging = false;
  static bool lastNoSignalEligible = false;
  static bool lastEspNowSignalState = false;
  static bool lastElrsLinkState = false;
  static bool lastElrsModuleState = false;
  static uint8_t lastHeaderElrsLq = 255;
  static ProtocolType lastProtocol = PROTOCOL_ELRS;
  static int lastHeaderLatency = -1;

  unsigned long now = millis();
  ProtocolType protocol = getModelProtocol(activeModel);
  bool protocolChanged = (protocol != lastProtocol);
  int latencySnapshot = espNowLatency;

  if (protocolChanged) {
    drawTopBarStatic();
    lastSignal = -1;
    lastHeaderLatency = -1;
    lastEspNowSignalState = false;
    lastElrsLinkState = false;
    lastElrsModuleState = false;
    lastHeaderElrsLq = 255;
  }

  bool espNowSignalPresent = hasEspNowHeaderSignal(now);
  bool elrsSignalPresent = elrsLinkActive;
  bool elrsNoSignalEligible = elrsModulePresent || lastElrsSerialRxTime > 0;
  bool noSignalEligible = false;
  if (protocol == PROTOCOL_ESPNOW) {
    noSignalEligible = (lastEspNowAliveTime > 0) ||
      (espNowProtocolStartTime > 0 &&
       (now - espNowProtocolStartTime) > ESPNOW_HEADER_SIGNAL_TIMEOUT_MS);
  } else {
    noSignalEligible = elrsNoSignalEligible;
  }

  bool headerStateChanged = topBarForceRedraw ||
    protocolChanged ||
    batteryLevel != lastBattery ||
    batteryPresent != lastBatteryPresent ||
    batteryCharging != lastBatteryCharging;

  if (protocol == PROTOCOL_ESPNOW) {
    headerStateChanged = headerStateChanged ||
      espNowSignalPresent != lastEspNowSignalState ||
      noSignalEligible != lastNoSignalEligible ||
      latencySnapshot != lastHeaderLatency;
  }
  else {
    headerStateChanged = headerStateChanged ||
      signalStrength != lastSignal ||
      elrsSignalPresent != lastElrsLinkState ||
      elrsModulePresent != lastElrsModuleState ||
      noSignalEligible != lastNoSignalEligible ||
      elrsUplinkLq != lastHeaderElrsLq;
  }

  if (headerStateChanged) {
    tft.fillRect(104, 5, 94, 25, COLOR_BG);

    if (protocol == PROTOCOL_ESPNOW) {
      if (espNowSignalPresent) {
        char latencyText[16];
        int displayLatency = max(0, latencySnapshot);
        snprintf(latencyText, sizeof(latencyText), "%d ms", displayLatency);
        tft.setTextFont(2);
        tft.setTextColor(getLatencyIndicatorColor(displayLatency), COLOR_BG);
        tft.drawRightString(latencyText, 194, 10, 2);
      }
      else {
        tft.setTextFont(2);
        if (noSignalEligible) {
          tft.setTextColor(TFT_RED, COLOR_BG);
          tft.drawString("No Signal", 118, 10, 2);
        } else {
          tft.setTextColor(COLOR_TEXT, COLOR_BG);
          tft.drawRightString("-- ms", 194, 10, 2);
        }
      }
    }
    else {
      if (elrsSignalPresent) {
        drawSignalBars(150, 10, signalStrength);
      }
      else {
        tft.setTextFont(2);
        if (noSignalEligible) {
          tft.setTextColor(TFT_RED, COLOR_BG);
          tft.drawString("No Signal", 118, 10, 2);
        } else {
          tft.setTextColor(COLOR_TEXT, COLOR_BG);
          tft.drawRightString("--", 178, 10, 2);
        }
      }
    }

    tft.fillRect(198, 8, 40, 22, COLOR_BG);
    drawBattery(200, 10, batteryLevel, batteryPresent, batteryCharging);

    lastSignal = signalStrength;
    lastHeaderLatency = latencySnapshot;
    lastBattery = batteryLevel;
    lastBatteryPresent = batteryPresent;
    lastBatteryCharging = batteryCharging;
    lastEspNowSignalState = espNowSignalPresent;
    lastElrsLinkState = elrsSignalPresent;
    lastElrsModuleState = elrsModulePresent;
    lastHeaderElrsLq = elrsUplinkLq;
    lastNoSignalEligible = noSignalEligible;
    lastProtocol = protocol;
    topBarForceRedraw = false;
  }

  if (protocol == PROTOCOL_ELRS && !elrsLinkActive) {
    signalStrength = 0;
  }
}

uint16_t fadeColor(uint16_t color, float factor) {
  // factor: 1.0 = full color, 0.0 = black

  uint8_t r = (color >> 11) & 0x1F;
  uint8_t g = (color >> 5)  & 0x3F;
  uint8_t b = color & 0x1F;

  r = r * factor;
  g = g * factor;
  b = b * factor;

  return (r << 11) | (g << 5) | b;
}

uint16_t tankThrottleColor(float t) {
  // t = -1.0 → 1.0
  t = constrain(t, -1.0, 1.0);

  float v = abs(t);  // distance from center (0 → 1)

  // green → red
  uint8_t r = 255 * v;
  uint8_t g = 255 * (1.0 - v);
  uint8_t b = 0;

  return tft.color565(r, g, b);
}

uint16_t throttleColor(float t) {
  // t = -1.0 → 1.0 (tank)
  // t =  0.0 → 1.0 (quad)

  t = constrain(t, -1.0, 1.0);

  // map to 0–1
  float v = (t + 1.0) * 0.5;

  uint8_t r, g, b = 0;

  if (v < 0.5) {
    // green → yellow
    float f = v * 2;
    r = 255 * f;
    g = 255;
  } else {
    // yellow → red
    float f = (v - 0.5) * 2;
    r = 255;
    g = 255 * (1 - f);
  }

  return tft.color565(r, g, b);
}

void drawStickBaseDirect(int x, int y, int size) {

  int radius = 10;

  // ===== GRADIENT BACKGROUND =====
  for (int i = 0; i < size; i++) {

    int shade = map(i, 0, size, 10, -15);

    uint8_t r = (COLOR_PANEL >> 11) & 0x1F;
    uint8_t g = (COLOR_PANEL >> 5)  & 0x3F;
    uint8_t b = COLOR_PANEL & 0x1F;

    int nr = constrain(r + shade / 2, 0, 31);
    int ng = constrain(g + shade,     0, 63);
    int nb = constrain(b + shade / 2, 0, 31);

    uint16_t c = (nr << 11) | (ng << 5) | nb;

    int xOffset = 0;

    if (i < radius) {
      int dy = radius - i;
      xOffset = radius - sqrt(radius * radius - dy * dy);
    }
    else if (i >= size - radius) {
      int dy = i - (size - radius);
      xOffset = radius - sqrt(radius * radius - dy * dy);
    }

    tft.drawFastHLine(x + xOffset, y + i, size - (2 * xOffset), c);
  }

  // border + crosshair
  tft.drawRoundRect(x, y, size, size, radius, COLOR_ACCENT);
  tft.drawRoundRect(x+2, y+2, size-4, size-4, radius, TFT_DARKGREY);

  int cx = x + size / 2;
  int cy = y + size / 2;

  tft.drawFastHLine(x + 10, cy, size - 20, COLOR_ACCENT);
  tft.drawFastVLine(cx, y + 10, size - 20, COLOR_ACCENT);

  tft.drawCircle(cx, cy, 15, COLOR_PANEL);
}

void drawStickBase(int x, int y, int size) {

  static bool spriteReady = false;
  static int spriteSize = 0;

  if (stickBaseSpriteNeedsRebuild && spriteReady) {
    stickBaseSprite.deleteSprite();
    spriteReady = false;
  }

  if (!spriteReady || spriteSize != size) {
    if (spriteReady) {
      stickBaseSprite.deleteSprite();
      spriteReady = false;
    }

    stickBaseSprite.setColorDepth(16);

    if (stickBaseSprite.createSprite(size, size) == nullptr) {
      drawStickBaseDirect(x, y, size);
      return;
    }

    stickBaseSprite.fillSprite(COLOR_BG);

    int radius = 10;

    for (int i = 0; i < size; i++) {

      int shade = map(i, 0, size, 10, -15);

      uint8_t r = (COLOR_PANEL >> 11) & 0x1F;
      uint8_t g = (COLOR_PANEL >> 5)  & 0x3F;
      uint8_t b = COLOR_PANEL & 0x1F;

      int nr = constrain(r + shade / 2, 0, 31);
      int ng = constrain(g + shade,     0, 63);
      int nb = constrain(b + shade / 2, 0, 31);

      uint16_t c = (nr << 11) | (ng << 5) | nb;

      int xOffset = 0;

      if (i < radius) {
        int dy = radius - i;
        xOffset = radius - sqrt(radius * radius - dy * dy);
      }
      else if (i >= size - radius) {
        int dy = i - (size - radius);
        xOffset = radius - sqrt(radius * radius - dy * dy);
      }

      stickBaseSprite.drawFastHLine(xOffset, i, size - (2 * xOffset), c);
    }

    stickBaseSprite.drawRoundRect(0, 0, size, size, radius, COLOR_ACCENT);
    stickBaseSprite.drawRoundRect(2, 2, size - 4, size - 4, radius, TFT_DARKGREY);

    int cx = size / 2;
    int cy = size / 2;

    stickBaseSprite.drawFastHLine(10, cy, size - 20, COLOR_ACCENT);
    stickBaseSprite.drawFastVLine(cx, 10, size - 20, COLOR_ACCENT);
    stickBaseSprite.drawCircle(cx, cy, 15, COLOR_PANEL);

    spriteReady = true;
    spriteSize = size;
    stickBaseSpriteNeedsRebuild = false;
  }

  stickBaseSprite.pushSprite(x, y);
}

void drawStickKnob(int x, int y, int size, int posX, int posY) {

  float maxRadius = size / 2 - 10;

  float dist = sqrt(posX * posX + posY * posY);

  if (dist > maxRadius) {
    posX = posX * maxRadius / dist;
    posY = posY * maxRadius / dist;
  }

  int radius = 10;

  int cx = x + size / 2;
  int cy = y + size / 2;

  int px = cx + posX;
  int py = cy + posY;

  // ===== DRAW KNOB =====
  tft.drawCircle(px, py, 8, COLOR_ACCENT);
  tft.drawCircle(px, py, 10, COLOR_ACCENT_HI);
  tft.fillCircle(px, py, 6, COLOR_ACCENT_HI);
  tft.fillCircle(px - 2, py - 2, 2, COLOR_TEXT);
}

// ==== STATIC REVERSE ====
void drawReverseStatic() {
  tft.fillScreen(COLOR_BG);

  // BACK button
  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    false,
    (dpadFocusVisible && focusIndex == CHANNEL_COUNT),
    BACK_TEXT_OFFSET);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);

  int startY = 42;
  int spacing = 38;

  for (int i = 0; i < CHANNEL_COUNT; i++) {

    int y = startY + (i * spacing);
    bool linked = hasLinkedReversePeers(activeModel, i);

    // Labels
    tft.setTextColor(linked ? COLOR_ACCENT_HI : COLOR_TEXT);
    tft.setCursor(20, y + 10);
    tft.print("CH");
    tft.print(i + 1);
    tft.setCursor(54, y + 10);
    tft.print(getChannelAxisName(i));

    if (linked) {
      tft.setCursor(82, y + 10);
      tft.print("*");
    }

    // Toggle outline
    tft.drawRoundRect(120, y, 80, 30, 10, COLOR_ACCENT);

    int tx = 120;
    int ty = y;
    int tw = 80;
    int th = 30;

    // LEFT label (OFF)
    tft.setTextColor(linked ? COLOR_ACCENT_HI : COLOR_TEXT);
    tft.setCursor(tx - 35, ty + (th / 2) - 6);
    tft.print("OFF");

    // RIGHT label (ON)
    tft.setCursor(tx + tw + 5, ty + (th / 2) - 6);
    tft.print("ON");
  }
}

// ==== DYNAMIC REVERSE ====
void drawReverseDynamic() {

  int startY = 42;
  int spacing = 38;

  for (int i = 0; i < CHANNEL_COUNT; i++) {

    if (!reverseChannelDirty[i] && !reverseNeedsRedraw) continue;

    int y = startY + (i * spacing);
    bool linked = hasLinkedReversePeers(activeModel, i);

    int tx = 120;
    int ty = y;
    int tw = 80;
    int th = 30;


    // CLEAR FULL TOGGLE AREA
    tft.fillRect(tx, ty - 2, tw, th + 4, COLOR_BG);

    // redraw track background
    tft.fillRoundRect(tx + 2, ty + 2, tw - 4, th - 4, 10, COLOR_PANEL);

    // redraw outline
    tft.drawRoundRect(tx, ty, tw, th, 10, COLOR_ACCENT);

    if (linked) {
      tft.drawRoundRect(tx - 2, ty - 2, tw + 4, th + 4, 10, COLOR_ACCENT_HI);
    }

    if (currentScreen == SCREEN_REVERSE && dpadFocusVisible && focusIndex == i) {
      tft.drawRoundRect(tx - 2, ty - 2, tw + 4, th + 4, 10, COLOR_ACCENT_HI);
    }

    // knob radius
    int r = 10;

    int knobX = models[activeModel].reverse[i]
    ? (tx + tw - r - 2)
    : (tx + r + 2);

    uint16_t knobColor = models[activeModel].reverse[i]
    ? COLOR_ACCENT
    : COLOR_TEXT;

    tft.fillCircle(knobX, ty + th / 2, 10, knobColor);

    reverseChannelDirty[i] = false;
  }

  reverseNeedsRedraw = false;
}

// ==== STATIC TRIM ====
void drawTrimGraphBase() {
  int cx = TRIM_CENTER_X;
  int cy = TRIM_CENTER_Y;
  int s  = TRIM_SIZE;
  int step = 20;

  tft.fillRect(cx - s, cy - s, (s * 2) + 1, (s * 2) + 1, COLOR_BG);

  for (int i = -s; i <= s; i += step) {
    tft.drawFastVLine(cx + i, cy - s, s * 2, TFT_DARKGREY);
  }

  for (int i = -s; i <= s; i += step) {
    tft.drawFastHLine(cx - s, cy + i, s * 2, TFT_DARKGREY);
  }

  tft.drawLine(cx - s, cy, cx + s, cy, COLOR_ACCENT);
  tft.drawLine(cx - s, cy + 1, cx + s, cy + 1, COLOR_ACCENT);
  tft.drawLine(cx, cy - s, cx, cy + s, COLOR_ACCENT);
  tft.drawLine(cx + 1, cy - s, cx + 1, cy + s, COLOR_ACCENT);
}

void drawTrimButtons() {
  int cx = TRIM_CENTER_X;
  int cy = TRIM_CENTER_Y;
  int s  = TRIM_SIZE;
  int sideBtnY = cy - (TRIM_BTN_SIZE / 2);
  int centerBtnX = cx - (TRIM_BTN_SIZE / 2);

  trimBtnLeftX  = cx - s - TRIM_BTN_SIZE - TRIM_BTN_GAP;
  trimBtnRightX = cx + s + TRIM_BTN_GAP;
  trimBtnTopY = cy - s - TRIM_BTN_SIZE - TRIM_BTN_GAP;
  trimBtnBottomY = cy + s + TRIM_BTN_GAP;

  tft.fillRoundRect(trimBtnLeftX, sideBtnY, TRIM_BTN_SIZE, TRIM_BTN_SIZE, 6, COLOR_ACCENT);
  tft.drawRoundRect(trimBtnLeftX, sideBtnY, TRIM_BTN_SIZE, TRIM_BTN_SIZE, 6, COLOR_ACCENT_HI);
  if (currentScreen == SCREEN_TRIM && dpadFocusVisible && focusIndex == 2) {
    tft.drawRoundRect(trimBtnLeftX - 2, sideBtnY - 2, TRIM_BTN_SIZE + 4, TRIM_BTN_SIZE + 4, 6, COLOR_TEXT);
  }
  tft.setTextColor(COLOR_BG);
  tft.drawString("-", trimBtnLeftX + 10, sideBtnY + 4);

  tft.fillRoundRect(trimBtnRightX, sideBtnY, TRIM_BTN_SIZE, TRIM_BTN_SIZE, 6, COLOR_ACCENT);
  tft.drawRoundRect(trimBtnRightX, sideBtnY, TRIM_BTN_SIZE, TRIM_BTN_SIZE, 6, COLOR_ACCENT_HI);
  if (currentScreen == SCREEN_TRIM && dpadFocusVisible && focusIndex == 3) {
    tft.drawRoundRect(trimBtnRightX - 2, sideBtnY - 2, TRIM_BTN_SIZE + 4, TRIM_BTN_SIZE + 4, 6, COLOR_TEXT);
  }
  tft.setTextColor(COLOR_BG);
  tft.drawString("+", trimBtnRightX + 9, sideBtnY + 4);

  tft.fillRoundRect(centerBtnX, trimBtnTopY, TRIM_BTN_SIZE, TRIM_BTN_SIZE, 6, COLOR_ACCENT);
  tft.drawRoundRect(centerBtnX, trimBtnTopY, TRIM_BTN_SIZE, TRIM_BTN_SIZE, 6, COLOR_ACCENT_HI);
  if (currentScreen == SCREEN_TRIM && dpadFocusVisible && focusIndex == 4) {
    tft.drawRoundRect(centerBtnX - 2, trimBtnTopY - 2, TRIM_BTN_SIZE + 4, TRIM_BTN_SIZE + 4, 6, COLOR_TEXT);
  }
  tft.setTextColor(COLOR_BG);
  tft.drawString("+", centerBtnX + 10, trimBtnTopY + 4);

  tft.fillRoundRect(centerBtnX, trimBtnBottomY, TRIM_BTN_SIZE, TRIM_BTN_SIZE, 6, COLOR_ACCENT);
  tft.drawRoundRect(centerBtnX, trimBtnBottomY, TRIM_BTN_SIZE, TRIM_BTN_SIZE, 6, COLOR_ACCENT_HI);
  if (currentScreen == SCREEN_TRIM && dpadFocusVisible && focusIndex == 5) {
    tft.drawRoundRect(centerBtnX - 2, trimBtnBottomY - 2, TRIM_BTN_SIZE + 4, TRIM_BTN_SIZE + 4, 6, COLOR_TEXT);
  }
  tft.setTextColor(COLOR_BG);
  tft.drawString("-", centerBtnX + 10, trimBtnBottomY + 6);
}

void drawTrimStatic() {
  tft.fillScreen(COLOR_BG);

  tft.setTextColor(COLOR_TEXT);
  tft.setCursor(90, 20);

  // BACK
  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK", false, (dpadFocusVisible && focusIndex == 0), BACK_TEXT_OFFSET);

  // NEXT button (right side)
  const char* navLabel = (currentTrimPage == 0) ? "NEXT" : "PREV";
  drawButtonBubble(130, BACK_BTN_Y, 100, BACK_BTN_H, navLabel, false, (dpadFocusVisible && focusIndex == 1), 40);
  drawButtonBubble(
    TRIM_RESET_BTN_X, TRIM_RESET_BTN_Y, TRIM_RESET_BTN_W, TRIM_RESET_BTN_H,
    "RESET", false, (dpadFocusVisible && focusIndex == 6), 24);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);

  // ===== GIMBAL LABEL =====
  tft.setTextColor(COLOR_TEXT);

  // center-ish alignment
  int labelY = FOOTER_Y - 22;

  if (currentTrimPage == 0) {
    tft.drawCentreString("Left Stick", 120, labelY, 2);
  }
  else {
    tft.drawCentreString("Right Stick", 120, labelY, 2);
  }

  drawTrimGraphBase();
  drawTrimButtons();
}

// ==== DYNAMIC TRIM ====
void drawTrimDynamic() {

  static int lastPx = TRIM_CENTER_X;
  static int lastPy = TRIM_CENTER_Y;

  if (!trimDirty && !trimNeedsRedraw) return;

  int cx = TRIM_CENTER_X;
  int cy = TRIM_CENTER_Y;

  // ===== TARGET VALUES FROM ACTIVE MODEL =====
  int targetX = models[activeModel].trimX[currentTrimPage];
  int targetY = models[activeModel].trimY[currentTrimPage];

  // ===== SMOOTH ANIMATION =====
  trimRenderX += (targetX - trimRenderX) * 0.2;
  trimRenderY += (targetY - trimRenderY) * 0.2;

  int px = cx + trimRenderX;
  int py = cy - trimRenderY;

  drawTrimGraphBase();
  drawTrimButtons();

  // ===== DRAW NEW DOT =====
  tft.fillCircle(px, py, 5, COLOR_ACCENT_HI);

  // store for next frame
  lastPx = px;
  lastPy = py;

  trimDirty = false;
  trimNeedsRedraw = false;

  // Keep the live values in a dedicated top band above the upper trim button.
  tft.fillRect(0, TRIM_VALUE_Y, 240, 18, COLOR_BG);

  char xText[12];
  char yText[12];
  snprintf(xText, sizeof(xText), "X:%+d", targetX);
  snprintf(yText, sizeof(yText), "Y:%+d", targetY);

  tft.setTextColor(COLOR_TEXT);
  tft.drawCentreString(xText, 56, TRIM_VALUE_Y, 2);
  tft.drawCentreString(yText, 184, TRIM_VALUE_Y, 2);

  // ===== KEEP ANIMATING UNTIL SETTLED =====
  if (abs(trimRenderX - targetX) > 0.1 ||
      abs(trimRenderY - targetY) > 0.1) {
    uiNeedsRedraw = true;
  }
}

// ==== FAILSAFE STATIC ====
void drawFailsafeStatic() {
  tft.fillScreen(COLOR_BG);

  // ===== HEADER =====
  tft.setTextColor(COLOR_TEXT);
  tft.setTextFont(2);

  tft.drawCentreString(
    "Position input to desired value",
    120, 40, 2);

  tft.drawCentreString(
    "before setting failsafe",
    120, 60, 2);

  // ===== BACK BUTTON =====
  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK",
    false,
    (dpadFocusVisible && focusIndex == CHANNEL_COUNT),
    BACK_TEXT_OFFSET);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);

  // ===== CHANNEL ROWS =====
  int startY = 85;
  int spacing = 34;

  for (int i = 0; i < CHANNEL_COUNT; i++) {

    int y = startY + (i * spacing);

    // CH label
    tft.setTextColor(COLOR_TEXT);
    tft.setCursor(20, y + 10);
    tft.print("CH");
    tft.print(i + 1);

    int tx = 120;
    int ty = y;
    int tw = 80;
    int th = 30;

    // ===== SWITCH OUTLINE =====
    tft.drawRoundRect(tx, ty, tw, th, 10, COLOR_ACCENT);

    // ===== LABELS =====
    tft.setTextColor(COLOR_TEXT);

    // OFF (left)
    tft.setCursor(tx - 35, ty + (th / 2) - 6);
    tft.print("OFF");

    // ON (right)
    tft.setCursor(tx + tw + 5, ty + (th / 2) - 6);
    tft.print("ON");
  }
}

// ==== FAILSAFE DYNAMIC ====
void drawFailsafeDynamic() {

  int startY = 85;
  int spacing = 34;
  char valueText[16];

  for (int i = 0; i < CHANNEL_COUNT; i++) {

    if (!failsafeDirty[i] && !failsafeNeedsRedraw) continue;

    int y = startY + (i * spacing);

    int tx = 120;
    int ty = y;
    int tw = 80;
    int th = 30;

    tft.fillRect(50, y, 64, 24, COLOR_BG);
    snprintf(valueText, sizeof(valueText), "%+d%%", getModelFailsafeValue(activeModel, i));
    tft.setTextColor(models[activeModel].failsafe[i] ? COLOR_ACCENT_HI : COLOR_TEXT);
    tft.drawString(valueText, 54, y + 10, 2);

    // ===== CLEAR FULL TOGGLE AREA =====
    tft.fillRect(tx, ty - 2, tw, th + 4, COLOR_BG);

    // ===== TRACK =====
    tft.fillRoundRect(tx + 2, ty + 2, tw - 4, th - 4, 10, COLOR_PANEL);

    // ===== OUTLINE =====
    tft.drawRoundRect(tx, ty, tw, th, 10, COLOR_ACCENT);

    if (currentScreen == SCREEN_FAILSAFE && dpadFocusVisible && focusIndex == i) {
      tft.drawRoundRect(tx - 2, ty - 2, tw + 4, th + 4, 10, COLOR_ACCENT_HI);
    }

    // ===== KNOB (IDENTICAL TO REVERSE) =====
    int r = 10;

    int knobX = models[activeModel].failsafe[i]
      ? (tx + tw - r - 2)
      : (tx + r + 2);

    uint16_t knobColor = models[activeModel].failsafe[i]
      ? COLOR_ACCENT
      : COLOR_TEXT;

    tft.fillCircle(knobX, ty + th / 2, 10, knobColor);

    failsafeDirty[i] = false;
  }

  failsafeNeedsRedraw = false;
}

// ==== KEYBOARD STATIC ====
void drawKeyboardStatic() {

  tft.fillRect(kbX, kbY, kbW, kbH, COLOR_BG);
  tft.drawFastHLine(0, kbY - 2, 240, COLOR_ACCENT);

  for (int r = 0; r < KB_ROWS; r++) {
    for (int c = 0; c < KB_COLS; c++) {

      const char* key = getKeyboardKey(r, c);
      if (strlen(key) == 0) continue;

      int x = kbX + c * (keyW + keySpacing) + 5;
      int y = kbY + r * (keyH + keySpacing) + 5;

      int w = keyW;

      // ===== SPECIAL KEYS =====
      if (strcmp(key, " ") == 0) {
        x = kbX + 10;              // ← FORCE LEFT ALIGN
        w = kbW - 20;              // ← FULL WIDTH
      }
      else if (strcmp(key, "OK") == 0) {
        w = keyW * 2;
      }

      tft.drawRoundRect(x, y, w, keyH, 4, COLOR_ACCENT);

      tft.setTextColor(COLOR_TEXT);
      tft.drawCentreString(key, x + w/2, y + 8, 2);
    }
  }
}

void drawKeyboardPreview() {
  if (!isOtaKeyboardTarget(keyboardTarget)) return;

  char preview[33];
  const char *source = keyboardBuffer.c_str();
  size_t sourceLen = strlen(source);
  size_t copyLen = sourceLen;
  if (copyLen > sizeof(preview) - 1) {
    copyLen = sizeof(preview) - 1;
  }

  memset(preview, 0, sizeof(preview));
  memcpy(preview, source, copyLen);
  preview[copyLen] = '\0';

  tft.fillRoundRect(8, kbY - 34, 224, 28, 6, COLOR_PANEL);
  tft.drawRoundRect(8, kbY - 34, 224, 28, 6, COLOR_ACCENT);
  tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
  tft.drawString(getKeyboardTargetLabel(keyboardTarget), 14, kbY - 30, 2);
  tft.setTextColor(COLOR_ACCENT_HI, COLOR_PANEL);
  tft.drawRightString((copyLen > 0) ? preview : "<empty>", 224, kbY - 30, 2);
}

// ==== KEYBOARD DYNAMIC ====
void drawKeyboardDynamic() {
  drawKeyboardPreview();

  for (int r = 0; r < KB_ROWS; r++) {
    for (int c = 0; c < KB_COLS; c++) {

      const char* key = getKeyboardKey(r, c);
      if (strlen(key) == 0) continue;

      int kx = kbX + c * (keyW + keySpacing) + 5;
      int ky = kbY + r * (keyH + keySpacing) + 5;

      int kw = keyW;

      // ===== SPECIAL KEYS =====
      if (strcmp(key, " ") == 0) {
        kx = kbX + 10;
        kw = kbW - 20;
      }
      else if (strcmp(key, "OK") == 0) {
        kw = keyW * 2;
      }

      // ===== KEY BASE =====
      tft.fillRoundRect(kx, ky, kw, keyH, 4, COLOR_PANEL);

      // ===== CURSOR HIGHLIGHT =====
      if (r == kbCursorRow && c == kbCursorCol) {
        tft.drawRoundRect(kx - 2, ky - 2, kw + 4, keyH + 4, 4, COLOR_ACCENT);
      }

      // ===== PRESSED EFFECT =====
      if (r == kbPressedRow && c == kbPressedCol) {
        tft.fillRoundRect(kx, ky, kw, keyH, 4, COLOR_ACCENT);
        tft.setTextColor(COLOR_BG);
      } else {
        tft.setTextColor(COLOR_TEXT);
      }

      // ===== TEXT =====
      tft.drawCentreString(key, kx + kw / 2, ky + 8, 2);
    }
  }

  // reset pressed state AFTER drawing
  kbPressedRow = -1;
  kbPressedCol = -1;
}

void handleKeyboardSelect() {

  kbPressedRow = kbCursorRow;
  kbPressedCol = kbCursorCol;

  const char* key = getKeyboardKey(kbCursorRow, kbCursorCol);
  processKeyboardKey(key);
}

// ==== MODEL NAME STATIC ====
void drawModelNameStatic() {

  tft.fillScreen(COLOR_BG);

  // ===== BACK =====
  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK", false, (dpadFocusVisible && focusIndex == 0), BACK_TEXT_OFFSET);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);

  // ===== LABEL =====
  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  tft.drawString("Model Name", 12, 42, 2);

  // ===== INPUT BOX =====
  tft.drawRoundRect(MODEL_NAME_INPUT_X, MODEL_NAME_INPUT_Y, MODEL_NAME_INPUT_W, MODEL_NAME_INPUT_H, 8, COLOR_ACCENT);
  if (dpadFocusVisible && focusIndex == 1) {
    tft.drawRoundRect(MODEL_NAME_INPUT_X - 2, MODEL_NAME_INPUT_Y - 2,
                      MODEL_NAME_INPUT_W + 4, MODEL_NAME_INPUT_H + 4, 8, COLOR_ACCENT_HI);
  }

  // ===== DIVIDER =====
  tft.drawFastHLine(10, 112, 220, COLOR_ACCENT);

  // ===== LIST LABEL =====
  tft.drawString("Saved", 12, 116, 2);
}

// ==== MODEL NAME DYNAMIC ====
void drawModelNameDynamic() {

  if (!modelNameDirty && !modelNameNeedsRedraw) return;

  // ===== INPUT TEXT =====
  tft.fillRect(MODEL_NAME_INPUT_X + 4, MODEL_NAME_INPUT_Y + 4,
               MODEL_NAME_INPUT_W - 8, MODEL_NAME_INPUT_H - 8, COLOR_BG);

  tft.setTextColor(COLOR_TEXT, COLOR_BG);
  int inputFont = (keyboardBuffer.length() > 12) ? 2 : 4;
  int inputTextY = (inputFont == 4) ? (MODEL_NAME_INPUT_Y + 9) : (MODEL_NAME_INPUT_Y + 13);
  tft.drawString(keyboardBuffer, MODEL_NAME_INPUT_X + 8, inputTextY, inputFont);

  // ===== MODEL LIST =====
  for (int i = 0; i < 4; i++) {

    int y = MODEL_NAME_LIST_Y + (i * MODEL_NAME_ROW_H);

    int numberX = 7;
    int nameX   = MODEL_NAME_NAME_X;
    int boxX    = MODEL_NAME_BOX_X;

    // ===== CLEAR ROW =====
    tft.fillRect(0, y - 3, 240, MODEL_NAME_ROW_H, COLOR_BG);

    bool isActive = (i == activeModel);

    // ===== HIGHLIGHT BOX =====
    if (isActive) {
      tft.fillRoundRect(boxX, y - 2, MODEL_NAME_BOX_W, MODEL_NAME_ROW_H - 6, 7, COLOR_ACCENT);
    }
    if (dpadFocusVisible && focusIndex == i + 2) {
      tft.drawRoundRect(boxX - 2, y - 4, MODEL_NAME_BOX_W + 4, MODEL_NAME_ROW_H - 2, 7, COLOR_ACCENT_HI);
    }

    // ===== SELECTOR ARROW =====
    if (isActive) {
      tft.setTextColor(COLOR_ACCENT);
      tft.drawString(">", 0, y + 4, 2);
    }

    // ===== INDEX NUMBER =====
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    char slotText[8];
    snprintf(slotText, sizeof(slotText), "[%d]", i + 1);
    tft.drawString(slotText, numberX, y + 4, 2);

    // ===== MODEL NAME =====
    tft.setTextColor(isActive ? COLOR_BG : COLOR_TEXT, isActive ? COLOR_ACCENT : COLOR_BG);
    tft.drawString(modelNames[i], nameX, y + 4, 2);

    // ===== DELETE BUTTON =====
    tft.fillRoundRect(MODEL_NAME_DELETE_X, y - 2, MODEL_NAME_DELETE_W, MODEL_NAME_ROW_H - 6, 6, COLOR_PANEL);
    tft.drawRoundRect(MODEL_NAME_DELETE_X, y - 2, MODEL_NAME_DELETE_W, MODEL_NAME_ROW_H - 6, 6, COLOR_ACCENT);
    if (dpadFocusVisible && focusIndex == i + 6) {
      tft.drawRoundRect(MODEL_NAME_DELETE_X - 2, y - 4, MODEL_NAME_DELETE_W + 4, MODEL_NAME_ROW_H - 2, 6, COLOR_ACCENT_HI);
    }

    tft.setTextColor(COLOR_TEXT, COLOR_PANEL);
    tft.drawCentreString("-", MODEL_NAME_DELETE_X + (MODEL_NAME_DELETE_W / 2), y + 4, 2);
  }

  modelNameDirty = false;
  modelNameNeedsRedraw = false;
}

void drawMixNumpad() {
  const char* mixPadLayout[4][3] = {
    {"1", "2", "3"},
    {"4", "5", "6"},
    {"7", "8", "9"},
    {"<", "0", "-"}
  };

  tft.fillRoundRect(MIX_NUMPAD_X, MIX_NUMPAD_Y, MIX_NUMPAD_W, MIX_NUMPAD_H, 10, COLOR_BG);
  tft.drawRoundRect(MIX_NUMPAD_X, MIX_NUMPAD_Y, MIX_NUMPAD_W, MIX_NUMPAD_H, 10, COLOR_ACCENT);
  tft.drawFastHLine(MIX_NUMPAD_X + 8, MIX_NUMPAD_Y + 30, MIX_NUMPAD_W - 16, TFT_DARKGREY);

  tft.setTextColor(COLOR_TEXT);
  const char* title = "Value";
  if (mixNumpadTarget == NUMPAD_TARGET_MIX_RATE) title = "Rate";
  else if (mixNumpadTarget == NUMPAD_TARGET_MIX_OFFSET) title = "Offset";
  else if (mixNumpadTarget == NUMPAD_TARGET_EXPO_VALUE) title = "Expo";
  else if (mixNumpadTarget == NUMPAD_TARGET_ENDPOINT_VALUE) title = "End Point";
  else if (mixNumpadTarget == NUMPAD_TARGET_ELRS_TX_POWER) title = "TX Power";
  else if (mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_MODEL_ID) title = "Model ID";
  else if (mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_FAILSAFE) title = "Failsafe";
  else if (mixNumpadTarget == NUMPAD_TARGET_ELRS_RX_PWM_FAILSAFE) title = "PWM Failsafe";
  else if (mixNumpadTarget == NUMPAD_TARGET_TIMER_VALUE) title = "Timer MMSS";
  tft.drawString(title, MIX_NUMPAD_X + 10, MIX_NUMPAD_Y + 8, 2);

  tft.fillRoundRect(MIX_NUMPAD_BOX_X, MIX_NUMPAD_BOX_Y, MIX_NUMPAD_BOX_W, MIX_NUMPAD_BOX_H, 6, COLOR_PANEL);
  tft.drawRoundRect(MIX_NUMPAD_BOX_X, MIX_NUMPAD_BOX_Y, MIX_NUMPAD_BOX_W, MIX_NUMPAD_BOX_H, 6, COLOR_ACCENT);
  tft.setTextColor(COLOR_ACCENT_HI);
  tft.drawRightString(mixNumpadBuffer, MIX_NUMPAD_BOX_X + MIX_NUMPAD_BOX_W - 8, MIX_NUMPAD_BOX_Y + 5, 2);

  tft.fillRoundRect(MIX_NUMPAD_OK_X, MIX_NUMPAD_OK_Y, MIX_NUMPAD_OK_W, MIX_NUMPAD_OK_H, 6, COLOR_ACCENT);
  tft.drawRoundRect(MIX_NUMPAD_OK_X, MIX_NUMPAD_OK_Y, MIX_NUMPAD_OK_W, MIX_NUMPAD_OK_H, 6, COLOR_ACCENT_HI);
  if (mixNumpadCursorRow == 0 && mixNumpadCursorCol == 3) {
    tft.drawRoundRect(MIX_NUMPAD_OK_X - 2, MIX_NUMPAD_OK_Y - 2, MIX_NUMPAD_OK_W + 4, MIX_NUMPAD_OK_H + 4, 6, COLOR_TEXT);
  }
  tft.setTextColor(COLOR_BG);
  tft.drawCentreString("OK", MIX_NUMPAD_OK_X + (MIX_NUMPAD_OK_W / 2), MIX_NUMPAD_OK_Y + 5, 2);

  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 3; c++) {
      int keyX = MIX_NUMPAD_GRID_X + (c * (MIX_NUMPAD_KEY_W + MIX_NUMPAD_KEY_GAP));
      int keyY = MIX_NUMPAD_GRID_Y + (r * (MIX_NUMPAD_KEY_H + MIX_NUMPAD_KEY_GAP));

      tft.fillRoundRect(keyX, keyY, MIX_NUMPAD_KEY_W, MIX_NUMPAD_KEY_H, 6, COLOR_PANEL);
      tft.drawRoundRect(keyX, keyY, MIX_NUMPAD_KEY_W, MIX_NUMPAD_KEY_H, 6, COLOR_ACCENT);
      if (r == mixNumpadCursorRow && c == mixNumpadCursorCol) {
        tft.drawRoundRect(keyX - 2, keyY - 2, MIX_NUMPAD_KEY_W + 4, MIX_NUMPAD_KEY_H + 4, 6, COLOR_ACCENT_HI);
      }
      tft.setTextColor(COLOR_TEXT);
      tft.drawCentreString(mixPadLayout[r][c], keyX + (MIX_NUMPAD_KEY_W / 2), keyY + 5, 2);
    }
  }
}

// ==== MIXING STATIC ====
void drawMixingStatic() {
  tft.fillScreen(COLOR_BG);

  drawButtonBubble(
    BACK_BTN_X, BACK_BTN_Y, BACK_BTN_W, BACK_BTN_H,
    "BACK", false, (dpadFocusVisible && focusIndex == 0), BACK_TEXT_OFFSET);

  tft.drawFastHLine(0, FOOTER_Y - 5, 240, COLOR_ACCENT);
}

// ==== MIXING DYNAMIC ====
void drawMixingDynamic() {
  if (!mixingNeedsRedraw) return;

  tft.setTextFont(1);
  tft.setTextSize(1);

  char valueText[32];
  int pageBase = getMixPageBase(selectedMixPage);
  int localMixIndex = getMixLocalIndex();
  MixData &mix = models[activeModel].mixes[selectedMixIndex];
  uint8_t source = getMixSource(mix);
  uint8_t destination = getMixDestination(mix);
  bool reverseLinked = isMixReverseLinked(mix);

  drawButtonBubble(
    MIX_PAGE_BTN_X, MIX_PAGE_BTN_Y, MIX_PAGE_BTN_W, MIX_PAGE_BTN_H,
    selectedMixPage == MIX_PAGE_PRESET ? "USER MIX" : "PRESET",
    false,
    (dpadFocusVisible && focusIndex == 15),
    -1);

  tft.fillRect(0, 38, 240, FOOTER_Y - 44, COLOR_BG);

  for (int i = 0; i < MIXES_PER_PAGE; i++) {
    int absoluteMixIndex = pageBase + i;
    bool isSelected = (i == localMixIndex);
    int tabX = MIX_TAB_X(i);

    tft.fillRoundRect(
      tabX, MIX_TAB_Y, MIX_TAB_W, MIX_TAB_H, 8,
      isSelected ? COLOR_ACCENT : COLOR_PANEL);
    tft.drawRoundRect(tabX, MIX_TAB_Y, MIX_TAB_W, MIX_TAB_H, 8, COLOR_ACCENT);
    if (dpadFocusVisible && focusIndex == i + 1) {
      tft.drawRoundRect(tabX - 2, MIX_TAB_Y - 2, MIX_TAB_W + 4, MIX_TAB_H + 4, 8, COLOR_ACCENT_HI);
    }

    tft.setTextColor(isSelected ? COLOR_BG : COLOR_TEXT);
    snprintf(valueText, sizeof(valueText), "%c%d",
             selectedMixPage == MIX_PAGE_USER ? 'U' : 'P',
             (absoluteMixIndex - pageBase) + 1);
    tft.drawCentreString(valueText, tabX + (MIX_TAB_W / 2), MIX_TAB_Y + 7, 2);
  }

  tft.setTextColor(COLOR_ACCENT_HI);
  if (mix.enabled) {
    snprintf(valueText, sizeof(valueText), "%s %d: CH%d %s > CH%d %s",
             getMixPageLabel(selectedMixPage), localMixIndex + 1,
             source + 1, getChannelAxisName(source),
             destination + 1, getChannelAxisName(destination));
  } else {
    snprintf(valueText, sizeof(valueText), "%s Mix %d Disabled",
             getMixPageLabel(selectedMixPage), localMixIndex + 1);
  }
  tft.drawCentreString(valueText, 120, 72, 2);

  const char* labels[6] = {"Enable", "Source", "Dest", "Rev Link", "Rate", "Offset"};
  const int rowY[6] = {
    MIX_ROW_ENABLE_Y, MIX_ROW_SOURCE_Y, MIX_ROW_DEST_Y, MIX_ROW_LINK_Y, MIX_ROW_RATE_Y, MIX_ROW_OFFS_Y
  };

  tft.setTextColor(COLOR_TEXT);
  for (int i = 0; i < 6; i++) {
    tft.setCursor(20, rowY[i] + 8);
    tft.print(labels[i]);
  }

  // Enable toggle
  tft.fillRoundRect(MIX_FIELD_X + 2, MIX_ROW_ENABLE_Y + 2, MIX_FIELD_W - 4, MIX_FIELD_H - 4, 10, COLOR_PANEL);
  tft.drawRoundRect(MIX_FIELD_X, MIX_ROW_ENABLE_Y, MIX_FIELD_W, MIX_FIELD_H, 10, COLOR_ACCENT);
  if (dpadFocusVisible && focusIndex == 5) {
    tft.drawRoundRect(MIX_FIELD_X - 2, MIX_ROW_ENABLE_Y - 2, MIX_FIELD_W + 4, MIX_FIELD_H + 4, 10, COLOR_ACCENT_HI);
  }
  tft.setTextColor(COLOR_TEXT);
  tft.drawString("OFF", MIX_FIELD_X - 36, MIX_ROW_ENABLE_Y + 8, 2);
  tft.drawString("ON", MIX_FIELD_X + MIX_FIELD_W + 6, MIX_ROW_ENABLE_Y + 8, 2);

  int knobRadius = 9;
  int knobX = mix.enabled
    ? (MIX_FIELD_X + MIX_FIELD_W - knobRadius - 3)
    : (MIX_FIELD_X + knobRadius + 3);
  tft.fillCircle(knobX, MIX_ROW_ENABLE_Y + (MIX_FIELD_H / 2), knobRadius,
                 mix.enabled ? COLOR_ACCENT : COLOR_TEXT);

  snprintf(valueText, sizeof(valueText), "CH%d %s", source + 1, getChannelAxisName(source));
  tft.fillRoundRect(MIX_FIELD_X, MIX_ROW_SOURCE_Y, MIX_FIELD_W, MIX_FIELD_H, 8, COLOR_PANEL);
  tft.drawRoundRect(MIX_FIELD_X, MIX_ROW_SOURCE_Y, MIX_FIELD_W, MIX_FIELD_H, 8, COLOR_ACCENT);
  if (dpadFocusVisible && focusIndex == 6) {
    tft.drawRoundRect(MIX_FIELD_X - 2, MIX_ROW_SOURCE_Y - 2, MIX_FIELD_W + 4, MIX_FIELD_H + 4, 8, COLOR_ACCENT_HI);
  }
  tft.setTextColor(COLOR_TEXT);
  tft.drawCentreString(valueText, MIX_FIELD_X + (MIX_FIELD_W / 2), MIX_ROW_SOURCE_Y + 8, 2);

  snprintf(valueText, sizeof(valueText), "CH%d %s", destination + 1, getChannelAxisName(destination));
  tft.fillRoundRect(MIX_FIELD_X, MIX_ROW_DEST_Y, MIX_FIELD_W, MIX_FIELD_H, 8, COLOR_PANEL);
  tft.drawRoundRect(MIX_FIELD_X, MIX_ROW_DEST_Y, MIX_FIELD_W, MIX_FIELD_H, 8, COLOR_ACCENT);
  if (dpadFocusVisible && focusIndex == 7) {
    tft.drawRoundRect(MIX_FIELD_X - 2, MIX_ROW_DEST_Y - 2, MIX_FIELD_W + 4, MIX_FIELD_H + 4, 8, COLOR_ACCENT_HI);
  }
  tft.setTextColor(COLOR_TEXT);
  tft.drawCentreString(valueText, MIX_FIELD_X + (MIX_FIELD_W / 2), MIX_ROW_DEST_Y + 8, 2);

  tft.fillRoundRect(MIX_FIELD_X + 2, MIX_ROW_LINK_Y + 2, MIX_FIELD_W - 4, MIX_FIELD_H - 4, 10, COLOR_PANEL);
  tft.drawRoundRect(MIX_FIELD_X, MIX_ROW_LINK_Y, MIX_FIELD_W, MIX_FIELD_H, 10, COLOR_ACCENT);
  if (dpadFocusVisible && focusIndex == 8) {
    tft.drawRoundRect(MIX_FIELD_X - 2, MIX_ROW_LINK_Y - 2, MIX_FIELD_W + 4, MIX_FIELD_H + 4, 10, COLOR_ACCENT_HI);
  }
  tft.setTextColor(COLOR_TEXT);
  tft.drawString("SEP", MIX_FIELD_X - 36, MIX_ROW_LINK_Y + 8, 2);
  tft.drawString("LINK", MIX_FIELD_X + MIX_FIELD_W + 4, MIX_ROW_LINK_Y + 8, 2);

  knobX = reverseLinked
    ? (MIX_FIELD_X + MIX_FIELD_W - knobRadius - 3)
    : (MIX_FIELD_X + knobRadius + 3);
  tft.fillCircle(knobX, MIX_ROW_LINK_Y + (MIX_FIELD_H / 2), knobRadius,
                 reverseLinked ? COLOR_ACCENT : COLOR_TEXT);

  tft.fillRoundRect(MIX_MINUS_X, MIX_ROW_RATE_Y, MIX_ADJUST_W, MIX_FIELD_H, 8, COLOR_ACCENT);
  tft.drawRoundRect(MIX_MINUS_X, MIX_ROW_RATE_Y, MIX_ADJUST_W, MIX_FIELD_H, 8, COLOR_ACCENT_HI);
  if (dpadFocusVisible && focusIndex == 9) {
    tft.drawRoundRect(MIX_MINUS_X - 2, MIX_ROW_RATE_Y - 2, MIX_ADJUST_W + 4, MIX_FIELD_H + 4, 8, COLOR_TEXT);
  }
  tft.setTextColor(COLOR_BG);
  tft.drawCentreString("-", MIX_MINUS_X + (MIX_ADJUST_W / 2), MIX_ROW_RATE_Y + 8, 2);

  tft.fillRoundRect(MIX_VALUE_X, MIX_ROW_RATE_Y, MIX_VALUE_W, MIX_FIELD_H, 8, COLOR_PANEL);
  tft.drawRoundRect(MIX_VALUE_X, MIX_ROW_RATE_Y, MIX_VALUE_W, MIX_FIELD_H, 8, COLOR_ACCENT);
  tft.setTextColor(COLOR_TEXT);
  snprintf(valueText, sizeof(valueText), "%d", mix.rate);
  tft.drawCentreString(valueText, MIX_VALUE_X + (MIX_VALUE_W / 2), MIX_ROW_RATE_Y + 8, 2);
  if (mixNumpadActive && mixNumpadEditingRate) {
    tft.drawRoundRect(MIX_VALUE_X - 2, MIX_ROW_RATE_Y - 2, MIX_VALUE_W + 4, MIX_FIELD_H + 4, 8, COLOR_ACCENT_HI);
  }
  else if (dpadFocusVisible && focusIndex == 10) {
    tft.drawRoundRect(MIX_VALUE_X - 2, MIX_ROW_RATE_Y - 2, MIX_VALUE_W + 4, MIX_FIELD_H + 4, 8, COLOR_TEXT);
  }

  tft.fillRoundRect(MIX_PLUS_X, MIX_ROW_RATE_Y, MIX_ADJUST_W, MIX_FIELD_H, 8, COLOR_ACCENT);
  tft.drawRoundRect(MIX_PLUS_X, MIX_ROW_RATE_Y, MIX_ADJUST_W, MIX_FIELD_H, 8, COLOR_ACCENT_HI);
  if (dpadFocusVisible && focusIndex == 11) {
    tft.drawRoundRect(MIX_PLUS_X - 2, MIX_ROW_RATE_Y - 2, MIX_ADJUST_W + 4, MIX_FIELD_H + 4, 8, COLOR_TEXT);
  }
  tft.setTextColor(COLOR_BG);
  tft.drawCentreString("+", MIX_PLUS_X + (MIX_ADJUST_W / 2), MIX_ROW_RATE_Y + 8, 2);

  tft.fillRoundRect(MIX_MINUS_X, MIX_ROW_OFFS_Y, MIX_ADJUST_W, MIX_FIELD_H, 8, COLOR_ACCENT);
  tft.drawRoundRect(MIX_MINUS_X, MIX_ROW_OFFS_Y, MIX_ADJUST_W, MIX_FIELD_H, 8, COLOR_ACCENT_HI);
  if (dpadFocusVisible && focusIndex == 12) {
    tft.drawRoundRect(MIX_MINUS_X - 2, MIX_ROW_OFFS_Y - 2, MIX_ADJUST_W + 4, MIX_FIELD_H + 4, 8, COLOR_TEXT);
  }
  tft.setTextColor(COLOR_BG);
  tft.drawCentreString("-", MIX_MINUS_X + (MIX_ADJUST_W / 2), MIX_ROW_OFFS_Y + 8, 2);

  tft.fillRoundRect(MIX_VALUE_X, MIX_ROW_OFFS_Y, MIX_VALUE_W, MIX_FIELD_H, 8, COLOR_PANEL);
  tft.drawRoundRect(MIX_VALUE_X, MIX_ROW_OFFS_Y, MIX_VALUE_W, MIX_FIELD_H, 8, COLOR_ACCENT);
  tft.setTextColor(COLOR_TEXT);
  snprintf(valueText, sizeof(valueText), "%d", mix.offset);
  tft.drawCentreString(valueText, MIX_VALUE_X + (MIX_VALUE_W / 2), MIX_ROW_OFFS_Y + 8, 2);
  if (mixNumpadActive && !mixNumpadEditingRate) {
    tft.drawRoundRect(MIX_VALUE_X - 2, MIX_ROW_OFFS_Y - 2, MIX_VALUE_W + 4, MIX_FIELD_H + 4, 8, COLOR_ACCENT_HI);
  }
  else if (dpadFocusVisible && focusIndex == 13) {
    tft.drawRoundRect(MIX_VALUE_X - 2, MIX_ROW_OFFS_Y - 2, MIX_VALUE_W + 4, MIX_FIELD_H + 4, 8, COLOR_TEXT);
  }

  tft.fillRoundRect(MIX_PLUS_X, MIX_ROW_OFFS_Y, MIX_ADJUST_W, MIX_FIELD_H, 8, COLOR_ACCENT);
  tft.drawRoundRect(MIX_PLUS_X, MIX_ROW_OFFS_Y, MIX_ADJUST_W, MIX_FIELD_H, 8, COLOR_ACCENT_HI);
  if (dpadFocusVisible && focusIndex == 14) {
    tft.drawRoundRect(MIX_PLUS_X - 2, MIX_ROW_OFFS_Y - 2, MIX_ADJUST_W + 4, MIX_FIELD_H + 4, 8, COLOR_TEXT);
  }
  tft.setTextColor(COLOR_BG);
  tft.drawCentreString("+", MIX_PLUS_X + (MIX_ADJUST_W / 2), MIX_ROW_OFFS_Y + 8, 2);

  mixingNeedsRedraw = false;
}

uint16_t tintRgb565Pixel(uint16_t srcColor, uint16_t tintColor) {
  uint8_t srcR = (uint8_t)((srcColor >> 11) & 0x1F);
  uint8_t srcG = (uint8_t)((srcColor >> 5) & 0x3F);
  uint8_t srcB = (uint8_t)(srcColor & 0x1F);

  uint16_t srcR8 = (uint16_t)((srcR * 255U) / 31U);
  uint16_t srcG8 = (uint16_t)((srcG * 255U) / 63U);
  uint16_t srcB8 = (uint16_t)((srcB * 255U) / 31U);

  uint16_t luminance = (uint16_t)((srcR8 * 2126U + srcG8 * 7152U + srcB8 * 722U) / 10000U);

  uint8_t tintR = (uint8_t)((tintColor >> 11) & 0x1F);
  uint8_t tintG = (uint8_t)((tintColor >> 5) & 0x3F);
  uint8_t tintB = (uint8_t)(tintColor & 0x1F);

  uint8_t outR = (uint8_t)((tintR * luminance) / 255U);
  uint8_t outG = (uint8_t)((tintG * luminance) / 255U);
  uint8_t outB = (uint8_t)((tintB * luminance) / 255U);

  return (uint16_t)((outR << 11) | (outG << 5) | outB);
}

void drawRgb565IconTinted(int x, int y, int w, int h, const uint16_t* iconData, int iconW, int iconH, uint16_t tintColor) {
  if (iconData == nullptr || iconW <= 0 || iconH <= 0 || w <= 0 || h <= 0) {
    return;
  }

  for (int dy = 0; dy < h; ++dy) {
    int sy = (dy * iconH) / h;
    int srcRow = sy * iconW;
    int py = y + dy;

    for (int dx = 0; dx < w; ++dx) {
      int sx = (dx * iconW) / w;
      uint16_t srcColor = iconData[srcRow + sx];

      if (srcColor == ICON_TRANSPARENT_RGB565) {
        continue;
      }

      tft.drawPixel(x + dx, py, tintRgb565Pixel(srcColor, tintColor));
    }
  }
}

// ==== CONTROLLER ICON ====
// Small icon wrappers. RGB565 assets are tinted at draw time so a single icon
// set can match dark/light theme palettes.
void drawControllerIcon(int x, int y, int s, uint16_t iconColor) {
  int size = 24 * s;
  drawRgb565IconTinted(x, y, size, size, icon_CONTROLLER, ICON_CONTROLLER_W, ICON_CONTROLLER_H, iconColor);
}

// ==== BACK ICON ====
void drawBackIcon(int x, int y, int s, uint16_t iconColor) {
  int size = 18 * s;
  drawRgb565IconTinted(x, y, size, size, icon_BACK, ICON_BACK_W, ICON_BACK_H, iconColor);
}

// ==== MODEL ICON ====
void drawCarIcon(int x, int y, int s, uint16_t iconColor) {
  int size = 24 * s;
  drawRgb565IconTinted(x, y, size, size, icon_CAR, ICON_CAR_W, ICON_CAR_H, iconColor);
}

// ==== HOME ICON ====
void drawHomeIcon(int x, int y, int s, uint16_t iconColor) {
  int size = 18 * s;
  drawRgb565IconTinted(x, y, size, size, icon_HOME, ICON_HOME_W, ICON_HOME_H, iconColor);
}
// ==== REVERSE ICON ====
void drawReverseIcon(int x, int y, int s, uint16_t iconColor) {
  int size = 24 * s;
  drawRgb565IconTinted(x, y, size, size, icon_REVERSE, ICON_REVERSE_W, ICON_REVERSE_H, iconColor);
}

// ==== TRIM ICON ====
void drawTrimIcon(int x, int y, int s, uint16_t iconColor) {
  int size = 24 * s;
  drawRgb565IconTinted(x, y, size, size, icon_TRIM, ICON_TRIM_W, ICON_TRIM_H, iconColor);
}

// ==== FAILSAFE ICON ====
void drawFailsafeIcon(int x, int y, int s, uint16_t iconColor) {
  int size = 24 * s;
  drawRgb565IconTinted(x, y, size, size, icon_FAILSAFE, ICON_FAILSAFE_W, ICON_FAILSAFE_H, iconColor);
}

// ==== ENDPOINT ICON ====
void drawEndpointIcon(int x, int y, int s, uint16_t iconColor) {
  int size = 24 * s;
  drawRgb565IconTinted(x, y, size, size, icon_ENDPOINT, ICON_ENDPOINT_W, ICON_ENDPOINT_H, iconColor);
}

void drawExpoIcon(int x, int y, int s, uint16_t iconColor) {
  int left = x + 2*s;
  int right = x + 22*s;
  int top = y + 2*s;
  int bottom = y + 22*s;
  int midX = (left + right) / 2;
  int midY = (top + bottom) / 2;

  drawThickLine(left, bottom, right, bottom, iconColor);
  drawThickLine(left, top, left, bottom, iconColor);

  tft.drawLine(midX, top + 2*s, midX, bottom - 1, iconColor);
  tft.drawLine(left + 2*s, midY, right - 1, midY, iconColor);

  drawThickLine(left + 3*s, bottom - 3*s, left + 8*s, bottom - 8*s, iconColor);
  drawThickLine(left + 8*s, bottom - 8*s, left + 13*s, bottom - 11*s, iconColor);
  drawThickLine(left + 13*s, bottom - 11*s, left + 18*s, top + 5*s, iconColor);
}

void drawRatesIcon(int x, int y, int s, uint16_t iconColor) {
  int baseSize = 24 * s;
  int size = (baseSize * 9) / 10;
  int offset = (baseSize - size) / 2;
  drawRgb565IconTinted(x + offset, y + offset, size, size, icon_RATES, ICON_RATES_W, ICON_RATES_H, iconColor);
}

void drawProtocolIcon(int x, int y, int s, uint16_t iconColor) {
  int baseSize = 24 * s;
  int size = (baseSize * 9) / 10;
  int offset = (baseSize - size) / 2;
  drawRgb565IconTinted(x + offset, y + offset, size, size, icon_PROTOCOL, ICON_PROTOCOL_W, ICON_PROTOCOL_H, iconColor);
}

// ==== MODEL NAME ICON ====
void drawModelNameIcon(int x, int y, int s, uint16_t iconColor) {
  int size = 24 * s;
  drawRgb565IconTinted(x, y, size, size, icon_MODEL_NAME, ICON_MODEL_NAME_W, ICON_MODEL_NAME_H, iconColor);
}

// ==== DRIVE TYPE ICON ====
void drawDriveTypeIcon(int x, int y, int s, uint16_t iconColor) {

  int size = 24 * s;

  drawTankIcon(x, y, size, size, iconColor);
}

// ==== MIXING ICON ====
void drawMixingIcon(int x, int y, int s, uint16_t iconColor) {
  int size = 24 * s;
  drawRgb565IconTinted(x, y, size, size, icon_MIXING, ICON_MIXING_W, ICON_MIXING_H, iconColor);
}
