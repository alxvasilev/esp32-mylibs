idf_component_register(
    SRCS spi.cpp st7735.cpp stdfonts.cpp
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_timer driver esp_rom
)
