![GitHub All Releases](https://img.shields.io/github/downloads/xrust83/homekit_wifi_thermostat/total)
![GitHub Releases](https://img.shields.io/github/downloads/xrust83/homekit_wifi_thermostat/latest/total)

# HomeKit_WiFi_Thermostat

<p align="center"><img width="400" src="https://github.com/xrust83/homekit_wifi_thermostat/blob/master/src/08D4D200-5688-41F1-91C0-7996D3E3F58B.jpeg"><img width="400" src="https://github.com/xrust83/homekit_wifi_thermostat/blob/master/src/5467679F-4C87-4804-B8A1-54945B27D2D0.jpeg"></p>
<p align="center"><img width="400" src="https://github.com/xrust83/homekit_wifi_thermostat/blob/master/src/E48C8F00-A029-4006-9528-29CF35A0B7F8.jpeg"><img width="400" src="https://github.com/xrust83/homekit_wifi_thermostat/blob/master/src/A9593DC6-FC35-4300-96FD-0EE5672E0FAC.jpeg"></p>

Thermostat accessory for remote control of central heating.
You will also need the Eve app to update the firmware, enable settings after a power outage.

It uses Si7021 to measure temperature and sets current_heating_cooling_status based on other accessory settings.

For example, when the thermostat sets current_heating_cooling_status to heating/off, use EVE to activate the switch accessory.
on/off respectively. You can also set timer based events in Eve to control the target_heating_cooling_status, in others
words, you can create a program when you want to turn the heating on or off.

Now updated to include a screen (SH1106 OLED) and buttons to adjust the target temperature up and down.

The screen will light up during the initial setup and when the power is turned on. And then it will go out after about 10 seconds, it will light up when you press the up or down button, and then go out again about 10 seconds after the last button press. Also, the screen will light up when switching between statuses in the process of work.

GPIO used are as follows:- 

  14 - Si7021 Sonoff version

  13 - Temperature Up (button connect to ground)

  0 - Temperature Down (button conected to ground) 
  
  10 - Set (button conected to ground)
  
  15 - Heating LED (conected to ground, resistor 1 kOm) 
  
  12 - Cooling LED (conected to ground, resistor 1 kOm)
  
  2 - LED 
  
  5 - SSD1306 SCL_PIN
  
  4 - SSD1306 SDA_PIN
  
  
  Online XBM redactor. https://xbm.jazzychad.net
  
|Vendor|Model|Chip|Functions|GPIOs and notes| MEPLHAA script |
|:-----|:----|:--:|:--------|:--------------|:---|
|Bawoo| RGBW Lightbulb|ESP8266|Color lightbulb||`{"c":{"io":[[[14,12,13],7],[[4],7,0,0,2]]},"a":[{"t":30,"g":[14,12,13,4],"fa":[1,1,1,4],"mp":0.85}]}`|
