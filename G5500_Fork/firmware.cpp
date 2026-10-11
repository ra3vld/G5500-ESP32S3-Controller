/* Fork modifications by RA3VLD, 2026-10-12. GPLv3; see docs/upstream.md. */
/*
  G5500 ESP32-S3 Controller v1.6.23 RECONNECT SAFE TFT + XPT2046 TOUCH CAL + HC-05
  Copyright (c) UA1CFM
  Added:
    - PSTROTATOR / Yaesu GS-232B serial command handling
    - RGB activity indication:
        GREEN = RX command received from PSTROTATOR or Look4Sat/TCP
        RED   = TX response sent to PSTROTATOR or Look4Sat/TCP
        BLUE  = short heartbeat every 2 seconds
    - Non-blocking LED timing (millis), no delay used for RGB indication
    - Multi-control:
        UART0 USB = PSTROTATOR / GS-232 (Windows COM number may vary)
        TCP 4533 = Look4Sat / Hamlib rotctld + GS-232B
        Web UI remains available

  Default onboard addressable RGB LED pin: GPIO48.
  If the selected ESP32-S3 board defines RGB_BUILTIN, that definition is used.
*/


#include <time.h>
#include "esp_system.h"
#include <Arduino.h>
#if ARDUINO_USB_MODE
  #define FORK_NATIVE_USB 1
  #include <HWCDC.h>
  #if !ARDUINO_USB_CDC_ON_BOOT
    HWCDC HWCDCSerial;
  #endif
#elif ARDUINO_USB_CDC_ON_BOOT
  #define FORK_NATIVE_USB 1
  #include <USB.h>
  static auto& HWCDCSerial = Serial;
#else
  #define FORK_NATIVE_USB 0
#endif
#if CONFIG_IDF_TARGET_ESP32S2
  #define FORK_CHIP_NAME "ESP32-S2"
  #define FORK_USB_NAME "CDC"
#else
  #define FORK_CHIP_NAME "ESP32-S3"
  #define FORK_USB_NAME "Serial/JTAG"
#endif
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <WiFiUdp.h>
#include "api.hpp"
#include "firmware.hpp"

static forkrot::Controller controller;
static forkrot::Protocol protocol(controller);
static forkrot::Settings loggerSettings;
static forkrot::Button confirmButton;
static const int PIN_CONFIRM=0, PIN_SPEED=21;
static WiFiUDP n1mmUdp, pstUdp;
static bool pwmReady=false;
static void applyOutputs();
static void stopAzimuth();
static bool readTouchXY(int &x,int &y);
static bool hit(int x,int y,int bx,int by,int bw,int bh);
static void saveForkSettings();
static void loadForkSettings();
static void serviceLoggerUdp();
static void setupLoggerUdp();
static void loadProfilePresets();
static void saveProfilePresets();
static const char* calNamespace(){return controller.model==forkrot::Model::G800DXA?"g800cal":"g5500";}
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include "cat_image.h"

static const int PIN_AZ_ADC=4, PIN_EL_ADC=5;

// ILI9341 320x240 + XPT2046, shared SPI bus.
// TFT: MOSI=10, SCK=11, CS=12, DC=13, RST=14.
// TOUCH: MISO=2, CS=8, IRQ=9; shares MOSI/SCK with TFT.
static const int PIN_TFT_MOSI=10;
static const int PIN_TFT_SCK =11;
static const int PIN_TFT_MISO=2;
static const int PIN_TFT_CS  =12;
static const int PIN_TFT_DC  =13;
static const int PIN_TFT_RST =14;
static const int PIN_TOUCH_CS=8;
static const int PIN_TOUCH_IRQ=9;

// Default XPT2046 raw calibration for common 2.8" modules.
// If a particular panel is shifted, only these four limits need correction.
static const int TOUCH_RAW_X_MIN=240;
static const int TOUCH_RAW_X_MAX=3850;
static const int TOUCH_RAW_Y_MIN=240;
static const int TOUCH_RAW_Y_MAX=3850;

static bool touchCalValid=false;
static bool touchSwapXY=true;
static int touchX0Raw=TOUCH_RAW_Y_MIN;
static int touchX1Raw=TOUCH_RAW_Y_MAX;
static int touchY0Raw=TOUCH_RAW_X_MAX;
static int touchY1Raw=TOUCH_RAW_X_MIN;

// HC-05 Bluetooth Classic module
// TXD -> GPIO6 (ESP RX), RXD <- GPIO7 (ESP TX), STATE -> GPIO3
static const int PIN_HC05_STATE=3;
static const int PIN_HC05_RX=6;
static const int PIN_HC05_TX=7;
static const uint32_t HC05_BAUD=9600;

static const int PIN_LEFT=15, PIN_RIGHT=16, PIN_UP=17, PIN_DOWN=18;

#if CONFIG_IDF_TARGET_ESP32S2
// S2 has no GPIO48; common S2 RGB GPIO18 conflicts with the EL output.
static const int PIN_RGB = -1;
#elif defined(RGB_BUILTIN)
static const int PIN_RGB = RGB_BUILTIN;
#else
static const int PIN_RGB = 48;   // Common onboard WS2812/NeoPixel pin on ESP32-S3 DevKit boards
#endif

static const uint8_t RGB_LEVEL = 5;       // LED brightness 0..255
static const uint32_t RGB_FLASH_MS = 100;   // TX/RX pulse duration
static const uint32_t RGB_HEARTBEAT_MS = 2000;
static const uint32_t RGB_HEARTBEAT_ON_MS = 100;

static const uint16_t TCP_PORT=4533;
static const char* AP_SSID="G5500-Rotator";
static const char* AP_PASS="";  // default: open AP; set 8..63 char password from Web UI

Adafruit_ILI9341 tft(&SPI, PIN_TFT_DC, PIN_TFT_CS, PIN_TFT_RST);
XPT2046_Touchscreen touch(PIN_TOUCH_CS, PIN_TOUCH_IRQ);
Preferences prefs;
WebServer web(80);
WiFiServer tcpServer(TCP_PORT);
WiFiClient rotctldClient;
String rotctldLine;

struct Calibration {
  // GS-232 two-point AZ calibration is intentionally separate for P36 and P45.
  int az36_0=0, az36Max=3100;
  int az45_0=0, az45Max=3100;
  // EL calibration is shared because the elevation range does not depend on P36/P45.
  int el0=0, elMax=3100;
  bool valid=false;
} cal;

// Exactly two calibration algorithms:
//   0 = GS-232 two-point calibration (O/O2/F/F2)
//   1 = multi-point piecewise-linear calibration
// Within each algorithm AZ has TWO independent data sets: P36 (0..360) and P45 (0..450).
enum CalibrationMode : uint8_t { CAL_GS232_2POINT=0, CAL_MULTI_POINT=1 };
CalibrationMode calibrationMode=CAL_GS232_2POINT;

static const uint8_t AZ36_CAL_N=5, AZ45_CAL_N=6, EL_CAL_N=5;
static const float az36CalDeg[AZ36_CAL_N]={0,90,180,270,360};
static const float az45CalDeg[AZ45_CAL_N]={0,90,180,270,360,450};
static const float elCalDeg[EL_CAL_N]={0,45,90,135,180};

int az36CalMv[AZ36_CAL_N]={-1,-1,-1,-1,-1};
int az45CalMv[AZ45_CAL_N]={-1,-1,-1,-1,-1,-1};
int elCalMv[EL_CAL_N]={-1,-1,-1,-1,-1};

bool az36MultiValid=false, az45MultiValid=false, elMultiValid=false;
bool az36LinearValid=false, az45LinearValid=false, elLinearValid=false;

// Explicit two-point endpoint flags.
bool az36OSet=false, az36FSet=false;
bool az45OSet=false, az45FSet=false;
bool elOSet=false, elFSet=false;

String wifiSsid,wifiPass,apPass;
float currentAz=0,currentEl=0,targetAz=0,targetEl=0;
bool targetValid=false;          // no movement until at least one real target has been received
bool azTrack=false;              // LVBTrack-style axis tracking flag
bool elTrack=false;              // LVBTrack-style axis tracking flag
uint32_t lastTargetRxMs=0;       // diagnostics only; never used to stop motion

// GS-232B timed sequence support (TCP). Kept compact to save RAM.
static const uint8_t GSSEQ_MAX=16;
float gsSeqAz[GSSEQ_MAX], gsSeqEl[GSSEQ_MAX];
uint8_t gsSeqCount=0, gsSeqIndex=0;
uint16_t gsSeqIntervalSec=0;
bool gsSeqHasEl=false, gsSeqActive=false;
uint32_t gsSeqNextMs=0;
uint8_t gsAzMode=45;       // 45 = 450-degree mode, 36 = 360-degree mode
uint8_t gsSpeed=4;          // X1..X4 accepted; relay hardware has fixed speed

static const int8_t RSSI_EMPTY = 127;
int8_t rssiHour[60], rssiDay[288], rssiWeek[336], rssiMonth[360], rssiYear[365];
uint16_t rssiHourPos=0,rssiHourCount=0,rssiDayPos=0,rssiDayCount=0;
uint16_t rssiWeekPos=0,rssiWeekCount=0,rssiMonthPos=0,rssiMonthCount=0,rssiYearPos=0,rssiYearCount=0;
int32_t acc1m=0,acc5m=0,acc30m=0,acc2h=0,acc24h=0;
uint16_t cnt1m=0,cnt5m=0,cnt30m=0,cnt2h=0,cnt24h=0;
uint32_t rssiLastSampleMs=0,rssiT1m=0,rssiT5m=0,rssiT30m=0,rssiT2h=0,rssiT24h=0;
static const uint32_t RSSI_SAMPLE_MS=10000UL;
static const uint32_t WIFI_RECONNECT_MS=30000UL;       // wait between reconnect attempts
static const uint32_t WIFI_RECONNECT_TIMEOUT_MS=8000UL; // one STA attempt timeout
static uint32_t wifiReconnectMs=0;
static uint32_t wifiReconnectTryStartedMs=0;
static bool wifiReconnectTryActive=false;

uint32_t lastTargetCommandMs=0,lastDisplayMs=0;

String serialCmd;
String hc05Cmd;
HardwareSerial HC05Serial(1);
enum GsReplyPort : uint8_t { GS_REPLY_USB=0, GS_REPLY_HC05=1 };
static GsReplyPort gsReplyPort=GS_REPLY_USB;
enum RgbState : uint8_t { RGB_IDLE, RGB_RX, RGB_TX, RGB_HEARTBEAT };
RgbState rgbState = RGB_IDLE;
uint32_t rgbUntilMs = 0;
uint32_t lastHeartbeatMs = 0;
bool pendingTxFlash = false;

enum TftPage : uint8_t {
  TFT_PAGE_DASH=0,
  TFT_PAGE_CAL_HOME=1,
  TFT_PAGE_CAL_AZ=2,
  TFT_PAGE_CAL_EL=3,
  TFT_PAGE_TOUCH_CAL=4,
  TFT_PAGE_IMAGE=5
};
static TftPage tftPage=TFT_PAGE_DASH;
static uint32_t lastTouchMs=0;
static uint32_t imagePageStartedMs=0;
static const uint32_t IMAGE_PAGE_DURATION_MS=3000;
static bool tftNeedsRedraw=true;

// Dashboard anti-flicker cache.
// Dynamic regions are repainted only when their visible value actually changes.
static float dashLastAz=-9999.0f, dashLastEl=-9999.0f;
static float dashLastTargetAz=-9999.0f, dashLastTargetEl=-9999.0f;
static bool dashLastTargetValid=false;
static int dashLastAzBar=-1, dashLastElBar=-1;
static String dashLastAzState="", dashLastElState="";
static String dashLastCal="", dashLastWifi="", dashLastBt="", dashLastIp="", dashLastTouch="";
static bool dashLastTcpConnected=false;
static uint8_t touchCalStep=0;
static int touchCalRawX[4]={0,0,0,0};
static int touchCalRawY[4]={0,0,0,0};
static bool touchWaitRelease=false;
static uint32_t touchHoldStartMs=0;
static bool touchHoldArmed=false;

static void saveCal();
static void updatePosition();
static void dbgPrint(const String &s);
static void dbgPrintln(const String &s);
static void processGs232Command(String cmd);
static void captureGs232Cal(const String &cmd);
static void initLocalDisplay();
static void updateLocalDisplay();
static void serviceTouch();
static void drawTftPage();
static void loadTouchCalibration();
static void saveTouchCalibration();
static void startTouchCalibration();
static void serviceTouchCalibration();
static void serviceTouchRecoveryHold();
static void showHeaderImage();
static void serviceHeaderImage();
static void serviceWifiReconnect();

static float clampf(float v,float lo,float hi){ return v<lo?lo:(v>hi?hi:v); }
static int adcAvgMv(int pin){ uint32_t s=0; for(int i=0;i<25;i++){s+=analogReadMilliVolts(pin);delayMicroseconds(150);} return s/25; }

// ADC diagnostic only. This DOES NOT change the position calculation.
static void adcDiagRead(int pin,int &raw,int &mv){
  uint32_t sr=0, sm=0;
  for(int i=0;i<25;i++){
    sr += analogRead(pin);
    sm += analogReadMilliVolts(pin);
    delayMicroseconds(150);
  }
  raw=(int)(sr/25);
  mv=(int)(sm/25);
}
static float mapCal(int raw,int a,int b,float maxd){ if(a==b)return 0; return clampf((raw-a)*maxd/(float)(b-a),0,maxd); }

static bool calPointsMonotonic(const int *adc,uint8_t n){
  if(n<2) return false;
  for(uint8_t i=0;i<n;i++) if(adc[i]<0) return false;
  bool inc=adc[n-1]>adc[0];
  if(adc[n-1]==adc[0]) return false;
  for(uint8_t i=1;i<n;i++){
    if(inc && adc[i]<=adc[i-1]) return false;
    if(!inc && adc[i]>=adc[i-1]) return false;
  }
  return true;
}
static float mapPiecewise(int raw,const int *adc,const float *deg,uint8_t n){
  if(!calPointsMonotonic(adc,n)) return 0.0f;
  bool inc=adc[n-1]>adc[0];
  if((inc && raw<=adc[0]) || (!inc && raw>=adc[0])) return deg[0];
  if((inc && raw>=adc[n-1]) || (!inc && raw<=adc[n-1])) return deg[n-1];
  for(uint8_t i=0;i<n-1;i++){
    bool inside=inc ? (raw>=adc[i] && raw<=adc[i+1]) : (raw<=adc[i] && raw>=adc[i+1]);
    if(inside){
      float t=(raw-adc[i])/(float)(adc[i+1]-adc[i]);
      return deg[i] + t*(deg[i+1]-deg[i]);
    }
  }
  return deg[n-1];
}

static bool activeAzLinearValid(){ return gsAzMode==36 ? az36LinearValid : az45LinearValid; }
static bool activeAzMultiValid(){ return gsAzMode==36 ? az36MultiValid : az45MultiValid; }
static int activeAz2Zero(){ return gsAzMode==36 ? cal.az36_0 : cal.az45_0; }
static int activeAz2Full(){ return gsAzMode==36 ? cal.az36Max : cal.az45Max; }
static bool activeAzOSet(){ return gsAzMode==36 ? az36OSet : az45OSet; }
static bool activeAzFSet(){ return gsAzMode==36 ? az36FSet : az45FSet; }
static float activeAzMaxDeg(){ return gsAzMode==36 ? 360.0f : 450.0f; }

static void refreshMultiCalValidity(){
  az36MultiValid=calPointsMonotonic(az36CalMv,AZ36_CAL_N);
  az45MultiValid=calPointsMonotonic(az45CalMv,AZ45_CAL_N);
  elMultiValid=calPointsMonotonic(elCalMv,EL_CAL_N);
}

static bool selectedCalibrationValid(){
  bool azOk=calibrationMode==CAL_MULTI_POINT?activeAzMultiValid():activeAzLinearValid();
  bool elOk=calibrationMode==CAL_MULTI_POINT?elMultiValid:elLinearValid;
  return azOk && (controller.model==forkrot::Model::G800DXA || elOk);
}

static const char* calibrationModeName(){
  return calibrationMode==CAL_MULTI_POINT ? "MULTI-POINT" : "GS232 2-POINT";
}

static void setCalibrationMode(CalibrationMode m){
  calibrationMode=m;
  prefs.begin(calNamespace(),false);
  prefs.putUChar("calmode",(uint8_t)calibrationMode);
  prefs.end();
}

static void setGsAzMode(uint8_t m){
  if(m!=36 && m!=45) return;

  // Repeating the already selected range must not interrupt tracking.
  if(m==gsAzMode){
    prefs.begin(calNamespace(),false);
    prefs.putUChar("azmode",gsAzMode);
    prefs.end();
    cal.valid=selectedCalibrationValid();

    // LIVE P36/P45 TFT indication:
    // even a repeated GS-232 range command is confirmed on the display.
    tftNeedsRedraw=true;
    return;
  }

  // SAFETY: prevent an unreachable old target after 450 -> 360 switch.
  stopAzimuth();
  gsSeqActive=false;

  controller.setMaximum(m==36?360:450);
  gsAzMode=m;
  prefs.begin(calNamespace(),false);
  prefs.putUChar("azmode",gsAzMode);
  prefs.end();

  updatePosition();
  targetAz=currentAz; // cancel stale AZ target without disturbing EL target
  cal.valid=selectedCalibrationValid();

  // P36/P45 is part of the static dashboard, so force immediate redraw.
  // Result: received P36 -> display P36, received P45 -> display P45.
  tftNeedsRedraw=true;

  dbgPrint("[AZ MODE] P");
  dbgPrint(String(gsAzMode));
  dbgPrintln(" selected; AZ stopped and stale AZ target cancelled");
}

