idf_component_register(SRCS httpFile.cpp  httpServer.cpp  netLogger.cpp  ota.cpp REQUIRES esp_http_server mySystem app_update INCLUDE_DIRS ".")
component_compile_options(-std=gnu++17 -Wno-missing-field-initializers)
