set(SRCS httpFile.cpp httpServer.cpp netLogger.cpp)

if (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
    list(APPEND SRCS ota.cpp)
    list(APPEND DEPS app_update)
endif()

idf_component_register(SRCS ${SRCS} REQUIRES esp_http_server mySystem esp_http_client INCLUDE_DIRS ".")
component_compile_options(-std=gnu++17 -Wno-missing-field-initializers)