static void captureGs232Cal(const String &cmd){
  if(controller.model==forkrot::Model::G800DXA && (cmd=="O2"||cmd=="F2"))return;

  int az=adcAvgMv(PIN_AZ_ADC);
  int el=adcAvgMv(PIN_EL_ADC);

  if(cmd=="O"){
    if(gsAzMode==36){ cal.az36_0=az; az36OSet=true; }
    else            { cal.az45_0=az; az45OSet=true; }
  }else if(cmd=="F"){
    if(gsAzMode==36){ cal.az36Max=az; az36FSet=true; }
    else            { cal.az45Max=az; az45FSet=true; }
  }else if(cmd=="O2"){
    cal.el0=el; elOSet=true;
  }else if(cmd=="F2"){
    cal.elMax=el; elFSet=true;
  }else{
    return;
  }

  az36LinearValid=(az36OSet && az36FSet && abs(cal.az36Max-cal.az36_0)>100);
  az45LinearValid=(az45OSet && az45FSet && abs(cal.az45Max-cal.az45_0)>100);
  elLinearValid=(elOSet && elFSet && abs(cal.elMax-cal.el0)>100);
  cal.valid=selectedCalibrationValid();
  saveCal();

  dbgPrint("[CAL GS232] "); dbgPrint(cmd);
  dbgPrint(" P"); dbgPrint(String(gsAzMode));
  dbgPrint(" AZ0="); dbgPrint(String(activeAz2Zero()));
  dbgPrint(" AZFULL="); dbgPrint(String(activeAz2Full()));
  dbgPrint(" EL0="); dbgPrint(String(cal.el0));
  dbgPrint(" EL180="); dbgPrintln(String(cal.elMax));
}

// -----------------------------------------------------------------------------
// Web diagnostic console.
// The same diagnostic stream that is written to Native USB Serial/JTAG is
// mirrored into a small volatile RAM ring buffer for the /diaglog web page.
// -----------------------------------------------------------------------------
static const uint16_t DIAG_MAX_LINES = 160;
static const uint16_t DIAG_MAX_LINE_LEN = 220;
String diagLines[DIAG_MAX_LINES];
uint16_t diagHead = 0;
uint16_t diagCount = 0;
String diagPending;

static void diagStoreLine(String line){
  line.replace("\r","");
  if(line.length() > DIAG_MAX_LINE_LEN){
    line = line.substring(0,DIAG_MAX_LINE_LEN-3) + "...";
  }
  diagLines[diagHead] = line;
  diagHead = (diagHead + 1) % DIAG_MAX_LINES;
  if(diagCount < DIAG_MAX_LINES) diagCount++;
}

static void diagMirror(const String &s,bool newline){
  for(size_t i=0;i<s.length();i++){
    char ch=s[i];
    if(ch=='\r') continue;
    if(ch=='\n'){
      diagStoreLine(diagPending);
      diagPending="";
    }else{
      diagPending += ch;
      if(diagPending.length() >= DIAG_MAX_LINE_LEN){
        diagStoreLine(diagPending);
        diagPending="";
      }
    }
  }
  if(newline){
    diagStoreLine(diagPending);
    diagPending="";
  }
}

static void diagClear(){
  for(uint16_t i=0;i<DIAG_MAX_LINES;i++) diagLines[i]="";
  diagHead=0;
  diagCount=0;
  diagPending="";
}

static String diagSnapshot(){
  String out;
  out.reserve(8192);
  uint16_t start=(diagHead + DIAG_MAX_LINES - diagCount) % DIAG_MAX_LINES;
  for(uint16_t i=0;i<diagCount;i++){
    uint16_t idx=(start+i)%DIAG_MAX_LINES;
    out += diagLines[idx];
    out += '\n';
  }
  if(diagPending.length()) out += diagPending;
  return out;
}

static void dbgPrint(const String &s){
  diagMirror(s,false);
#if FORK_NATIVE_USB
  if(HWCDCSerial) HWCDCSerial.print(s);
#endif
}
static void dbgPrintln(const String &s){
  diagMirror(s,true);
#if FORK_NATIVE_USB
  if(HWCDCSerial) HWCDCSerial.println(s);
#endif
}
static void dbgPstRx(const String &s){
  dbgPrint("[PST RX] ");
  dbgPrintln(s);
}
static void dbgPstTx(const String &s){
  dbgPrint("[PST TX] ");
  String t=s;
  t.replace("\r","");
  t.replace("\n","");
  dbgPrintln(t);
}


static esp_reset_reason_t bootResetReason = ESP_RST_UNKNOWN;
static String bootResetReasonText = "UNKNOWN";

static String resetReasonToText(esp_reset_reason_t r){
  switch(r){
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_EXT:       return "EXTERNAL";
    case ESP_RST_SW:        return "SOFTWARE";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_SDIO:      return "SDIO";
    default:                return "UNKNOWN";
  }
}


static const uint8_t RESET_HISTORY_MAX = 20;

static bool syncClockFromNtp(uint32_t timeoutMs=3500){
  if(WiFi.status()!=WL_CONNECTED) return false;
  configTime(0,0,"pool.ntp.org","time.nist.gov");
  uint32_t t0=millis();
  time_t now=0;
  while(millis()-t0<timeoutMs){
    now=time(nullptr);
    if(now>1700000000) return true;  // sane Unix time
    delay(100);
  }
  return false;
}

static void addResetHistoryEntry(){
  prefs.begin("g5500rst",false);

  uint8_t head=prefs.getUChar("head",0);
  uint8_t count=prefs.getUChar("count",0);
  if(head>=RESET_HISTORY_MAX) head=0;
  if(count>RESET_HISTORY_MAX) count=RESET_HISTORY_MAX;

  time_t now=time(nullptr);
  uint64_t epoch=(now>1700000000) ? (uint64_t)now : 0ULL;

  char kt[8], kr[8], kc[8];
  snprintf(kt,sizeof(kt),"t%02u",head);
  snprintf(kr,sizeof(kr),"r%02u",head);
  snprintf(kc,sizeof(kc),"c%02u",head);

  prefs.putULong64(kt,epoch);
  prefs.putString(kr,bootResetReasonText);
  prefs.putInt(kc,(int)bootResetReason);

  head=(head+1)%RESET_HISTORY_MAX;
  if(count<RESET_HISTORY_MAX) count++;

  prefs.putUChar("head",head);
  prefs.putUChar("count",count);
  prefs.end();
}

static String resetHistoryJson(){
  prefs.begin("g5500rst",true);
  uint8_t head=prefs.getUChar("head",0);
  uint8_t count=prefs.getUChar("count",0);
  if(head>=RESET_HISTORY_MAX) head=0;
  if(count>RESET_HISTORY_MAX) count=RESET_HISTORY_MAX;

  String j="{\"count\":"+String(count)+",\"entries\":[";
  for(uint8_t n=0;n<count;n++){
    int idx=(int)head-1-(int)n;
    while(idx<0) idx+=RESET_HISTORY_MAX;

    char kt[8], kr[8], kc[8];
    snprintf(kt,sizeof(kt),"t%02d",idx);
    snprintf(kr,sizeof(kr),"r%02d",idx);
    snprintf(kc,sizeof(kc),"c%02d",idx);

    uint64_t epoch=prefs.getULong64(kt,0ULL);
    String reason=prefs.getString(kr,"UNKNOWN");
    int code=prefs.getInt(kc,0);

    if(n) j+=",";
    j+="{\"epoch\":"+String((unsigned long long)epoch)+
       ",\"reason\":\""+reason+"\",\"code\":"+String(code)+"}";
  }
  j+="]}";
  prefs.end();
  return j;
}

static void clearResetHistory(){
  prefs.begin("g5500rst",false);
  prefs.clear();
  prefs.end();
}

static void updatePosition();
static void allStop();
static void moveDir(char d);
static void setReferenceTarget(float az,float el,bool setAz,bool setEl);
static void refreshMultiCalValidity();

static void rgbSet(uint8_t r,uint8_t g,uint8_t b){
  if(PIN_RGB>=0) rgbLedWrite(PIN_RGB,r,g,b);
}
static void rgbStart(RgbState s){
  rgbState=s;
  if(s==RGB_RX){ rgbSet(0,RGB_LEVEL,0); rgbUntilMs=millis()+RGB_FLASH_MS; }
  else if(s==RGB_TX){ rgbSet(RGB_LEVEL,0,0); rgbUntilMs=millis()+RGB_FLASH_MS; }
  else if(s==RGB_HEARTBEAT){ rgbSet(0,0,RGB_LEVEL); rgbUntilMs=millis()+RGB_HEARTBEAT_ON_MS; }
  else { rgbSet(0,0,0); rgbUntilMs=0; }
}
static void flashRx(){
  // RX gets shown first. A TX response generated immediately afterwards is queued.
  rgbStart(RGB_RX);
}
static void flashTx(){
  if(rgbState==RGB_RX && (int32_t)(rgbUntilMs-millis())>0) pendingTxFlash=true;
  else rgbStart(RGB_TX);
}
static void updateRgb(){
  uint32_t now=millis();
  if(rgbState!=RGB_IDLE && (int32_t)(now-rgbUntilMs)>=0){
    rgbSet(0,0,0);
    rgbState=RGB_IDLE;
    if(pendingTxFlash){
      pendingTxFlash=false;
      rgbStart(RGB_TX);
      return;
    }
  }
  if(rgbState==RGB_IDLE && now-lastHeartbeatMs>=RGB_HEARTBEAT_MS){
    lastHeartbeatMs=now;
    rgbStart(RGB_HEARTBEAT);
  }
}
static void sendReply(const String &s){
  if(gsReplyPort==GS_REPLY_HC05){
    HC05Serial.print(s);
    dbgPrint("[HC05 TX] ");
    dbgPrintln(s);
  }else{
    Serial0.print(s);
    dbgPstTx(s);
  }
  flashTx();
}

static int roundedAz(){ return constrain((int)lroundf(currentAz),0,450); }
static int roundedEl(){ return constrain((int)lroundf(currentEl),0,180); }

static void stopAzimuth(){
  controller.stop(0);controller.tick(millis());applyOutputs();
}
static void stopElevation(){
  controller.stop(1);controller.tick(millis());applyOutputs();
}

static void processGs232Command(String cmd){
  updatePosition();
  auto packet=protocol.line(cmd.c_str(),millis());
  if(packet.matched){
    if(packet.result==forkrot::Result::Ok||packet.result==forkrot::Result::Pending){targetValid=true;lastTargetRxMs=millis();}
    controller.tick(millis());applyOutputs();
    if(!packet.reply.empty())sendReply(packet.reply.c_str());return;
  }

  cmd.trim();
  if(!cmd.length()) return;
  cmd.toUpperCase();
  flashRx();

  // GS-232 azimuth operating range. This also defines the full-scale angle
  // captured by F in the GS232 2-POINT calibration mode.
  if(cmd=="P36"){ setGsAzMode(36); return; }
  if(cmd=="P45"){ setGsAzMode(45); return; }

  // Standard GS-232 calibration commands.
  // O  = current AZ feedback becomes 0 degrees
  // F  = current AZ feedback becomes full scale (360 or 450, from P36/P45)
  // O2 = current EL feedback becomes 0 degrees
  // F2 = current EL feedback becomes 180 degrees
  if(cmd=="O" || cmd=="O2" || cmd=="F" || cmd=="F2"){
    captureGs232Cal(cmd);
    return;
  }

  // Basic help response. Z is intentionally not implemented.
  if(cmd=="H"){
    sendReply("GS-232B: R L A C M S O F P36 P45\r\n");
    return;
  }
  if(cmd=="H2"){
    sendReply("GS-232B: U D E C2 W S O2 F2 B\r\n");
    return;
  }
  if(cmd=="H3"){
    sendReply(String("CAL=")+calibrationModeName()+"\r\n");
    return;
  }
}
static void handlePstRotatorSerial(){
  static forkrot::LineBuffer receiver;std::string command;
  while(Serial0.available())if(receiver.feed((char)Serial0.read(),command)){
    gsReplyPort=GS_REPLY_USB;String cmd(command.c_str());dbgPstRx(cmd);processGs232Command(cmd);
  }
}


static bool hc05Connected(){
  return digitalRead(PIN_HC05_STATE)==HIGH;
}

static void handleHc05Serial(){
  static forkrot::LineBuffer receiver;std::string command;
  while(HC05Serial.available())if(receiver.feed((char)HC05Serial.read(),command)){
    gsReplyPort=GS_REPLY_HC05;String cmd(command.c_str());dbgPrint("[HC05 RX] ");dbgPrintln(cmd);processGs232Command(cmd);
  }
}


static void updatePosition(){
  // factory-calibrated ESP32-S3 millivolts -> selected mechanical calibration -> angle.
  // AZ table is selected by P36/P45; EL table is shared.
  int az=adcAvgMv(PIN_AZ_ADC), el=controller.model==forkrot::Model::G5500?adcAvgMv(PIN_EL_ADC):0;
  float azMax=activeAzMaxDeg();

  if(calibrationMode==CAL_MULTI_POINT){
    if(gsAzMode==36 && az36MultiValid) currentAz=mapPiecewise(az,az36CalMv,az36CalDeg,AZ36_CAL_N);
    else if(gsAzMode==45 && az45MultiValid) currentAz=mapPiecewise(az,az45CalMv,az45CalDeg,AZ45_CAL_N);
    else currentAz=clampf(az*azMax/3100.0f,0,azMax);

    if(elMultiValid) currentEl=mapPiecewise(el,elCalMv,elCalDeg,EL_CAL_N);
    else currentEl=clampf(el*180.0f/3100.0f,0,180);
  }else{
    if(activeAzLinearValid()) currentAz=mapCal(az,activeAz2Zero(),activeAz2Full(),azMax);
    else currentAz=clampf(az*azMax/3100.0f,0,azMax);

    if(elLinearValid) currentEl=mapCal(el,cal.el0,cal.elMax,180.0f);
    else currentEl=clampf(el*180.0f/3100.0f,0,180);
  }
  cal.valid=selectedCalibrationValid();
  if(controller.model==forkrot::Model::G800DXA)currentEl=0;
  controller.feedback(currentAz,currentEl,
    calibrationMode==CAL_MULTI_POINT?activeAzMultiValid():activeAzLinearValid(),
    calibrationMode==CAL_MULTI_POINT?elMultiValid:elLinearValid);
}
static void allStop(){
  controller.stop();controller.tick(millis());applyOutputs();
}
// Movement logic ported from the supplied LVBTrack.c reference.
// No global Tracking ON/OFF state:
// STOP cancels current motion only; the next valid target can start motion again.
// A new target chooses the direction once. The periodic update only stops
// the axis when the destination is reached or crossed.
static void setReferenceTarget(float az,float el,bool setAz,bool setEl){
  updatePosition();
  auto result=controller.target({az,el,setAz,setEl},true,millis());
  if(result==forkrot::Result::Ok||result==forkrot::Result::Pending){targetValid=true;lastTargetRxMs=millis();}
  controller.tick(millis());applyOutputs();
}

