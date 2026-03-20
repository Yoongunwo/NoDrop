set(CTRL ctrl)
set(CTRL_SOURCES
        nodrop-ctl.c
        ${PROJECT_SOURCE_DIR}/include/ioctl.h)
include_directories(${PROJECT_SOURCE_DIR}/include)

add_executable(${CTRL} EXCLUDE_FROM_ALL ${CTRL_SOURCES})

set(NODROP_DUMP nodrop-dump)
set(NODROP_DUMP_SOURCES
        nodrop-dump.c
        ${PROJECT_SOURCE_DIR}/kmodule/tables/events_table.c
        ${PROJECT_SOURCE_DIR}/kmodule/tables/flags_table.c
        ${PROJECT_SOURCE_DIR}/kmodule/tables/dynamic_params_table.c
        ${PROJECT_SOURCE_DIR}/include/events.h)
add_executable(${NODROP_DUMP} EXCLUDE_FROM_ALL ${NODROP_DUMP_SOURCES})
add_dependencies(${CTRL} ${NODROP_DUMP})
