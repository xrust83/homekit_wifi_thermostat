/*
* Copyright 2022 Wadim Korneliuk (@xrust83)
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
* http://www.apache.org/licenses/LICENSE-2.0

* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*
*
* EPS8266 HomeKit Thermostat
*
* Uses a DS18B20 (temperature sensor)
*
*
*/

#define DEVICE_MANUFACTURER "Wadim Korneliuk"
#define DEVICE_NAME "Thermostat"
#define DEVICE_MODEL "Basic"
#define DEVICE_SERIAL "12345678"
#define FW_VERSION "1.0"

#include <stdio.h>
#include <espressif/esp_wifi.h>
#include <espressif/esp_sta.h>
#include <espressif/esp_system.h>
#include <esp/uart.h>
#include <esp8266.h>
#include <etstimer.h>
#include <esplibs/libmain.h>
#include <FreeRTOS.h>
#include <task.h>
#include <sysparam.h>
#include <math.h>


#include <ssd1306/ssd1306.h>

#include <homekit/homekit.h>
#include <homekit/characteristics.h>
#include <qrcode.h>

#include <wifi_thermostat.h>
#include <adv_button.h>
#include <led_codes.h>
#include <custom_characteristics.h>
#include <shared_functions.h>
#include <ds18b20/ds18b20.h>

#define SENSOR_PIN 4 //D2
#define DEVICE_NUMBER 1
#define TEMPERATURE_POLL_PERIOD 1500
#define TEMP_DIFF_NOTIFY_TRIGGER 0.1
#define TEMP_DIFF_TRIGGER 0.1
#define UP_BUTTON_GPIO 12 //D6
#define DOWN_BUTTON_GPIO 0 //D3
#define SET_BUTTON_GPIO 10
#define LED_GPIO 2 //D4
#define QRCODE_VERSION 2
#define HEATER_PIN 15 //D8
#define COOLER_PIN 13 //D7
uint16_t debounce_time = 60;

int led_off_value=1; /* global varibale to support LEDs set to 0 where the LED is connected to GND, 1 where +3.3v */
const int status_led_gpio = 2; /*set the gloabl variable for the led to be sued for showing status */

// add this section to make your device OTA capable
// create the extra characteristic &ota_trigger, at the end of the primary service (before the NULL)
// it can be used in Eve, which will show it, where Home does not
// and apply the four other parameters in the accessories_information section

//#ifndef SENSOR_PIN  No works with no sensor
//#error SENSOR_PIN is not specified
//#endif

/// I2C

/* Remove this line if your display connected by SPI */
#define I2C_CONNECTION

#ifdef I2C_CONNECTION
   #include <i2c/i2c.h>
#endif

#include "fonts/fonts.h"

/* Change this according to you schematics and display size */
#define DISPLAY_WIDTH  128
#define DISPLAY_HEIGHT 64

#ifdef I2C_CONNECTION
    #define PROTOCOL SSD1306_PROTO_I2C
    #define ADDR     SSD1306_I2C_ADDR_0
    #define I2C_BUS  0
    #define SCL_PIN  14
    #define SDA_PIN  5
#else
    #define PROTOCOL SSD1306_PROTO_SPI4
    #define CS_PIN   5
    #define DC_PIN   4
#endif

#define DEFAULT_FONT FONT_FACE_TERMINUS_16X32_ISO8859_1

/* Declare device descriptor */
static const ssd1306_t display = {
    .protocol = PROTOCOL,
    .screen = SH1106_SCREEN,
    //.screen = SSD1306_SCREEN,
#ifdef I2C_CONNECTION
.i2c_dev.bus      = I2C_BUS,
.i2c_dev.addr     = ADDR,
#else
    .cs_pin   = CS_PIN,
    .dc_pin   = DC_PIN,
#endif
    .width    = DISPLAY_WIDTH,
    .height   = DISPLAY_HEIGHT
};


/* Local frame display_buffer */
static uint8_t display_buffer[DISPLAY_WIDTH * DISPLAY_HEIGHT / 8];
static bool screen_on = false;
enum screen_display {logo, thermostat}  ;
static enum screen_display screen;

