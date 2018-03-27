#include <stm32l0xx.h>
#include <bc_module_battery.h>
#include <bc_gpio.h>
#include <bc_adc.h>
#include <bc_scheduler.h>
#include <bc_timer.h>

#define _BC_MODULE_BATTERY_CELL_VOLTAGE 1.6f

#define _BC_MODULE_BATTERY_STANDATD_DEFAULT_LEVEL_LOW        (1.2 * 4)
#define _BC_MODULE_BATTERY_DEFAULT_DEFAULT_LEVEL_CRITICAL   (1.0 * 4)

#define _BC_MODULE_BATTERY_MINI_DEFAULT_LEVEL_LOW        (1.2 * 2)
#define _BC_MODULE_BATTERY_MINI_DEFAULT_LEVEL_CRITICAL   (1.0 * 2)

#define _BC_MODULE_BATTERY_MINI_VOLTAGE_ON_BATTERY_TO_PERCENTAGE(__VOLTAGE__)      ((100. * __VOLTAGE__) / (_BC_MODULE_BATTERY_CELL_VOLTAGE * 2))
#define _BC_MODULE_BATTERY_STANDARD_VOLTAGE_ON_BATTERY_TO_PERCENTAGE(__VOLTAGE__)  ((100. * __VOLTAGE__) / (_BC_MODULE_BATTERY_CELL_VOLTAGE * 4))

#define _BC_MODULE_BATTERY_MINI_RESULT_TO_VOLTAGE(__RESULT__)       ((__RESULT__) * (1 / 0.33))
#define _BC_MODULE_BATTERY_STANDARD_RESULT_TO_VOLTAGE(__RESULT__)   ((__RESULT__) * (1 / 0.13))

static struct
{
    float voltage;
    bc_module_battery_format_t format;
    float level_low_threshold;
    float level_critical_threshold;
    void (*event_handler)(bc_module_battery_event_t, void *);
    void *event_param;
    bool valid;
    bool measurement_active;
    bc_tick_t update_interval;
    bc_scheduler_task_id_t task_id;

} _bc_module_battery;

static void _bc_module_battery_task();
static void _bc_module_battery_adc_event_handler(bc_adc_channel_t channel, bc_adc_event_t event, void *param);
static void _bc_module_battery_measurement(int state);
static void _bc_module_battery_update_voltage_on_battery(void);

void bc_module_battery_init(bc_module_battery_format_t format)
{
    memset(&_bc_module_battery, 0, sizeof(_bc_module_battery));

    _bc_module_battery.update_interval = BC_TICK_INFINITY;
    _bc_module_battery.task_id = bc_scheduler_register(_bc_module_battery_task, NULL, BC_TICK_INFINITY);
    _bc_module_battery.format = format;

    bc_gpio_init(BC_GPIO_P1);

    _bc_module_battery_measurement(DISABLE);

    if (format == BC_MODULE_BATTERY_FORMAT_MINI)
    {
        _bc_module_battery.level_low_threshold = _BC_MODULE_BATTERY_MINI_DEFAULT_LEVEL_LOW;
        _bc_module_battery.level_critical_threshold = _BC_MODULE_BATTERY_MINI_DEFAULT_LEVEL_CRITICAL;

        bc_gpio_set_output(BC_GPIO_P1, 0);
    }
    else
    {
        _bc_module_battery.level_low_threshold = _BC_MODULE_BATTERY_STANDATD_DEFAULT_LEVEL_LOW;
        _bc_module_battery.level_critical_threshold = _BC_MODULE_BATTERY_DEFAULT_DEFAULT_LEVEL_CRITICAL;

        bc_gpio_set_mode(BC_GPIO_P1, BC_GPIO_MODE_OUTPUT);
    }

    bc_timer_init();

    bc_adc_init(BC_ADC_CHANNEL_A0, BC_ADC_FORMAT_FLOAT);
}

void bc_module_battery_set_event_handler(void (*event_handler)(bc_module_battery_event_t, void *), void *event_param)
{
    _bc_module_battery.event_handler = event_handler;
    _bc_module_battery.event_param = event_param;
}

void bc_module_battery_set_update_interval(bc_tick_t interval)
{
    _bc_module_battery.update_interval = interval;

    if (_bc_module_battery.update_interval == BC_TICK_INFINITY)
    {
        bc_scheduler_plan_absolute(_bc_module_battery.task_id, BC_TICK_INFINITY);
    }
    else
    {
        bc_scheduler_plan_relative(_bc_module_battery.task_id, _bc_module_battery.update_interval);

        bc_module_battery_measure();
    }
}

