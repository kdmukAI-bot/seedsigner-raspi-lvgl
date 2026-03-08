set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(CMAKE_C_COMPILER arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER arm-linux-gnueabihf-g++)
set(CMAKE_AR arm-linux-gnueabihf-ar)
set(CMAKE_RANLIB arm-linux-gnueabihf-ranlib)
set(CMAKE_STRIP arm-linux-gnueabihf-strip)

# Raspberry Pi Zero / Zero W baseline
# Force ARM mode to avoid Thumb-1 + hard-float ABI incompatibilities.
set(ARMV6_FLAGS "-march=armv6zk -mtune=arm1176jzf-s -marm -mfpu=vfp -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT "${ARMV6_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${ARMV6_FLAGS}")

# Avoid trying to run target binaries during configure checks.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