#define SECOND_TICKS (1000 / portTICK_PERIOD_MS) /* a second in ticks */
#define SCREEN_DELAY 30000 /* in milliseconds */

void heaterOn() {
    gpio_write(HEATER_PIN, true);
}

void heaterOff() {
    gpio_write(HEATER_PIN, false);
}

void coolerOn() {
    gpio_write(COOLER_PIN, true);
}

void coolerOff() {
    gpio_write(COOLER_PIN, false);
}

// I2C

ETSTimer screen_off_timer;
#include <ota-api.h>
homekit_characteristic_t wifi_reset   = HOMEKIT_CHARACTERISTIC_(CUSTOM_WIFI_RESET, false, .setter=wifi_reset_set);
homekit_characteristic_t wifi_check_interval   = HOMEKIT_CHARACTERISTIC_(CUSTOM_WIFI_CHECK_INTERVAL, 10, .setter=wifi_check_interval_set);
/* checks the wifi is connected and flashes status led to indicated connected */
homekit_characteristic_t task_stats   = HOMEKIT_CHARACTERISTIC_(CUSTOM_TASK_STATS, true , .setter=task_stats_set);
homekit_characteristic_t ota_beta     = HOMEKIT_CHARACTERISTIC_(CUSTOM_OTA_BETA, false, .setter=ota_beta_set);
homekit_characteristic_t lcm_beta    = HOMEKIT_CHARACTERISTIC_(CUSTOM_LCM_BETA, false, .setter=lcm_beta_set);
homekit_characteristic_t preserve_state   = HOMEKIT_CHARACTERISTIC_(CUSTOM_PRESERVE_STATE, true, .setter=preserve_state_set);

homekit_characteristic_t ota_trigger  = API_OTA_TRIGGER;
homekit_characteristic_t name         = HOMEKIT_CHARACTERISTIC_(NAME, DEVICE_NAME);
homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER,  DEVICE_MANUFACTURER);
homekit_characteristic_t serial       = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, DEVICE_SERIAL);
homekit_characteristic_t model        = HOMEKIT_CHARACTERISTIC_(MODEL,         DEVICE_MODEL);
homekit_characteristic_t revision     = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION,  FW_VERSION);

void switch_screen_on (uint32_t time_to_be_on);
void display_logo ();

void thermostat_identify_task(void *_args) {

   led_code(LED_GPIO, IDENTIFY_ACCESSORY);
   switch_screen_on (30*SECOND_TICKS);
   display_logo ();
   vTaskDelete(NULL);
}

void thermostat_identify(homekit_value_t _value) {
   printf("Thermostat identify\n");
   xTaskCreate(thermostat_identify_task, "identify", 256, NULL, tskIDLE_PRIORITY+1, NULL);
}

void on_update(homekit_characteristic_t *ch, homekit_value_t value, void *context);
void process_setting_update();

homekit_characteristic_t current_temperature = HOMEKIT_CHARACTERISTIC_( CURRENT_TEMPERATURE, 0 );
homekit_characteristic_t target_temperature  = HOMEKIT_CHARACTERISTIC_( TARGET_TEMPERATURE, 22,.callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update));
homekit_characteristic_t units               = HOMEKIT_CHARACTERISTIC_( TEMPERATURE_DISPLAY_UNITS, 0 );
homekit_characteristic_t current_state       = HOMEKIT_CHARACTERISTIC_( CURRENT_HEATING_COOLING_STATE, 0 );
homekit_characteristic_t target_state        = HOMEKIT_CHARACTERISTIC_( TARGET_HEATING_COOLING_STATE, 0, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update) );
homekit_characteristic_t cooling_threshold   = HOMEKIT_CHARACTERISTIC_( COOLING_THRESHOLD_TEMPERATURE, 25, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update) );
homekit_characteristic_t heating_threshold   = HOMEKIT_CHARACTERISTIC_( HEATING_THRESHOLD_TEMPERATURE, 15, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update) );

void display_logo (){

   screen = logo;
   if (ssd1306_fill_rectangle(&display, display_buffer, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, OLED_COLOR_BLACK)){
       printf("Error printing rectangle\bn");
   }

   ssd1306_clear_screen(&display);
   ssd1306_load_xbm(&display, homekit_logo, display_buffer);
   if (ssd1306_load_frame_buffer(&display, display_buffer)){
       printf ( "Error loading frame buffer for logo\n");
   }
}

