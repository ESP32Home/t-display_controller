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


#include "wifi_secret.h"

const char* host = "10.0.0.42";
int port = 1883;
const char *mqtt_client_id = "heat_controller";

const char* rx_topic = "/heater/request";
const char* tx_topic = "/heater/response";
const char* status_topic = "/heater/status";

MQTTClient mqtt(1024);// 1KB
WiFiClient wifi;//needed to stay on global scope

#define FORMAT_SPIFFS_IF_FAILED true


#define ADC_EN              14  //ADC_EN is the ADC detection enable port
#define ADC_PIN             34
#define BUTTON_1            35
#define BUTTON_2            0

TFT_eSPI tft = TFT_eSPI(135, 240); // Invoke custom library
Button2 btn1(BUTTON_1);
Button2 btn2(BUTTON_2);

GfxUi ui = GfxUi(&tft);

int vref = 1100;

bool spiffs_init(){
    if(!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)){
        Serial.println("SPIFFS Mount Failed");
        return false;
    }
    return true;
}

void connect() {
    Serial.print("checking wifi...");
    while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
    }

    Serial.print("\nmqtt connecting...");
    while (!mqtt.connect(mqtt_client_id)) {
    Serial.print(".");
    delay(1000);
    }
    Serial.println("\nconnected!");
    mqtt.subscribe(rx_topic);
    mqtt.publish(status_topic, "running");

    //tft.fillScreen(TFT_BLACK);
    //tft.setTextDatum(MC_DATUM);
    //tft.drawString("MQTT connected", tft.width() / 2, tft.height() / 2);
}

void mqtt_loop(){
  mqtt.loop();
  if (!mqtt.connected()) {
    connect();
  }
}

void messageReceived(String &topic, String &payload) {
  Serial.println(payload);
}

//! Long time delay, it is recommended to use shallow sleep, which can effectively reduce the current consumption
void espDelay(int ms)
{
    esp_sleep_enable_timer_wakeup(ms * 1000);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_light_sleep_start();
}

void showVoltage()
{
    static uint64_t timeStamp = 0;
    if (millis() - timeStamp > 1000) {
        timeStamp = millis();
        uint16_t v = analogRead(ADC_PIN);
        float battery_voltage = ((float)v / 4095.0) * 2.0 * 3.3 * (vref / 1000.0);
        String voltage = "Voltage :" + String(battery_voltage) + "V";
        Serial.println(voltage);
        tft.fillScreen(TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(voltage,  tft.width() / 2, tft.height() / 2 );
    }
}

void button_init()
{
    btn1.setLongClickHandler([](Button2 & b) {
        Serial.println("button 1 long pressed");

        int r = digitalRead(TFT_BL);
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(TFT_GREEN, TFT_BLACK);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Going into deep sleep, Press again to wake up",  tft.width() / 2, tft.height() / 2 );
        espDelay(6000);
        digitalWrite(TFT_BL, !r);

        tft.writecommand(TFT_DISPOFF);
        tft.writecommand(TFT_SLPIN);
        //After using light sleep, you need to disable timer wake, because here use external IO port to wake up
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
        // esp_sleep_enable_ext1_wakeup(GPIO_SEL_35, ESP_EXT1_WAKEUP_ALL_LOW);
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);
        delay(200);
        esp_deep_sleep_start();
    });
    btn1.setPressedHandler([](Button2 & b) {
        Serial.println("button 1 pressed");
    });

    btn2.setPressedHandler([](Button2 & b) {
        Serial.println("button 2 pressed");
    });
}


void setup()
{
    Serial.begin(115200);

    tft.init();
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0, 0);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_GREEN);
    tft.setTextSize(1);

    tft.setRotation(0);
    tft.fillScreen(TFT_NAVY);
    espDelay(300);
    tft.fillScreen(TFT_OLIVE);
    espDelay(300);
    tft.fillScreen(TFT_GOLD);
    espDelay(300);

    button_init();

    esp_adc_cal_characteristics_t adc_chars;
    esp_adc_cal_value_t val_type = esp_adc_cal_characterize((adc_unit_t)ADC_UNIT_1, (adc_atten_t)ADC1_CHANNEL_6, (adc_bits_width_t)ADC_WIDTH_BIT_12, 1100, &adc_chars);
    //Check type of calibration value used to characterize ADC
    if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
        Serial.printf("eFuse Vref:%u mV", adc_chars.vref);
        vref = adc_chars.vref;
    } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
        Serial.printf("Two Point --> coeff_a:%umV coeff_b:%umV\n", adc_chars.coeff_a, adc_chars.coeff_b);
    } else {
        Serial.println("Default Vref: 1100mV");
    }


    WiFi.begin(ssid, password);
    mqtt.begin(host,port, wifi);
    mqtt.onMessage(messageReceived);

    spiffs_init();
    tft.setRotation(3);
    ui.drawBmp("/esp_home.bmp", 0, 0);

}

void loop()
{
    mqtt_loop();
    btn1.loop();
    btn2.loop();
}
