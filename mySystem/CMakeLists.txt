set(SRCS nvsSimple.cpp sdcard.cpp uart.cpp utils.cpp utils-parse.cpp wifi.cpp asyncCall.cpp)
set(DEPS nvs_flash esp_http_server httpLib fatfs esp_timer esp_wifi bt esp_hid mdns)

if (CONFIG_BT_ENABLED)
    list(APPEND SRCS bluetooth.cpp)
    list(APPEND DEPS bt)
endif()

if (CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID)
    list(APPEND SRCS taskList.cpp)
endif()
#message(STATUS "=============SRCS=${SRCS}")
idf_component_register(SRCS ${SRCS} REQUIRES ${DEPS} INCLUDE_DIRS ".")
