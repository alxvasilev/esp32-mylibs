set(SRCS nvsSimple.cpp sdcard.cpp uart.cpp utils.cpp utils-parse.cpp wifi.cpp)
set(EXTRA_DEPS)

if (CONFIG_BT_ENABLED)
    set(SRCS ${SRCS} bluetooth.cpp)
    list(APPEND EXTRA_DEPS bt)
endif()

if (CONFIG_TASKLIST_INCLUDE_COREID)
    list(APPEND SRCS taskList.cpp)
endif()

idf_component_register(SRCS ${SRCS} REQUIRES nvs_flash esp_http_server httpLib fatfs ${EXTRA_DEPS} INCLUDE_DIRS ".")
component_compile_options(-std=gnu++17)
