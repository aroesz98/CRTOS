# The boot loader of the STM32H7S78-DK: a program of its own for the 64 KB internal flash
# (0x08000000) that sets up the clocks and the two XSPI memories and starts the kernel from the
# NOR flash (boot/main.c). Results: build/kernel/crtos-boot.axf, crtos-boot.bin ('crtos flash
# --boot' writes it; it rarely changes).

set(HAL ${CRTOS_STM32_HAL})
add_executable(crtos_boot
  startup.c main.c xspi.c
  ${HAL}/Src/stm32h7rsxx_hal.c
  ${HAL}/Src/stm32h7rsxx_hal_cortex.c
  ${HAL}/Src/stm32h7rsxx_hal_dma.c
  ${HAL}/Src/stm32h7rsxx_hal_dma_ex.c
  ${HAL}/Src/stm32h7rsxx_hal_gpio.c
  ${HAL}/Src/stm32h7rsxx_hal_pwr.c
  ${HAL}/Src/stm32h7rsxx_hal_pwr_ex.c
  ${HAL}/Src/stm32h7rsxx_hal_rcc.c
  ${HAL}/Src/stm32h7rsxx_hal_rcc_ex.c
  ${HAL}/Src/stm32h7rsxx_hal_xspi.c)
set_target_properties(crtos_boot PROPERTIES
  OUTPUT_NAME crtos-boot
  SUFFIX .axf
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/kernel)
target_include_directories(crtos_boot PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CRTOS_PLATFORM}/board
  ${CRTOS_PLATFORM}/hal
  ${HAL}/Inc
  ${CRTOS_STM32_CMSIS}
  ${CRTOS_CMSIS_CORE})
target_compile_definitions(crtos_boot PRIVATE STM32H7S7xx USE_HAL_DRIVER)
target_compile_options(crtos_boot PRIVATE ${CRTOS_CPU_FLAGS} -Os -g -Wall -ffunction-sections -fdata-sections
  -fno-common -std=gnu11)
target_link_options(crtos_boot PRIVATE ${CRTOS_CPU_FLAGS} -T ${CMAKE_CURRENT_SOURCE_DIR}/boot.ld -nostartfiles
  -specs=nano.specs -specs=nosys.specs -Wl,--gc-sections -Wl,-Map=${CMAKE_BINARY_DIR}/kernel/crtos-boot.map)
set_target_properties(crtos_boot PROPERTIES LINK_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/boot.ld)
add_custom_command(TARGET crtos_boot POST_BUILD
  COMMAND ${CRTOS_OBJCOPY} -O binary $<TARGET_FILE:crtos_boot> ${CMAKE_BINARY_DIR}/kernel/crtos-boot.bin
  COMMAND ${CRTOS_SIZE} $<TARGET_FILE:crtos_boot>
  COMMENT "Boot loader build/kernel/crtos-boot.bin"
  VERBATIM)
# built with the kernel
add_dependencies(kernel crtos_boot)
