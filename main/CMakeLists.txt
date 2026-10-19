set(bc250_embeds)
if(CONFIG_BC250_WIFI)
    get_filename_component(bc250_tls "${CONFIG_BC250_TLS_DIRECTORY}" ABSOLUTE BASE_DIR "${PROJECT_DIR}")
    foreach(pem servercert.pem prvtkey.pem)
        if(NOT EXISTS "${bc250_tls}/${pem}")
            message(FATAL_ERROR "Missing local TLS file ${bc250_tls}/${pem}; see docs/BC250_POWER.md")
        endif()
        list(APPEND bc250_embeds "${bc250_tls}/${pem}")
    endforeach()
    list(APPEND bc250_embeds "power/power.html")
endif()

idf_component_register(
    SRCS
        # Main application
        "main_lvgl.c"

        # Drivers
        "lvgl_gc9a01_driver.c"
        "drivers/usb_serial_comm.c"
        "power/power_fsm.c"
        "power/power_security.c"
        "power/power_controller.c"
        "power/power_network.c"

        # Storage modules
        "storage/storage_mgr.c"
        "storage/hw_identity.c"
        "gui_settings.c"

        # UI modules
        "ui/ui_manager.c"
        "ui/screensaver_mgr.c"

        # Screen implementations
        "screens/screen_unified_lvgl.c"
        "screens/screen_split_ring.c"

        # Compiled images (system icons - Desert-Spec v2.2)
        "images/CPU.c"
        "images/GPU.c"
        "images/RAM.c"
        "images/NET.c"

    INCLUDE_DIRS
        "."
        "core"
        "screens"
        "storage"
        "drivers"
        "ui"
        "power"

    EMBED_TXTFILES ${bc250_embeds}

    REQUIRES
        lvgl
        esp_lcd
        driver
        esp_driver_usb_serial_jtag
        esp_driver_uart
        esp_timer
        esp_wifi
        esp_event
        esp_netif
        esp_https_server
        nvs_flash
)

littlefs_create_partition_image(storage "${PROJECT_DIR}/lfs" FLASH_IN_PROJECT)
