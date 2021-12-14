#include <rcl/error_handling.h>
#include <rcl/rcl.h>
#include <rclc/executor.h>
#include <rclc/rclc.h>
#include <rmw_microros/rmw_microros.h>
#include <rmw_microxrcedds_c/config.h>
#include <std_msgs/msg/float32.h>
#include <stdio.h>
#include <unistd.h>

#include "driver/mcpwm.h"
#include "esp32_serial_transport.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

// You can get these value from the datasheet of servo you use, in general pulse width varies between 1000 to 2000
// microseconds
#define SERVO_MIN_PULSEWIDTH_US (800)   // Minimum pulse width in microsecond
#define SERVO_MAX_PULSEWIDTH_US (2200)  // Maximum pulse width in microsecond
#define SERVO_MAX_DEGREE (90)           // Maximum angle in degree upto which servo can rotate
#define SERVO_PULSE_GPIO_SAIL (19)      // GPIO connects to the PWM signal line
#define SERVO_PULSE_GPIO_RUDDER (18)    // GPIO connects to the PWM signal line

#define MS_DELAY (10)  // Callback delay

rcl_subscription_t sub_sail;
rcl_subscription_t sub_rudder;
std_msgs__msg__Float32 msg_sail;
std_msgs__msg__Float32 msg_rudder;

void sail_callback(const void *msgin) {
    const std_msgs__msg__Float32 *msg = (const std_msgs__msg__Float32 *)msgin;

    // Use angle to calculate PMW

    int32_t angle = msg->data;  // Data sent in degrees
    printf("Sail angle received: %d\r\n", angle);

    uint32_t duty_us =
        (angle + SERVO_MAX_DEGREE) * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) / (2 * SERVO_MAX_DEGREE) +
        SERVO_MIN_PULSEWIDTH_US;

    ESP_ERROR_CHECK(mcpwm_set_duty_in_us(MCPWM_UNIT_0, MCPWM_TIMER_0, MCPWM_OPR_A, duty_us));
    vTaskDelay(pdMS_TO_TICKS(MS_DELAY));  // Add delay, since it takes time for servo to rotate, generally
                                          // 100ms/60degree rotation under 5V power supply

    printf("Sail duty cycle set to: %f\r\n", (float)duty_us);
}

void rudder_callback(const void *msgin) {
    const std_msgs__msg__Float32 *msg = (const std_msgs__msg__Float32 *)msgin;

    // Use angle to calculate PMW

    int32_t angle = msg->data;  // Data sent in degrees
    printf("Rudder angle received: %d\r\n", angle);

    uint32_t duty_us =
        (angle + SERVO_MAX_DEGREE) * (SERVO_MAX_PULSEWIDTH_US - SERVO_MIN_PULSEWIDTH_US) / (2 * SERVO_MAX_DEGREE) +
        SERVO_MIN_PULSEWIDTH_US;

    ESP_ERROR_CHECK(mcpwm_set_duty_in_us(MCPWM_UNIT_1, MCPWM_TIMER_1, MCPWM_OPR_A, duty_us));
    vTaskDelay(pdMS_TO_TICKS(MS_DELAY));  // Add delay, since it takes time for servo to rotate, generally
                                          // 100ms/60degree rotation under 5V power supply

    printf("Rudder duty cycle set to: %f\r\n", (float)duty_us);
}

void micro_ros_task(void *arg) {
    rcl_allocator_t allocator = rcl_get_default_allocator();
    rclc_support_t support;

    // create init_options
    RCCHECK(rclc_support_init(&support, 0, NULL, &allocator));

    // create node
    rcl_node_t node;
    RCCHECK(rclc_node_init_default(&node, "motor_node", "", &support));

    RCCHECK(rclc_subscription_init_default(&sub_sail, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Float32),
                                           "/position/SAIL_ANGLE"));
    vTaskDelay(500 / portTICK_PERIOD_MS);
    RCCHECK(rclc_subscription_init_default(&sub_rudder, &node, ROSIDL_GET_MSG_TYPE_SUPPORT(std_msgs, msg, Float32),
                                           "/rudder/ANGLE"));

    // create executor
    rclc_executor_t executor = rclc_executor_get_zero_initialized_executor();
    RCCHECK(rclc_executor_init(&executor, &support.context, 2, &allocator));

    RCCHECK(rclc_executor_add_subscription(&executor, &sub_sail, &msg_sail, &sail_callback, ON_NEW_DATA));
    RCCHECK(rclc_executor_add_subscription(&executor, &sub_rudder, &msg_rudder, &rudder_callback, ON_NEW_DATA));

    // configure motor control pulse width modulator (MCPWM)
    mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM0A,
                    SERVO_PULSE_GPIO_SAIL);  // To drive a RC servo, one MCPWM generator is enough
    mcpwm_gpio_init(MCPWM_UNIT_1, MCPWM1A,
                    SERVO_PULSE_GPIO_RUDDER);  // To drive a RC servo, one MCPWM generator is enough

    mcpwm_config_t pwm_config = {
        .frequency = 50,  // frequency = 50Hz, i.e. for every servo motor time period should be 20ms
        .cmpr_a = 0,      // duty cycle of PWMxA = 0
        .counter_mode = MCPWM_UP_COUNTER,
        .duty_mode = MCPWM_DUTY_MODE_0,
    };

    mcpwm_init(MCPWM_UNIT_0, MCPWM_TIMER_0, &pwm_config);
    mcpwm_init(MCPWM_UNIT_1, MCPWM_TIMER_1, &pwm_config);

    while (1) {
        rclc_executor_spin_some(&executor, RCL_MS_TO_NS(100));
        usleep(10000);
    }

    // free resources
    RCCHECK(rcl_subscription_fini(&sub_sail, &node));
    RCCHECK(rcl_subscription_fini(&sub_rudder, &node));
    RCCHECK(rcl_node_fini(&node));

    vTaskDelete(NULL);
}

static size_t uart_port = 0;  // UART_NUM_0;

void app_main(void) {
#if defined(RMW_UXRCE_TRANSPORT_CUSTOM)
    rmw_uros_set_custom_transport(true, (void *)&uart_port, esp32_serial_open, esp32_serial_close, esp32_serial_write,
                                  esp32_serial_read);
#else
#error micro-ROS transports misconfigured
#endif  // RMW_UXRCE_TRANSPORT_CUSTOM

    while (RMW_RET_OK != rmw_uros_ping_agent(1000, 1))
        ;

    xTaskCreate(micro_ros_task, "uros_task", CONFIG_MICRO_ROS_APP_STACK, NULL, CONFIG_MICRO_ROS_APP_TASK_PRIO, NULL);
}
