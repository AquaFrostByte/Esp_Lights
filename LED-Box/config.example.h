// Copy this file to config.h and fill in your values. config.h is git-ignored.
#pragma once

#define WIFI_SSID "your-wifi"
#define WIFI_PASS "your-wifi-password"

// Static IP Configuration (Leave STATIC_IP empty "" to use DHCP)
#define STATIC_IP "192.168.2.100"
#define STATIC_GATEWAY "192.168.2.1"
#define STATIC_SUBNET "255.255.255.0"
#define STATIC_DNS "8.8.8.8"

#define MQTT_HOST "192.168.2.10"     // your server
#define MQTT_PORT 1883
#define MQTT_USER ""                 // leave empty for anonymous
#define MQTT_PASS ""
#define MQTT_CLIENT_ID "led_box"     // also the Home Assistant unique id, keep it unique per box

#define MQTT_PREFIX "led/box"        // must match the web page

#define HA_DISCOVERY_PREFIX "homeassistant"   // Home Assistant default