static void moveDir(char d){
  if(d=='S')controller.stop();
  else if(d=='A'||d=='E')controller.stop(d=='A'?0:1);
  else if(d=='L'||d=='R'||d=='U'||d=='D')controller.jog(d=='U'||d=='D'?1:0,d=='R'||d=='U'?1:-1,false,millis());
  controller.tick(millis());applyOutputs();
}
static String motion(){
  bool l=digitalRead(PIN_LEFT);
  bool r=digitalRead(PIN_RIGHT);
  bool u=digitalRead(PIN_UP);
  bool d=digitalRead(PIN_DOWN);

  if(u && r) return "UP+RIGHT";
  if(u && l) return "UP+LEFT";
  if(d && r) return "DOWN+RIGHT";
  if(d && l) return "DOWN+LEFT";
  if(l) return "LEFT";
  if(r) return "RIGHT";
  if(u) return "UP";
  if(d) return "DOWN";
  return "STOP";
}
static void controlRotator(){
  controller.tick(millis());
  applyOutputs();
}
static void initRssiHistory(){
  memset(rssiHour,RSSI_EMPTY,sizeof(rssiHour));
  memset(rssiDay,RSSI_EMPTY,sizeof(rssiDay));
  memset(rssiWeek,RSSI_EMPTY,sizeof(rssiWeek));
  memset(rssiMonth,RSSI_EMPTY,sizeof(rssiMonth));
  memset(rssiYear,RSSI_EMPTY,sizeof(rssiYear));
  prefs.begin("g5500rssi",true);
  size_t n=prefs.getBytesLength("year");
  if(n==sizeof(rssiYear)) prefs.getBytes("year",rssiYear,sizeof(rssiYear));
  rssiYearPos=prefs.getUShort("ypos",0);
  rssiYearCount=prefs.getUShort("ycnt",0);
  prefs.end();
  if(rssiYearPos>=365) rssiYearPos=0;
  if(rssiYearCount>365) rssiYearCount=365;
  uint32_t now=millis();
  rssiT1m=rssiT5m=rssiT30m=rssiT2h=rssiT24h=now;
}
static void rssiPush(int8_t *buf,uint16_t size,uint16_t &pos,uint16_t &count,int8_t v){
  buf[pos]=v; pos=(uint16_t)((pos+1)%size); if(count<size) count++;
}
static int8_t rssiAvg(int32_t sum,uint16_t cnt){
  if(!cnt) return RSSI_EMPTY;
  int v=(int)lroundf((float)sum/(float)cnt);
  if(v<-127)v=-127; if(v>0)v=0; return (int8_t)v;
}
static void persistRssiYear(){
  prefs.begin("g5500rssi",false);
  prefs.putBytes("year",rssiYear,sizeof(rssiYear));
  prefs.putUShort("ypos",rssiYearPos); prefs.putUShort("ycnt",rssiYearCount);
  prefs.end();
}
static void serviceRssiHistory(){
  uint32_t now=millis();
  if(now-rssiLastSampleMs<RSSI_SAMPLE_MS) return;
  rssiLastSampleMs=now;
  if(WiFi.status()==WL_CONNECTED){
    int r=WiFi.RSSI(); if(r<-127)r=-127; if(r>0)r=0;
    acc1m+=r;acc5m+=r;acc30m+=r;acc2h+=r;acc24h+=r;
    cnt1m++;cnt5m++;cnt30m++;cnt2h++;cnt24h++;
  }
  if(now-rssiT1m>=60000UL){rssiPush(rssiHour,60,rssiHourPos,rssiHourCount,rssiAvg(acc1m,cnt1m));acc1m=0;cnt1m=0;rssiT1m=now;}
  if(now-rssiT5m>=300000UL){rssiPush(rssiDay,288,rssiDayPos,rssiDayCount,rssiAvg(acc5m,cnt5m));acc5m=0;cnt5m=0;rssiT5m=now;}
  if(now-rssiT30m>=1800000UL){rssiPush(rssiWeek,336,rssiWeekPos,rssiWeekCount,rssiAvg(acc30m,cnt30m));acc30m=0;cnt30m=0;rssiT30m=now;}
  if(now-rssiT2h>=7200000UL){rssiPush(rssiMonth,360,rssiMonthPos,rssiMonthCount,rssiAvg(acc2h,cnt2h));acc2h=0;cnt2h=0;rssiT2h=now;}
  if(now-rssiT24h>=86400000UL){rssiPush(rssiYear,365,rssiYearPos,rssiYearCount,rssiAvg(acc24h,cnt24h));acc24h=0;cnt24h=0;rssiT24h=now;persistRssiYear();}
}
static String rssiHistoryJson(const int8_t *buf,uint16_t size,uint16_t pos,uint16_t count,const char *range){
  String j; j.reserve(2600); j="{\"range\":\"";j+=range;j+="\",\"values\":[";
  int minv=0,maxv=-127;long sum=0;uint16_t valid=0;uint16_t start=(count<size)?0:pos;
  for(uint16_t i=0;i<count;i++){uint16_t idx=(uint16_t)((start+i)%size);int8_t v=buf[idx];if(i)j+=',';if(v==RSSI_EMPTY)j+="null";else{j+=String((int)v);int iv=(int)v;if(!valid||iv<minv)minv=iv;if(!valid||iv>maxv)maxv=iv;sum+=iv;valid++;}}
  j+="],\"count\":"+String(count);
  if(valid)j+=",\"min\":"+String(minv)+",\"max\":"+String(maxv)+",\"avg\":"+String((float)sum/valid,1);
  else j+=",\"min\":null,\"max\":null,\"avg\":null";
  j+="}"; return j;
}

static void loadPrefs(){
  prefs.begin("g5500",true);
  wifiSsid=prefs.getString("ssid","");
  wifiPass=prefs.getString("pass","");
  apPass=prefs.getString("appass",AP_PASS);

  // v1.6.23 format 4: independent P36/P45 + explicit two-point endpoint flags.
  prefs.end();
  prefs.begin(calNamespace(),true);
  uint8_t calFmt=prefs.getUChar("calfmt",0);

  cal.az36_0=0; cal.az36Max=3100;
  cal.az45_0=0; cal.az45Max=3100;
  cal.el0=0; cal.elMax=3100;

  az36OSet=az36FSet=az45OSet=az45FSet=false;
  elOSet=elFSet=false;
  az36LinearValid=az45LinearValid=elLinearValid=false;

  for(uint8_t i=0;i<AZ36_CAL_N;i++) az36CalMv[i]=-1;
  for(uint8_t i=0;i<AZ45_CAL_N;i++) az45CalMv[i]=-1;
  for(uint8_t i=0;i<EL_CAL_N;i++) elCalMv[i]=-1;

  if(calFmt==4){
    cal.az36_0=prefs.getInt("a36z",0);      cal.az36Max=prefs.getInt("a36f",3100);
    cal.az45_0=prefs.getInt("a45z",0);      cal.az45Max=prefs.getInt("a45f",3100);
    cal.el0=prefs.getInt("el0mv",0);        cal.elMax=prefs.getInt("elmaxmv",3100);

    az36OSet=prefs.getBool("a36os",false);   az36FSet=prefs.getBool("a36fs",false);
    az45OSet=prefs.getBool("a45os",false);   az45FSet=prefs.getBool("a45fs",false);
    elOSet=prefs.getBool("elos",false);      elFSet=prefs.getBool("elfs",false);

    az36CalMv[0]=prefs.getInt("a36m0",-1);   az36CalMv[1]=prefs.getInt("a36m90",-1);
    az36CalMv[2]=prefs.getInt("a36m180",-1); az36CalMv[3]=prefs.getInt("a36m270",-1);
    az36CalMv[4]=prefs.getInt("a36m360",-1);

    az45CalMv[0]=prefs.getInt("a45m0",-1);   az45CalMv[1]=prefs.getInt("a45m90",-1);
    az45CalMv[2]=prefs.getInt("a45m180",-1); az45CalMv[3]=prefs.getInt("a45m270",-1);
    az45CalMv[4]=prefs.getInt("a45m360",-1); az45CalMv[5]=prefs.getInt("a45m450",-1);

    elCalMv[0]=prefs.getInt("elm0",-1);      elCalMv[1]=prefs.getInt("elm45",-1);
    elCalMv[2]=prefs.getInt("elm90",-1);     elCalMv[3]=prefs.getInt("elm135",-1);
    elCalMv[4]=prefs.getInt("elm180",-1);

  }else if(calFmt==3){
    // Migration from 1.5.58. Its per-endpoint state was unreliable.
    // Preserve measured 2-point values but force endpoints to be captured again.
    cal.az36_0=prefs.getInt("a36z",0);       cal.az36Max=prefs.getInt("a36f",3100);
    cal.az45_0=prefs.getInt("a45z",0);       cal.az45Max=prefs.getInt("a45f",3100);
    cal.el0=prefs.getInt("el0mv",0);         cal.elMax=prefs.getInt("elmaxmv",3100);

    // AZ MULTI was independent in 1.5.58 and is safe to preserve.
    az36CalMv[0]=prefs.getInt("a36m0",-1);   az36CalMv[1]=prefs.getInt("a36m90",-1);
    az36CalMv[2]=prefs.getInt("a36m180",-1); az36CalMv[3]=prefs.getInt("a36m270",-1);
    az36CalMv[4]=prefs.getInt("a36m360",-1);

    az45CalMv[0]=prefs.getInt("a45m0",-1);   az45CalMv[1]=prefs.getInt("a45m90",-1);
    az45CalMv[2]=prefs.getInt("a45m180",-1); az45CalMv[3]=prefs.getInt("a45m270",-1);
    az45CalMv[4]=prefs.getInt("a45m360",-1); az45CalMv[5]=prefs.getInt("a45m450",-1);

    // EL MULTI endpoints in 1.5.58 could be inherited from 2-point.
    // Keep only the unambiguous middle points; recapture 0° and 180°.
    elCalMv[0]=-1;
    elCalMv[1]=prefs.getInt("elm45",-1);
    elCalMv[2]=prefs.getInt("elm90",-1);
    elCalMv[3]=prefs.getInt("elm135",-1);
    elCalMv[4]=-1;

  }else if(calFmt==2){
    // Migration from the earlier single-AZ format with explicit flags when available.
    int oldAz0=prefs.getInt("az0mv",0);
    int oldAzMax=prefs.getInt("azmaxmv",3100);
    float oldMaxDeg=prefs.getFloat("az2maxdeg",450.0f);
    bool oldO=prefs.getBool("az2oset",false);
    bool oldF=prefs.getBool("az2fset",false);

    if(oldMaxDeg<400.0f){
      cal.az36_0=oldAz0; cal.az36Max=oldAzMax; az36OSet=oldO; az36FSet=oldF;
    }else{
      cal.az45_0=oldAz0; cal.az45Max=oldAzMax; az45OSet=oldO; az45FSet=oldF;
    }

    cal.el0=prefs.getInt("el0mv",0);
    cal.elMax=prefs.getInt("elmaxmv",3100);
    elOSet=prefs.getBool("el2oset",false);
    elFSet=prefs.getBool("el2fset",false);

    int oldAz[6];
    oldAz[0]=prefs.getInt("azm0",-1); oldAz[1]=prefs.getInt("azm90",-1);
    oldAz[2]=prefs.getInt("azm180",-1); oldAz[3]=prefs.getInt("azm270",-1);
    oldAz[4]=prefs.getInt("azm360",-1); oldAz[5]=prefs.getInt("azm450",-1);
    for(uint8_t i=0;i<AZ36_CAL_N;i++) az36CalMv[i]=oldAz[i];
    for(uint8_t i=0;i<AZ45_CAL_N;i++) az45CalMv[i]=oldAz[i];

    elCalMv[0]=prefs.getInt("elm0",-1);      elCalMv[1]=prefs.getInt("elm45",-1);
    elCalMv[2]=prefs.getInt("elm90",-1);     elCalMv[3]=prefs.getInt("elm135",-1);
    elCalMv[4]=prefs.getInt("elm180",-1);
  }

  uint8_t savedMode=prefs.getUChar("calmode",255);
  uint8_t savedAzMode=prefs.getUChar("azmode",45);
  prefs.end();

  az36LinearValid=(az36OSet && az36FSet && abs(cal.az36Max-cal.az36_0)>100);
  az45LinearValid=(az45OSet && az45FSet && abs(cal.az45Max-cal.az45_0)>100);
  elLinearValid=(elOSet && elFSet && abs(cal.elMax-cal.el0)>100);

  refreshMultiCalValidity();
  gsAzMode=(savedAzMode==36)?36:45;

  if(savedMode==CAL_GS232_2POINT || savedMode==CAL_MULTI_POINT){
    calibrationMode=(CalibrationMode)savedMode;
  }else{
    bool anyMulti=(az36MultiValid || az45MultiValid) && elMultiValid;
    calibrationMode=anyMulti ? CAL_MULTI_POINT : CAL_GS232_2POINT;
  }

  cal.valid=selectedCalibrationValid();
  if(calFmt!=4) saveCal();
}

static void saveCal(){
  az36LinearValid=(az36OSet && az36FSet && abs(cal.az36Max-cal.az36_0)>100);
  az45LinearValid=(az45OSet && az45FSet && abs(cal.az45Max-cal.az45_0)>100);
  elLinearValid=(elOSet && elFSet && abs(cal.elMax-cal.el0)>100);

  refreshMultiCalValidity();
  cal.valid=selectedCalibrationValid();

  prefs.begin(calNamespace(),false);
  prefs.putUChar("calfmt",4);
  prefs.putUChar("calmode",(uint8_t)calibrationMode);
  prefs.putUChar("azmode",gsAzMode);

  prefs.putInt("a36z",cal.az36_0); prefs.putInt("a36f",cal.az36Max);
  prefs.putInt("a45z",cal.az45_0); prefs.putInt("a45f",cal.az45Max);
  prefs.putBool("a36os",az36OSet); prefs.putBool("a36fs",az36FSet);
  prefs.putBool("a45os",az45OSet); prefs.putBool("a45fs",az45FSet);

  prefs.putInt("el0mv",cal.el0); prefs.putInt("elmaxmv",cal.elMax);
  prefs.putBool("elos",elOSet); prefs.putBool("elfs",elFSet);

  prefs.putInt("a36m0",az36CalMv[0]); prefs.putInt("a36m90",az36CalMv[1]);
  prefs.putInt("a36m180",az36CalMv[2]); prefs.putInt("a36m270",az36CalMv[3]);
  prefs.putInt("a36m360",az36CalMv[4]);

  prefs.putInt("a45m0",az45CalMv[0]); prefs.putInt("a45m90",az45CalMv[1]);
  prefs.putInt("a45m180",az45CalMv[2]); prefs.putInt("a45m270",az45CalMv[3]);
  prefs.putInt("a45m360",az45CalMv[4]); prefs.putInt("a45m450",az45CalMv[5]);

  prefs.putInt("elm0",elCalMv[0]); prefs.putInt("elm45",elCalMv[1]);
  prefs.putInt("elm90",elCalMv[2]); prefs.putInt("elm135",elCalMv[3]);
  prefs.putInt("elm180",elCalMv[4]);
  prefs.end();
}

static void saveWifi(String s,String p){
  prefs.begin("g5500",false);prefs.putString("ssid",s);prefs.putString("pass",p);prefs.end();
}
static void saveApPassword(const String &p){
  prefs.begin("g5500",false);prefs.putString("appass",p);prefs.end();
}

#include "web_page.hpp"


