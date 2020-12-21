#include <TFT_eSPI.h>
#include <SPI.h>
#include "WiFi.h"
#include <Wire.h>
#include "Button2.h"
#include "esp_adc_cal.h"
#include <ArduinoJson.h>
#include <MQTT.h>
#include "GfxUi.h"
#include <SPIFFS.h>

//create a new file 'wifi_secret.h' with the two following constants
//the reson is that 'wifi_sercert.h' is ignored by git
//const char* ssid = "SSID";
//const char* password =  "PASSWORD";
#include "wifi_secret.h"

const char* host = "10.0.0.42";
int port = 1883;
const char *mqtt_client_id = "heat_controller";
const char* main_topic = "esp/remote 1";

const char* heater_topic        = "lzig/living heat";//"mzig/office heat"
const char* heater_topic_set    = "lzig/living heat/set";//"mzig/office heat/set"
const char* weather_topic    = "mzig/living heat weather";

typedef struct Heater_s { 
    bool set_available;
    float set;
    bool req_available;
    float req;
}Heater_t;

Heater_t heater = {false, 0, false, 0};

StaticJsonDocument<1024> json_payload;//1 KB

MQTTClient mqtt(1024);// 1KB
WiFiClient wifi;//needed to stay on global scope

#define ADC_PIN             34
#define BUTTON_1            35
#define BUTTON_2            0

TFT_eSPI tft = TFT_eSPI(135, 240); // Invoke custom library

const int line_height = 20;
const int line2_start = 25;
const int line3_start = 50;
const int line4_start = 75;
const int line5_start = 100;

Button2 btn1(BUTTON_1);
Button2 btn2(BUTTON_2);

GfxUi ui = GfxUi(&tft);

int g_vref = 1100;

uint64_t idle_timeStamp = 0;

void tft_off(){
    int r = digitalRead(TFT_BL);
    digitalWrite(TFT_BL, !r);
    tft.writecommand(TFT_DISPOFF);
    tft.writecommand(TFT_SLPIN);
}

void tft_log(const String &text){
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_LIGHTGREY);
    tft.setTextSize(2);
    tft.drawString(text.c_str(),  tft.width() / 2, tft.height() / 2 );
}

void timelog(String Text){
  Serial.println(String(millis())+" : "+Text);//micros()
}

void sleep_delay(int ms)
{
    esp_sleep_enable_timer_wakeup(ms * 1000);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_light_sleep_start();
}

void deep_sleep(){
    tft_log("Going to Sleep");
    sleep_delay(1000);
    ui.drawBmp("/esp_home.bmp", 0, 0);//'ui' needs tft and SPIFF
    sleep_delay(1000);
    tft_off();

    //After using light sleep, you need to disable timer wake, because here use external IO port to wake up
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
    //esp_sleep_enable_ext1_wakeup(GPIO_SEL_35|GPIO_SEL_0, ESP_EXT1_WAKEUP_ANY_HIGH);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);
    delay(200);
    esp_deep_sleep_start();
}

void light_sleep(){
    tft.fillScreen(TFT_BLACK);
    tft.drawString("Light Sleep 30 sec",  tft.width() / 2, tft.height() / 2 );

    sleep_delay(3000);
    tft_off();
    esp_deep_sleep(30*1000*1000);
}

void mqtt_connected();

void connect() {
    const int wait_time_sec = 30;
    int count = 0;
    Serial.print("checking wifi...");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(1000);
        if(count++ > wait_time_sec){
            tft_log("No Wifi");
            sleep_delay(3000);
            deep_sleep();
        }
    }

    count = 0;
    Serial.print("\nmqtt connecting...");
    while (!mqtt.connect(mqtt_client_id)) {
        Serial.print(".");
        delay(1000);
        if(count++ > wait_time_sec){
            tft_log("No MQTT");
            sleep_delay(3000);
            deep_sleep();
        }
    }
    mqtt_connected();
}

void mqtt_loop(){
  mqtt.loop();
  if (!mqtt.connected()) {
    connect();
  }
}


void idle_sleep_snooze(){
    idle_timeStamp = millis();
}

void idle_sleep_check(int sec){
    if (millis() - idle_timeStamp > (sec * 1000)) {
        deep_sleep();
    }
}

void adc_vref_init(){
    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize((adc_unit_t)ADC_UNIT_1, (adc_atten_t)ADC1_CHANNEL_6, (adc_bits_width_t)ADC_WIDTH_BIT_12, 1100, &adc_chars);
    //Check type of calibration value used to characterize ADC
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        Serial.printf("eFuse Vref:%u mV\n", adc_chars.vref);
        g_vref = adc_chars.vref;
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        Serial.printf("Two Point --> coeff_a:%umV coeff_b:%umV\n", adc_chars.coeff_a, adc_chars.coeff_b);
    } else {
        Serial.println("Default Vref: 1100mV");
    }
}

void report_battery(int sec)
{
    static uint64_t timeStamp = 0;
    if (millis() - timeStamp > (sec * 1000)) {
        timeStamp = millis();
        uint16_t v = analogRead(ADC_PIN);
        float battery_voltage = ((float)v / 4095.0) * 2.0 * 3.3 * (g_vref / 1000.0);
        String voltage_text = "Voltage :" + String(battery_voltage) + " V";
        Serial.println(voltage_text);
        String mqtt_payload = "{\"voltage\":" + String(battery_voltage) + "}";
        mqtt.publish(main_topic, mqtt_payload);
    }
}

