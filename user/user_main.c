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


#define ABS(x) (x < 0 ? x * -1 : x)

MQTT_Client mqttClient;
LOCAL os_timer_t sleep_timer;

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
publish_data(uint32_t *arg) {
    MQTTRPC_Conf* rpc_conf = (MQTTRPC_Conf*)arg;

    INFO("Reading temperature and humidity ...\n");
    float temperature = readTemperature(false);
    float humidity = readHumidity();

    char* temp_str = (char*)os_zalloc(32);
    char* hum_str = (char*)os_zalloc(32);

    os_sprintf(temp_str, "%d.%d", (int)temperature, ABS((int)((temperature - ((int)temperature)) * 1000)));
    os_sprintf(hum_str, "%d.%d", (int)humidity, (int)((humidity - ((int)humidity)) * 1000));

    INFO("Publishing TEMP: %s  HUM: %s\n", temp_str, hum_str);

    MQTTRPC_Publish(rpc_conf, "temperature", temp_str, os_strlen(temp_str), 2, 1);
    MQTTRPC_Publish(rpc_conf, "humidity", hum_str, os_strlen(hum_str), 2, 1);

    os_free(temp_str);
    os_free(hum_str);

    // Give it some time so MQTT can do its thing
    os_timer_disarm(&sleep_timer);
    os_timer_setfn(&sleep_timer, (os_timer_func_t *)go_to_sleep, (void *)0);
    os_timer_arm(&sleep_timer, 2000, 0);
}

const MQTTRPC_Topic_Map topics_map[] = {
        { .topic = 0 }
    };
MQTTRPC_Conf rpc_conf = MQTTRPC_INIT_CONF(
    .handlers = topics_map, .connected_cb = publish_data);


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

    INFO("Initialize MQTT client\r\n");
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
    print_info();

    INFO("Connecting to wifi...\n");
    WIFI_Connect(WIFI_SSID, WIFI_PASS, init_mqtt_rpc);
}

void ICACHE_FLASH_ATTR
user_init()
{
    system_init_done_cb(init_all);
}
