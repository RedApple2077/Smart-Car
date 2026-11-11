cmake_minimum_required(VERSION 3.22)

project(sc_i2c2_host_tests LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(SC_I2C2_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_i2c2.c"
    CACHE FILEPATH "sc_i2c2.c under test")
set(SC_MPU6050_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_mpu6050.c"
    CACHE FILEPATH "sc_mpu6050.c under test")
set(SC_SSD1306_SOURCE
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Src/sc_ssd1306.c"
    CACHE FILEPATH "sc_ssd1306.c under test")

add_executable(sc_i2c2_tests
    "${SC_I2C2_SOURCE}"
    "${SC_MPU6050_SOURCE}"
    "${SC_SSD1306_SOURCE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/fake/fake_hal.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/test_sc_i2c2.c"
)
target_include_directories(sc_i2c2_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/fake"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../Core/Inc"
)

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(sc_i2c2_tests PRIVATE
        -Wall
        -Wextra
        -Werror
    )
endif()

enable_testing()

set(SC_I2C2_TEST_CASES
    i2c_config_and_idempotency
    blocking_busy_settle
    i2c_single_transaction
    mpu_init_who_and_config
    mpu_decode_and_failure
    mpu_bias_api
    mpu_calibration
    mpu_calibration_completion_race
    mpu_calibration_failure_isolation
    oled_init_chunks
    oled_generation_protection
    oled_failure_backoff
    shared_bus_priority
    mpu_failure_oled_isolation
    device_failure_isolation
)

foreach(test_case IN LISTS SC_I2C2_TEST_CASES)
    add_test(NAME "${test_case}" COMMAND sc_i2c2_tests "${test_case}")
endforeach()