void heat_request(int diff_request){
    float new_request;
    bool request_updated = false;
    if(heater.req_available){
        Serial.print("updating previous request "+String(heater.req));
        new_request = heater.req + diff_request;
        request_updated = true;
    }else if(heater.set_available){
        Serial.print("updating previous set "+String(heater.set));
        new_request = heater.set + diff_request;
        request_updated = true;
    }else{
        Serial.println("no reference available");
    }
    if(request_updated){
        Serial.println(" => new request : " + String(new_request));
        heater.req_available = true;
        heater.req = new_request;
        String str = "request " + String(new_request);
        tft.fillRect(0,0, tft.width() , line2_start, TFT_BLACK);
        tft.setTextColor(TFT_DARKGREY);
        tft.setTextDatum(TC_DATUM);
        tft.drawString(str.c_str(), tft.width() / 2, 0);
        String payload = "{\"current_heating_setpoint\":" + String(new_request) +"}";
        mqtt.publish(heater_topic_set, payload);
        Serial.println("mqtt publish : " + String(heater_topic_set) +" => "+ String(payload));
    }
}

void button_init()
{
    #ifdef USER_BUTTON_ISR
        pinMode(BUTTON_1, INPUT_PULLUP);
        attachInterrupt(BUTTON_1, buttons_isr, CHANGE);
        pinMode(BUTTON_2, INPUT_PULLUP);
        attachInterrupt(BUTTON_2, buttons_isr, CHANGE);
    #endif

    btn1.setLongClickHandler([](Button2 & b) {
        Serial.println("button 1 long click");
    });

    btn2.setLongClickHandler([](Button2 & b) {
        Serial.println("button 2 long click");
    });

    btn1.setPressedHandler([](Button2 & b){
        Serial.println("button 1 Press (-)");
        idle_sleep_snooze();
        heat_request(-1);
    });

    btn2.setPressedHandler([](Button2 & b){
        Serial.println("button 2 Press (+)");
        idle_sleep_snooze();
        heat_request(1);
    });


}

void mqtt_connected(){
    Serial.println("\nconnected!");
    mqtt.subscribe(heater_topic);
    mqtt.subscribe(weather_topic);
    mqtt.publish(main_topic, "{\"status\":\"connected\"}");

    //tft.fillScreen(TFT_BLACK);
    //tft.setTextDatum(MC_DATUM);
    //tft.drawString("MQTT connected", tft.width() / 2, tft.height() / 2);
}

void mqtt_weather_received(String &topic, String &payload) {
    deserializeJson(json_payload,payload);
    tft.setTextDatum(TC_DATUM);
    if(json_payload.containsKey("temperature")){
        float val_float = json_payload["temperature"];
        String str = "metal " + String(val_float);
        tft.fillRect(0,line5_start, tft.width() , tft.height()-line5_start, TFT_BLACK);
        tft.setTextColor(TFT_MAROON);
        tft.drawString(str.c_str(), tft.width() / 2, line5_start);
    }
}

void mqtt_heater_received(String &topic, String &payload) {
    deserializeJson(json_payload,payload);
    tft.fillRect(0,0, tft.width() , line5_start, TFT_BLACK);
    tft.setTextDatum(TC_DATUM);
    if(json_payload.containsKey("current_heating_setpoint")){
        tft.setTextColor(TFT_GREEN);
        float val_float = json_payload["current_heating_setpoint"];
        heater.set_available = true;
        heater.set = val_float;
        String str = "target " + String(val_float);
        tft.drawString(str.c_str(), tft.width() / 2, line2_start);
    }
    if(heater.req_available){
        if(heater.set_available && (heater.set == heater.req)){
            heater.req_available = false;
            Serial.println("request already handled");
        }else{
            tft.setTextColor(TFT_DARKGREY);
            String str = "request " + String(heater.req);
            tft.drawString(str.c_str(), tft.width() / 2, 0);
        }
    }
    if(json_payload.containsKey("local_temperature")){
        tft.setTextColor(TFT_DARKGREY);
        float val_float = json_payload["local_temperature"];
        String str = "current " + String(val_float);
        tft.drawString(str.c_str(), tft.width() / 2, line3_start);
    }
    if(json_payload.containsKey("pi_heating_demand")){
        tft.setTextColor(TFT_DARKGREY);
        int pi_int = json_payload["pi_heating_demand"];
        String pi_str = "flow " + String(pi_int);
        tft.drawString(pi_str.c_str(), tft.width() / 2, line4_start);
    }
}

void mqtt_received(String &topic, String &payload) {
    Serial.print("mqtt: "+topic+" => ");
    Serial.println(payload);
    if(topic == heater_topic){
        mqtt_heater_received(topic,payload);
    }else if(topic == weather_topic){
        mqtt_weather_received(topic,payload);
    }
}

void setup()
{
    Serial.begin(115200);
    timelog("begin");

    button_init();
    adc_vref_init();

    tft.init();
    tft.setRotation(3);
    SPIFFS.begin(true);//FORMAT_SPIFFS_IF_FAILED
    ui.drawBmp("/esp_home.bmp", 0, 0);//'ui' needs tft and SPIFF
    timelog("bitmap done");

    tft.setCursor(0, 0);
    tft.setTextColor(TFT_GREEN);
    tft.setTextSize(2);

    WiFi.begin(ssid, password);
    mqtt.begin(host,port, wifi);
    mqtt.onMessage(mqtt_received);

    timelog("setup done");
}

void loop()
{
    mqtt_loop();
    btn1.loop();
    btn2.loop();
    report_battery(30);
    idle_sleep_check(60);
}
