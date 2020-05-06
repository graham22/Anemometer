#include "IOT.h"
#include <sys/time.h>
#include <EEPROM.h>
#include "time.h"
#include "Log.h"


namespace AnemometerNS
{
AsyncMqttClient _mqttClient;
TimerHandle_t mqttReconnectTimer;
DNSServer _dnsServer;
WebServer* _pWebServer;
HTTPUpdateServer _httpUpdater;
IotWebConf _iotWebConf(TAG, &_dnsServer, _pWebServer, TAG, CONFIG_VERSION);
char _mqttRootTopic[STR_LEN];
char _willTopic[STR_LEN];
char _mqttServer[IOTWEBCONF_WORD_LEN];
char _mqttPort[5];
char _mqttUserName[IOTWEBCONF_WORD_LEN];
char _mqttUserPassword[IOTWEBCONF_WORD_LEN];
u_int _uniqueId = 0;
IotWebConfSeparator seperatorParam = IotWebConfSeparator("MQTT");
IotWebConfParameter mqttServerParam = IotWebConfParameter("MQTT server", "mqttServer", _mqttServer, IOTWEBCONF_WORD_LEN);
IotWebConfParameter mqttPortParam = IotWebConfParameter("MQTT port", "mqttSPort", _mqttPort, 5, "text", NULL, "1883");
IotWebConfParameter mqttUserNameParam = IotWebConfParameter("MQTT user", "mqttUser", _mqttUserName, IOTWEBCONF_WORD_LEN);
IotWebConfParameter mqttUserPasswordParam = IotWebConfParameter("MQTT password", "mqttPass", _mqttUserPassword, IOTWEBCONF_WORD_LEN, "password");
IotWebConfParameter mqttRootTopicParam = IotWebConfParameter("MQTT Root Topic", "mqttRootTopic", _mqttRootTopic, IOTWEBCONF_WORD_LEN);


void publishDiscovery()
{
	char buffer[STR_LEN];
	StaticJsonDocument<1024> doc; // MQTT discovery
	doc["name"] = _iotWebConf.getThingName();
	sprintf(buffer, "%s_%X_WindSpeed", _iotWebConf.getThingName(), _uniqueId);
	doc["unique_id"] = buffer;
	doc["unit_of_measurement"] = "km-h";
	doc["stat_t"] = "~/stat";
	doc["stat_tpl"] = "{{ value_json.windspeed}}";
	doc["avty_t"] = "~/tele/LWT";
	doc["pl_avail"] = "Online";
	doc["pl_not_avail"] = "Offline";
	JsonObject device = doc.createNestedObject("device");
	device["name"] = _iotWebConf.getThingName();
	device["sw_version"] = CONFIG_VERSION;
	device["manufacturer"] = "ClassicDIY";
	sprintf(buffer, "ESP32-Bit (%X)", _uniqueId);
	device["model"] = buffer;
	device["identifiers"] = _uniqueId;
	doc["~"] = _mqttRootTopic;
	String s;
	serializeJson(doc, s);
	char configurationTopic[64];
	sprintf(configurationTopic, "%s/sensor/%s/%X/WindSpeed/config", HOME_ASSISTANT_PREFIX, _iotWebConf.getThingName(), _uniqueId);
	if (_mqttClient.publish(configurationTopic, 0, true, s.c_str(), s.length()) == 0)
	{
		loge("**** Configuration payload exceeds MAX MQTT Packet Size");
	}
}

void onMqttConnect(bool sessionPresent)
{
	logd("Connected to MQTT. Session present: %d", sessionPresent);
	publishDiscovery();
	_mqttClient.publish(_willTopic, 0, false, "Online");
}

void onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
	logw("Disconnected from MQTT. Reason: %d", (int8_t)reason);
	if (WiFi.isConnected())
	{
		xTimerStart(mqttReconnectTimer, 0);
	}
}

void connectToMqtt()
{
	logd("Connecting to MQTT...");
	if (WiFi.isConnected())
	{
		_mqttClient.connect();
	}
}

