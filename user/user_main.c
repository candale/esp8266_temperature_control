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
#define FRACTIONAL(x) (ABS((int)((x - ((int)x)) * 1000)))
#define WHOLE(x) ((int)x)
// Float os_sprintf
// #define ftoa(buff, val) (os_sprintf(buff, "%d.%d", WHOLE(val), FRACTIONAL(val)))

#define INIT_SETPOINT    4
#define INIT_KP          2
#define INIT_KI          0.005
#define INIT_KD          1.2
// WHen the output gets above this value, we turn on the heating
#define ON_TRESHOLD      1.3
// Temperature sample time in seconds
#define TEMP_SAMPLE_TIME 10
// PID config publish time in seconds
#define PID_CONFIG_PUBLISH_TIME 600

#define HEATER_ON_MSG "on"
#define HEATER_OFF_MSG "off"

#define HEATER_GPIO_4 4 // D2 ON THE ESP8266 D1_MINI
#define HEATER_GPIO_MUX_4 PERIPHS_IO_MUX_GPIO4_U
#define HEATER_GPIO_FUNC_4 FUNC_GPIO4


MQTT_Client mqtt_client;
PID_Conf pid_config;
LOCAL os_timer_t sample_timer;
LOCAL os_timer_t pid_config_pub_timer;

uint8_t heater_state = 0;
double on_threshold = ON_TRESHOLD;


void ICACHE_FLASH_ATTR change_setpoint(
    MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count);
void ICACHE_FLASH_ATTR change_kp(
    MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count);
void ICACHE_FLASH_ATTR change_ki(
    MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count);
void ICACHE_FLASH_ATTR change_kd(
    MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count);
void ICACHE_FLASH_ATTR change_on_threshold(
    MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count);
void ICACHE_FLASH_ATTR change_windup_guard(
    MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count);

void ICACHE_FLASH_ATTR on_mqtt_connected(uint32_t* arg);
void ICACHE_FLASH_ATTR publish_data(void* arg);

// RPC structures
const MQTTRPC_Topic_Map topics_map[] = {
        { .topic = "change-setpoint", .handler = (MQTTRPC_Handler)change_setpoint },
        { .topic = "change-kp", .handler = (MQTTRPC_Handler)change_kp },
        { .topic = "change-ki", .handler = (MQTTRPC_Handler)change_ki },
        { .topic = "change-kd", .handler = (MQTTRPC_Handler)change_kd },
        { .topic = "change-on-threshold", .handler = (MQTTRPC_Handler)change_on_threshold },
        { .topic = "change-windup-guard", .handler = (MQTTRPC_Handler)change_windup_guard },
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

    INFO("Changed setpoint to %d.%d\n", WHOLE(setpoint), FRACTIONAL(setpoint));
}


void ICACHE_FLASH_ATTR
change_kp(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got KP data: %s\n", data);

    double kp = atof(data);
    PID_SetKProportinal(&pid_config, kp);

    INFO("Changed PID KP to %d.%d\n", WHOLE(kp), FRACTIONAL(kp));
}


void ICACHE_FLASH_ATTR
change_ki(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got KI data: %s\n", data);

    double ki = atof(data);
    PID_SetKIntegral(&pid_config, ki);

    INFO("Changed PID KI to %d.%d\n", WHOLE(ki), FRACTIONAL(ki));
}

void ICACHE_FLASH_ATTR
change_kd(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got KD data: %s\n", data);

    double kd = atof(data);
    PID_SetKDerivative(&pid_config, kd);

    INFO("Changed PID KD to %d.%d\n", WHOLE(kd), FRACTIONAL(kd));
}


void ICACHE_FLASH_ATTR
change_windup_guard(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got windup guard data: %s\n", data);

    double windup_guard = atof(data);
    PID_SetWindupGuard(&pid_config, windup_guard);

    INFO("Changed windup guard to %d.%d\n", WHOLE(windup_guard), FRACTIONAL(windup_guard));
}


void ICACHE_FLASH_ATTR
change_on_threshold(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got on threshold data: %s\n", data);

    on_threshold = atof(data);

    INFO("Changed on threshold to %d.%d\n", WHOLE(on_threshold), FRACTIONAL(on_threshold));
}