// LCD ssd1306

static void ssd1306_task(void *pvParameters)
{
   char target_temp_string[5];
   char mode_string[5];
   char temperature_string[5];
   char target_string[5];

   vTaskDelay(SECOND_TICKS);
   ssd1306_set_whole_display_lighting(&display, false);

   ssd1306_set_scan_direction_fwd( &display, true );
   ssd1306_set_segment_remapping_enabled( &display, false );

   display_logo ();
   vTaskDelay(SECOND_TICKS*5);

   ssd1306_clear_screen(&display);

   while (1) {

       if (screen_on){
           if (screen==logo){
               vTaskDelay(SECOND_TICKS*5); /* leave the logo on for 5 seconds before changing*/
               screen = thermostat;
               if (ssd1306_fill_rectangle(&display, display_buffer, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, OLED_COLOR_BLACK)){
                   printf("Error printing rectangle\bn");
               }

               ssd1306_load_xbm(&display, thermostat_github2_xbm, display_buffer);
               if (ssd1306_load_frame_buffer(&display, display_buffer))
                   goto error_loop;
           }

           sprintf(target_temp_string, "%2.1f", (float)target_temperature.value.float_value);
           switch( (int)target_state.value.int_value)
           {
               case 0:
                   sprintf(target_string, " OFF");
                   break;
               case 1:
                   sprintf(target_string, "HEAT");
                   break;
               case 2:
                   sprintf(target_string, "COOL");
                   break;
               case 3:
                   sprintf(target_string, "AUTO");
                   break;
               default:
                   sprintf(target_string, "?   ");
           }


           if (ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[FONT_FACE_TERMINUS_BOLD_14X28_ISO8859_1], 3, 2, target_temp_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK) < 1){
               printf("Error printing target temp\n");
           }

           if (ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[FONT_FACE_TERMINUS_BOLD_14X28_ISO8859_1], 72, 2, target_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK) < 1 ){
               printf("Error printing mode\n");
           }

           sprintf(temperature_string, "%2.1f", (float)current_temperature.value.float_value);

           if (ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[FONT_FACE_TERMINUS_BOLD_8X14_ISO8859_1], 22, 50 , temperature_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK) < 1){
               printf("Error printing temperature\n");
           }

           //sprintf(target_state_string, "%g", (float)target_state.value.int_value);  //
           switch( (int)current_state.value.int_value)
           {
               case 0:
                   sprintf(mode_string, "    Off");
                   break;
               case 1:
                   sprintf(mode_string, "Heating");
                   break;
               case 2:
                   sprintf(mode_string, "Cooling");
                   break;
               case 3:
                   sprintf(mode_string, "   Auto");
                   break;
               default:
                   sprintf(mode_string, "?   ");
           }

           if (ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[FONT_FACE_TERMINUS_BOLD_8X14_ISO8859_1], 30, 30 , mode_string, OLED_COLOR_WHITE, OLED_COLOR_BLACK) < 1){
                printf("Error printing state\n");
            }

           if (ssd1306_load_frame_buffer(&display, display_buffer))
               goto error_loop;
       }
       vTaskDelay(SECOND_TICKS/4);
   }

error_loop:
   printf("%s: error while loading framebuffer into ssd1306\n", __func__);
   for (;;) {
       vTaskDelay(2 * SECOND_TICKS);
       printf("%s: error loop\n", __FUNCTION__);
       led_code(LED_GPIO, FUNCTION_A);
   }
}

void switch_screen_off (){

   printf("%s:", __func__);
screen_on = false;
   ssd1306_display_on(&display, false);
   sdk_os_timer_disarm (&screen_off_timer ); /* esnuer the screen off timer is disabled */
   printf("Screen turned off and off timer disbaled\n");
}

void switch_screen_on (uint32_t time_to_be_on){

   printf("%s:", __func__);
screen_on = true;
   ssd1306_display_on(&display, true);
   if (time_to_be_on > 0 ) { /* we call this with 0 when we are showing the homekit qr code, so no timeout */
        sdk_os_timer_arm(&screen_off_timer, time_to_be_on, 0);
    }
   printf("Screen turned on and off timer set to %d\n", time_to_be_on);
}

