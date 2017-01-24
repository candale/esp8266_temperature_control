#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"
#include "os_type.h"
#include "user_config.h"
#include "wifi.h"
#include "mqtt.h"
#include "mem.h"
#include "mqtt_rpc.h"
#include "user_interface.h"
#include "dht22.h"
#include "utils.h"


#define ABS(x) (x < 0 ? x * -1 : x)

#define SETPOINT 10
#define SETPOINT_ERROR 0.7

#define HEATER_ON_MSG "on"
#define HEATER_OFF_MSG "off"

#define HEATER_GPIO_4 4 // D2 ON THE ESP8266 D1_MINI
#define HEATER_GPIO_MUX_4 PERIPHS_IO_MUX_GPIO4_U
#define HEATER_GPIO_FUNC_4 FUNC_GPIO4


MQTT_Client mqttClient;
LOCAL os_timer_t publish_timer;
float setpoint = SETPOINT;
uint8_t current_state = 0;


void ICACHE_FLASH_ATTR change_setpoint(
    MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count);
void ICACHE_FLASH_ATTR on_mqtt_connected(uint32_t* arg);
void ICACHE_FLASH_ATTR publish_data(void* arg);

// RPC structures
const MQTTRPC_Topic_Map topics_map[] = {
        { .topic = "change-setpoint", .handler = change_setpoint },
        { .topic = 0 }
    };
MQTTRPC_Conf rpc_conf = MQTTRPC_INIT_CONF(
    .handlers = topics_map, .connected_cb = on_mqtt_connected);


void ICACHE_FLASH_ATTR
set_heater(uint8_t state) {
    GPIO_OUTPUT_SET(HEATER_GPIO_4, state);
}


void ICACHE_FLASH_ATTR
change_setpoint(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got setpoint data: %s\n", data);

    setpoint = atof(data);

    INFO("Changed setpoint to %d.%d\n", (int)setpoint, ABS((int)((setpoint - ((int)setpoint)) * 1000)));
}


void ICACHE_FLASH_ATTR
on_mqtt_connected(uint32_t* arg) {
    os_timer_disarm(&publish_timer);
    os_timer_setfn(&publish_timer, (os_timer_func_t *)publish_data, (void *)0);
    os_timer_arm(&publish_timer, 15000, 1);
}


void ICACHE_FLASH_ATTR
publish_data(void* arg) {
    INFO("Reading temperature and humidity ...\n");
    uint8_t state_changed = 0;
    float temperature = readTemperature(false);
    float humidity = readHumidity();

    // Decide the state in which the header should be
    uint8_t should_be_on = temperature < setpoint - SETPOINT_ERROR;

    if(should_be_on != current_state) {
        set_heater(should_be_on);
        current_state = should_be_on;
        state_changed = 1;
    }

    INFO("Publishing ...\n", temperature, humidity);

    // Get string representations of temperature, humidity and state
    char* temp_str = (char*)os_zalloc(32);
    char* hum_str = (char*)os_zalloc(32);
    char* state_str = (char*)os_zalloc(5);

    if(state_changed) {
        if(current_state) {
            os_strcpy(state_str, HEATER_ON_MSG);
        } else {
            os_strcpy(state_str, HEATER_OFF_MSG);
        }
        MQTTRPC_Publish(&rpc_conf, "state", state_str, os_strlen(state_str), 1, 0);
        INFO("Turned heater -- %s --\n", state_str);
    }

    os_sprintf(temp_str, "%d.%d", (int)temperature, ABS((int)((temperature - ((int)temperature)) * 1000)));
    os_sprintf(hum_str, "%d.%d", (int)humidity, (int)((humidity - ((int)humidity)) * 1000));

    INFO("Temperature: %s || Humidity: %s\n", temp_str, hum_str);

    MQTTRPC_Publish(&rpc_conf, "temperature", temp_str, os_strlen(temp_str), 1, 1);
    MQTTRPC_Publish(&rpc_conf, "humidity", hum_str, os_strlen(hum_str), 1, 1);

    os_free(temp_str);
    os_free(hum_str);
}


void ICACHE_FLASH_ATTR
init_mqtt_rpc(uint8_t status) {
    if(status != STATION_GOT_IP) {
        return;
    }

    // Init DHT on PIN 14 -> D5
    // Not sure what the third argument is, it is called count
    INFO("Init DHT22\n");
    DHT_init(14, DHT22, 6);
    DHT_begin();

    // Set pin 4 (D2 on d1 mini) as output
    PIN_FUNC_SELECT(HEATER_GPIO_MUX_4, HEATER_GPIO_FUNC_4);

    MQTT_InitConnection(&mqttClient, MQTT_HOST, MQTT_PORT, DEFAULT_SECURITY);
    if (!MQTT_InitClient(&mqttClient, MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, MQTT_KEEPALIVE, MQTT_CLEAN_SESSION) )
    {
        INFO("Failed to initialize properly. Check MQTT version.\n\r");
        return;
    }
    MQTTRpc_Init(&rpc_conf, &mqttClient);
}


void ICACHE_FLASH_ATTR
init_all(void) {
    // system_set_os_print(0);
    uart_div_modify(0, UART_CLK_FREQ / 115200);

    INFO("Connecting to wifi...\n");
    WIFI_Connect(WIFI_SSID, WIFI_PASS, init_mqtt_rpc);
}

void ICACHE_FLASH_ATTR
user_init()
{
    system_init_done_cb(init_all);
}