void WiFiEvent(WiFiEvent_t event)
{
	logd("[WiFi-event] event: %d", event);
	String s;
	StaticJsonDocument<128> doc;
	switch (event)
	{
	case SYSTEM_EVENT_STA_GOT_IP:
		// logd("WiFi connected, IP address: %s", WiFi.localIP().toString().c_str());
		doc["IP"] = WiFi.localIP().toString().c_str();
		doc["ApPassword"] = TAG;
		serializeJson(doc, s);
		s += '\n';
		Serial.printf(s.c_str()); // send json to flash tool
		configTime(0, 0, NTP_SERVER);
		printLocalTime();
		xTimerStart(mqttReconnectTimer, 0);
		break;
	case SYSTEM_EVENT_STA_DISCONNECTED:
		logw("WiFi lost connection");
		xTimerStop(mqttReconnectTimer, 0); // ensure we don't reconnect to MQTT while reconnecting to Wi-Fi
		break;
	default:
		break;
	}
}

void onMqttPublish(uint16_t packetId)
{
	logd("Publish acknowledged.  packetId: %d", packetId);
}

void onMqttMessage(char *topic, char *payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
	logd("MQTT Message arrived [%s]  qos: %d len: %d index: %d total: %d", topic, properties.qos, len, index, total);
	printHexString(payload, len);
}

IOT::IOT(WebServer* pWebServer)
{
	_pWebServer = pWebServer;
}

/**
 * Handle web requests to "/" path.
 */
void handleSettings()
{
	// -- Let IotWebConf test and handle captive portal requests.
	if (_iotWebConf.handleCaptivePortal())
	{
		logd("Captive portal");
		// -- Captive portal request were already served.
		return;
	}
	logd("handleSettings");
	String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
	s += "<title>";
	s += _iotWebConf.getThingName();
	s += "</title></head><body>";
	s += _iotWebConf.getThingName();
	s += "<ul>";
	s += "<li>MQTT server: ";
	s += _mqttServer;
	s += "</ul>";
	s += "<ul>";
	s += "<li>MQTT port: ";
	s += _mqttPort;
	s += "</ul>";
	s += "<ul>";
	s += "<li>MQTT user: ";
	s += _mqttUserName;
	s += "</ul>";
	s += "<ul>";
	s += "<li>MQTT root topic: ";
	s += _mqttRootTopic;
	s += "</ul>";
	s += "Go to <a href='config'>configure page</a> to change values.";
	s += "</body></html>\n";
	_pWebServer->send(200, "text/html", s);
}

