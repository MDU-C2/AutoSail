# Motor control
This application creates micro-ROS nodes that takes an angle in radians and converts it into a PWM signal to be sent to the connected motors. The input values can be between -pi/2 and pi/2, but the physical servo does not quite achieve a full 180 degree rotation; but it is close enough that it should hardly make a difference. The outputted PWM singal is per the servo's specifications between 800 and 2200 microseconds. The version of ROS2 is Foxy on Ubuntu 20.04.

The sail PWM signal is sent to GPIO19 and the rudder PWM signal is sent to GPIO18, being pin 19 and 18 on the ESP32 respectively.

The application subscribes to /position/SAIL_ANGLE and /rudder/ANGLE.

## Prerequsities
This application assumes the user has installed ROS2 and micro-ROS using the following tutorials:

[ros2 installtion script](https://github.com/Tiryoh/ros2_setup_scripts_ubuntu)

[micro-ROS](https://micro.ros.org/docs/tutorials/core/first_application_rtos/freertos/)

## Usage

Place the motor_control folder in ../firmware/freertos_apps/apps/ in your micro-ROS workspace. 

### Setup
Sources the proper micro-ROS environment and prepares the building and flashing to the correct app.
```
cd ~/microros_ws

colcon build

source install/local_setup.bash
```
```
ros2 run micro_ros_setup configure_firmware.sh motor_control --transport serial
```

### Build, flash
Builds the app and flashes it to the esp32. Make sure the device is plugged in, and that you have your port unlocked, which can be done by [adding your user to the dialout group](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/establish-serial-connection.html#linux-dialout-group) .
```
ros2 run micro_ros_setup build_firmware.sh

ros2 run micro_ros_setup flash_firmware.sh
```
### Agent

You only have to create and build the agent the first time, afterwards you only need to run the micro-ROS agent

```
ros2 run micro_ros_setup create_agent_ws.sh

ros2 run micro_ros_setup build_agent.sh

source install/local_setup.bash
```
Find the device ID using:
```
ls /dev/serial/by-id/*
```
Use the ID from earlier when running the agent
```
ros2 run micro_ros_agent micro_ros_agent serial --dev [device ID]
```
You might have to press the restart button on the esp32 if the agent does not work properly. See the image below on what is a normal look for the agent. For example, if only the first two rows are shown in the terminal, you might have to press the restart button on the esp32.

![normal_agent](https://user-images.githubusercontent.com/31732187/141467001-6a39c2ac-4bb9-48d2-903c-675f5fb736d9.png)

In another terminal, monitor the topic you published to
```
ros2 topic echo /position/SAIL_ANGLE
ros2 topic echo /rudder/ANGLE
```