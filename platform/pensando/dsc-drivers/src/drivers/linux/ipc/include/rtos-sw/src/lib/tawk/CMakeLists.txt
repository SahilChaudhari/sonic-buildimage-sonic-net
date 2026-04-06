# SPDX-License-Identifier: MIT
# SPDX-FileCopyrightText: Copyright (C) 2024 Advanced Micro Devices, Inc.

zephyr_library_named(tawk)

find_package(Doxygen)

target_include_directories(tawk PUBLIC include)

set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/docs")
set(DOXYGEN_PROJECT_NAME "TAWK")
set(DOXYGEN_PROJECT_NUMBER "0.0.0")
set(DOXYGEN_PREDEFINED "__DOXYGEN__")
set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C "YES")
set(DOXYGEN_EXTRACT_ALL "YES")
set(DOXYGEN_EXTRACT_PRIVATE "YES")
#doxygen_add_docs(tawk_docs include/tawk/ipc.h)

set(DOXYGEN_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/internal_docs")
set(DOXYGEN_PROJECT_NAME "TAWK INTERNALS")
set(DOXYGEN_PROJECT_NUMBER "0.0.0")
set(DOXYGEN_PREDEFINED "__DOXYGEN__")
set(DOXYGEN_OPTIMIZE_OUTPUT_FOR_C "YES")
set(DOXYGEN_EXTRACT_ALL "YES")
set(DOXYGEN_EXTRACT_PRIVATE "YES")
set(DOXYGEN_EXCLUDE "extra")
#doxygen_add_docs(tawk_internal_docs .)

zephyr_library_sources(protocol.c)
zephyr_library_sources(ipc.c)
zephyr_library_sources(platform/platform.c)
zephyr_library_sources(device/device.c)
zephyr_library_sources_ifdef(CONFIG_BOARD_SIM device/sim.c)
zephyr_library_sources_ifndef(CONFIG_BOARD_SIM device/mm.c device/pcie.c)
zephyr_library_sources(hardware/hardware.c hardware/rstsigs.c hardware/mailbox.c)
zephyr_library_sources(datalink/datalink.c datalink/rstfsm.c datalink/fifo.c)
zephyr_library_sources(transport/transport.c transport/cfgfsm.c)

# This is solely do to get the RTOS define.  That should really
# be provided at a wider level.
zephyr_library_link_libraries(platform log_helper)
zephyr_library_link_libraries_ifdef(CONFIG_TAWK_IPC_WITH_INTR intrutils)