static const char ADC_TEST_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>G5500 ADC TEST v1.6.23</title>
<style>
:root{--bg:#081018;--p:#111d28;--t:#f4f7fa;--m:#96a9b7;--a:#24d18d;--b:#2b4557;--az:#33b8ff;--el:#ffb347}
*{box-sizing:border-box}body{margin:0;background:linear-gradient(#081018,#0b1721);color:var(--t);font-family:Arial,sans-serif}
.wrap{max-width:980px;margin:auto;padding:16px}.card{background:var(--p);border:1px solid var(--b);border-radius:16px;padding:16px;margin-top:14px}
h1{margin:.25em 0}.muted{color:var(--m)}.grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.kv{display:grid;grid-template-columns:1fr 1fr;gap:8px}.kv div{background:#0b1720;padding:10px;border-radius:9px}
.val{font-size:25px;font-weight:700}.az{color:var(--az)}.el{color:var(--el)}
.btn{border:0;border-radius:12px;min-height:42px;padding:8px 14px;background:#235b78;color:white;font-size:15px;font-weight:700;cursor:pointer}
.good{background:#167a5c}.warn{background:#81541d}
canvas{width:100%;height:360px;background:#071018;border-radius:10px;margin-top:10px}
a{color:#24d18d;text-decoration:none;font-weight:700}
@media(max-width:700px){.grid{grid-template-columns:1fr}.kv{grid-template-columns:1fr}}
.nav{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0 14px;padding:8px;background:#0b1720;border:1px solid #294153;border-radius:12px;position:sticky;top:0;z-index:20}.nav a{display:inline-block;padding:9px 12px;border-radius:9px;background:#173044;color:#fff;text-decoration:none;font-weight:700;font-size:14px}.nav a.active{background:#167a5c}.nav a:hover{background:#235b78}\n</style></head><body><div class="wrap">
<div class="muted" style="font-size:13px">G5500 Controller v1.6.23 &copy; UA1CFM</div>
<h1>ADC TEST — ESP32-S3</h1>
<nav class="nav"><a href="/">УПРАВЛЕНИЕ</a><a class="active" href="/adc">ADC TEST</a><a href="/resetdiag">RESET DIAG</a><a href="/diaglog">DIAG CONSOLE</a></nav>

<div class="card">
<div><b>Назначение:</b> диагностический замер ADC. Он не меняет калибровку AZ<span id="adcElHelp">/EL</span> и не управляет реле.</div>
<div class="muted" style="margin-top:7px">RAW — обычный 12-битный analogRead(). CAL mV — калиброванное значение analogReadMilliVolts(). LINEAR mV — условная идеальная прямая 0…3100 mV для ADC_11db. Δ показывает отличие заводской калиброванной шкалы от этой прямой, а не абсолютную погрешность без внешнего эталонного вольтметра.</div>
<div style="margin-top:10px"><button id="collectBtn" class="btn good" onclick="toggleCollect()">AUTO COLLECT: ON</button> <button class="btn warn" onclick="clearData()">CLEAR GRAPH</button></div>
</div>

<div class="grid">
<div class="card"><div class="az"><b>AZ — GPIO4</b></div><div class="kv" style="margin-top:10px">
<div>RAW ADC<br><span id="azraw" class="val az">-</span></div>
<div>CAL mV<br><span id="azmv" class="val az">-</span></div>
<div>LINEAR mV<br><span id="azlin" class="val">-</span></div>
<div>Δ mV<br><span id="azdmv" class="val">-</span></div>
<div>Δ %<br><span id="azpct" class="val">-</span></div>
<div>Samples<br><span id="azn" class="val">0</span></div>
</div></div>
<div id="adcElCard" class="card"><div class="el"><b>EL — GPIO5</b></div><div class="kv" style="margin-top:10px">
<div>RAW ADC<br><span id="elraw" class="val el">-</span></div>
<div>CAL mV<br><span id="elmv" class="val el">-</span></div>
<div>LINEAR mV<br><span id="ellin" class="val">-</span></div>
<div>Δ mV<br><span id="eldmv" class="val">-</span></div>
<div>Δ %<br><span id="elpct" class="val">-</span></div>
<div>Samples<br><span id="eln" class="val">0</span></div>
</div></div>
</div>

<div class="card">
<div><b>График RAW ADC → напряжение</b></div>
<div class="muted">Синяя линия/точки — AZ, <span id="adcElLegend">оранжевая — EL, </span>серая — условная линейная шкала 0…3100 mV. Для заполнения всей кривой медленно проведите соответствующую ось по диапазону вручную; страница автоматически набирает точки.</div>
<canvas id="chart" width="920" height="360"></canvas>
</div>
</div>
<script>
let collect=true, azPts=[], elPts=[], MAXPTS=900, hasEl=true;
function toggleCollect(){collect=!collect;collectBtn.textContent='AUTO COLLECT: '+(collect?'ON':'OFF');collectBtn.className='btn '+(collect?'good':'warn')}
function clearData(){azPts=[];elPts=[];draw();azn.textContent='0';eln.textContent='0'}
function addPoint(a,raw,mv){
  if(!collect)return;
  let p={x:Number(raw),y:Number(mv)};
  if(a.length){
    let q=a[a.length-1];
    if(Math.abs(q.x-p.x)<2 && Math.abs(q.y-p.y)<2)return;
  }
  a.push(p);
  if(a.length>MAXPTS)a.shift();
}
function fmt(v,n=0){return Number(v).toFixed(n)}
function updateAxis(prefix,raw,mv,arr){
  let lin=raw*3100/4095, d=mv-lin, pct=lin>50?100*d/lin:0;
  document.getElementById(prefix+'raw').textContent=raw;
  document.getElementById(prefix+'mv').textContent=mv;
  document.getElementById(prefix+'lin').textContent=fmt(lin,1);
  document.getElementById(prefix+'dmv').textContent=(d>=0?'+':'')+fmt(d,1);
  document.getElementById(prefix+'pct').textContent=(pct>=0?'+':'')+fmt(pct,2)+'%';
  document.getElementById(prefix+'n').textContent=arr.length;
}
function draw(){
  const c=chart,x=c.getContext('2d'),w=c.width,h=c.height,left=58,right=w-18,top=18,bottom=h-38;
  x.clearRect(0,0,w,h);x.fillStyle='#071018';x.fillRect(0,0,w,h);
  x.strokeStyle='#294153';x.fillStyle='#96a9b7';x.font='12px Arial';x.lineWidth=1;
  for(let r=0;r<=4095;r+=512){let px=left+r/4095*(right-left);x.beginPath();x.moveTo(px,top);x.lineTo(px,bottom);x.stroke();if(r<4095)x.fillText(r,px-12,bottom+18)}
  for(let mv=0;mv<=3100;mv+=500){let py=bottom-mv/3100*(bottom-top);x.beginPath();x.moveTo(left,py);x.lineTo(right,py);x.stroke();x.fillText(mv+'mV',3,py+4)}
  x.fillText('4095',right-28,bottom+18);
  // Linear reference
  x.strokeStyle='#788a96';x.setLineDash([6,6]);x.beginPath();x.moveTo(left,bottom);x.lineTo(right,top);x.stroke();x.setLineDash([]);
  function series(a,color){
    if(!a.length)return;
    let s=a.slice().sort((p,q)=>p.x-q.x);
    x.strokeStyle=color;x.fillStyle=color;x.lineWidth=2;x.beginPath();
    s.forEach((p,i)=>{let px=left+p.x/4095*(right-left),py=bottom-Math.max(0,Math.min(3100,p.y))/3100*(bottom-top);if(i)x.lineTo(px,py);else x.moveTo(px,py)});
    x.stroke();
    for(let i=0;i<s.length;i+=Math.max(1,Math.floor(s.length/180))){let p=s[i],px=left+p.x/4095*(right-left),py=bottom-Math.max(0,Math.min(3100,p.y))/3100*(bottom-top);x.fillRect(px-1.5,py-1.5,3,3)}
  }
  series(azPts,'#33b8ff');if(hasEl)series(elPts,'#ffb347');
  x.fillStyle='#33b8ff';x.fillText('AZ',right-95,top+14);x.fillStyle='#ffb347';if(hasEl)x.fillText('EL',right-60,top+14);x.fillStyle='#96a9b7';x.fillText('RAW ADC',right-80,bottom+32);
}
async function upd(){
  try{
    let j=await (await fetch('/api/adcdiag',{cache:'no-store'})).json();
    hasEl=j.hasEl!==false;
    for(const id of ['adcElCard','adcElHelp','adcElLegend'])document.getElementById(id).style.display=hasEl?'':'none';
    document.querySelector('.grid').style.gridTemplateColumns=hasEl?'':'1fr';
    addPoint(azPts,j.azRaw,j.azMv); if(hasEl)addPoint(elPts,j.elRaw,j.elMv);
    updateAxis('az',j.azRaw,j.azMv,azPts); if(hasEl)updateAxis('el',j.elRaw,j.elMv,elPts);
    draw();
  }catch(e){}
}
setInterval(upd,350);upd();draw();
</script></body></html>
)HTML";



static const char RESET_DIAG_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>G5500 RESET DIAG v1.6.23</title>
<style>
:root{--bg:#081018;--p:#111d28;--t:#f4f7fa;--m:#96a9b7;--a:#24d18d;--b:#2b4557;--w:#e5a94f;--red:#d9534f}
*{box-sizing:border-box}body{margin:0;background:linear-gradient(#081018,#0b1721);color:var(--t);font-family:Arial,sans-serif}
.wrap{max-width:920px;margin:auto;padding:16px}.card{background:var(--p);border:1px solid var(--b);border-radius:16px;padding:16px;margin-top:14px}
h1{margin:.25em 0}.muted{color:var(--m)}a{color:var(--a);text-decoration:none;font-weight:700}
.reason{font-size:38px;font-weight:800;color:var(--a);margin-top:8px}
.code{font-size:20px;font-weight:700}.kv{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:10px}
.kv div{background:#0b1720;padding:10px;border-radius:9px}
.note{line-height:1.45}.warn{color:var(--w)}
table{width:100%;border-collapse:collapse;margin-top:10px}
th,td{padding:9px 7px;border-bottom:1px solid #294153;text-align:left;font-size:14px}
th{color:var(--m);font-weight:700}
.btn{border:0;border-radius:9px;padding:10px 14px;font-weight:700;cursor:pointer}
.danger{background:#8f2e2b;color:#fff}
@media(max-width:650px){.kv{grid-template-columns:1fr}.reason{font-size:32px}th,td{font-size:12px;padding:7px 4px}}
.nav{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0 14px;padding:8px;background:#0b1720;border:1px solid #294153;border-radius:12px;position:sticky;top:0;z-index:20}.nav a{display:inline-block;padding:9px 12px;border-radius:9px;background:#173044;color:#fff;text-decoration:none;font-weight:700;font-size:14px}.nav a.active{background:#167a5c}.nav a:hover{background:#235b78}\n</style></head><body><div class="wrap">
<div class="muted" style="font-size:13px">G5500 Controller v1.6.23 &copy; UA1CFM</div>
<h1>RESET DIAG</h1>
<nav class="nav"><a href="/">УПРАВЛЕНИЕ</a><a href="/adc">ADC TEST</a><a class="active" href="/resetdiag">RESET DIAG</a><a href="/diaglog">DIAG CONSOLE</a></nav>

<div class="card">
<div class="muted">Причина последней загрузки ESP32-S3</div>
<div id="reason" class="reason">--</div>
<div class="kv">
<div>Reset code<br><span id="code" class="code">--</span></div>
<div>Обновление<br><span id="stamp" class="code">--</span></div>
</div>
</div>

<div class="card">
<div style="display:flex;align-items:center;justify-content:space-between;gap:12px;flex-wrap:wrap">
  <div>
    <b>История перезагрузок</b>
    <div class="muted">Последние 20 запусков. Время хранится как UTC и показывается браузером в вашем местном часовом поясе.</div>
  </div>
  <button class="btn danger" onclick="clearHist()">ОЧИСТИТЬ ИСТОРИЮ</button>
</div>
<div style="overflow-x:auto">
<table>
<thead><tr><th>#</th><th>Дата и время</th><th>Причина</th><th>Код</th></tr></thead>
<tbody id="hist"><tr><td colspan="4" class="muted">Загрузка...</td></tr></tbody>
</table>
</div>
<div id="timeNote" class="muted" style="margin-top:8px"></div>
</div>

<div class="card note">
<b>Расшифровка:</b><br><br>
<b>POWERON</b> — обычное включение питания.<br>
<b>BROWNOUT</b> — просадка питания ESP32.<br>
<b>EXTERNAL</b> — внешний аппаратный reset.<br>
<b>SOFTWARE</b> — программный перезапуск.<br>
<b>PANIC</b> — аварийное исключение программы.<br>
<b>INT_WDT / TASK_WDT / WDT</b> — сработал watchdog.<br>
<b>DEEPSLEEP</b> — выход из deep sleep.<br><br>
<span class="warn">Если домашний Wi‑Fi недоступен при загрузке, NTP-время получить нельзя — такая запись будет показана как «Время не синхронизировано».</span>
</div>
</div>
<script>
async function upd(){
  try{
    let j=await (await fetch('/api/resetreason',{cache:'no-store'})).json();
    reason.textContent=j.reason||'UNKNOWN';
    code.textContent=j.code;
    stamp.textContent=new Date().toLocaleTimeString();
  }catch(e){reason.textContent='NO CONNECTION'}
}
async function histUpd(){
  try{
    let j=await (await fetch('/api/resethistory',{cache:'no-store'})).json();
    let h='';
    let unsynced=0;
    (j.entries||[]).forEach((e,i)=>{
      let when='Время не синхронизировано';
      if(Number(e.epoch)>0) when=new Date(Number(e.epoch)*1000).toLocaleString();
      else unsynced++;
      h+='<tr><td>'+(i+1)+'</td><td>'+when+'</td><td><b>'+e.reason+'</b></td><td>'+e.code+'</td></tr>';
    });
    hist.innerHTML=h||'<tr><td colspan="4" class="muted">История пуста</td></tr>';
    timeNote.textContent=unsynced?('Без точного времени: '+unsynced+' записей'):'';
  }catch(e){hist.innerHTML='<tr><td colspan="4">Ошибка загрузки истории</td></tr>'}
}
async function clearHist(){
  if(!confirm('Очистить всю историю перезагрузок?')) return;
  await fetch('/api/resethistory/clear',{cache:'no-store'});
  histUpd();
}
setInterval(upd,1000);upd();histUpd();
</script></body></html>
)HTML";


static const char DIAG_PAGE[] PROGMEM = R"HTML(
<!doctype html><html lang="ru"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>G5500 Diagnostic Console v1.6.23</title>
<style>
:root{--bg:#071018;--border:#294153;--text:#d8f7df;--muted:#91a5b2}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:white;font-family:Arial,sans-serif}
.wrap{max-width:1200px;margin:auto;padding:14px}
.top{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-bottom:10px}
h2{margin:0 14px 0 0;font-size:21px}
button,a.btn{border:1px solid var(--border);background:#173044;color:white;border-radius:9px;padding:9px 13px;font-weight:700;text-decoration:none;cursor:pointer}
button.good{background:#126b50}
button.danger{background:#a93d3d}
label{color:var(--muted);font-size:14px;display:flex;gap:7px;align-items:center}
#state{color:var(--muted);margin-left:auto;font-size:13px}
#term{height:calc(100vh - 92px);min-height:420px;overflow:auto;white-space:pre-wrap;word-break:break-word;background:#020608;color:var(--text);border:1px solid var(--border);border-radius:10px;padding:12px;font:14px/1.42 Consolas,"Courier New",monospace}
@media(max-width:700px){#state{width:100%;margin-left:0}#term{height:calc(100vh - 140px);font-size:12px}}
.nav{display:flex;gap:8px;flex-wrap:wrap;margin:8px 0 14px;padding:8px;background:#0b1720;border:1px solid #294153;border-radius:12px;position:sticky;top:0;z-index:20}.nav a{display:inline-block;padding:9px 12px;border-radius:9px;background:#173044;color:#fff;text-decoration:none;font-weight:700;font-size:14px}.nav a.active{background:#167a5c}.nav a:hover{background:#235b78}\n</style></head><body><div class="wrap">
<div class="top">
<h2>G5500 Web Diagnostic Console</h2>
<a class="btn" href="/">УПРАВЛЕНИЕ</a>
<a class="btn" href="/adc">ADC TEST</a>
<a class="btn" href="/resetdiag">RESET DIAG</a>
<a class="btn" href="/diaglog" style="background:#167a5c">DIAG CONSOLE</a>
<button class="good" onclick="refreshNow()">Refresh</button>
<button class="danger" onclick="clearLog()">Clear RAM log</button>
<label><input id="auto" type="checkbox" checked> Auto scroll</label>
<label><input id="pause" type="checkbox"> Pause</label>
<span id="state">connecting...</span>
</div>
<pre id="term">Loading...</pre>
</div>
<script>
let last='';
let prevRawLines=[];
let stampedLines=[];

function pcStamp(){
  const d=new Date();
  const p=n=>String(n).padStart(2,'0');
  const ms=String(d.getMilliseconds()).padStart(3,'0');
  return '['+p(d.getHours())+':'+p(d.getMinutes())+':'+p(d.getSeconds())+'.'+ms+']';
}

function stampSnapshot(raw){
  if(!raw)return '';

  let lines=raw.replace(/\r/g,'').split('\n');
  if(lines.length && lines[lines.length-1]==='')lines.pop();

  // Preserve time labels for lines still present after the ESP32 RAM ring-buffer shifts.
  let overlap=0;
  const max=Math.min(prevRawLines.length,lines.length);
  for(let k=max;k>=0;k--){
    let ok=true;
    for(let i=0;i<k;i++){
      if(prevRawLines[prevRawLines.length-k+i]!==lines[i]){ok=false;break;}
    }
    if(ok){overlap=k;break;}
  }

  const nextStamped=[];
  for(let i=0;i<overlap;i++){
    nextStamped.push(stampedLines[stampedLines.length-overlap+i]);
  }
  for(let i=overlap;i<lines.length;i++){
    nextStamped.push(pcStamp()+' | '+lines[i]);
  }

  prevRawLines=lines;
  stampedLines=nextStamped;
  return nextStamped.join('\n');
}

async function loadLog(){
  if(pause.checked)return;
  try{
    const r=await fetch('/api/diaglog',{cache:'no-store'});
    if(!r.ok)throw new Error('HTTP '+r.status);
    const t=await r.text();
    if(t!==last){
      const nearBottom=(term.scrollHeight-term.scrollTop-term.clientHeight)<80;
      term.textContent=t ? stampSnapshot(t) : '(RAM diagnostic log is empty)';
      if(auto.checked && nearBottom)term.scrollTop=term.scrollHeight;
      last=t;
    }
    state.textContent='LIVE • PC '+new Date().toLocaleTimeString();
  }catch(e){
    state.textContent='ERROR: '+e.message;
  }
}
async function refreshNow(){await loadLog();if(auto.checked)term.scrollTop=term.scrollHeight}
async function clearLog(){
  if(!confirm('Очистить только диагностический RAM-буфер?'))return;
  await fetch('/api/diaglog/clear',{cache:'no-store'});
  last='';
  prevRawLines=[];
  stampedLines=[];
  await loadLog();
}
setInterval(loadLog,500);
loadLog();
</script></body></html>
)HTML";

static void setupForkWeb();
static void setupWeb(){
  web.on("/",HTTP_GET,[]{web.send_P(200,"text/html; charset=utf-8",WEB_PAGE);});
  web.on("/diaglog",HTTP_GET,[]{web.send_P(200,"text/html; charset=utf-8",DIAG_PAGE);});
  web.on("/api/diaglog",HTTP_GET,[]{
    web.send(200,"text/plain; charset=utf-8",diagSnapshot());
  });
  web.on("/api/diaglog/clear",HTTP_GET,[]{
    diagClear();
    web.send(200,"text/plain","OK");
  });
  web.on("/resetdiag",HTTP_GET,[]{web.send_P(200,"text/html; charset=utf-8",RESET_DIAG_PAGE);});
  web.on("/adc",HTTP_GET,[]{web.send_P(200,"text/html; charset=utf-8",ADC_TEST_PAGE);});
  web.on("/api/adcdiag",HTTP_GET,[]{
    int azRaw=0,azMv=0,elRaw=0,elMv=0;
    adcDiagRead(PIN_AZ_ADC,azRaw,azMv);
    if(controller.model==forkrot::Model::G5500)adcDiagRead(PIN_EL_ADC,elRaw,elMv);
    String j=String("{\"hasEl\":")+(controller.model==forkrot::Model::G5500?"true":"false")+",\"azRaw\":"+String(azRaw)+",\"azMv\":"+String(azMv)+",\"elRaw\":"+String(elRaw)+",\"elMv\":"+String(elMv)+"}";
    web.send(200,"application/json",j);
  });
  web.on("/api/move",HTTP_GET,[]{
    forkrot::Form form;form["d"]=web.arg("d").c_str();
    auto r=forkrot::api(controller,protocol,loggerSettings,"/api/move",form,millis());
    controller.tick(millis());applyOutputs();web.send(r.status,"application/json",r.body.c_str());
  });
  web.on("/api/target",HTTP_GET,[]{
    gsSeqActive=false;
    forkrot::Form form;form["az"]=web.arg("az").c_str();
    if(web.hasArg("el"))form["el"]=web.arg("el").c_str();
    updatePosition();auto response=forkrot::api(controller,protocol,loggerSettings,"/api/target",form,millis());
    controller.tick(millis());applyOutputs();targetValid=true;
    web.send(response.status,"application/json",response.body.c_str());
  });
  web.on("/api/cal",HTTP_GET,[]{
    // MULTI-POINT. AZ is written only to the currently selected P36/P45 table.
    String p=web.arg("p");if(controller.model==forkrot::Model::G800DXA&&p.startsWith("el")){web.send(400,"text/plain","NO EL AXIS");return;} int a=adcAvgMv(PIN_AZ_ADC), e=adcAvgMv(PIN_EL_ADC);
    if(p.startsWith("az")){
      int *tbl = (gsAzMode==36) ? az36CalMv : az45CalMv;
      uint8_t n = (gsAzMode==36) ? AZ36_CAL_N : AZ45_CAL_N;
      int idx=-1;
      if(p=="az0") idx=0; else if(p=="az90") idx=1; else if(p=="az180") idx=2;
      else if(p=="az270") idx=3; else if(p=="az360") idx=4; else if(p=="az450") idx=5;
      if(idx<0 || idx>=n){ web.send(400,"text/plain","POINT NOT IN ACTIVE AZ RANGE"); return; }
      tbl[idx]=a;
    }else if(p=="el0")elCalMv[0]=e; else if(p=="el45")elCalMv[1]=e; else if(p=="el90")elCalMv[2]=e;
    else if(p=="el135")elCalMv[3]=e; else if(p=="el180")elCalMv[4]=e;
    else {web.send(400,"text/plain","BAD POINT");return;}
    saveCal();
    web.send(200,"text/plain","MULTI SAVED");
  });

  web.on("/api/cal2",HTTP_GET,[]{
    String p=web.arg("p"); p.toUpperCase();
    if(controller.model==forkrot::Model::G800DXA&&(p=="O2"||p=="F2")){web.send(400,"text/plain","NO EL AXIS");return;}
    if(p=="O" || p=="O2" || p=="F" || p=="F2"){
      captureGs232Cal(p);
      web.send(200,"text/plain","2-POINT SAVED");
    }else web.send(400,"text/plain","BAD COMMAND");
  });

  web.on("/api/calmode",HTTP_GET,[]{
    String m=web.arg("m"); m.toLowerCase();
    if(m=="2" || m=="gs232" || m=="2point") setCalibrationMode(CAL_GS232_2POINT);
    else if(m=="multi") setCalibrationMode(CAL_MULTI_POINT);
    else {web.send(400,"text/plain","BAD MODE");return;}
    cal.valid=selectedCalibrationValid();
    web.send(200,"text/plain",calibrationModeName());
  });

  web.on("/api/azmode",HTTP_GET,[]{
    int m=web.arg("m").toInt();
    if(m!=36 && m!=45){ web.send(400,"text/plain","BAD AZ MODE"); return; }
    setGsAzMode((uint8_t)m);
    web.send(200,"text/plain",m==36?"P36":"P45");
  });

  web.on("/api/calreset",HTTP_GET,[]{
    String axis=web.arg("axis"); axis.toLowerCase();
    if(controller.model==forkrot::Model::G800DXA&&axis=="el"){web.send(400,"text/plain","NO EL AXIS");return;}
    String m=web.arg("mode"); m.toLowerCase();

    if(m=="multi"){
      if(axis=="az"){
        if(gsAzMode==36){ for(uint8_t i=0;i<AZ36_CAL_N;i++) az36CalMv[i]=-1; az36MultiValid=false; }
        else { for(uint8_t i=0;i<AZ45_CAL_N;i++) az45CalMv[i]=-1; az45MultiValid=false; }
      }else if(axis=="el"){
        for(uint8_t i=0;i<EL_CAL_N;i++) elCalMv[i]=-1;
        elMultiValid=false;
      }else{ web.send(400,"text/plain","BAD AXIS"); return; }
    }else if(m=="2"){
      if(axis=="az"){
        if(gsAzMode==36){
          cal.az36_0=0; cal.az36Max=3100;
          az36OSet=false; az36FSet=false; az36LinearValid=false;
        }else{
          cal.az45_0=0; cal.az45Max=3100;
          az45OSet=false; az45FSet=false; az45LinearValid=false;
        }
      }else if(axis=="el"){
        cal.el0=0; cal.elMax=3100;
        elOSet=false; elFSet=false; elLinearValid=false;
      }else{ web.send(400,"text/plain","BAD AXIS"); return; }
    }else{
      web.send(400,"text/plain","BAD MODE"); return;
    }

    saveCal();
    web.send(200,"text/plain","RESET OK");
  });
  web.on("/api/wifi",HTTP_GET,[]{saveWifi(web.arg("ssid"),web.arg("pass"));web.send(200,"text/plain","Saved");delay(500);ESP.restart();});
  web.on("/api/ap",HTTP_GET,[]{
    String p=web.arg("pass");
    if(p.length()!=0 && (p.length()<8 || p.length()>63)){
      web.send(400,"text/plain","AP password must be empty or 8..63 characters");
      return;
    }
    saveApPassword(p);
    web.send(200,"text/plain",p.length()?"AP password saved":"AP set to open");
    delay(500);
    ESP.restart();
  });
  web.on("/api/resetreason",HTTP_GET,[]{
    String j="{\"reason\":\""+bootResetReasonText+"\",\"code\":"+String((int)bootResetReason)+"}";
    web.send(200,"application/json",j);
  });
  web.on("/api/resethistory",HTTP_GET,[]{
    web.send(200,"application/json",resetHistoryJson());
  });
  web.on("/api/resethistory/clear",HTTP_GET,[]{
    clearResetHistory();
    web.send(200,"text/plain","OK");
  });
  web.on("/api/rssi",HTTP_GET,[]{
    String r=web.arg("range");
    if(r=="day") web.send(200,"application/json",rssiHistoryJson(rssiDay,288,rssiDayPos,rssiDayCount,"day"));
    else if(r=="week") web.send(200,"application/json",rssiHistoryJson(rssiWeek,336,rssiWeekPos,rssiWeekCount,"week"));
    else if(r=="month") web.send(200,"application/json",rssiHistoryJson(rssiMonth,360,rssiMonthPos,rssiMonthCount,"month"));
    else if(r=="year") web.send(200,"application/json",rssiHistoryJson(rssiYear,365,rssiYearPos,rssiYearCount,"year"));
    else web.send(200,"application/json",rssiHistoryJson(rssiHour,60,rssiHourPos,rssiHourCount,"hour"));
  });
  web.on("/api/status",HTTP_GET,[]{updatePosition();
    bool staOk = (WiFi.status()==WL_CONNECTED);
    String staIp = staOk ? WiFi.localIP().toString() : "-";
    String apIp = WiFi.softAPIP().toString();
    String ip = staOk ? staIp : apIp;
    String mode = staOk ? "AP + Wi-Fi Client" : "Access Point";
    int rssi = staOk ? WiFi.RSSI() : 0;
    String signal = !staOk ? "-" : (rssi>=-55 ? "Excellent" : (rssi>=-67 ? "Good" : (rssi>=-75 ? "Fair" : "Weak")));
    String rssiJson = staOk ? String(rssi) : "null";
    uint32_t targetAge = targetValid ? (millis()-lastTargetRxMs) : 0;
    const int *azTbl = (gsAzMode==36) ? az36CalMv : az45CalMv;
    int azN = (gsAzMode==36) ? AZ36_CAL_N : AZ45_CAL_N;
    bool azLin = activeAzLinearValid();
    bool azMul = activeAzMultiValid();
    int az450v = (azN>5) ? azTbl[5] : -1;
    int az2z = activeAzOSet() ? activeAz2Zero() : -1;
    int az2f = activeAzFSet() ? activeAz2Full() : -1;
    cal.valid=selectedCalibrationValid();

    String j="{\"az\":"+String(currentAz,2)+
      ",\"el\":"+String(currentEl,2)+
      ",\"targetAz\":"+String(targetAz,2)+
      ",\"targetEl\":"+String(targetEl,2)+
      ",\"targetValid\":"+(targetValid?"true":"false")+
      ",\"targetAgeMs\":"+String(targetAge)+
      ",\"calibrated\":"+(cal.valid?"true":"false")+
      ",\"selectedCalOk\":"+(selectedCalibrationValid()?"true":"false")+
      ",\"azMode\":"+String(gsAzMode)+
      ",\"azLinear\":"+(azLin?"true":"false")+
      ",\"azLinear36\":"+(az36LinearValid?"true":"false")+
      ",\"azLinear45\":"+(az45LinearValid?"true":"false")+
      ",\"elLinear\":"+(elLinearValid?"true":"false")+
      ",\"azMulti\":"+(azMul?"true":"false")+
      ",\"azMulti36\":"+(az36MultiValid?"true":"false")+
      ",\"azMulti45\":"+(az45MultiValid?"true":"false")+
      ",\"elMulti\":"+(elMultiValid?"true":"false")+
      ",\"calMode\":\""+String(calibrationMode==CAL_MULTI_POINT?"multi":"2")+"\""+
      ",\"az2p0\":"+String(az2z)+",\"az2pf\":"+String(az2f)+
      ",\"el2p0\":"+String(elOSet?cal.el0:-1)+",\"el2pf\":"+String(elFSet?cal.elMax:-1)+
      ",\"motion\":\""+motion()+"\""+
      ",\"azp0\":"+String(azTbl[0])+",\"azp90\":"+String(azTbl[1])+",\"azp180\":"+String(azTbl[2])+
      ",\"azp270\":"+String(azTbl[3])+",\"azp360\":"+String(azTbl[4])+",\"azp450\":"+String(az450v)+
      ",\"elp0\":"+String(elCalMv[0])+",\"elp45\":"+String(elCalMv[1])+",\"elp90\":"+String(elCalMv[2])+
      ",\"elp135\":"+String(elCalMv[3])+",\"elp180\":"+String(elCalMv[4])+
      ",\"ip\":\""+ip+"\",\"apIp\":\""+apIp+"\",\"staIp\":\""+staIp+
      "\",\"wifiMode\":\""+mode+"\",\"ssid\":"+String(forkrot::jsonQuote(wifiSsid.c_str()).c_str())+",\"rssi\":"+rssiJson+
      ",\"signal\":\""+signal+"\",\"btConnected\":"+(hc05Connected()?"true":"false")+
      ",\"resetReason\":\""+bootResetReasonText+"\"}";
    auto extra=forkrot::stateJson(controller,protocol,loggerSettings,millis());
    j.remove(j.length()-1);j+=",";j+=extra.c_str()+1;
    web.send(200,"application/json",j);});
  setupForkWeb();
  web.begin();
}

static int parseGsNumbers(const String &s, float *vals, int maxVals){
  char buf[128];
  String a=s;
  a.trim();
  a.toCharArray(buf,sizeof(buf));
  int n=0;
  char *save=nullptr;
  char *tok=strtok_r(buf," ,\t",&save);
  while(tok && n<maxVals){
    vals[n++]=atof(tok);
    tok=strtok_r(nullptr," ,\t",&save);
  }
  return n;
}

static void gsSeqStart(){
  if(gsSeqCount==0) return;
  gsSeqIndex=0;
  float az=clampf(gsSeqAz[0],0,activeAzMaxDeg());
  float el=gsSeqHasEl?clampf(gsSeqEl[0],0,180):currentEl;
  setReferenceTarget(az,el,true,gsSeqHasEl);
  lastTargetCommandMs=millis(); lastTargetRxMs=millis();
  gsSeqNextMs=millis()+(uint32_t)gsSeqIntervalSec*1000UL;
  gsSeqActive=true;
}

static void serviceGsSequence(){
  // Timed sequences are serviced by the shared controller tick.
}

static void rotctldSend(const String &s){
  bool sent=false;
  if(rotctldClient && rotctldClient.connected()){
    rotctldClient.print(s);
    flashTx();
    sent=true;
  }
#if FORK_NATIVE_USB
  if(sent){
    dbgPrint("[TCP TX] ");
    String t=s;
    t.replace("\r","");
    t.replace("\n"," | ");
    dbgPrintln(t);
  }
#endif
}

static void processRotctldCommand(String cmd){
  updatePosition();
  auto packet=protocol.line(cmd.c_str(),millis(),true);
  if(packet.matched){
    if(packet.result==forkrot::Result::Ok||packet.result==forkrot::Result::Pending){targetValid=true;lastTargetRxMs=millis();}
    controller.tick(millis());applyOutputs();
    if(!packet.reply.empty())rotctldSend(packet.reply.c_str());return;
  }

  cmd.trim();
  if(!cmd.length()) return;

  flashRx();

#if FORK_NATIVE_USB
  dbgPrint("[TCP RX] ");
  dbgPrintln(cmd);
#endif

  updatePosition();

  String lower = cmd;
  lower.toLowerCase();

  String upper=cmd;upper.toUpperCase();
  if(upper=="P36"){ setGsAzMode(36); return; }
  if(upper=="P45"){ setGsAzMode(45); return; }

  // Standard GS-232 two-point calibration commands.
  if(upper=="O" || upper=="O2" || upper=="F" || upper=="F2"){
    captureGs232Cal(upper);
    return;
  }

  if(upper=="H"){
    rotctldSend("R L A C M T N S O F X1 X2 X3 X4\r\n");
    return;
  }
  if(upper=="H2"){
    rotctldSend("U D E C2 W T N S O2 F2 B\r\n");
    return;
  }
  if(upper=="H3"){
    String s="P45 P36 MODE=";
    s += (gsAzMode==45 ? "450" : "360");
    s += " CAL=";
    s += calibrationModeName();
    s += "\r\n";
    rotctldSend(s);
    return;
  }

  // Quit current network session.
  if(cmd=="q" || cmd=="Q" || lower=="\\quit" || lower=="quit"){
    rotctldSend("RPRT 0\n");
    delay(5);
    rotctldClient.stop();
    return;
  }

  // Minimal info support.
  if(cmd=="_" || lower=="\\get_info" || lower=="get_info"){
    rotctldSend("G5500 ESP32-S3 v1.6.23 UA1CFM Look4Sat bridge\n");
    return;
  }

  // Some clients probe capabilities/help. Do not tear down the session.
  if(cmd=="?" || lower=="\\dump_caps" || lower=="dump_caps"){
    rotctldSend("RPRT 0\n");
    return;
  }

#if FORK_NATIVE_USB
  dbgPrint("[TCP UNKNOWN] ");
  dbgPrintln(cmd);
#endif
  rotctldSend("RPRT -1\n");
}

static void handleRotctldTcp(){
  static forkrot::LineBuffer receiver;
  // Accept a client if none is currently connected.
  if(!rotctldClient || !rotctldClient.connected()){
    WiFiClient c = tcpServer.accept();
    if(c){
      if(rotctldClient) rotctldClient.stop();
      rotctldClient = c;
      receiver.clear();
#if FORK_NATIVE_USB
      dbgPrintln("[TCP] Look4Sat/rotctld client connected");
#endif
    }else{
      return;
    }
  }

  std::string command;
  while(rotctldClient.connected()&&rotctldClient.available())if(receiver.feed((char)rotctldClient.read(),command)){
    processRotctldCommand(String(command.c_str()));
  }
}

static void startWifi(){
  // SAFE Wi-Fi logic:
  // 1) Start the controller AP.
  // 2) If saved home Wi-Fi credentials exist, try STA only ONCE.
  // 3) If home Wi-Fi is unavailable, stop STA completely and return to AP-only.
  // This prevents repeated STA scans from making the SoftAP disappear.

  WiFi.persistent(false);
  WiFi.setSleep(false);

  // Start with AP only.
  WiFi.mode(WIFI_AP);
  delay(250);

  IPAddress apIP(192,168,4,1);
  IPAddress apGW(192,168,4,1);
  IPAddress apMask(255,255,255,0);
  WiFi.softAPConfig(apIP,apGW,apMask);

  bool apOk = apPass.length()>=8 ? WiFi.softAP(AP_SSID,apPass.c_str()) : WiFi.softAP(AP_SSID);
  delay(300);

#if FORK_NATIVE_USB
  dbgPrintln("[V1559] AP-only start");
  dbgPrint("[V1559] AP start = ");
  dbgPrintln(apOk ? "OK" : "FAILED");
  dbgPrint("[V1559] AP IP = ");
  dbgPrintln(WiFi.softAPIP().toString());
#endif

  // Try home Wi-Fi once, only if credentials are stored.
  if(wifiSsid.length()){
#if FORK_NATIVE_USB
    dbgPrint("[V1559] one STA attempt: ");
    dbgPrintln(wifiSsid);
#endif

    WiFi.mode(WIFI_AP_STA);
    delay(100);
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());

    uint32_t t0=millis();
    while(WiFi.status()!=WL_CONNECTED && millis()-t0<8000){
      delay(100);
    }

    if(WiFi.status()==WL_CONNECTED){
#if FORK_NATIVE_USB
      dbgPrint("[V1559] LAN connected = ");
      dbgPrintln(WiFi.localIP().toString());
      dbgPrint("[V1559] AP remains = ");
      dbgPrintln(WiFi.softAPIP().toString());
#endif
    }else{
      // Home network not available: completely stop STA and restore AP-only.
      WiFi.disconnect(true, false);
      delay(150);
      WiFi.mode(WIFI_AP);
      delay(150);

      WiFi.softAPConfig(apIP,apGW,apMask);
      if(apPass.length()>=8) WiFi.softAP(AP_SSID,apPass.c_str()); else WiFi.softAP(AP_SSID);
      delay(250);

#if FORK_NATIVE_USB
      dbgPrintln("[V1559] LAN unavailable -> AP ONLY");
      dbgPrint("[V1559] AP restored = ");
      dbgPrintln(WiFi.softAPIP().toString());
#endif
    }
  }else{
#if FORK_NATIVE_USB
    dbgPrintln("[V1559] no saved LAN credentials -> AP ONLY");
#endif
  }

  tcpServer.begin();
  setupWeb();

#if FORK_NATIVE_USB
  dbgPrintln("[WIFI] safe auto-reconnect: 30 s interval, 8 s attempt, AP-only fallback");
#endif
}


static void restoreApOnlyAfterReconnectFail(){
  // Stop the failed STA side completely and return to the same stable
  // AP-only state used by startWifi(). No long delay is used here because
  // rotator control must continue to run.
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_AP);

  IPAddress apIP(192,168,4,1);
  IPAddress apGW(192,168,4,1);
  IPAddress apMask(255,255,255,0);
  WiFi.softAPConfig(apIP,apGW,apMask);

  if(apPass.length()>=8) WiFi.softAP(AP_SSID,apPass.c_str());
  else WiFi.softAP(AP_SSID);
}

static void serviceWifiReconnect(){
  if(!wifiSsid.length()) return;

  uint32_t now=millis();

  // LAN came back: remain in AP+STA and arm the timer for a future loss.
  if(WiFi.status()==WL_CONNECTED){
    if(wifiReconnectTryActive){
#if FORK_NATIVE_USB
      dbgPrint("[WIFI] reconnect OK, LAN IP = ");
      dbgPrintln(WiFi.localIP().toString());
#endif
    }
    wifiReconnectTryActive=false;
    wifiReconnectMs=now;
    return;
  }

  // A reconnect attempt is running. Give it at most 8 seconds.
  // This is non-blocking: the normal control loop continues to execute.
  if(wifiReconnectTryActive){
    if((uint32_t)(now-wifiReconnectTryStartedMs)<WIFI_RECONNECT_TIMEOUT_MS) return;

#if FORK_NATIVE_USB
    dbgPrintln("[WIFI] reconnect timeout -> restore AP ONLY");
#endif

    wifiReconnectTryActive=false;
    wifiReconnectMs=now;
    restoreApOnlyAfterReconnectFail();
    return;
  }

  // First observation after boot/loss: start the 30-second retry interval.
  if(wifiReconnectMs==0){
    wifiReconnectMs=now;
    return;
  }

  if((uint32_t)(now-wifiReconnectMs)<WIFI_RECONNECT_MS) return;

  // One controlled STA attempt. AP remains available while the attempt runs.
#if FORK_NATIVE_USB
  dbgPrint("[WIFI] reconnect attempt: ");
  dbgPrintln(wifiSsid);
#endif

  if(WiFi.getMode()!=WIFI_AP_STA) WiFi.mode(WIFI_AP_STA);

  wifiReconnectTryActive=true;
  wifiReconnectTryStartedMs=now;
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
}

static void loadTouchCalibration(){
  prefs.begin("g5500",true);
  touchCalValid=prefs.getBool("tcalok",false);
  touchSwapXY=prefs.getBool("tswap",true);
  touchX0Raw=prefs.getInt("tx0",TOUCH_RAW_Y_MIN);
  touchX1Raw=prefs.getInt("tx1",TOUCH_RAW_Y_MAX);
  touchY0Raw=prefs.getInt("ty0",TOUCH_RAW_X_MAX);
  touchY1Raw=prefs.getInt("ty1",TOUCH_RAW_X_MIN);
  prefs.end();
  if(abs(touchX1Raw-touchX0Raw)<500 || abs(touchY1Raw-touchY0Raw)<500){
    touchCalValid=false;
  }
}

static void saveTouchCalibration(){
  prefs.begin("g5500",false);
  prefs.putBool("tcalok",touchCalValid);
  prefs.putBool("tswap",touchSwapXY);
  prefs.putInt("tx0",touchX0Raw);
  prefs.putInt("tx1",touchX1Raw);
  prefs.putInt("ty0",touchY0Raw);
  prefs.putInt("ty1",touchY1Raw);
  prefs.end();
}

// -----------------------------------------------------------------------------
// TFT + XPT2046 local UI
// -----------------------------------------------------------------------------
static const uint16_t TFT_BG     = ILI9341_BLACK;
static const uint16_t TFT_PANEL  = 0x18E3;
static const uint16_t TFT_BORDER = 0x4208;
static const uint16_t TFT_TEXT   = ILI9341_WHITE;
static const uint16_t TFT_MUTED  = 0x9CF3;
static const uint16_t TFT_AZ     = ILI9341_CYAN;
static const uint16_t TFT_EL     = ILI9341_YELLOW;
static const uint16_t TFT_OK     = ILI9341_GREEN;
static const uint16_t TFT_WARN   = ILI9341_ORANGE;
static const uint16_t TFT_STOP   = ILI9341_RED;
static const uint16_t TFT_BLUE   = ILI9341_BLUE;
static const uint16_t TFT_TARGET = ILI9341_MAGENTA;

static void tftText(int16_t x,int16_t y,uint8_t size,uint16_t fg,uint16_t bg,const String &txt){
  tft.setTextWrap(false);
  tft.setTextSize(size);
  tft.setTextColor(fg,bg);
  tft.setCursor(x,y);
  tft.print(txt);
}

static void tftButton(int x,int y,int w,int h,const String &label,bool active=false,bool saved=false){
  uint16_t fill=TFT_PANEL;
  uint16_t frame=TFT_WARN;       // normal calibration button: colored frame
  if(active){
    fill=TFT_BLUE;
    frame=TFT_AZ;                // selected mode/range: bright cyan frame
  }
  if(saved){
    fill=0x03E0;
    frame=TFT_OK;                // captured calibration point: green frame
  }

  tft.fillRoundRect(x,y,w,h,5,fill);

  // Two-pixel frame so it is clearly visible on the 2.4" TFT.
  tft.drawRoundRect(x,y,w,h,5,frame);
  if(w>4 && h>4) tft.drawRoundRect(x+1,y+1,w-2,h-2,4,frame);

  int tw=(int)label.length()*6*2;
  int tx=x+(w-tw)/2;
  int ty=y+(h-16)/2;
  tftText(tx,ty,2,TFT_TEXT,fill,label);
}

static String tftMotionText(){
  String m=motion();
  if(m=="RIGHT") return "RIGHT >";
  if(m=="LEFT")  return "< LEFT";
  if(m=="UP")    return "UP ^";
  if(m=="DOWN")  return "DOWN v";
  return "STOP";
}

static String tftCalibrationText(){
  if(calibrationMode==CAL_MULTI_POINT){
    return selectedCalibrationValid() ? "MULTI OK" : "MULTI";
  }
  return selectedCalibrationValid() ? "2PT OK" : "2-POINT";
}

static void resetDashboardCache(){
  dashLastAz=-9999.0f; dashLastEl=-9999.0f;
  dashLastTargetAz=-9999.0f; dashLastTargetEl=-9999.0f;
  dashLastTargetValid=!targetValid; // force first target redraw
  dashLastAzBar=-1; dashLastElBar=-1;
  dashLastAzState=""; dashLastElState="";
  dashLastCal=""; dashLastWifi=""; dashLastBt=""; dashLastIp=""; dashLastTouch="";
  dashLastTcpConnected=false;
}

static void drawDashboardStatic(){
  tft.fillScreen(TFT_BG);
  resetDashboardCache();
  tft.fillRoundRect(3,3,314,29,5,TFT_BLUE);
  tftText(10,9,2,TFT_TEXT,TFT_BLUE,"G-5500 ROTATOR");
  tftText(190,9,2,TFT_TEXT,TFT_BLUE,"(c)UA1CFM");

  tft.drawRoundRect(3,36,154,139,6,TFT_BORDER);
  tft.drawRoundRect(163,36,154,139,6,TFT_BORDER);
  tft.fillRect(4,37,152,21,TFT_PANEL);
  tft.fillRect(164,37,152,21,TFT_PANEL);
  tftText(39,42,2,TFT_TEXT,TFT_PANEL,"AZIMUTH");
  tftText(188,42,2,TFT_TEXT,TFT_PANEL,"ELEVATION");

  // TARGET label and value are intentionally on ONE line.
  // DELTA is not used in this TFT branch.
  tftText(8,112,2,TFT_MUTED,TFT_BG,"TARGET ");
  tftText(168,112,2,TFT_MUTED,TFT_BG,"TARGET ");

  tft.drawRoundRect(11,143,138,12,3,TFT_BORDER);
  tft.drawRoundRect(171,143,138,12,3,TFT_BORDER);

  // Short large range labels.
  tftText(12,160,2,TFT_MUTED,TFT_BG,gsAzMode==36?"P36":"P45");
  tftText(172,160,2,TFT_MUTED,TFT_BG,"EL");

  // Bottom controls/status. All visible labels are size 2.
  tft.drawRoundRect(3,180,118,26,4,TFT_WARN);
  tft.drawRoundRect(4,181,116,24,3,TFT_WARN);
  tft.drawRoundRect(126,180,103,26,4,TFT_BORDER);
  tft.drawRoundRect(234,180,83,26,4,TFT_BORDER);
  tft.drawRoundRect(3,210,194,26,4,TFT_BORDER);
  tft.drawRoundRect(202,210,115,26,4,TFT_WARN);
  tft.drawRoundRect(203,211,113,24,3,TFT_WARN);

  tftText(8,186,2,TFT_MUTED,TFT_BG,"CAL");
  tftText(131,186,2,TFT_MUTED,TFT_BG,"WIFI");
  tftText(239,186,2,TFT_MUTED,TFT_BG,"BT");
  tftText(8,216,2,TFT_MUTED,TFT_BG,"IP");
  tftText(207,216,2,TFT_MUTED,TFT_BG,"TOUCH");
}

static void updateDashboard(){
  if(controller.model==forkrot::Model::G800DXA){
    if(millis()-lastDisplayMs<500 && !tftNeedsRedraw)return;
    lastDisplayMs=millis();
    if(tftNeedsRedraw){
      tft.fillScreen(TFT_BG);tft.fillRoundRect(3,3,314,29,5,TFT_BLUE);
      tftText(10,9,2,TFT_TEXT,TFT_BLUE,"G-800DXA / UA1CFM");
      tftButton(3,180,118,26,"CAL");tftButton(202,210,115,26,"TOUCH");
      tftNeedsRedraw=false;
    }
    tft.fillRect(3,36,314,138,TFT_BG);
    tftText(12,40,2,TFT_MUTED,TFT_BG,"AZIMUTH");
    tftText(12,62,5,TFT_AZ,TFT_BG,String(currentAz,1));
    tftText(12,110,2,TFT_TEXT,TFT_BG,String("TARGET ")+String(targetAz,1));
    tftText(12,136,2,TFT_MUTED,TFT_BG,String(controller.pendingValid?"WAIT ":"MODE ")+
      (controller.pendingValid?String(controller.pending.az,1):String(forkrot::Controller::modeName(controller.mode))));
    tftText(12,160,1,TFT_TEXT,TFT_BG,String(gsAzMode==36?"P36 ":"P45 ")+motion()+" "+tftCalibrationText());
    tft.fillRect(126,180,191,27,TFT_BG);
    tftText(130,187,1,TFT_TEXT,TFT_BG,WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString());
    return;
  }
  // 4 Hz is enough for a rotator display and avoids wasting SPI bandwidth.
  if(millis()-lastDisplayMs<500 && !tftNeedsRedraw) return;
  lastDisplayMs=millis();

  if(tftNeedsRedraw){
    drawDashboardStatic();
    tftNeedsRedraw=false;
  }

  // AZ / EL: show integer degrees only.
  int azShown=(int)lroundf(currentAz);
  int elShown=(int)lroundf(currentEl);

  if((int)dashLastAz!=azShown){
    tft.fillRect(10,63,140,37,TFT_BG);
    tft.setTextSize(4); tft.setTextColor(TFT_AZ,TFT_BG); tft.setCursor(16,67);
    tft.print(azShown); tft.setTextSize(2); tft.print((char)247);
    dashLastAz=(float)azShown;
  }

  if((int)dashLastEl!=elShown){
    tft.fillRect(170,63,140,37,TFT_BG);
    tft.setTextSize(4); tft.setTextColor(TFT_EL,TFT_BG); tft.setCursor(176,67);
    tft.print(elShown); tft.setTextSize(2); tft.print((char)247);
    dashLastEl=(float)elShown;
  }

  // TARGET only, integer degrees, on the SAME line as "TARGET -".
  int taShown=(int)lroundf(targetAz);
  int teShown=(int)lroundf(targetEl);

  bool needTarget =
      (targetValid!=dashLastTargetValid) ||
      (targetValid && (
        (int)dashLastTargetAz!=taShown ||
        (int)dashLastTargetEl!=teShown));

  if(needTarget){
    tft.fillRect(100,109,50,24,TFT_BG);
    tft.fillRect(260,109,50,24,TFT_BG);

    if(targetValid){
      tftText(100,112,2,TFT_TARGET,TFT_BG,String(taShown)+String((char)247));
      tftText(260,112,2,TFT_TARGET,TFT_BG,String(teShown)+String((char)247));
    }else{
      tftText(100,112,2,TFT_MUTED,TFT_BG,"---");
      tftText(260,112,2,TFT_MUTED,TFT_BG,"---");
    }

    dashLastTargetValid=targetValid;
    dashLastTargetAz=(float)taShown;
    dashLastTargetEl=(float)teShown;
  }

  // Progress bars: only change when width changes.
  float azMax=activeAzMaxDeg();
  int azw=(int)constrain(currentAz/azMax*134.0f,0.0f,134.0f);
  int elw=(int)constrain(currentEl/180.0f*134.0f,0.0f,134.0f);

  if(azw!=dashLastAzBar){
    // Instead of clearing the whole bar every time, change only the difference.
    if(dashLastAzBar<0){
      tft.fillRect(13,145,134,8,TFT_BG);
      if(azw>0) tft.fillRect(13,145,azw,8,TFT_AZ);
    }else if(azw>dashLastAzBar){
      tft.fillRect(13+dashLastAzBar,145,azw-dashLastAzBar,8,TFT_AZ);
    }else if(azw<dashLastAzBar){
      tft.fillRect(13+azw,145,dashLastAzBar-azw,8,TFT_BG);
    }
    dashLastAzBar=azw;
  }

  if(elw!=dashLastElBar){
    if(dashLastElBar<0){
      tft.fillRect(173,145,134,8,TFT_BG);
      if(elw>0) tft.fillRect(173,145,elw,8,TFT_EL);
    }else if(elw>dashLastElBar){
      tft.fillRect(173+dashLastElBar,145,elw-dashLastElBar,8,TFT_EL);
    }else if(elw<dashLastElBar){
      tft.fillRect(173+elw,145,dashLastElBar-elw,8,TFT_BG);
    }
    dashLastElBar=elw;
  }

  // Motion text: update only on state change.
  // Read each axis directly from its GPIOs so simultaneous motion
  // (e.g. UP+RIGHT) is displayed correctly.
  bool left  = digitalRead(PIN_LEFT);
  bool right = digitalRead(PIN_RIGHT);
  bool up    = digitalRead(PIN_UP);
  bool down  = digitalRead(PIN_DOWN);

  bool azMoving = left || right;
  bool elMoving = up || down;

  String azState = right ? "RIGHT" :
                   left  ? "LEFT"  : "STOP";

  String elState = up   ? "UP"   :
                   down ? "DOWN" : "STOP";

  if(azState!=dashLastAzState){
    tft.fillRect(60,158,90,17,TFT_BG);
    tftText(64,160,2,azMoving?TFT_OK:TFT_STOP,TFT_BG,azState);
    dashLastAzState=azState;
  }

  if(elState!=dashLastElState){
    tft.fillRect(218,158,92,17,TFT_BG);
    tftText(222,160,2,elMoving?TFT_OK:TFT_STOP,TFT_BG,elState);
    dashLastElState=elState;
  }

  // Bottom status fields: repaint only when text changes.
  String calText=(calibrationMode==CAL_MULTI_POINT)?"MULTI":"2PT";
  if(calText!=dashLastCal){
    tft.fillRect(48,183,68,20,TFT_BG);
    tftText(50,186,2,selectedCalibrationValid()?TFT_OK:TFT_WARN,TFT_BG,calText);
    dashLastCal=calText;
  }

  String wifiText;
  uint16_t wifiColor;
  if(WiFi.status()==WL_CONNECTED){
    wifiText=String(WiFi.RSSI());
    wifiColor=TFT_OK;
  }else{
    wifiText="AP";
    wifiColor=TFT_WARN;
  }
  if(wifiText!=dashLastWifi){
    tft.fillRect(183,183,40,20,TFT_BG);
    tftText(184,186,2,wifiColor,TFT_BG,wifiText);
    dashLastWifi=wifiText;
  }

  bool bt=digitalRead(PIN_HC05_STATE);
  String btText=bt?"ON":"OFF";
  if(btText!=dashLastBt){
    tft.fillRect(271,183,40,20,TFT_BG);
    tftText(272,186,2,bt?TFT_OK:TFT_MUTED,TFT_BG,btText);
    dashLastBt=btText;
  }

  String ip=(WiFi.status()==WL_CONNECTED)?WiFi.localIP().toString():WiFi.softAPIP().toString();
  bool tcpConnected=(rotctldClient && rotctldClient.connected());
  if(ip!=dashLastIp || tcpConnected!=dashLastTcpConnected){
    tft.fillRect(34,213,160,20,TFT_BG);
    tftText(36,216,2,tcpConnected?TFT_OK:TFT_TEXT,TFT_BG,ip);
    dashLastIp=ip;
    dashLastTcpConnected=tcpConnected;
  }

  String touchText=touchCalValid?"OK":"CAL";
  if(touchText!=dashLastTouch){
    tft.fillRect(273,213,39,20,TFT_BG);
    tftText(274,216,2,touchCalValid?TFT_OK:TFT_WARN,TFT_BG,touchText);
    dashLastTouch=touchText;
  }
}

static void drawCalHeader(const String &title){
  tft.fillScreen(TFT_BG);
  tft.fillRoundRect(3,3,314,29,5,TFT_BLUE);
  tftText(10,9,2,TFT_TEXT,TFT_BLUE,title);
  tftButton(252,5,63,25,"BACK");
}

static void drawCalHome(){
  drawCalHeader("CALIBRATION");

  tftText(8,39,2,TFT_MUTED,TFT_BG,"MODE");
  tftButton(8,55,145,34,"2-POINT",calibrationMode==CAL_GS232_2POINT);
  tftButton(167,55,145,34,"MULTI",calibrationMode==CAL_MULTI_POINT);

  tftText(8,96,2,TFT_MUTED,TFT_BG,"AZ RANGE");
  tftButton(8,112,145,34,"P36",gsAzMode==36);
  tftButton(167,112,145,34,"P45",gsAzMode==45);

  tftButton(8,166,145,52,"AZ CAL");
  if(controller.model==forkrot::Model::G5500)tftButton(167,166,145,52,"EL CAL");

  tftText(8,223,2,TFT_MUTED,TFT_BG,
          String("A")+String(adcAvgMv(PIN_AZ_ADC))+
          (controller.model==forkrot::Model::G5500?String(" E")+String(adcAvgMv(PIN_EL_ADC)):String(""))+" mV");
}

static void drawCalAz(){
  drawCalHeader(String("AZ CAL ")+(gsAzMode==36?"P36":"P45"));
  int mv=adcAvgMv(PIN_AZ_ADC);
  tftText(8,40,2,TFT_AZ,TFT_BG,String("ADC ")+String(mv)+" mV");

  if(calibrationMode==CAL_GS232_2POINT){
    tftButton(8,70,145,48,"O = 0",false,activeAzOSet());
    tftButton(167,70,145,48,gsAzMode==36?"F = 360":"F = 450",false,activeAzFSet());
    tftText(8,130,2,TFT_MUTED,TFT_BG,
            String("O:")+(activeAzOSet()?String(activeAz2Zero()):"---"));
    tftText(167,130,2,TFT_MUTED,TFT_BG,
            String("F:")+(activeAzFSet()?String(activeAz2Full()):"---"));
    tftButton(8,166,304,45,"RESET AZ");
  }else{
    const int vals36[5]={0,90,180,270,360};
    const int vals45[6]={0,90,180,270,360,450};
    int n=gsAzMode==36?5:6;
    for(int i=0;i<n;i++){
      int row=i/3, col=i%3;
      int x=8+col*104, y=68+row*58;
      int deg=gsAzMode==36?vals36[i]:vals45[i];
      int val=gsAzMode==36?az36CalMv[i]:az45CalMv[i];
      tftButton(x,y,96,44,String(deg),false,val>=0);
    }
    tftButton(8,190,304,38,"RESET AZ");
  }
}

static void drawCalEl(){
  drawCalHeader("EL CAL");
  int mv=adcAvgMv(PIN_EL_ADC);
  tftText(8,40,2,TFT_EL,TFT_BG,String("ADC ")+String(mv)+" mV");

  if(calibrationMode==CAL_GS232_2POINT){
    tftButton(8,70,145,48,"O2 = 0",false,elOSet);
    tftButton(167,70,145,48,"F2 = 180",false,elFSet);
    tftText(8,130,2,TFT_MUTED,TFT_BG,String("O2:")+(elOSet?String(cal.el0):"---"));
    tftText(167,130,2,TFT_MUTED,TFT_BG,String("F2:")+(elFSet?String(cal.elMax):"---"));
    tftButton(8,166,304,45,"RESET EL");
  }else{
    const int vals[5]={0,45,90,135,180};
    for(int i=0;i<5;i++){
      int row=i/3, col=i%3;
      int x=8+col*104, y=68+row*58;
      tftButton(x,y,96,44,String(vals[i]),false,elCalMv[i]>=0);
    }
    tftButton(8,190,304,38,"RESET EL");
  }
}


static void drawTouchCalibration(){
  tft.fillScreen(TFT_BG);
  tft.fillRoundRect(3,3,314,29,5,TFT_BLUE);
  tftText(10,9,2,TFT_TEXT,TFT_BLUE,"TOUCH CALIBRATION");

  if(touchCalStep>=4){
    tftText(42,64,2,touchCalValid?TFT_OK:TFT_STOP,TFT_BG,
            touchCalValid?"CALIBRATION SAVED":"CALIBRATION FAILED");
    if(touchCalValid){
      tftText(12,90,2,TFT_MUTED,TFT_BG,
              String("X ")+String(touchX0Raw)+"-"+String(touchX1Raw));
      tftText(12,108,2,TFT_MUTED,TFT_BG,
              String("Y ")+String(touchY0Raw)+"-"+String(touchY1Raw));
      tftText(12,126,2,TFT_MUTED,TFT_BG,
              String("SWAP ")+(touchSwapXY?"YES":"NO"));
    }else{
      tftText(12,104,2,TFT_WARN,TFT_BG,"PRESS FIRMLY");
    }
    tftButton(65,155,190,48,touchCalValid?"BACK":"RETRY");
    return;
  }

  static const char* names[4]={"TOP LEFT","TOP RIGHT","BOTTOM RIGHT","BOTTOM LEFT"};
  static const int px[4]={20,299,299,20};
  static const int py[4]={20,20,219,219};

  tftText(16,39,2,TFT_MUTED,TFT_BG,"PRESS CROSS");
  tftText(16,57,2,TFT_TEXT,TFT_BG,String(touchCalStep+1)+"/4  "+String(names[touchCalStep]));

  int x=px[touchCalStep], y=py[touchCalStep];
  tft.drawCircle(x,y,10,TFT_WARN);
  tft.drawFastHLine(x-15,y,31,TFT_WARN);
  tft.drawFastVLine(x,y-15,31,TFT_WARN);
}

static void startTouchCalibration(){
  touchCalStep=0;
  touchWaitRelease=true;
  for(int i=0;i<4;i++){ touchCalRawX[i]=0; touchCalRawY[i]=0; }
  tftPage=TFT_PAGE_TOUCH_CAL;
  tftNeedsRedraw=true;
  drawTftPage();
}

static bool readTouchRawAverage(int &rx,int &ry){
  if(!touch.touched()) return false;
  long sx=0, sy=0;
  int n=0;
  for(int i=0;i<12;i++){
    if(touch.touched()){
      TS_Point p=touch.getPoint();
      if(p.z==0 || p.z>=150){
        sx+=p.x; sy+=p.y; n++;
      }
    }
    delay(3);
  }
  if(n<5) return false;
  rx=(int)(sx/n);
  ry=(int)(sy/n);
  return true;
}

static void finishTouchCalibration(){
  long horizDX=labs(touchCalRawX[1]-touchCalRawX[0]) + labs(touchCalRawX[2]-touchCalRawX[3]);
  long horizDY=labs(touchCalRawY[1]-touchCalRawY[0]) + labs(touchCalRawY[2]-touchCalRawY[3]);

  touchSwapXY=(horizDY>horizDX);

  int sx0 = touchSwapXY ? touchCalRawY[0] : touchCalRawX[0];
  int sx1 = touchSwapXY ? touchCalRawY[1] : touchCalRawX[1];
  int sx2 = touchSwapXY ? touchCalRawY[2] : touchCalRawX[2];
  int sx3 = touchSwapXY ? touchCalRawY[3] : touchCalRawX[3];

  int sy0 = touchSwapXY ? touchCalRawX[0] : touchCalRawY[0];
  int sy1 = touchSwapXY ? touchCalRawX[1] : touchCalRawY[1];
  int sy2 = touchSwapXY ? touchCalRawX[2] : touchCalRawY[2];
  int sy3 = touchSwapXY ? touchCalRawX[3] : touchCalRawY[3];

  touchX0Raw=(sx0+sx3)/2;
  touchX1Raw=(sx1+sx2)/2;
  touchY0Raw=(sy0+sy1)/2;
  touchY1Raw=(sy2+sy3)/2;

  touchCalValid=(abs(touchX1Raw-touchX0Raw)>500 &&
                 abs(touchY1Raw-touchY0Raw)>500);
  saveTouchCalibration();
}

static void serviceTouchCalibration(){
  if(tftPage!=TFT_PAGE_TOUCH_CAL) return;

  if(touchCalStep>=4){
    if(!touch.touched()){ touchWaitRelease=false; return; }
    if(touchWaitRelease) return;

    int rx,ry;
    if(!readTouchRawAverage(rx,ry)) return;

    if(!touchCalValid){
      // On failed calibration, accept the lower half of the panel in RAW form
      // as RETRY so recovery never depends on a bad coordinate map.
      startTouchCalibration();
      return;
    }

    int x,y;
    if(readTouchXY(x,y) && hit(x,y,45,145,230,68)){
      tftPage=TFT_PAGE_DASH;
      tftNeedsRedraw=true;
      drawTftPage();
      touchWaitRelease=true;
    }
    return;
  }

  if(!touch.touched()){
    touchWaitRelease=false;
    return;
  }
  if(touchWaitRelease) return;

  int rx,ry;
  if(!readTouchRawAverage(rx,ry)) return;

  touchCalRawX[touchCalStep]=rx;
  touchCalRawY[touchCalStep]=ry;
  touchCalStep++;
  touchWaitRelease=true;

  if(touchCalStep>=4) finishTouchCalibration();
  tftNeedsRedraw=true;
  drawTftPage();
}


static void showHeaderImage(){
  // Full-screen image, but controller logic keeps running normally.
  tft.fillScreen(TFT_BG);
  tft.drawRGBBitmap(0,0,catImage,CAT_IMAGE_W,CAT_IMAGE_H);
  imagePageStartedMs=millis();
  tftPage=TFT_PAGE_IMAGE;
  tftNeedsRedraw=false;
}

static void serviceHeaderImage(){
  if(tftPage!=TFT_PAGE_IMAGE) return;
  if(millis()-imagePageStartedMs>=IMAGE_PAGE_DURATION_MS){
    tftPage=TFT_PAGE_DASH;
    tftNeedsRedraw=true;
    drawTftPage();
  }
}

static void drawTftPage(){
  if(tftPage==TFT_PAGE_DASH) drawDashboardStatic();
  else if(tftPage==TFT_PAGE_CAL_HOME) drawCalHome();
  else if(tftPage==TFT_PAGE_CAL_AZ) drawCalAz();
  else if(tftPage==TFT_PAGE_CAL_EL) drawCalEl();
  else if(tftPage==TFT_PAGE_TOUCH_CAL) drawTouchCalibration();
  else if(tftPage==TFT_PAGE_IMAGE) return;
  tftNeedsRedraw=false;
}

static bool hit(int x,int y,int bx,int by,int bw,int bh){
  return x>=bx && x<bx+bw && y>=by && y<by+bh;
}

static bool readTouchXY(int &x,int &y){
  if(!touch.touched()) return false;
  TS_Point p=touch.getPoint();

  // Ignore extremely weak/noisy contacts.
  if(p.z>0 && p.z<150) return false;

  int rawX=touchSwapXY ? p.y : p.x;
  int rawY=touchSwapXY ? p.x : p.y;

  long sx,sy;
  if(touchCalValid){
    sx=20L+(long)(rawX-touchX0Raw)*(299-20)/(long)(touchX1Raw-touchX0Raw);
    sy=20L+(long)(rawY-touchY0Raw)*(219-20)/(long)(touchY1Raw-touchY0Raw);
  }else{
    sx=map((long)p.y,TOUCH_RAW_Y_MIN,TOUCH_RAW_Y_MAX,0,319);
    sy=map((long)p.x,TOUCH_RAW_X_MIN,TOUCH_RAW_X_MAX,239,0);
  }

  x=constrain((int)sx,0,319);
  y=constrain((int)sy,0,239);
  return true;
}

static void resetTouchCalAxis(bool az){
  if(calibrationMode==CAL_GS232_2POINT){
    if(az){
      if(gsAzMode==36){
        cal.az36_0=0; cal.az36Max=3100;
        az36OSet=false; az36FSet=false; az36LinearValid=false;
      }else{
        cal.az45_0=0; cal.az45Max=3100;
        az45OSet=false; az45FSet=false; az45LinearValid=false;
      }
    }else{
      cal.el0=0; cal.elMax=3100;
      elOSet=false; elFSet=false; elLinearValid=false;
    }
  }else{
    if(az){
      if(gsAzMode==36) for(uint8_t i=0;i<AZ36_CAL_N;i++) az36CalMv[i]=-1;
      else for(uint8_t i=0;i<AZ45_CAL_N;i++) az45CalMv[i]=-1;
    }else{
      for(uint8_t i=0;i<EL_CAL_N;i++) elCalMv[i]=-1;
    }
    refreshMultiCalValidity();
  }
  saveCal();
  tftNeedsRedraw=true;
}

static void captureTouchMultiAz(uint8_t idx){
  int mv=adcAvgMv(PIN_AZ_ADC);
  if(gsAzMode==36){
    if(idx<AZ36_CAL_N) az36CalMv[idx]=mv;
  }else{
    if(idx<AZ45_CAL_N) az45CalMv[idx]=mv;
  }
  refreshMultiCalValidity();
  saveCal();
  tftNeedsRedraw=true;
}

static void captureTouchMultiEl(uint8_t idx){
  if(controller.model==forkrot::Model::G800DXA)return;
  if(idx>=EL_CAL_N) return;
  elCalMv[idx]=adcAvgMv(PIN_EL_ADC);
  refreshMultiCalValidity();
  saveCal();
  tftNeedsRedraw=true;
}


static void serviceTouchRecoveryHold(){
  if(tftPage!=TFT_PAGE_DASH) {
    touchHoldStartMs=0;
    touchHoldArmed=false;
    return;
  }

  if(touch.touched()){
    if(!touchHoldArmed){
      touchHoldArmed=true;
      touchHoldStartMs=millis();
    }else if(millis()-touchHoldStartMs>=2500){
      touchHoldArmed=false;
      touchHoldStartMs=0;
      startTouchCalibration();
    }
  }else{
    touchHoldArmed=false;
    touchHoldStartMs=0;
  }
}

static void serviceTouch(){
  if(tftPage==TFT_PAGE_IMAGE) return;
  if(tftPage==TFT_PAGE_TOUCH_CAL){ serviceTouchCalibration(); return; }

  // Raw long-press recovery does not depend on touch calibration.
  serviceTouchRecoveryHold();
  if(tftPage==TFT_PAGE_TOUCH_CAL) return;

  int x,y;
  if(!readTouchXY(x,y)) return;
  if(millis()-lastTouchMs<260) return;
  lastTouchMs=millis();

  // BACK exists on every calibration page.
  if(tftPage!=TFT_PAGE_DASH && hit(x,y,235,0,85,40)){
    if(tftPage==TFT_PAGE_CAL_HOME) tftPage=TFT_PAGE_DASH;
    else tftPage=TFT_PAGE_CAL_HOME;
    tftNeedsRedraw=true;
    drawTftPage();
    return;
  }

  if(tftPage==TFT_PAGE_DASH){
    // Tap the top title bar to show the cat image for 3 seconds.
    if(hit(x,y,0,0,320,40)){
      showHeaderImage();
    }else if(hit(x,y,0,174,125,36)){
      tftPage=TFT_PAGE_CAL_HOME;
      tftNeedsRedraw=true;
      drawTftPage();
    }else if(hit(x,y,198,204,122,36)){
      startTouchCalibration();
    }
    return;
  }

  if(tftPage==TFT_PAGE_CAL_HOME){
    if(hit(x,y,8,55,145,34)){
      setCalibrationMode(CAL_GS232_2POINT);
      saveCal();
      tftNeedsRedraw=true;
    }else if(hit(x,y,167,55,145,34)){
      setCalibrationMode(CAL_MULTI_POINT);
      saveCal();
      tftNeedsRedraw=true;
    }else if(hit(x,y,8,112,145,34)){
      setGsAzMode(36);
      saveCal();
      tftNeedsRedraw=true;
    }else if(hit(x,y,167,112,145,34)){
      setGsAzMode(45);
      saveCal();
      tftNeedsRedraw=true;
    }else if(hit(x,y,8,166,145,52)){
      tftPage=TFT_PAGE_CAL_AZ; tftNeedsRedraw=true;
    }else if(controller.model==forkrot::Model::G5500 && hit(x,y,167,166,145,52)){
      tftPage=TFT_PAGE_CAL_EL; tftNeedsRedraw=true;
    }
    if(tftNeedsRedraw) drawTftPage();
    return;
  }

  if(tftPage==TFT_PAGE_CAL_AZ){
    if(calibrationMode==CAL_GS232_2POINT){
      if(hit(x,y,8,70,145,48)) captureGs232Cal("O");
      else if(hit(x,y,167,70,145,48)) captureGs232Cal("F");
      else if(hit(x,y,4,158,312,62)) resetTouchCalAxis(true);
    }else{
      int n=gsAzMode==36?5:6;
      for(int i=0;i<n;i++){
        int row=i/3, col=i%3;
        if(hit(x,y,8+col*104,68+row*58,96,44)){
          captureTouchMultiAz((uint8_t)i);
          break;
        }
      }
      if(hit(x,y,4,184,312,52)) resetTouchCalAxis(true);
    }
    tftNeedsRedraw=true;
    drawTftPage();
    return;
  }

  if(tftPage==TFT_PAGE_CAL_EL){
    if(calibrationMode==CAL_GS232_2POINT){
      if(hit(x,y,8,70,145,48)) captureGs232Cal("O2");
      else if(hit(x,y,167,70,145,48)) captureGs232Cal("F2");
      else if(hit(x,y,4,158,312,62)) resetTouchCalAxis(false);
    }else{
      for(int i=0;i<5;i++){
        int row=i/3, col=i%3;
        if(hit(x,y,8+col*104,68+row*58,96,44)){
          captureTouchMultiEl((uint8_t)i);
          break;
        }
      }
      if(hit(x,y,4,184,312,52)) resetTouchCalAxis(false);
    }
    tftNeedsRedraw=true;
    drawTftPage();
  }
}

static void initLocalDisplay(){
  pinMode(PIN_TFT_CS,OUTPUT);
  pinMode(PIN_TOUCH_CS,OUTPUT);
  digitalWrite(PIN_TFT_CS,HIGH);
  digitalWrite(PIN_TOUCH_CS,HIGH);

  SPI.begin(PIN_TFT_SCK,PIN_TFT_MISO,PIN_TFT_MOSI,PIN_TFT_CS);
  tft.begin();
  tft.setRotation(1);

  touch.begin(SPI);
  touch.setRotation(1);

  tft.fillScreen(TFT_BG);
  tftText(23,62,2,TFT_TEXT,TFT_BG,"G-5500 " FORK_CHIP_NAME);
  tftText(40,92,2,TFT_TARGET,TFT_BG,"TFT + TOUCH");
  tftText(72,122,2,TFT_OK,TFT_BG,"UA1CFM");
  tftText(38,154,2,TFT_WARN,TFT_BG,"FW v1.6.23 P36/P45");
  delay(1200);

  tftPage=TFT_PAGE_DASH;
  tftNeedsRedraw=true;
}

static void updateLocalDisplay(){
  if(tftPage==TFT_PAGE_IMAGE){
    serviceHeaderImage();
    return;
  }
  if(tftPage==TFT_PAGE_DASH) updateDashboard();
  else if(tftNeedsRedraw) drawTftPage();
}

void controllerSetup(){
  bootResetReason = esp_reset_reason();
  bootResetReasonText = resetReasonToText(bootResetReason);

  // Preload output latches LOW before enabling output mode to reduce relay glitches at boot.
  digitalWrite(PIN_LEFT,LOW);
  digitalWrite(PIN_RIGHT,LOW);
  digitalWrite(PIN_UP,LOW);
  digitalWrite(PIN_DOWN,LOW);
  pinMode(PIN_LEFT,OUTPUT);
  pinMode(PIN_RIGHT,OUTPUT);
  pinMode(PIN_UP,OUTPUT);
  pinMode(PIN_DOWN,OUTPUT);
  allStop();
  rgbSet(0,0,0);
  // UART0 USB: PSTROTATOR GS-232 working channel
  Serial0.begin(9600);

  // HC-05 Bluetooth Classic on UART1
  pinMode(PIN_HC05_STATE,INPUT);
  HC05Serial.begin(HC05_BAUD, SERIAL_8N1, PIN_HC05_RX, PIN_HC05_TX);

  // Native USB Serial/JTAG: diagnostics/programming
#if FORK_NATIVE_USB
  HWCDCSerial.begin();
#endif

  analogReadResolution(12);analogSetPinAttenuation(PIN_AZ_ADC,ADC_11db);analogSetPinAttenuation(PIN_EL_ADC,ADC_11db);
  serialCmd.reserve(80);
  hc05Cmd.reserve(80);
  initLocalDisplay();
  loadForkSettings();
  loadPrefs();
  controller.maximum=activeAzMaxDeg();
  loadProfilePresets();
  pinMode(PIN_CONFIRM,INPUT_PULLUP);
  pinMode(PIN_SPEED,OUTPUT);digitalWrite(PIN_SPEED,LOW);
  loadTouchCalibration();

  // IMPORTANT: before the first successful touch calibration there is no
  // trustworthy screen-coordinate mapping, so do not require the user to hit
  // a mapped button. Start the raw 4-point wizard automatically.
  if(!touchCalValid){
    startTouchCalibration();
  }else{
    tftPage=TFT_PAGE_DASH;
    tftNeedsRedraw=true;
    drawTftPage();
  }
  initRssiHistory();

  dbgPrintln("");
  dbgPrintln("######## FIRMWARE v1.6.23 TFT TOUCH CAL + HC-05 ########");
  dbgPrintln("[DIAG] Web console: /diaglog, RAM ring buffer 160 lines");
  dbgPrint("[RESET REASON] "); dbgPrintln(bootResetReasonText);
  dbgPrint("[RESET CODE] "); dbgPrintln(String((int)bootResetReason));
  dbgPrintln("[V1559] USB debug alive");
  dbgPrintln("[V1559] internal BLE disabled; external HC-05 enabled");
  startWifi();
  setupLoggerUdp();

  bool timeOk=syncClockFromNtp();
  addResetHistoryEntry();
  dbgPrint("[TIME SYNC] "); dbgPrintln(timeOk ? "NTP OK" : "NO NTP - history entry has no exact time");

  dbgPrintln("");
  dbgPrintln("=== G5500 " FORK_CHIP_NAME " v1.6.23 TFT TOUCH CAL ===");
  dbgPrintln("Copyright (c) UA1CFM");
  dbgPrintln("UART0 USB: PSTROTATOR GS-232, 9600 baud (COM number assigned by Windows)");
  dbgPrintln("Native USB " FORK_USB_NAME ": diagnostics + programming (COM number assigned by Windows)");
  dbgPrintln("TCP 4533: Look4Sat/Hamlib + GS-232B full command set");
  dbgPrintln("R4UAB DDE Client TCP: decimal comma accepted in Hamlib P az el command");
  dbgPrintln("BUGFIX: Serial/Web manual or target commands cancel active GS-232 timed sequence");
  dbgPrintln("AP password: configurable from Web UI; empty = open, 8..63 chars = WPA2");
  dbgPrintln("RSSI history: 1h/1d/1w/1m/1y, year saved daily");
  dbgPrintln("Reset history: last 20 boots stored in NVS with NTP UTC time when available");
  dbgPrintln("STOP preserves the mode; preset discards incoming movement while active");
  dbgPrintln("Fork modes: numerical remote targets wait in manual; jog commands always execute");
  dbgPrintln("Calibration: GS232 2-point + multi-point; separate AZ P36/P45 tables, shared EL");
  dbgPrintln("ADC averaging: 25 samples, matching LVBTrack smoothing depth");
  dbgPrintln("Manual axis behavior: AZ manual cancels AZ track only; EL manual cancels EL track only");
  dbgPrintln("Boot outputs: LOW preloaded before pinMode(OUTPUT)");
  dbgPrintln("Web calibration: explicit GS232 2-POINT / MULTI-POINT mode selector");
  dbgPrintln("Web calibration menu: collapsible, default closed");
  dbgPrintln("Calibration buttons: saved points highlighted in green");
  dbgPrintln("Calibration data stored separately; separate reset for each mode and axis");
  dbgPrintln("TFT SPI: MOSI GPIO10, SCK GPIO11, MISO GPIO2, TFT_CS GPIO12, DC GPIO13, RST GPIO14");
  dbgPrintln("XPT2046: TOUCH_CS GPIO8, TOUCH_IRQ GPIO9; shared SPI bus");
    dbgPrintln("Fork control: calibrated target movement, 300ms reverse dead-time");
  dbgPrintln("Web main cards: current / target shown inline for AZ and EL");
  dbgPrintln("TFT: ILI9341 320x240 dashboard + XPT2046 touch calibration UI");
  dbgPrintln("TFT calibration: 2-POINT / MULTI-POINT, P36/P45, AZ/EL capture + reset");
  dbgPrint("[WiFi AP]  ");
  dbgPrintln(WiFi.softAPIP().toString());
  if(WiFi.status()==WL_CONNECTED){
    dbgPrint("[WiFi LAN] ");
    dbgPrintln(WiFi.localIP().toString());
  }else{
    dbgPrintln("[WiFi LAN] not connected");
  }
  dbgPrintln("==============================");

  lastHeartbeatMs=millis();
}
void controllerLoop(){
  updatePosition();
  controller.tick(millis()); // expire the hour before accepting any external command
  if(confirmButton.sample(digitalRead(PIN_CONFIRM)==HIGH,millis()))controller.execute(millis());
  serviceLoggerUdp();
  handlePstRotatorSerial();   // UART0 USB / PSTROTATOR / GS-232
  handleHc05Serial();         // HC-05 Bluetooth Classic / GS-232B
  handleRotctldTcp();         // Wi-Fi TCP 4533 / Look4Sat / Hamlib rotctld
  web.handleClient();
  serviceWifiReconnect();
  serviceRssiHistory();
  serviceGsSequence();
  controlRotator();
  serviceTouch();
  updateLocalDisplay();
  updateRgb();
  delay(5);
}

// Fork hardware adapter. All GPIO direction writes pass through this function.
static void applyOutputs(){
  const int neg[2]={PIN_LEFT,PIN_DOWN},pos[2]={PIN_RIGHT,PIN_UP};
  for(unsigned i=0;i<2;++i){
    int d=controller.axes[i].direction;
    if(d<=0)digitalWrite(pos[i],LOW);
    if(d>=0)digitalWrite(neg[i],LOW);
    if(d<0)digitalWrite(neg[i],HIGH);
    if(d>0)digitalWrite(pos[i],HIGH);
  }
  targetAz=controller.axes[0].target;targetEl=controller.axes[1].target;targetValid=controller.targetKnown;
  azTrack=controller.axes[0].tracking;elTrack=controller.axes[1].tracking;
  bool enable=controller.model==forkrot::Model::G800DXA&&controller.speedEnabled;
  if(enable&&!pwmReady){pwmReady=ledcAttach(PIN_SPEED,20000,10);if(!pwmReady){controller.speedEnabled=false;controller.fault="pwm_attach_failed";}}
  if(pwmReady&&!enable){ledcWrite(PIN_SPEED,0);ledcDetach(PIN_SPEED);pwmReady=false;pinMode(PIN_SPEED,OUTPUT);digitalWrite(PIN_SPEED,LOW);}
  if(pwmReady)ledcWrite(PIN_SPEED,controller.axes[0].direction?controller.speedPercent*1023/100:0);
}
static void loadForkSettings(){
  prefs.begin("forkcfg",true);
  controller.model=prefs.getUChar("model",0)==1?forkrot::Model::G800DXA:forkrot::Model::G5500;
  controller.restoreMode(prefs.getUChar("mode",0)==1?forkrot::Mode::Auto:forkrot::Mode::Manual);
  controller.speedEnabled=prefs.getBool("speedon",false);
  controller.speedPercent=constrain(prefs.getUInt("speed",50),25u,100u);
  protocol.rotorName=prefs.getString("name","G5500").c_str();
  loggerSettings.n1mmPort=prefs.getUShort("nport",12040);loggerSettings.pstPort=prefs.getUShort("pport",1234);
  loggerSettings.reportPort=prefs.getUShort("rport",13010);loggerSettings.reportHost=prefs.getString("rhost","").c_str();
  prefs.end();
}
static void saveForkSettings(){
  prefs.begin("forkcfg",false);
  prefs.putUChar("model",controller.model==forkrot::Model::G800DXA?1:0);
  prefs.putUChar("mode",controller.mode==forkrot::Mode::Auto?1:0);
  prefs.putBool("speedon",controller.speedEnabled);prefs.putUInt("speed",controller.speedPercent);
  prefs.putString("name",protocol.rotorName.c_str());prefs.putUShort("nport",loggerSettings.n1mmPort);
  prefs.putUShort("pport",loggerSettings.pstPort);prefs.putUShort("rport",loggerSettings.reportPort);
  prefs.putString("rhost",loggerSettings.reportHost.c_str());prefs.end();
}
static const char* presetNamespace(){return controller.model==forkrot::Model::G800DXA?"fork800":"fork5500";}
static void loadProfilePresets(){
  prefs.begin(presetNamespace(),true);
  for(unsigned i=0;i<4;++i){
    String base="p"+String(i);
    controller.presets[i].label=prefs.getString((base+"n").c_str(),String(i*90)+"°").c_str();
    controller.presets[i].target={prefs.getFloat((base+"a").c_str(),i*90),prefs.getFloat((base+"e").c_str(),0),true,controller.model==forkrot::Model::G5500};
  }
  prefs.end();
}
static void saveProfilePresets(){
  prefs.begin(presetNamespace(),false);
  for(unsigned i=0;i<4;++i){String base="p"+String(i);prefs.putString((base+"n").c_str(),controller.presets[i].label.c_str());prefs.putFloat((base+"a").c_str(),controller.presets[i].target.az);prefs.putFloat((base+"e").c_str(),controller.presets[i].target.el);}
  prefs.end();
}
static void setupForkWeb(){
  web.on("/api/target",HTTP_POST,[]{
    forkrot::Form f;for(unsigned i=0;i<web.args();++i)f[web.argName(i).c_str()]=web.arg(i).c_str();
    updatePosition();auto r=forkrot::api(controller,protocol,loggerSettings,"/api/target",f,millis());
    controller.tick(millis());applyOutputs();if(r.status==200)targetValid=true;web.send(r.status,"application/json",r.body.c_str());
  });
  for(const char* route:{"/api/mode","/api/execute","/api/preset","/api/preset/run","/api/settings"}){
    String path=route;
    web.on(path,HTTP_POST,[path]{
      forkrot::Form form;for(unsigned i=0;i<web.args();++i)form[web.argName(i).c_str()]=web.arg(i).c_str();
      auto oldModel=controller.model;auto oldSettings=loggerSettings;
      updatePosition();
      auto response=forkrot::api(controller,protocol,loggerSettings,path.c_str(),form,millis());
      if(response.status==200){
        if(oldModel!=controller.model){
          // Old calibration was already saved on each calibration edit.
          loadPrefs();controller.maximum=activeAzMaxDeg();loadProfilePresets();tftNeedsRedraw=true;targetValid=false;
        }
        if(path=="/api/mode"||path=="/api/settings")saveForkSettings();
        if(path=="/api/preset")saveProfilePresets();
        if(oldSettings.n1mmPort!=loggerSettings.n1mmPort||oldSettings.pstPort!=loggerSettings.pstPort)setupLoggerUdp();
        if(path=="/api/execute"||path=="/api/preset/run")targetValid=true;
      }
      controller.tick(millis());applyOutputs();
      web.send(response.status,"application/json",response.body.c_str());
    });
  }
}
struct ReportPeer {IPAddress address;bool used=false;};
static ReportPeer reportPeers[8];
static uint8_t nextReportPeer=0;
static uint32_t lastReportMs=0;
static void setupLoggerUdp(){
  n1mmUdp.stop();pstUdp.stop();
  bool a=n1mmUdp.begin(loggerSettings.n1mmPort),b=pstUdp.begin(loggerSettings.pstPort);
  if(!a||!b)controller.fault="udp_bind_failed";
  for(auto& peer:reportPeers)peer.used=false;
}
static void serviceLoggerUdp(){
  for(WiFiUDP* socket:{&n1mmUdp,&pstUdp}){
    for(unsigned count=0;count<4;++count){
      int size=socket->parsePacket();if(size<=0)break;
      IPAddress sender=socket->remoteIP();
      if(size>4096){while(socket->available())socket->read();continue;}
      std::string text(size,'\0');int got=socket->read(reinterpret_cast<uint8_t*>(&text[0]),size);text.resize(got>0?got:0);
      auto packet=protocol.datagram(text,millis());
      if(packet.result==forkrot::Result::Invalid){dbgPrintln("[UDP] rejected invalid packet");continue;}
      if(packet.matched){flashRx();lastTargetRxMs=millis();if(packet.result==forkrot::Result::Ok||packet.result==forkrot::Result::Pending)targetValid=true;}
      if(packet.matched&&text.find("N1MMRotor")!=std::string::npos){
        bool known=false;for(auto peer:reportPeers)if(peer.used&&peer.address==sender)known=true;
        if(!known){reportPeers[nextReportPeer]={sender,true};nextReportPeer=(nextReportPeer+1)%8;}
      }
      if(!packet.reply.empty()){
        socket->beginPacket(sender,loggerSettings.pstPort+1);socket->write(reinterpret_cast<const uint8_t*>(packet.reply.data()),packet.reply.size());socket->endPacket();flashTx();
      }
    }
  }
  if(uint32_t(millis()-lastReportMs)>=500){
    lastReportMs=millis();
    String message=protocol.positionReport(currentAz).c_str();
    for(auto peer:reportPeers)if(peer.used){n1mmUdp.beginPacket(peer.address,loggerSettings.reportPort);n1mmUdp.print(message);n1mmUdp.endPacket();}
    IPAddress host;if(!loggerSettings.reportHost.empty()&&host.fromString(loggerSettings.reportHost.c_str())){n1mmUdp.beginPacket(host,loggerSettings.reportPort);n1mmUdp.print(message);n1mmUdp.endPacket();}
  }
}
