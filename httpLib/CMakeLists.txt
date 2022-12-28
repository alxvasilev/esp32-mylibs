set(SRCS httpFile.cpp httpServer.cpp netLogger.cpp)
set(DEPS esp_http_server mySystem)

if (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
    list(APPEND SRCS ota.cpp)
    list(APPEND DEPS app_update)
endif()

idf_component_register(SRCS ${SRCS} REQUIRES ${DEPS} INCLUDE_DIRS ".")
component_compile_options(-std=gnu++17 -Wno-missing-field-initializers)