void IOT::Init()
{
	pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
	_iotWebConf.setStatusPin(WIFI_STATUS_PIN);
	_iotWebConf.setConfigPin(WIFI_AP_PIN);
	if (digitalRead(FACTORY_RESET_PIN) == LOW)
	{
		EEPROM.begin(IOTWEBCONF_CONFIG_START + IOTWEBCONF_CONFIG_VERSION_LENGTH );
		for (byte t = 0; t < IOTWEBCONF_CONFIG_VERSION_LENGTH; t++)
		{
			EEPROM.write(IOTWEBCONF_CONFIG_START + t, 0);
		}
		EEPROM.commit();
		EEPROM.end();
		logw("Factory Reset!");
	}
	mqttReconnectTimer = xTimerCreate("mqttTimer", pdMS_TO_TICKS(5000), pdFALSE, (void *)0, reinterpret_cast<TimerCallbackFunction_t>(connectToMqtt));
	WiFi.onEvent(WiFiEvent);
	_iotWebConf.setupUpdateServer(&_httpUpdater);
	_iotWebConf.addParameter(&seperatorParam);
	_iotWebConf.addParameter(&mqttServerParam);
	_iotWebConf.addParameter(&mqttPortParam);
	_iotWebConf.addParameter(&mqttUserNameParam);
	_iotWebConf.addParameter(&mqttUserPasswordParam);
	_iotWebConf.addParameter(&mqttRootTopicParam);
	boolean validConfig = _iotWebConf.init();
	if (!validConfig)
	{
		logw("!invalid configuration!");
		_mqttServer[0] = '\0';
		_mqttPort[0] = '\0';
		_mqttUserName[0] = '\0';
		_mqttUserPassword[0] = '\0';
		strcpy(_mqttRootTopic, _iotWebConf.getThingName());
		_iotWebConf.resetWifiAuthInfo();
	}
	else
	{
		_iotWebConf.skipApStartup(); // Set WIFI_AP_PIN to gnd to force AP mode
		if (_mqttServer[0] != '\0') // skip if factory reset
		{
			logd("Valid configuration!");
			_clientsConfigured = true;
			// setup MQTT
			_mqttClient.onConnect(onMqttConnect);
			_mqttClient.onDisconnect(onMqttDisconnect);
			_mqttClient.onMessage(onMqttMessage);
			_mqttClient.onPublish(onMqttPublish);
			IPAddress ip;
			if (ip.fromString(_mqttServer))
			{
				int port = atoi(_mqttPort);
				_mqttClient.setServer(ip, port);
				_mqttClient.setCredentials(_mqttUserName, _mqttUserPassword);
				sprintf(_willTopic, "%s/tele/LWT", _mqttRootTopic);
				_mqttClient.setWill(_willTopic, 0, false, "Offline");
			}
		}
	}
	// generate unique id from mac address NIC segment
	uint8_t chipid[6];
	esp_efuse_mac_get_default(chipid);
	_uniqueId = chipid[3] << 16;
	_uniqueId += chipid[4] << 8;
	_uniqueId += chipid[5];
	// Set up required URL handlers on the web server.
	_pWebServer->on("/settings", handleSettings);
	_pWebServer->on("/config", [] { _iotWebConf.handleConfig(); });
	_pWebServer->onNotFound([]() { _iotWebConf.handleNotFound(); });
}

void IOT::Run()
{
	_iotWebConf.doLoop();
	if (_clientsConfigured && WiFi.isConnected())
	{
		// ToDo MQTT monitoring
		// if (_mqttClient.connected())
		// {
		// 	if (_lastPublishTimeStamp < millis())
		// 	{
		// 		_lastPublishTimeStamp = millis() + _currentPublishRate;
		// 		_publishCount++;
		// 		publishReadings();
		// 		// Serial.printf("%d ", _publishCount);
		// 	}
		// 	if (!_stayAwake && _publishCount >= WAKE_COUNT)
		// 	{
		// 		_publishCount = 0;
		// 		_currentPublishRate = SNOOZE_PUBLISH_RATE;
		// 		logd("Snoozing!");
		// 	}
		// }
	}
	else
	{
		if (Serial.peek() == '{')
		{
			String s = Serial.readStringUntil('}');
			s += "}";
			StaticJsonDocument<128> doc;
			DeserializationError err = deserializeJson(doc, s);
			if (err)
			{
				loge("deserializeJson() failed: %s", err.c_str());
			}
			else
			{
				if (doc.containsKey("ssid") && doc.containsKey("password"))
				{
					IotWebConfParameter *p = _iotWebConf.getWifiSsidParameter();
					strcpy(p->valueBuffer, doc["ssid"]);
					logd("Setting ssid: %s", p->valueBuffer);
					p = _iotWebConf.getWifiPasswordParameter();
					strcpy(p->valueBuffer, doc["password"]);
					logd("Setting password: %s", p->valueBuffer);
					p = _iotWebConf.getApPasswordParameter();
					strcpy(p->valueBuffer, TAG); // reset to default AP password
					_iotWebConf.configSave();
					esp_restart(); // force reboot
				}
				else
				{
					logw("Received invalid json: %s", s.c_str());
				}
			}
		}
		else
		{
			Serial.read(); // discard data
		}
	}
}

void IOT::publish(const char *subtopic, const char *value, boolean retained)
{
	if (_mqttClient.connected())
	{
		char buf[64];
		sprintf(buf, "%s/%s", _mqttRootTopic, subtopic);
		_mqttClient.publish(buf, 0, retained, value);
	}
}


} // namespace SkyeTracker