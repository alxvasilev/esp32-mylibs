COMPONENT_SRCDIRS := .
COMPONENT_ADD_INCLUDEDIRS += .
CXXFLAGS += -std=gnu++17
$(call compile_only_if,$(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE), ota.o)