void screen_off_timer_fn (){

   switch_screen_off ();
   printf("Screen Off timer fucntion\n");
}

void screen_init(void) {

   printf("%s: SDK version:%s, Free Heap %d\n", __func__, sdk_system_get_sdk_version(),  xPortGetFreeHeapSize());

#ifdef I2C_CONNECTION
   i2c_init(I2C_BUS, SCL_PIN, SDA_PIN, I2C_FREQ_400K);
#endif

   while (ssd1306_init(&display) != 0) {
       printf("%s: failed to init ssd1306 lcd\n", __func__);
       vTaskDelay(SECOND_TICKS);
   }
   ssd1306_set_whole_display_lighting(&display, false);
   ssd1306_set_scan_direction_fwd( &display, true );
   ssd1306_set_segment_remapping_enabled( &display, false );


   printf("%s: end, Free Heap %d\n", __func__, xPortGetFreeHeapSize());
}

// LCD ssd1306

// QR CODE

void display_draw_pixel(uint8_t x, uint8_t y, bool white) {
   ssd1306_color_t color = white ? OLED_COLOR_WHITE : OLED_COLOR_BLACK;
   ssd1306_draw_pixel(&display, display_buffer, x, y, color);
}

void display_draw_pixel_2x2(uint8_t x, uint8_t y, bool white) {
   ssd1306_color_t color = white ? OLED_COLOR_WHITE : OLED_COLOR_BLACK;

   ssd1306_draw_pixel(&display, display_buffer, x, y, color);
   ssd1306_draw_pixel(&display, display_buffer, x+1, y, color);
   ssd1306_draw_pixel(&display, display_buffer, x, y+1, color);
   ssd1306_draw_pixel(&display, display_buffer, x+1, y+1, color);
}

void display_draw_qrcode(QRCode *qrcode, uint8_t x, uint8_t y, uint8_t size) {

   printf("%s: Start, Free Heap %d\n", __func__, xPortGetFreeHeapSize());

   void (*draw_pixel)(uint8_t x, uint8_t y, bool white) = display_draw_pixel;
   if (size >= 2) {
       draw_pixel = display_draw_pixel_2x2;
   }

   uint8_t cx;
   uint8_t cy = y;

   cx = x + size;
   draw_pixel(x, cy, 1);
   for (uint8_t i = 0; i < qrcode->size; i++, cx+=size)
       draw_pixel(cx, cy, 1);
   draw_pixel(cx, cy, 1);

   cy += size;

   for (uint8_t j = 0; j < qrcode->size; j++, cy+=size) {
       cx = x + size;
       draw_pixel(x, cy, 1);
       for (uint8_t i = 0; i < qrcode->size; i++, cx+=size) {
           draw_pixel(cx, cy, qrcode_getModule(qrcode, i, j)==0);
       }
       draw_pixel(cx, cy, 1);
   }

   cx = x + size;
   draw_pixel(x, cy, 1);
   for (uint8_t i = 0; i < qrcode->size; i++, cx+=size)
       draw_pixel(cx, cy, 1);
   draw_pixel(cx, cy, 1);

   printf("%s: End, Free Heap %d\n", __func__, xPortGetFreeHeapSize());

}

bool qrcode_shown = false;
void qrcode_show(homekit_server_config_t *config) {
   char setupURI[20];
   homekit_get_setup_uri(config, setupURI, sizeof(setupURI));

   printf("%s: Start, Free Heap %d\n", __func__, xPortGetFreeHeapSize());

   printf ("setupURI: %s, password %s, setupId: %s\n",setupURI, config->password, config->setupId);
   QRCode qrcode;

   uint8_t *qrcodeBytes = malloc(qrcode_getBufferSize(QRCODE_VERSION));
   qrcode_initText(&qrcode, qrcodeBytes, QRCODE_VERSION, ECC_MEDIUM, setupURI);

   qrcode_print(&qrcode);  // print on console

   switch_screen_on (0);

   ssd1306_fill_rectangle(&display, display_buffer, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, OLED_COLOR_BLACK);
   ssd1306_draw_string(&display, display_buffer, font_builtin_fonts[FONT_FACE_TERMINUS_6X12_ISO8859_1], 0, 26, config->password, OLED_COLOR_WHITE, OLED_COLOR_BLACK);
   display_draw_qrcode(&qrcode, 64, 5, 2);

   ssd1306_load_frame_buffer(&display, display_buffer);

   free(qrcodeBytes);
   qrcode_shown = true;
   printf("%s: End, Free Heap %d\n", __func__, xPortGetFreeHeapSize());

}

