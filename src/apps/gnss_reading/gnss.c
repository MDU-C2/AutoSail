#include <rcl/error_handling.h>
#include <rcl/rcl.h>
#include <rclc/rclc.h>
#include <std_msgs/msg/float32_multi_array.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include "components/nmea/include/nmea.h"
#include "components/protocol/include/protocol.h"
#include "components/nmea/nmea.c"
#include "components/protocol/protocol.c"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "driver/timer.h"
#include "esp_log.h"
#include "esp_system.h"
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

#define FATAL -1000.0
#define THREE_SECONDS 3

rcl_publisher_t publisher_gnss;
std_msgs__msg__Float32MultiArray msg_gnss;

// variables
int i;
float lon;
float lat;
uint8_t* data;
char* message;
int calibrate = 1;
int begin_timer = 0;
volatile int timeout = 0;
clock_t start_t, end_t;

int count_gnss = 0;

void gnss_callback(rcl_timer_t * timer, int64_t last_call_time) 
{
    (void) last_call_time;
    if (timer != NULL) {
        // read data from the sensor
        i2c_read(I2C_MASTER_NUM, data, 400);

        // convert the data to char
        i = 0;
        lon = 0;
        lat = 0;

        // Only convert up to the * sign, since that marks the end of a message
        while ((data[i] != 42) && (i < 82)) {
            message[i] = (char)data[i];
            i++;
        }

        // get the gnss position
        if (!getPos(message, &lat, &lon)) {
            if (calibrate == 0) {
                begin_timer = 1;
            }
            // printf("No data avalible for %d readings\n", c);
        } else {
            start_t = clock();
            calibrate = 0;
            begin_timer = 0;
            // printf("Lat: %.4f : Long: %.4f\n", lat, lon);
        }

        //
        if (begin_timer) {
            end_t = clock();
            if (((end_t - start_t) / CLOCKS_PER_SEC) >= THREE_SECONDS) {
                timeout = 1;
                msg_gnss.data.data[0] = FATAL;
                msg_gnss.data.size++;
                msg_gnss.data.data[1] = FATAL;
                msg_gnss.data.size++;
            }
        }

        if ((lon != 0 && lat != 0) && timeout == 0) {
            msg_gnss.data.data[0] = lat;
            msg_gnss.data.size++;
            msg_gnss.data.data[1] = lon;
            msg_gnss.data.size++;
        } else {
            msg_gnss.data.data[0] = 0.0;
            msg_gnss.data.size++;
            msg_gnss.data.data[1] = 0.0;
            msg_gnss.data.size++;
        }

        count_gnss++;
        msg_gnss.layout.data_offset = count_gnss;

        RCSOFTCHECK(rcl_publish(&publisher_gnss, &msg_gnss, NULL));
        msg_gnss.data.size = 0;
    }
}

void init_gnss_wind() {
    // variables
    i = 0;
    lon = 0;
    lat = 0;

    data = calloc(100, 4);
    message = calloc(100, 1);

    // msg setup
    static float memory_gnss[2];
    msg_gnss.data.capacity = 2;
    msg_gnss.data.data = memory_gnss;
    msg_gnss.data.size = 0;

    if (message == NULL) {
        printf("Calloc for msg failed\n");
    }
    if (data == NULL) {
        printf("Calloc for data failed\n");
    }


    // configure i2c
    configure_i2c_master();
}