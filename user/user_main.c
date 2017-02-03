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
#include "pid.h"

#define ABS(x) (x < 0 ? x * -1 : x)

#define INIT_SETPOINT    4
#define INIT_KP          2
#define INIT_KI          0.03
#define INIT_KD          1.2
// WHen the output gets above this value, we turn on the heating
#define ON_TRESHOLD      1.3
// Temperature sample time in seconds
#define TEMP_SAMPLE_TIME 10

#define HEATER_ON_MSG "on"
#define HEATER_OFF_MSG "off"

#define HEATER_GPIO_4 4 // D2 ON THE ESP8266 D1_MINI
#define HEATER_GPIO_MUX_4 PERIPHS_IO_MUX_GPIO4_U
#define HEATER_GPIO_FUNC_4 FUNC_GPIO4


MQTT_Client mqtt_client;
PID_Conf pid_config;
LOCAL os_timer_t publish_timer;

uint8_t header_state = 0;
double on_threshold = ON_TRESHOLD;


void ICACHE_FLASH_ATTR change_setpoint(
    MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count);
void ICACHE_FLASH_ATTR change_kp(
    MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count);
void ICACHE_FLASH_ATTR change_ki(
    MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count);
void ICACHE_FLASH_ATTR change_kd(
    MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count);

void ICACHE_FLASH_ATTR on_mqtt_connected(uint32_t* arg);
void ICACHE_FLASH_ATTR publish_data(void* arg);

// RPC structures
const MQTTRPC_Topic_Map topics_map[] = {
        { .topic = "change-setpoint", .handler = (MQTTRPC_Handler)change_setpoint },
        { .topic = "change-kp", .handler = (MQTTRPC_Handler)change_kp },
        { .topic = "change-ki", .handler = (MQTTRPC_Handler)change_ki },
        { .topic = "change-kd", .handler = (MQTTRPC_Handler)change_kd },
        { .topic = 0 }
    };
MQTTRPC_Conf rpc_conf = MQTTRPC_INIT_CONF(.handlers = topics_map);


void ICACHE_FLASH_ATTR
set_heater(uint8_t state) {
    GPIO_OUTPUT_SET(HEATER_GPIO_4, state);
}


void ICACHE_FLASH_ATTR
change_setpoint(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got setpoint data: %s\n", data);

    double setpoint = atof(data);
    PID_Setpoint(&pid_config, setpoint);

    INFO("Changed setpoint to %d.%d\n", (int)setpoint, ABS((int)((setpoint - ((int)setpoint)) * 1000)));
}


void ICACHE_FLASH_ATTR
change_kp(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got KP data: %s\n", data);

    double kp = atof(data);
    PID_SetKProportinal(&pid_config, kp);

    INFO("Changed PID KP to %d.%d\n", (int)kp, ABS((int)((kp - ((int)kp)) * 1000)));
}


void ICACHE_FLASH_ATTR
change_ki(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got KI data: %s\n", data);

    double ki = atof(data);
    PID_SetKIntegral(&pid_config, ki);

    INFO("Changed PID KI to %d.%d\n", (int)ki, ABS((int)((ki - ((int)ki)) * 1000)));
}

void ICACHE_FLASH_ATTR
change_kd(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got KD data: %s\n", data);

    double kd = atof(data);
    PID_SetKDerivative(&pid_config, kd);

    INFO("Changed PID KD to %d.%d\n", (int)kd, ABS((int)((kd - ((int)kd)) * 1000)));
}


void ICACHE_FLASH_ATTR
change_on_threshold(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got on threshold data: %s\n", data);

    on_threshold = atof(data);

    INFO("Changed on threshold to %d.%d\n", (int)on_threshold, ABS((int)((on_threshold - ((int)on_threshold)) * 1000)));
}



void ICACHE_FLASH_ATTR
publish_data(void* arg) {
    INFO("Reading temperature and humidity ...\n");
    uint8_t state_changed = 0;
    float temperature = readTemperature(false);
    float humidity = readHumidity();

    // Decide the state in which the header should be
    double pid_output = PID_Compute(&pid_config, temperature);
    uint8_t should_be_on = pid_output >= on_threshold;

    if(should_be_on != header_state) {
        set_heater(should_be_on);
        header_state = should_be_on;
        state_changed = 1;
    }

    INFO("Publishing ...\n", temperature, humidity);

    // Get string representations of temperature, humidity and state
    char* temp_str = (char*)os_zalloc(12);
    char* hum_str = (char*)os_zalloc(12);
    char* pid_output_str = (char*)os_zalloc(12);
    char* state_str = (char*)os_zalloc(5);

    if(state_changed) {
        if(header_state) {
            os_strcat(state_str, HEATER_ON_MSG);
        } else {
            os_strcat(state_str, HEATER_OFF_MSG);
        }

        MQTTRPC_Publish(&rpc_conf, "state", state_str, os_strlen(state_str), 1, 0);
        INFO("Turned heater -- %s --\n", state_str);
    }

    os_sprintf(temp_str, "%d.%d", (int)temperature, ABS((int)((temperature - ((int)temperature)) * 1000)));
    os_sprintf(hum_str, "%d.%d", (int)humidity, (int)((humidity - ((int)humidity)) * 1000));
    os_sprintf(pid_output_str, "%d.%d", (int)pid_output, (int)((pid_output - ((int)pid_output)) * 1000));

    INFO("Temperature: %s || Humidity: %s\n", temp_str, hum_str);

    MQTTRPC_Publish(&rpc_conf, "temperature", temp_str, os_strlen(temp_str), 1, 1);
    MQTTRPC_Publish(&rpc_conf, "humidity", hum_str, os_strlen(hum_str), 1, 1);
    MQTTRPC_Publish(&rpc_conf, "pid-output", pid_output_str, os_strlen(pid_output_str), 1, 1);

    os_free(temp_str);
    os_free(hum_str);
    os_free(state_str);
    os_free(pid_output_str);
}


void ICACHE_FLASH_ATTR
on_mqtt_connected(uint32_t* arg) {
    os_timer_disarm(&publish_timer);
    os_timer_setfn(&publish_timer, (os_timer_func_t *)publish_data, (void *)0);
    os_timer_arm(&publish_timer, TEMP_SAMPLE_TIME, 1);
}

void ICACHE_FLASH_ATTR
init_systems(uint8_t status) {
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

    MQTT_InitConnection(&mqtt_client, MQTT_HOST, MQTT_PORT, DEFAULT_SECURITY);
    if (!MQTT_InitClient(&mqtt_client, MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, MQTT_KEEPALIVE, MQTT_CLEAN_SESSION) )
    {
        INFO("Failed to initialize properly. Check MQTT version.\n\r");
        return;
    }

    // Initialize PID controller
    PID_Init(&pid_config, INIT_KP, INIT_KP, INIT_KD, TEMP_SAMPLE_TIME, INIT_SETPOINT);

    MQTTRPC_OnConnected(on_mqtt_connected);
    MQTTRpc_Init(&rpc_conf, &mqtt_client);
}


void ICACHE_FLASH_ATTR
start_init(void) {
    // system_set_os_print(0);
    uart_div_modify(0, UART_CLK_FREQ / 115200);

    INFO("Connecting to wifi...\n");
    WIFI_Connect(WIFI_SSID, WIFI_PASS, init_systems);
}

void ICACHE_FLASH_ATTR
user_init()
{
    system_init_done_cb(start_init);
}