void qrcode_hide() {

   printf("%s: Start, Free Heap %d\n", __func__, xPortGetFreeHeapSize());

   if (!qrcode_shown)
       return;

   ssd1306_clear_screen(&display);
   switch_screen_off();

   qrcode_shown = false;
   printf("%s: End, Free Heap %d\n", __func__, xPortGetFreeHeapSize());

}

// QR CODE END

void on_update(homekit_characteristic_t *ch, homekit_value_t value, void *context) {

   process_setting_update();
   sdk_os_timer_arm (&save_timer, SAVE_DELAY, 0 );
}

void up_button_callback(uint8_t gpio, void* args, const uint8_t param) {

   switch_screen_on (SCREEN_DELAY); // ensure the screen is on
   printf("Button UP single press\n");
   if (target_temperature.value.float_value <= (target_temperature.max_value[0] - 0.5))
   {
       target_temperature.value.float_value += 0.5;
       sdk_os_timer_arm (&save_timer, SAVE_DELAY, 0 );
       homekit_characteristic_notify(&target_temperature, target_temperature.value);
   }
}

void down_button_callback(uint8_t gpio, void* args, const uint8_t param) {

   switch_screen_on (SCREEN_DELAY); /* ensure the screen is on */
   printf("Button DOWN single press\n");
   if (target_temperature.value.float_value >= (target_temperature.min_value[0] + 0.5))
   {
       target_temperature.value.float_value -= 0.5;
       sdk_os_timer_arm (&save_timer, SAVE_DELAY, 0 );
       homekit_characteristic_notify(&target_temperature, target_temperature.value);
   }
}

void set_button_callback(uint8_t gpio, void* args, const uint8_t param) {

  switch_screen_on (SCREEN_DELAY); /* ensure the screen is on */
  printf("Button SET single press\n");
  {

              uint8_t state = target_state.value.int_value + 1;
   		switch (state){
        case 1:
            //heat
            state = 1;
            break;
            //cool
        case 2:
            state = 2;
            break;
            //auto
        case 3:
            state = 3;
           break;
        default:
            //off
            state = 0;
            break;
        }
        target_state.value = HOMEKIT_UINT8(state);
        homekit_characteristic_notify(&target_state, target_state.value);
		}
}

void process_setting_update() {

   printf("%s: Start - Free Heap %d\n",__func__,  xPortGetFreeHeapSize());
          uint8_t state = target_state.value.int_value;
   if ((state == 1 && current_temperature.value.float_value < target_temperature.value.float_value) ||
           (state == 3 && current_temperature.value.float_value < heating_threshold.value.float_value)) {
       if (current_state.value.int_value != 1) {
           current_state.value = HOMEKIT_UINT8(1);
           sdk_os_timer_arm (&save_timer, SAVE_DELAY, 0 );
           homekit_characteristic_notify(&current_state, current_state.value);
           switch_screen_on (SCREEN_DELAY);

           heaterOn();
           coolerOff();

       }
   } else if ((state == 2 && current_temperature.value.float_value > target_temperature.value.float_value) ||
           (state == 3 && current_temperature.value.float_value > cooling_threshold.value.float_value)) {
       if (current_state.value.int_value != 2) {
           current_state.value = HOMEKIT_UINT8(2);
           sdk_os_timer_arm (&save_timer, SAVE_DELAY, 0 );
           homekit_characteristic_notify(&current_state, current_state.value);
           switch_screen_on (SCREEN_DELAY);

           coolerOn();
           heaterOff();

       }
   } else {
       if (current_state.value.int_value != 0) {
           current_state.value = HOMEKIT_UINT8(0);
           sdk_os_timer_arm (&save_timer, SAVE_DELAY, 0 );
           homekit_characteristic_notify(&current_state, current_state.value);
           switch_screen_on (SCREEN_DELAY);

           heaterOff();
           coolerOff();

       }
   }
   printf("%s: End - Free Heap %d\n",__func__,  xPortGetFreeHeapSize());
}

