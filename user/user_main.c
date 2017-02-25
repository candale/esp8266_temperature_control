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
#include "utils.h"
#include "ds18b20.h"
#include "pid.h"


#define INIT_SETPOINT    4
#define INIT_KP          2
#define INIT_KI          0.005
#define INIT_KD          2
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
DS18B20_Sensors sensors;
LOCAL os_timer_t sample_timer;
LOCAL os_timer_t pid_config_pub_timer;

// Flags for different state. Might be better to treat this somehow different
uint8_t heater_state = 0;
uint8_t connected_to_wifi = 0;
uint8_t mqtt_connected = 0;
uint8_t rpc_client_init = 0;
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

void ICACHE_FLASH_ATTR setup_mqtt_publishing(uint32_t* arg);

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
}


void ICACHE_FLASH_ATTR
change_kp(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got KP data: %s\n", data);

    double kp = atof(data);
    PID_SetKProportinal(&pid_config, kp);
}


void ICACHE_FLASH_ATTR
change_ki(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got KI data: %s\n", data);

    double ki = atof(data);
    PID_SetKIntegral(&pid_config, ki);
}

void ICACHE_FLASH_ATTR
change_kd(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got KD data: %s\n", data);

    double kd = atof(data);
    PID_SetKDerivative(&pid_config, kd);
}


void ICACHE_FLASH_ATTR
change_windup_guard(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got windup guard data: %s\n", data);

    double windup_guard = atof(data);
    PID_SetWindupGuard(&pid_config, windup_guard);
}


void ICACHE_FLASH_ATTR
change_on_threshold(MQTTRPC_Conf* rpc_conf, char* data, char* args[], uint8_t arg_count) {
    INFO("Got on threshold data: %s\n", data);

    on_threshold = atof(data);
}


void ICACHE_FLASH_ATTR
publish_sample_data(float temperature, float pid_output, int state) {
    // Don't try to publish if we are not connected to the internet
    if(connected_to_wifi != 1) {
        return;
    }

    char* buff = (char*)os_zalloc(12);
    INFO("Publishing ...\n");

    if(state != -1) {
        if(state) {
            MQTTRPC_Publish(&rpc_conf, "state", HEATER_ON_MSG, os_strlen(HEATER_ON_MSG), 1, 0);
            INFO("Turned heater -- %s --\n", HEATER_ON_MSG);
        } else {
            MQTTRPC_Publish(&rpc_conf, "state", HEATER_OFF_MSG, os_strlen(HEATER_OFF_MSG), 1, 0);
            INFO("Turned heater -- %s --\n", HEATER_OFF_MSG);
        }
    }

    ftoa(buff, temperature);
    MQTTRPC_Publish(&rpc_conf, "temperature", buff, os_strlen(buff), 1, 1);

    ftoa(buff, pid_output);
    MQTTRPC_Publish(&rpc_conf, "pid-output", buff, os_strlen(buff), 1, 1);

    os_free(buff);
}


void ICACHE_FLASH_ATTR
sample_and_make_decision(void* arg) {
    INFO("Reading temperature ...\n");
    char buff[20];

    uint8_t state_changed = 0;
    ds18b20_request_temperatures(&sensors);
    float temperature = ds18b20_read(&sensors, 0);

    // Decide the state in which the header should be
    double pid_output = PID_Compute(&pid_config, temperature);
    uint8_t should_be_on = pid_output >= on_threshold;

    if(should_be_on != heater_state) {
        set_heater(should_be_on);
        heater_state = should_be_on;
        state_changed = 1;
    }

    ftoa(buff, temperature);
    INFO("Temperature: %s \n", buff);

    publish_sample_data(
        temperature, pid_output, state_changed == 1 ? heater_state : -1);
}


