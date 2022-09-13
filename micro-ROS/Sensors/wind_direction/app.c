#include <rcl/error_handling.h>
#include <rcl/rcl.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>
#include <rmw_microros/rmw_microros.h>
#include <std_msgs/msg/float32_multi_array.h>
#include <stdio.h>
#include <unistd.h>

#include "driver/adc.h"
#include "driver/gpio.h"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define RCCHECK(fn)                                                                      \
    {                                                                                    \
        rcl_ret_t temp_rc = fn;                                                          \
        if ((temp_rc != RCL_RET_OK)) {                                                   \
            printf("Failed status on line %d: %d. Aborting.\n", __LINE__, (int)temp_rc); \
            esp_restart();                                                               \
        }                                                                                \
    }
#define RCSOFTCHECK(fn)                                                                    \
    {                                                                                      \
        rcl_ret_t temp_rc = fn;                                                            \
        if ((temp_rc != RCL_RET_OK)) {                                                     \
            printf("Failed status on line %d: %d. Continuing.\n", __LINE__, (int)temp_rc); \
        }                                                                                  \
    }

#define NO_OF_SAMPLES 64
#define FATAL -1000.0

rcl_publisher_t publisher;
std_msgs__msg__Float32MultiArray msg;

float windDir, direction, reading;
int errorTime = 0, errorCounter = 0;
static const adc_channel_t channel = ADC1_CHANNEL_0;
static const adc_atten_t atten = ADC_ATTEN_11db;

void timer_callback(rcl_timer_t* timer, int64_t last_call_time) {
    RCLC_UNUSED(last_call_time);
    if (timer != NULL) {
        // Reset reading between
        reading = 0;

        // Multisampling, takes an amount of samples and averages them
        for (int i = 0; i < NO_OF_SAMPLES; i++) {
            reading += adc1_get_raw((adc1_channel_t)channel);
        }

        // Average readings
        windDir = reading / NO_OF_SAMPLES;
        // Convert to radians
        direction = windDir / 12.3;

        // Adjust for starting position (wait for hardware team to modify)
        direction += 32;

        // Bound to [0, 360]
        if (direction > 360) {
            direction = direction - 360;
        }

        errorTime++;
        if (windDir != -1)
            errorTime = 0;
        else if (errorTime == 1)
            errorCounter++;

        if (errorTime >= 100 || errorCounter >= 4) {
            msg.data.data[0] = FATAL;
            msg.data.size++;
        } else {
            msg.data.data[0] = windDir;
            msg.data.size++;
            msg.data.data[1] = direction;
            msg.data.size++;
        }

        RCCHECK(rcl_publish(&publisher, &msg, NULL));

        msg.data.size = 0;
    }
}

void appMain(void* arg) {
    while (RMW_RET_OK != rmw_uros_ping_agent(1000, 1))
        ;

    rcl_allocator_t allocator = rcl_get_default_allocator();
    rclc_support_t support;

    // create init_options
    RCCHECK(rclc_support_init(&support, 0, NULL, &allocator));

    // create node
    rcl_node_t node;
    RCCHECK(rclc_node_init_default(&node, "wind_node", "", &support));

    // create publisher
    RCCHECK(rclc_publisher_init_default(
        &publisher, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Float32MultiArray), "direction/wind"));
    // RCCHECK(rclc_publisher_init_default(&publisher,	&node, ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Int32),
    // "std_msgs_msg_Int32"));

    // create timer,
    rcl_timer_t timer;
    const unsigned int timer_timeout = 100;
    RCCHECK(rclc_timer_init_default(&timer, &support, RCL_MS_TO_NS(timer_timeout), timer_callback));

    // create executor
    rclc_executor_t executor;
    RCCHECK(rclc_executor_init(&executor, &support.context, 1, &allocator));
    RCCHECK(rclc_executor_add_timer(&executor, &timer));

    //
    // Configure attenuation
    adc1_config_channel_atten(channel, atten);

    static float memory[2];
    msg.data.capacity = 2;
    msg.data.data = memory;
    msg.data.size = 0;
    //

    while (1) {
        rclc_executor_spin_some(&executor, RCL_MS_TO_NS(100));
        usleep(100000);
    }

    // free resources
    RCCHECK(rcl_publisher_fini(&publisher, &node))
    RCCHECK(rcl_node_fini(&node))

    vTaskDelete(NULL);
}