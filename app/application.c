#include <application.h>
#include <bc_usb_cdc.h>

#define MINUTE 60000

#define INTERVAL 2 * MINUTE
#define BATTERY_INTERVAL 60 * MINUTE


void measurement(bc_module_climate_event_t event, void *event_param)
{
    (void) event_param;
    float value = 0.0f;
    uint8_t buffer[7] = {0};

    uint8_t humidity;
    int16_t temperature;
    uint16_t pressure;
    uint16_t light;

    if(event == BC_MODULE_CLIMATE_EVENT_UPDATE_BAROMETER)
    {
        
        bc_module_climate_get_temperature_celsius(&value);
        temperature = value * 100;

        bc_module_climate_get_humidity_percentage(&value);
        humidity = (uint8_t)value;

        bc_module_climate_get_illuminance_lux(&value);
        light = (uint16_t)value;

        bc_module_climate_get_pressure_pascal(&value);
        value /= 100.0f;
        pressure = (value - 900) * 100;

        memcpy(&buffer[0], &temperature, 2);
        memcpy(&buffer[2], &humidity, 1);
        memcpy(&buffer[3], &light, 2);
        memcpy(&buffer[5], &pressure, 2);

        bc_radio_pub_buffer(&buffer, sizeof(buffer));

    }
}

static void battery_event(bc_module_battery_event_t event, void *event_param)
{
    (void) event_param;

    float voltage;
    int percentage;


    if(bc_module_battery_get_voltage(&voltage))
    {
       bc_radio_pub_battery(&voltage);
    }

    if (bc_module_battery_get_charge_level(&percentage))
    {
        bc_radio_pub_int("bat-pct", &percentage);
    }

    if(event == BC_MODULE_BATTERY_EVENT_LEVEL_LOW)
    {
        int data = 1;
        bc_radio_pub_int("bat-alarm", &data);
    }
    if(event == BC_MODULE_BATTERY_EVENT_LEVEL_CRITICAL)
    {
        int data = 0;
        bc_radio_pub_int("bat-alarm", &data);
    }
}


void application_init(void)
{
   bc_radio_init(BC_RADIO_MODE_NODE_SLEEPING);
   bc_module_battery_init(BC_MODULE_BATTERY_FORMAT_MINI);
   bc_module_battery_set_event_handler(battery_event, NULL);
   bc_module_battery_set_update_interval(BATTERY_INTERVAL);

   bc_module_climate_init();
   bc_module_climate_set_event_handler(measurement, NULL);
   bc_module_climate_set_update_interval_all_sensors(INTERVAL);

   bc_radio_pairing_request("METEOSONDA", "1");
   
}
