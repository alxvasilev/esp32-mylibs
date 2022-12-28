set(SRCS nvsSimple.cpp sdcard.cpp uart.cpp utils.cpp utils-parse.cpp wifi.cpp)
set(DEPS nvs_flash esp_http_server httpLib fatfs)

if (CONFIG_BT_ENABLED)
    list(APPEND SRCS bluetooth.cpp)
    list(APPEND DEPS bt)
endif()

if (CONFIG_TASKLIST_INCLUDE_COREID)
    list(APPEND SRCS taskList.cpp)
endif()

idf_component_register(SRCS ${SRCS} REQUIRES ${DEPS} INCLUDE_DIRS ".")
component_compile_options(-std=gnu++17)
