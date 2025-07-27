ZEPHYR_ROOT=~/zephyrproject
source ${ZEPHYR_ROOT}/.venv/bin/activate
source ${ZEPHYR_ROOT}/zephyr/zephyr-env.sh

west config build.board arduino_giga_r1/stm32h747xx/m7
west config build.board arduino_portenta_h7/stm32h747xx/m7

