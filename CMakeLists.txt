# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

macro(app_set_runner_args)
  board_runner_args(canopen "--node-id=${CONFIG_CANOPEN_NODE_ID}")
endmacro()


find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(rehab-bot)

target_sources(app PRIVATE
  src/main.c
  src/bt_main.c
  src/bt_settings.c
  src/config_svc.c
  src/can.c
  src/sdcard.c
  src/button.c
  src/events.c
  src/led.c
  src/loadcell.c
  # src/flashdrive.c
)
target_sources_ifdef(CONFIG_HAS_BLE_FSR app PRIVATE
  src/fsr.c
)
target_sources_ifdef(CONFIG_HAS_BLE_CONTROLLER app PRIVATE
  src/controller.c
)

target_sources_ifdef(CONFIG_HAS_UART_IMU app PRIVATE
  src/uart_imu.c
)

target_sources_ifdef(CONFIG_BT_CYW43XX_ALT app PRIVATE
  src/h4_ifx_cyw43xxx_alt.c
  src/h4_alt.c
)