void temperature_sensor_identify(homekit_value_t _value) {
   printf("Temperature sensor identify\n");
}

void temperature_sensor_task(void *_args) {
   ds18b20_addr_t addrs[1];
   float temperature_value, temp_diff;
   int sensor_count;

   gpio_enable(HEATER_PIN, GPIO_OUTPUT);
   gpio_enable(COOLER_PIN, GPIO_OUTPUT);

   heaterOff();
   coolerOff();

   while (1) {
     sensor_count = ds18b20_scan_devices(SENSOR_PIN, addrs, 1);
     if (sensor_count == 1) {
         temperature_value = ds18b20_read_single(SENSOR_PIN);
         printf("%s: Got readings: temperature %2.1f, Stack Words left: %lu\n", __func__, temperature_value, uxTaskGetStackHighWaterMark(NULL)/4);
         temp_diff = abs (current_temperature.value.float_value - temperature_value);
         if (temperature_value >= current_temperature.min_value[0] && temperature_value <= current_temperature.max_value[0] && temp_diff >= TEMP_DIFF_NOTIFY_TRIGGER)
         {
             printf ("%s: Notify Temperature updated\n", __func__);
             current_temperature.value = HOMEKIT_FLOAT(temperature_value);
             homekit_characteristic_notify(&current_temperature, current_temperature.value);
         }
         if (temp_diff > TEMP_DIFF_TRIGGER) {
             process_setting_update();
         }
     } else {
       printf("%s: Couldnt read data from sensor\n", __func__);
       led_code(LED_GPIO, SENSOR_ERROR);
     }
     homekit_characteristic_notify(&current_state, current_state.value);
     vTaskDelay(TEMPERATURE_POLL_PERIOD / portTICK_PERIOD_MS);
   }
}

homekit_accessory_t *accessories[] = {
   HOMEKIT_ACCESSORY(.id=1, .category=homekit_accessory_category_thermostat, .services=(homekit_service_t*[]) {
       HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics=(homekit_characteristic_t*[]) {
           HOMEKIT_CHARACTERISTIC(NAME, "HomeKit Thermostat"),
           &manufacturer,
           &serial,
           HOMEKIT_CHARACTERISTIC(MODEL, "SSD1306+DS18B20 Thermostat "),
           &revision,
           HOMEKIT_CHARACTERISTIC(IDENTIFY, thermostat_identify),
           NULL
       }),
       HOMEKIT_SERVICE(THERMOSTAT, .primary=true, .characteristics=(homekit_characteristic_t*[]) {
           HOMEKIT_CHARACTERISTIC(NAME, "Thermostat"),
           &current_temperature,
           &target_temperature,
           &current_state,
           &target_state,
           &cooling_threshold,
           &heating_threshold,
           &units,
           &ota_trigger,
           &wifi_reset,
           &wifi_check_interval,
           &task_stats,
           &ota_beta,
           &lcm_beta,
           &preserve_state,
           NULL
       }),
       NULL
   }),
   NULL
};

void temperature_sensor_init() {
   xTaskCreate(temperature_sensor_task, "Temperatore Sensor", 256, NULL, 2, NULL);
}