void bc_module_battery_set_threshold_levels(float level_low_threshold, float level_critical_threshold)
{
    _bc_module_battery.level_low_threshold = level_low_threshold;
    _bc_module_battery.level_critical_threshold = level_critical_threshold;
}

bool bc_module_battery_measure(void)
{
    if (_bc_module_battery.measurement_active)
    {
        return false;
    }

    _bc_module_battery.measurement_active = true;

    _bc_module_battery_measurement(ENABLE);

    bc_adc_set_event_handler(BC_ADC_CHANNEL_A0, _bc_module_battery_adc_event_handler, NULL);

    bc_adc_async_read(BC_ADC_CHANNEL_A0);

    return true;
}

bool bc_module_battery_get_voltage(float *voltage)
{
    if(_bc_module_battery.valid == true)
    {
        *voltage = _bc_module_battery.voltage;

        return true;
    }

    return false;
}

bool bc_module_battery_get_charge_level(int *percentage)
{
    float voltage;

    if (bc_module_battery_get_voltage(&voltage))
    {
        // Calculate the percentage of charge
        if (_bc_module_battery.format == BC_MODULE_BATTERY_FORMAT_MINI)
        {
            *percentage = _BC_MODULE_BATTERY_MINI_VOLTAGE_ON_BATTERY_TO_PERCENTAGE(voltage);
        }
        else
        {
            *percentage = _BC_MODULE_BATTERY_STANDARD_VOLTAGE_ON_BATTERY_TO_PERCENTAGE(voltage);
        }

        if (*percentage >= 100)
        {
            *percentage = 100;
        }

        return true;
    }

    return false;
}

static void _bc_module_battery_task(void *param)
{
    (void) param;

    if (_bc_module_battery.update_interval == BC_TICK_INFINITY)
    {
        bc_scheduler_plan_absolute(_bc_module_battery.task_id, BC_TICK_INFINITY);
    }
    else
    {
        bc_scheduler_plan_current_relative(_bc_module_battery.update_interval);
    }

    bc_module_battery_measure();
}

static void _bc_module_battery_adc_event_handler(bc_adc_channel_t channel, bc_adc_event_t event, void *param)
{
    (void) channel;
    (void) param;

    if (event == BC_ADC_EVENT_DONE)
    {
        _bc_module_battery_update_voltage_on_battery();

        _bc_module_battery_measurement(DISABLE);

        // Unlock measurement
        _bc_module_battery.measurement_active = false;

        if ((_bc_module_battery.voltage < 0) || (_bc_module_battery.voltage > 10))
        {
            _bc_module_battery.valid = false;

            bc_scheduler_plan_relative(_bc_module_battery.task_id, 1000);

            return;
        }

        if (_bc_module_battery.valid && _bc_module_battery.event_handler != NULL)
        {
            // Notify event based on calculated percentage
            if (_bc_module_battery.voltage <= _bc_module_battery.level_critical_threshold)
            {
                _bc_module_battery.event_handler(BC_MODULE_BATTERY_EVENT_LEVEL_CRITICAL, _bc_module_battery.event_param);
            }
            else if (_bc_module_battery.voltage <= _bc_module_battery.level_low_threshold)
            {
                _bc_module_battery.event_handler(BC_MODULE_BATTERY_EVENT_LEVEL_LOW, _bc_module_battery.event_param);
            }

            _bc_module_battery.event_handler(BC_MODULE_BATTERY_EVENT_UPDATE, _bc_module_battery.event_param);
        }
    }
}

static void _bc_module_battery_measurement(int state)
{
    if (_bc_module_battery.format == BC_MODULE_BATTERY_FORMAT_MINI)
    {
        bc_gpio_set_mode(BC_GPIO_P1, state == ENABLE ? BC_GPIO_MODE_OUTPUT : BC_GPIO_MODE_ANALOG);
    }
    else
    {
        bc_gpio_set_output(BC_GPIO_P1, state);
    }

    if (state == ENABLE)
    {
        bc_timer_start();

        bc_timer_delay(100);

        bc_timer_stop();
    }
}

static void _bc_module_battery_update_voltage_on_battery(void)
{
    float v;

    _bc_module_battery.valid = bc_adc_get_result(BC_ADC_CHANNEL_A0, &v);

    if (!_bc_module_battery.valid)
    {
        return;
    }

    if (_bc_module_battery.format == BC_MODULE_BATTERY_FORMAT_MINI)
    {
        _bc_module_battery.voltage = _BC_MODULE_BATTERY_MINI_RESULT_TO_VOLTAGE(v);
    }
    else
    {
        _bc_module_battery.voltage = _BC_MODULE_BATTERY_STANDARD_RESULT_TO_VOLTAGE(v);
    }
}
