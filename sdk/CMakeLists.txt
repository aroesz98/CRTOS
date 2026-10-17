# The SDK: what programs built outside this tree need. It is the toolchain directory
# (build/toolchain/arm-crtos: arm-crtos-gcc, crtos.specs, the libraries and headers) plus the
# CMake support, the program templates and the crtos command. "crtos sdk" builds it and
# installs this component into build/sdk:
#
#   cmake --build build --target sdk_parts
#   cmake --install build --prefix build/sdk --component sdk

add_custom_target(sdk_parts DEPENDS crtos_toolchain)

# bin/ (when "crtos toolchain" built the launchers), lib/, include/
install(DIRECTORY ${CRTOS_SYSROOT}/ DESTINATION . USE_SOURCE_PERMISSIONS COMPONENT sdk)

install(FILES README.md DESTINATION . COMPONENT sdk)
install(PROGRAMS ${CRTOS_TOP}/crtos ${CRTOS_TOP}/crtos.cmd DESTINATION . COMPONENT sdk)
install(FILES crtos-sdk.cmake ${CRTOS_TOP}/cmake/crtos-app.cmake
              ${CRTOS_TOP}/cmake/toolchain-arm-none-eabi.cmake
        DESTINATION cmake COMPONENT sdk)
install(DIRECTORY templates DESTINATION . COMPONENT sdk)

# the crtos command (deploy, run, kmon, crash, desktop, ...), the program check, the fast-code
# list, crash addresses -> lines, icons (PNG -> PAM)
install(FILES ${CRTOS_TOP}/tools/crtos.py ${CRTOS_TOP}/tools/swdcon.py ${CRTOS_TOP}/tools/netdeploy.py
              ${CRTOS_TOP}/tools/vnc.py ${CRTOS_TOP}/tools/modcheck.py ${CRTOS_TOP}/tools/appfast.py
              ${CRTOS_TOP}/tools/appsym.py ${CRTOS_TOP}/tools/icon.py ${CRTOS_TOP}/tools/requirements.txt
        DESTINATION tools COMPONENT sdk)

# the sources of arm-crtos-gcc and crtos-app ("crtos toolchain" builds them when the SDK came
# without them)
install(FILES ${CRTOS_TOP}/toolchain/launcher.c ${CRTOS_TOP}/toolchain/crtos-app.c DESTINATION src COMPONENT sdk)