void ICACHE_FLASH_ATTR
publish_pid_config(void* arg) {
    // Don't try to publish if we are not connected to the internet
    if(connected_to_wifi != 1) {
        return;
    }

    char buff[20];;

    ftoa(buff, pid_config.k_derivative);
    MQTTRPC_Publish(&rpc_conf, "report-kd", buff, os_strlen(buff), 1, 1);
    INFO("KD Publish: %s\n", buff);
    os_memset(buff, 0, 20);

    ftoa(buff, pid_config.k_proportioanl);
    MQTTRPC_Publish(&rpc_conf, "report-kp", buff, os_strlen(buff), 1, 1);
    INFO("KP Publish: %s\n", buff);
    os_memset(buff, 0, 20);

    ftoa(buff, pid_config.k_integral);
    MQTTRPC_Publish(&rpc_conf, "report-ki", buff, os_strlen(buff), 1, 1);
    INFO("KI Publish: %s\n", buff);
    os_memset(buff, 0, 20);

    ftoa(buff, pid_config.setpoint);
    MQTTRPC_Publish(&rpc_conf, "report-setpoint", buff, os_strlen(buff), 1, 1);
    INFO("Setpoint Publish: %s\n", buff);

    if(heater_state) {
        MQTTRPC_Publish(&rpc_conf, "state", HEATER_ON_MSG, os_strlen(HEATER_ON_MSG), 1, 0);
        INFO("Turned heater -- %s --\n", HEATER_ON_MSG);
    } else {
        MQTTRPC_Publish(&rpc_conf, "state", HEATER_OFF_MSG, os_strlen(HEATER_OFF_MSG), 1, 0);
        INFO("Turned heater -- %s --\n", HEATER_OFF_MSG);
    }
}


void ICACHE_FLASH_ATTR
setup_mqtt_publishing(uint32_t* arg) {
    INFO("MQTT connection is live\n");
    mqtt_connected = 1;

    publish_pid_config(0);

    os_timer_disarm(&pid_config_pub_timer);
    os_timer_setfn(&pid_config_pub_timer, (os_timer_func_t *)publish_pid_config, (void *)0);
    os_timer_arm(&pid_config_pub_timer, PID_CONFIG_PUBLISH_TIME * 1000, 2);
}


void ICACHE_FLASH_ATTR
init_temp_control() {
    // Initialize PID controller
    INFO("Init PID controller\n");
    PID_Init(&pid_config, INIT_KP, INIT_KI, INIT_KD, TEMP_SAMPLE_TIME, INIT_SETPOINT);

    // Init sensor
    ds18b20_init(&sensors);
    INFO("Found %d sensors\n", ds18b20_get_all(&sensors));
    if(sensors.count < 1) {
        INFO("ERROR: No sensors found. Cannot go on\n");
    }

    INFO("Changing resolution to 12 bit\n");
    ds18b20_set_resolution(&sensors, 0, DS18B20_TEMP_12_BIT);

    // Set pin 4 (D2 on d1 mini) as output to be used for relay
    PIN_FUNC_SELECT(HEATER_GPIO_MUX_4, HEATER_GPIO_FUNC_4);
}


void ICACHE_FLASH_ATTR
init_mqtt() {
    INFO("Setting up MQTT client\n");
    MQTT_InitConnection(&mqtt_client, MQTT_HOST, MQTT_PORT, DEFAULT_SECURITY);
    if (!MQTT_InitClient(&mqtt_client, MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, MQTT_KEEPALIVE, MQTT_CLEAN_SESSION) )
    {
        INFO("Failed to initialize properly. Check MQTT version.\n\r");
        return;
    }

    INFO("Set on MQTTRpc connected callback\n");
    MQTTRPC_OnConnected(&rpc_conf, setup_mqtt_publishing);

    INFO("Initializing MQTTRpc\n");
    MQTTRpc_Init(&rpc_conf, &mqtt_client);
}


void ICACHE_FLASH_ATTR
manage_wifi(uint8_t status) {
    if(status != STATION_GOT_IP) {
        connected_to_wifi = 0;
        MQTT_Disconnect(&mqtt_client);
    } else {
        connected_to_wifi = 1;
        // If rpc obj was not setup, it will call connect on mqtt
        // TODO: not the nicest thing around. Add state on roc_conf or smth
        if(rpc_client_init == 0) {
            init_mqtt();
            rpc_client_init = 1;
        } else {
            MQTT_Connect(&mqtt_client);
        }
    }
}


void ICACHE_FLASH_ATTR
start_init(void) {
    // Init serial communication
    // system_set_os_print(0);
    uart_div_modify(0, UART_CLK_FREQ / 115200);

    init_temp_control();

    INFO("Connecting to wifi...\n");
    WIFI_Connect(WIFI_SSID, WIFI_PASS, manage_wifi);

    INFO("Setup sample worker\n");
    os_timer_disarm(&sample_timer);
    os_timer_setfn(&sample_timer, (os_timer_func_t *)sample_and_make_decision, (void *)0);
    os_timer_arm(&sample_timer, TEMP_SAMPLE_TIME * 1000, 1);

}

void ICACHE_FLASH_ATTR
user_init()
{
    system_init_done_cb(start_init);
}

