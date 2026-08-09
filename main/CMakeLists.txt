set(srcs
    main.c
    speech_commands_action.c
    cpu_monitor.c
    mqtt_server.c
    )

set(requires
    hardware_driver
    esp-wifi-manager
    mqtt
    )

idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS include
                       REQUIRES ${requires})

component_compile_options(-w)