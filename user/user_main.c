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
#include "ds18b20.h"
#include "utils.h"


#define DEEP_SLEEP_OPERATION 1
#define NORMAL_OPERATION     2
#define THERMOSTAT_OPERATION_MODE NORMAL_OPERATION

#define ABS(x) (x < 0 ? x * -1 : x)

MQTT_Client mqttClient;
DS18B20_Sensors sensors;
LOCAL os_timer_t sleep_timer;
LOCAL os_timer_t read_and_pub_timer;;

const MQTTRPC_Topic_Map topics_map[] = {
        { .topic = 0 }
    };
MQTTRPC_Conf rpc_conf = MQTTRPC_INIT_CONF(.handlers = topics_map);


void ICACHE_FLASH_ATTR print_info()
{
  INFO("\r\n\r\n[INFO] BOOTUP...\r\n");
  INFO("[INFO] SDK: %s\r\n", system_get_sdk_version());
  INFO("[INFO] Chip ID: %08X\r\n", system_get_chip_id());
  INFO("[INFO] Memory info:\r\n");
  system_print_meminfo();
}


void ICACHE_FLASH_ATTR
go_to_sleep(void *arg) {
    INFO("Going to sleep\n");
    os_timer_disarm(&sleep_timer);
    // 0 means Radio calibration after deep-sleep wake up depends on init data byte 108.
    system_deep_sleep_set_option(0);
    system_deep_sleep(1200 * 1000 * 1000);
}

void ICACHE_FLASH_ATTR
read_and_publish_data() {
    char* temp_str = (char*)os_zalloc(32);


    if(sensors.count >= 1) {
        INFO("Reading temperature 1 ...\n");
        float temperature1 = ds18b20_read(&sensors, 0);
        ftoa(temp_str, temperature1);
        INFO("Publishing TEMP 1: %s\n", temp_str);
        MQTTRPC_Publish(&rpc_conf, "temperature", temp_str, os_strlen(temp_str), 2, 1);
    }


    if(sensors.count >= 2) {
        INFO("Reading temperature 2 ...\n");
        float temperature2 = ds18b20_read(&sensors, 1);
        ftoa(temp_str, temperature2);
        INFO("Publishing TEMP 2: %s\n", temp_str);
        MQTTRPC_Publish(&rpc_conf, "temperature_2", temp_str, os_strlen(temp_str), 2, 1);
    }

    os_free(temp_str);
}


void ICACHE_FLASH_ATTR
read_and_publish_data_fn(void* arg) {
    read_and_publish_data();
}


void ICACHE_FLASH_ATTR
setup_worker(void* arg) {
    if(THERMOSTAT_OPERATION_MODE == DEEP_SLEEP_OPERATION) {
        read_and_publish_data();

        // Give it some time so MQTT can do its thing and then go to sleep
        os_timer_disarm(&sleep_timer);
        os_timer_setfn(&sleep_timer, (os_timer_func_t*)go_to_sleep, (void *)0);
        os_timer_arm(&sleep_timer, 2000, 0);
    } else if(THERMOSTAT_OPERATION_MODE == NORMAL_OPERATION) {
        os_timer_disarm(&read_and_pub_timer);
        os_timer_setfn(&read_and_pub_timer, (os_timer_func_t*)read_and_publish_data_fn, (void*)0);
        os_timer_arm(&read_and_pub_timer, 3000, 1);
    }
}


void ICACHE_FLASH_ATTR
init_mqtt_rpc(uint8_t status) {
    if(status != STATION_GOT_IP) {
        return;
    }

    INFO("Initialize DS18B20\n");
    ds18b20_init(&sensors);
    ds18b20_get_all(&sensors);
    INFO("Found %d sensors\n", sensors.count);

    if(sensors.count != 2) {
        INFO("WARNING: You don't seem to have two sensors\n");
    }

    INFO("Initialize MQTT client\r\n");
    MQTT_InitConnection(&mqttClient, MQTT_HOST, MQTT_PORT, DEFAULT_SECURITY);
    if (!MQTT_InitClient(&mqttClient, MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, MQTT_KEEPALIVE, MQTT_CLEAN_SESSION) )
    {
        INFO("Failed to initialize properly. Check MQTT version.\n\r");
        return;
    }

    MQTTRPC_OnConnected(&rpc_conf, setup_worker);
    MQTTRpc_Init(&rpc_conf, &mqttClient);
}


void ICACHE_FLASH_ATTR
init_all(void) {
    // system_set_os_print(0);
    uart_div_modify(0, UART_CLK_FREQ / 115200);
    print_info();

    INFO("Connecting to wifi...\n");
    WIFI_Connect(WIFI_SSID, WIFI_PASS, init_mqtt_rpc);
}

void ICACHE_FLASH_ATTR
user_init()
{
    system_init_done_cb(init_all);
}