void ICACHE_FLASH_ATTR
publish_data(void* arg) {
    INFO("Reading temperature and humidity ...\n");

    uint8_t state_changed = 0;
    char* buff = (char*)os_zalloc(12);
    float temperature = readTemperature(false);
    float humidity = readHumidity();

    // Decide the state in which the header should be
    double pid_output = PID_Compute(&pid_config, temperature);
    uint8_t should_be_on = pid_output >= on_threshold;

    if(should_be_on != heater_state) {
        set_heater(should_be_on);
        heater_state = should_be_on;
        state_changed = 1;
    }

    INFO("Publishing ...\n");

    if(state_changed) {
        if(heater_state) {
            MQTTRPC_Publish(&rpc_conf, "state", HEATER_ON_MSG, os_strlen(HEATER_ON_MSG), 1, 0);
            INFO("Turned heater -- %s --\n", HEATER_ON_MSG);
        } else {
            MQTTRPC_Publish(&rpc_conf, "state", HEATER_OFF_MSG, os_strlen(HEATER_OFF_MSG), 1, 0);
            INFO("Turned heater -- %s --\n", HEATER_OFF_MSG);
        }
    }

    ftoa(buff, temperature);
    MQTTRPC_Publish(&rpc_conf, "temperature", buff, os_strlen(buff), 1, 1);

    ftoa(buff, humidity);
    MQTTRPC_Publish(&rpc_conf, "humidity", buff, os_strlen(buff), 1, 1);

    ftoa(buff, pid_output);
    MQTTRPC_Publish(&rpc_conf, "pid-output", buff, os_strlen(buff), 1, 1);

    os_free(buff);
}


void ICACHE_FLASH_ATTR
publish_pid_config(void* arg) {
    char* buff = (char*)os_zalloc(20);

    ftoa(buff, pid_config.k_proportioanl);
    MQTTRPC_Publish(&rpc_conf, "report-kp", buff, os_strlen(buff), 1, 1);

    ftoa(buff, pid_config.k_integral);
    MQTTRPC_Publish(&rpc_conf, "report-ki", buff, os_strlen(buff), 1, 1);

    ftoa(buff, pid_config.k_derivative);
    MQTTRPC_Publish(&rpc_conf, "report-kd", buff, os_strlen(buff), 1, 1);

    ftoa(buff, pid_config.setpoint);
    MQTTRPC_Publish(&rpc_conf, "report-setpoint", buff, os_strlen(buff), 1, 1);

    if(heater_state) {
        MQTTRPC_Publish(&rpc_conf, "state", HEATER_ON_MSG, os_strlen(HEATER_ON_MSG), 1, 0);
        INFO("Turned heater -- %s --\n", HEATER_ON_MSG);
    } else {
        MQTTRPC_Publish(&rpc_conf, "state", HEATER_OFF_MSG, os_strlen(HEATER_OFF_MSG), 1, 0);
        INFO("Turned heater -- %s --\n", HEATER_OFF_MSG);
    }

    os_free(buff);
}


void ICACHE_FLASH_ATTR
on_mqtt_connected(uint32_t* arg) {
    os_timer_disarm(&sample_timer);
    os_timer_setfn(&sample_timer, (os_timer_func_t *)publish_data, (void *)0);
    os_timer_arm(&sample_timer, TEMP_SAMPLE_TIME * 1000, 1);

    publish_pid_config(0);

    os_timer_disarm(&pid_config_pub_timer);
    os_timer_setfn(&pid_config_pub_timer, (os_timer_func_t *)publish_data, (void *)0);
    os_timer_arm(&pid_config_pub_timer, PID_CONFIG_PUBLISH_TIME * 1000, 2);
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

    INFO("Setting up MQTT client\n");
    MQTT_InitConnection(&mqtt_client, MQTT_HOST, MQTT_PORT, DEFAULT_SECURITY);
    if (!MQTT_InitClient(&mqtt_client, MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, MQTT_KEEPALIVE, MQTT_CLEAN_SESSION) )
    {
        INFO("Failed to initialize properly. Check MQTT version.\n\r");
        return;
    }

    // Initialize PID controller
    INFO("Init PID controller\n");
    PID_Init(&pid_config, INIT_KP, INIT_KI, INIT_KD, TEMP_SAMPLE_TIME, INIT_SETPOINT);

    INFO("Set on MQTTRpc connected callback\n");
    MQTTRPC_OnConnected(&rpc_conf, on_mqtt_connected);

    INFO("Initializing MQTTRpc\n");
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