void thermostat_init() {

   printf("%s: Start, Freep Heap=%d\n", __func__, xPortGetFreeHeapSize());

   while (ssd1306_init(&display) != 0) {
       printf("%s: failed to init ssd1306 lcd\n", __func__);
       vTaskDelay(SECOND_TICKS);
   }
   printf("%s: Display initailised, Freep Heap=%d\n", __func__, xPortGetFreeHeapSize());


   adv_button_set_evaluate_delay(10);

   /* GPIO for button, pull-up resistor, inverted */
   adv_button_create(UP_BUTTON_GPIO, true, false);
   adv_button_register_callback_fn(UP_BUTTON_GPIO, up_button_callback, SINGLEPRESS_TYPE, NULL, 0);

   adv_button_create(DOWN_BUTTON_GPIO, true, false);
   adv_button_register_callback_fn(DOWN_BUTTON_GPIO, down_button_callback, SINGLEPRESS_TYPE, NULL, 0);

   adv_button_create(SET_BUTTON_GPIO, true, false);
   adv_button_register_callback_fn(SET_BUTTON_GPIO, set_button_callback, SINGLEPRESS_TYPE, NULL, 0);

   printf("%s: Buttons Created, Freep Heap=%d\n", __func__, xPortGetFreeHeapSize());

   xTaskCreate(temperature_sensor_task, "Thermostat", 256, NULL, tskIDLE_PRIORITY+1, NULL);

   printf("%s: Temperaure Sensor Task Created, Freep Heap=%d\n", __func__, xPortGetFreeHeapSize());

   gpio_enable(LED_GPIO, GPIO_OUTPUT);

   xTaskCreate(ssd1306_task, "ssd1306_task", 384, NULL, tskIDLE_PRIORITY+1, NULL);
   sdk_os_timer_arm(&screen_off_timer, SCREEN_DELAY, 0);

   printf("%s: Screen Taks and Timer, Freep Heap=%d\n", __func__, xPortGetFreeHeapSize());


   printf("%s: End, Freep Heap=%d\n\n", __func__, xPortGetFreeHeapSize());

}

void accessory_init (void ){
   /* initalise anything you don't want started until wifi and pairing is confirmed */
   printf("%s: Start, Freep Heap=%d\n", __func__, xPortGetFreeHeapSize());

   qrcode_hide();
   thermostat_init();

   printf("%s: End, Freep Heap=%d\n\n", __func__, xPortGetFreeHeapSize());

}

void accessory_init_not_paired (void) {
   /* initalise anything you don't want started until wifi and homekit imitialisation is confirmed, but not paired */
   printf("%s: Start, Freep Heap=%d\n", __func__, xPortGetFreeHeapSize());

   qrcode_show(&config);

   printf("%s: End, Freep Heap=%d\n\n", __func__, xPortGetFreeHeapSize());

}

void recover_from_reset (int reason){
   /* called if we restarted abnormally */
   printf("%s: Reason %d, Freep Heap=%d\n", __func__, reason, xPortGetFreeHeapSize());

   load_characteristic_from_flash(&target_temperature);
   load_characteristic_from_flash(&current_state);

   printf("%s: End, Freep Heap=%d\n\n", __func__, xPortGetFreeHeapSize());

}

void save_characteristics ( ){
   /* called by save timer*/
   printf ("%s:\n", __func__);
   save_characteristic_to_flash(&preserve_state, preserve_state.value);
   if ( preserve_state.value.bool_value == true){
       printf ("%s:Preserving state\n", __func__);
       save_characteristic_to_flash (&target_temperature, target_temperature.value);
       save_characteristic_to_flash (&target_state, target_state.value);
       save_characteristic_to_flash(&wifi_check_interval, wifi_check_interval.value);
   } else {
       printf ("%s:Not preserving state\n", __func__);
   }
}

void load_settings_from_flash (){

   printf("%s: Start, Freep Heap=%d\n", __func__, xPortGetFreeHeapSize());

   load_characteristic_from_flash (&target_state);
   load_characteristic_from_flash (&target_temperature);

   printf("%s: End, Freep Heap=%d\n\n", __func__, xPortGetFreeHeapSize());

}

homekit_server_config_t config = {
   .accessories = accessories,
   .password = "021-82-017",
   .setupId = "1234",
   .on_event = on_homekit_event
};

void user_init(void) {

   sdk_os_timer_setfn(&screen_off_timer, screen_off_timer_fn, NULL);

   standard_init (&name, &manufacturer, &model, &serial, &revision);

   load_settings_from_flash ();

   printf ("Calling screen init\n");
   printf ("fonts count %i\n", font_builtin_fonts_count);
   screen_init();
   printf ("Screen init called\n");

   wifi_config_init(DEVICE_NAME, NULL, on_wifi_ready);

}
