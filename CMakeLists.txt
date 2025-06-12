cmake_minimum_required(VERSION 3.15)
set(CMAKE_SYSTEM_NAME Generic)
project(basic_ble)

set(COMPILER_CHOICE "GCC") # <--- CHANGE THIS LINE TO "GCC" or "TI"

# --- TI Configuration ---
if ("${COMPILER_CHOICE}" STREQUAL "TI")
    message(STATUS "Selected Compiler type: ${COMPILER_CHOICE}")
	# IMPORTANT: Replace these paths with the actual paths to your TI ARM compiler
	set(CMAKE_C_COMPILER   C:/Simple_link_toolchain/ti-cgt-armllvm_4.0.2.LTS/bin/tiarmclang.exe)
	set(CMAKE_CXX_COMPILER C:/Simple_link_toolchain/ti-cgt-armllvm_4.0.2.LTS/bin/tiarmclang.exe)
	set(CMAKE_ASM_COMPILER C:/Simple_link_toolchain/ti-cgt-armllvm_4.0.2.LTS/bin/tiarmclang.exe)

	# --- Define the path to your local libraries folder ---
	set(LOCAL_LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source/libs/TI")
# --- TI Configuration ---

elseif ("${COMPILER_CHOICE}" STREQUAL "GCC")
	message(STATUS "Selected Compiler type: ${COMPILER_CHOICE}")
	# --- GCC Configuration ---
	set(CMAKE_C_COMPILER    arm-none-eabi-gcc)
	set(CMAKE_CXX_COMPILER  arm-none-eabi-g++)
	set(AS                  arm-none-eabi-as)
	set(AR                  arm-none-eabi-ar)
	set(OBJCOPY             arm-none-eabi-objcopy)
	set(OBJDUMP             arm-none-eabi-objdump)
	set(SIZE                arm-none-eabi-size)
	
	# --- Define the path to your local libraries folder ---
	set(LOCAL_LIBS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/source/libs/GCC")
	# --- GCC Configuration ---	

else()
    message(FATAL_ERROR "Unsupported C compiler ID: ${CMAKE_C_COMPILER_ID}. Please configure your toolchain.")
endif()


# Needed otherwise the CMake compiler checks will complain and abort
set(CMAKE_C_COMPILER_ID_RUN TRUE CACHE PATH "" FORCE)
set(CMAKE_C_COMPILER_FORCED TRUE CACHE PATH "" FORCE)
set(CMAKE_C_COMPILER_WORKS TRUE CACHE PATH "" FORCE)

set(CMAKE_CXX_COMPILER_ID_RUN TRUE CACHE PATH "" FORCE)
set(CMAKE_CXX_COMPILER_FORCED TRUE CACHE PATH "" FORCE)
set(CMAKE_CXX_COMPILER_WORKS TRUE CACHE PATH "" FORCE)

# Ensure that cmake doesn't try to run ranlib on archives from TICLANG
# This can avoid a situation where the host's ranlib is invoked on ARM compiled binaries
set(CMAKE_C_ARCHIVE_FINISH "")
set(CMAKE_CXX_ARCHIVE_FINISH "")
#------------------ END ------------------

add_compile_definitions( 
DeviceFamily_CC2340R53
ICALL_NO_APP_EVENTS
CC23X0
NVOCMP_NWSAMEITEM=1
FLASH_ONLY_BUILD
NVOCMP_NVPAGES=6
FREERTOS
NVOCMP_POSIX_MUTEX
NDEBUG

PERIPHERAL_CFG=4
HOST_CONFIG=PERIPHERAL_CFG
HCI_TL_NONE
GAP_BOND_MGR
SYSCFG
MAX_NUM_BLE_CONNS=1
GATT_MAX_PREPARE_WRITES=5
EXTENDED_STACK_SETTINGS=EXTENDED_STACK_SETTINGS_DEFAULT
GAP_BONDINGS_MAX=5
POWER_SAVING
STACK_LIBRARY
ONE_BLE_LIB_SIZE_OPTIMIZATION

ICALL_EVENTS
ICALL_JT
ICALL_LITE
ICALL_STACK0_ADDR
USE_ICALL
ICALL_MAX_NUM_ENTITIES=6
ICALL_MAX_NUM_TASKS=3

OSAL_CBTIMER_NUM_TASKS=1
NPI_USE_UART
)


set(PROJECT_SOURCES_GCC
	# Removed from sources as it was causing clash in resetISR and Localstartup symbols
	source/freertos/startup/startup_cc23x0r5_gcc.c
	#source/ti/devices/cc23x0r5/startup_files/startup_gcc.c
)

set(PROJECT_SOURCES_TI
	# Removed from sources as it was causing clash in resetISR and Localstartup symbols
	source/freertos/startup/startup_cc23x0r5_ticlang.c
)


# --- Project Sources ---
set(PROJECT_SOURCES_COMMON
    empty.c 
	app/Profiles/app_dev_info.c
	app/Profiles/app_simple_gatt.c
	app/app_broadcaster.c
	app/app_central.c
	app/app_connection.c
	app/app_data.c
	app/app_l2cap_coc.c
	app/app_main.c
	app/app_menu.c
	app/app_observer.c
	app/app_pairing.c
	app/app_peripheral.c
	
	source/syscfg/ti_ble_config.c
	source/syscfg/ti_radio_config.c
	source/syscfg/ti_devices_config.c 
	source/syscfg/ti_drivers_config.c
	source/syscfg/ti_freertos_portable_config.c
	"${CMAKE_CURRENT_SOURCE_DIR}/source/syscfg/ti_freertos_config.c"
	
	source/ble5stack/basic_ble/common/Startup/main_freertos.c
	source/ble5stack/basic_ble/common/Startup/osal_icall_ble.c
    
	source/ble5stack/basic_ble/ble_app_util/src/bleapputil_init.c
	source/ble5stack/basic_ble/ble_app_util/src/bleapputil_process.c
	source/ble5stack/basic_ble/ble_app_util/src/bleapputil_stack_callbacks.c
	source/ble5stack/basic_ble/ble_app_util/src/bleapputil_task.c
	
	source/ble5stack/basic_ble/menu_module/menu_module.c	
	source/ble5stack/basic_ble/profiles/simple_gatt/simple_gatt_profile.c
	source/ble5stack/basic_ble/services/dev_info/dev_info_service.c
	
	source/ti/common/nv/crc.c
	source/ti/common/nv/nvocmp.c
	
	source/ti/ble5stack_flash/icall/src/icall_cc23x0.c
	source/ti/ble5stack_flash/icall/src/icall_POSIX.c
	source/ti/ble5stack_flash/icall/src/icall_user_config.c	

	source/ti/ble5stack_flash/icall/app/ble_stack_api.c
	source/ti/ble5stack_flash/icall/stack/ble_user_config_stack.c	
	source/ti/ble5stack_flash/icall/app/ble_user_config.c
	source/ti/ble5stack_flash/icall/app/icall_api_lite.c
	source/ti/ble5stack_flash/rom/agama_r1/rom_init.c
	
	# Added these sources after undefined reference error during linking
	source/ti/display/Display.c
	source/ti/drivers/apps/Button.c
	
	source/ti/drivers/AESCCM.c
	source/ti/drivers/AESECB.c
	source/ti/drivers/rcl/RCL.c
	source/ti/drivers/rcl/RCL_Buffer.c
	source/ti/drivers/rcl/handlers/ble5.c
	source/ti/drivers/rcl/wrappers/RCL_AdcNoise.c
	source/ti/ble5stack_flash/osal/src/common/osal_list.c
	source/ti/ble5stack_flash/host/sm_ecc.c
	source/ti/ble5stack_flash/host/gap.c
	
	source/ti/drivers/ECDH.c
	source/ti/display/DisplayUart2.c
	
	#source/ti/devices/cc23x0r5/startup_files/ccfg.c
)

if ("${COMPILER_CHOICE}" STREQUAL "TI")
	include_directories(
		source/ti/posix/ticlang
		source/ti/posix/ticlang/sys
		source/freertos/startup/startup_cc23x0r5_ticlang
	)

elseif ("${COMPILER_CHOICE}" STREQUAL "GCC")
	include_directories(
		source/ti/posix/gcc
		source/ti/posix/gcc/sys
	)
endif()

# --- Include Directories ---
include_directories(
	app
	Release
	source/syscfg
	
	source/ti/common
	source/ble5stack/basic_ble/menu_module
	source/ble5stack/basic_ble/profiles/simple_gatt
	source/ble5stack/basic_ble/services/dev_info
	
	source
	source/ti
	source/ti/display
	source/ti/ble5stack_flash
	source/ti/ble5stack_flash/controller/cc26xx/inc
	source/ti/ble5stack_flash/inc
	source/ti/ble5stack_flash/rom
	source/ti/ble5stack_flash/common/cc26xx
	source/ti/ble5stack_flash/icall/inc
	source/ti/ble5stack_flash/icall/src
	source/ti/ble5stack_flash/icall/src/inc
	source/ti/ble5stack_flash/hal/src/target/_common
	source/ti/ble5stack_flash/common/cc26xx/npi/stack
	source/ti/ble5stack_flash/hal/src/inc
	source/ti/ble5stack_flash/heapmgr
	source/ti/ble5stack_flash/profiles/dev_info
	source/ti/ble5stack_flash/profiles/simple_profile

	source/ti/ble5stack_flash/npi/src
	source/ti/ble5stack_flash/osal/src/inc
	source/ti/ble5stack_flash/services/src/saddr
	source/ti/ble5stack_flash/services/src/sdata
	source/ti/common/nv 
	source/ti/common/cc26xx
	source/ti/ble5stack_flash/icall/src
	source/ti/drivers/rcl
	source/ti/drivers/cryptoutils/
	source/ti/drivers/cryptoutils/ecc
	
	source/third_party/freertos/portable/GCC/ARM_CM0
	
	source/ti/posix/freertos
	source/third_party/freertos/include
	source/freertos
	source/ti/bleapp/profiles/health_thermometer
	source/ti/bleapp/services/health_thermometer
	
)
# --- TI Configuration ---
if ("${COMPILER_CHOICE}" STREQUAL "TI")
	##### Kent changes #####
	
	# --- Compiler Flags ---	
	#set(MCPU_FLAGS "-mthumb -mcpu=cortex-m0plus -march=thumbv6m -mlittle-endian")
	#set(VFP_FLAGS "-mfloat-abi=soft")
	#set(SPEC_FLAGS "")
	#set(WARN_IGN_FLAGS "-Wno-int-conversion -Wno-incompatible-pointer-types -Wno-implicit-function-declaration")
	#set(CFLAG_EXT1 "@${CMAKE_SOURCE_DIR}/Release/syscfg/ti_utils_build_compiler.opt @${CMAKE_SOURCE_DIR}/linker/TI/build_components.opt @${CMAKE_SOURCE_DIR}/linker/TI/factory_config.opt")
	
	#set(CMAKE_C_FLAGS   "${MCPU_FLAGS} ${VFP_FLAGS} ${CFLAG_EXT1} -std=gnu99 -std=c99 -gdwarf-3 -ffunction-sections" CACHE INTERNAL "c compiler flags")
	#set(CMAKE_CXX_FLAGS "${MCPU_FLAGS} ${VFP_FLAGS} ${CFLAG_EXT1} -std=gnu99 -std=c99 -gdwarf-3 -ffunction-sections" CACHE INTERNAL "cxx compiler flags")
	#set(CMAKE_ASM_FLAGS "${MCPU_FLAGS} -x assembler-with-cpp" CACHE INTERNAL "asm compiler flags")
	
	#SET(CMAKE_C_FLAGS_DEBUG "-O0 -g -ggdb3" CACHE INTERNAL "c debug compiler flags")
	#SET(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -ggdb3" CACHE INTERNAL "cxx debug compiler flags")
	#SET(CMAKE_ASM_FLAGS_DEBUG "-g -ggdb3" CACHE INTERNAL "asm debug compiler flags")

	#SET(CMAKE_C_FLAGS_RELEASE "-O3 -g -ggdb3" CACHE INTERNAL "c release compiler flags")
	#SET(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -ggdb3" CACHE INTERNAL "cxx release compiler flags")
	#SET(CMAKE_ASM_FLAGS_RELEASE "" CACHE INTERNAL "asm release compiler flags")
	
	# --- Linker Flags ---
	#set(LD_FLAGS_1   "-Wl,-u,_c_int00 -Wl,--diag_wrap=off -Wl,--display_error_number -Wl,-x -Wl,-c ")
	#set(LD_FLAGS_2   "-L${CMAKE_SOURCE_DIR}/Release/syscfg ${CMAKE_SOURCE_DIR}/Release/syscfg/ti_utils_build_linker.cmd.genlibs ${CMAKE_CURRENT_SOURCE_DIR}/linker/TI/lpf3_app_freertos.cmd")
	#set(LD_FLAGS_3   "-Wl,-m,bin/${CMAKE_PROJECT_NAME}.map -Wl,--rom_model -Wl,--warn_sections")

	#set(LD_FLAGS     "${LD_FLAGS_1} ${LD_FLAGS_2} ${LD_FLAGS_3}" CACHE INTERNAL "linker flags")
	
	#set(CMAKE_EXE_LINKER_FLAGS "-v ${MCPU_FLAGS} ${VFP_FLAGS} ${LD_FLAGS} " CACHE INTERNAL "exe link flags")

	##### Kent changes #####
	
	# --- Compiler Flags ---
	set(CMAKE_C_FLAGS "-march=thumbv6m -mcpu=cortex-m0plus -mfloat-abi=soft -mlittle-endian -mthumb -Oz -DICALL_NO_APP_EVENTS -DCC23X0 -DNVOCMP_NWSAMEITEM=1 -DFLASH_ONLY_BUILD -DNVOCMP_NVPAGES=6 -DFREERTOS -DNVOCMP_POSIX_MUTEX -gdwarf-3 -ffunction-sections")
	set(CMAKE_ASM_FLAGS "-mcpu=cortex-m0plus -mthumb")

	# --- Linker Flags ---
	set(CMAKE_EXE_LINKER_FLAGS "-Wl,-map=${PROJECT_NAME}.map")
	link_directories("linker_scripts") # Assuming you have a linker script directory

	# --- Linker Script ---
	set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/linker/TI/lpf3_app_freertos.lds")
	
	add_executable(${PROJECT_NAME} ${PROJECT_SOURCES_COMMON} ${PROJECT_SOURCES_TI} ) 
	
	# TI libraries - For TIClang
	target_link_libraries(${PROJECT_NAME}
						"${LOCAL_LIBS_DIR}/drivers_cc23x0r5.a"
						"${LOCAL_LIBS_DIR}/rcl_cc23x0r5.a"
						"${LOCAL_LIBS_DIR}/driverlib.a"
						"${LOCAL_LIBS_DIR}/ecc.a"
						"${LOCAL_LIBS_DIR}/lrf_cc23x0r5.a"
						"${LOCAL_LIBS_DIR}/StackWrapper.a"
						"${LOCAL_LIBS_DIR}/OneLib.a")
						
	set_target_properties(${PROJECT_NAME} PROPERTIES 
		OUTPUT_NAME "${PROJECT_NAME}"
		SUFFIX ".out"  # TI compiler typically generates .out files
	)

	# In your target_link_options, use:
	target_link_options(basic_ble PRIVATE
		"SHELL:-T ${LINKER_SCRIPT}"
	)


	# --- Post-Build Step (Generate HEX File using TI's armhex) ---
	# You might need to adjust the path to armhex based on your CCS installation
	find_program(ARMHEX tiarmhex
		HINTS "C:/Simple_link_toolchain/ti-cgt-armllvm_4.0.2.LTS/bin"
	   PATH_SUFFIXES bin
	)

	#--- Post-Build Step (Optional: Generate Binary File using TI's armobjcopy) ---
	find_program(ARMOBJCOPY tiarmobjcopy
		 HINTS "C:/Simple_link_toolchain/ti-cgt-armllvm_4.0.2.LTS/bin"
		 PATH_SUFFIXES bin
	 )

	# Generate HEX file (simplified command)
	if(ARMHEX)
    add_custom_command(
        TARGET ${PROJECT_NAME}
        POST_BUILD
        # Input should be .out
        COMMAND ${ARMHEX} "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.out" --outfile="${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex"
        #COMMENT "Generating Intel HEX file"
    )
	endif()

# Generate binary file
	if(ARMOBJCOPY)
		add_custom_command(
			TARGET ${PROJECT_NAME}
			POST_BUILD
			# Input should be .out
			COMMAND ${ARMOBJCOPY} -O binary "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.out" "${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin"
			COMMENT "Generating binary file"
		)
	endif()
# --- TI Configuration ---

endif()


# --- GCC Configuration ---
if ("${COMPILER_CHOICE}" STREQUAL "GCC")
	set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/linker/gcc/lpf3_cc23xx_freertos.lds")
	set(LD_FLAGS -L "${CMAKE_CURRENT_SOURCE_DIR}/linker/gcc"
				  -Wl,-Map=${CMAKE_PROJECT_NAME}.map
				  -mcpu=cortex-m0plus -mthumb -nostartfiles -static
				  -Wl,--gc-sections
				  -Wl,-T${LINKER_SCRIPT} 
				   -Wl,-e,resetISR
				   -Wl,--start-group -lc -lm --specs=nano.specs -Wl,--end-group)

	set(MCPU_FLAGS "-mthumb -mcpu=cortex-m0plus -march=armv6-m -std=c99 -gstrict-dwarf")
	set(VFP_FLAGS "-mfloat-abi=soft")
	set(SPEC_FLAGS "-specs=nosys.specs -static")

	set(CMAKE_C_FLAGS   "${MCPU_FLAGS} ${VFP_FLAGS} ${SPEC_FLAGS} -fdata-sections -ffunction-sections -mlong-calls" CACHE INTERNAL "c compiler flags")
	set(CMAKE_CXX_FLAGS "${MCPU_FLAGS} ${VFP_FLAGS} -fdata-sections -ffunction-sections -fno-rtti -fno-exceptions -mlong-calls" CACHE INTERNAL "cxx compiler flags")
	set(CMAKE_ASM_FLAGS "${MCPU_FLAGS} -x assembler-with-cpp" CACHE INTERNAL "asm compiler flags")
	set(CMAKE_EXE_LINKER_FLAGS "${MCPU_FLAGS} ${LD_FLAGS} -Wl,--gc-sections" CACHE INTERNAL "exe link flags")

	SET(CMAKE_C_FLAGS_DEBUG "-O0 -g -ggdb3" CACHE INTERNAL "c debug compiler flags")
	SET(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -ggdb3" CACHE INTERNAL "cxx debug compiler flags")
	SET(CMAKE_ASM_FLAGS_DEBUG "-g -ggdb3" CACHE INTERNAL "asm debug compiler flags")

	SET(CMAKE_C_FLAGS_RELEASE "-O3 -g -ggdb3" CACHE INTERNAL "c release compiler flags")
	SET(CMAKE_CXX_FLAGS_RELEASE "-O3 -g -ggdb3" CACHE INTERNAL "cxx release compiler flags")
	SET(CMAKE_ASM_FLAGS_RELEASE "" CACHE INTERNAL "asm release compiler flags")
	 #--- GCC Configuration ---

	#-------- Resolving genmap file error ----------

	 #[1] FIRST Create GCC-Compatible Genmap (BEFORE the executable)
	set(GCC_GENMAP "${CMAKE_BINARY_DIR}/gcc_genmap.ld")
	file(WRITE ${GCC_GENMAP} "
		/* Converted for GCC linker */
	ti_utils_build_GenMap_sym_CRC_CCFG_BOOT_CFG_begin = 0x4e020000;
	ti_utils_build_GenMap_sym_CRC_CCFG_BOOT_CFG_end = 0x4e02000b;
	ti_utils_build_GenMap_sym_CRC_CCFG_begin = 0x4e020010;
	ti_utils_build_GenMap_sym_CRC_CCFG_end = 0x4e02074b;
	ti_utils_build_GenMap_sym_CRC_CCFG_DEBUG_begin = 0x4e0207d0;
	ti_utils_build_GenMap_sym_CRC_CCFG_DEBUG_end = 0x4e0207fb;
	ti_utils_build_GenMap_sym_CRC_CCFG_USER_RECORD_begin = 0x4e020750;
	ti_utils_build_GenMap_sym_CRC_CCFG_USER_RECORD_end = 0x4e0207cb;

	ti_utils_build_GenMap_NVS_CONFIG_NVSINTERNAL_BASE = 0x7c000;
	ti_utils_build_GenMap_NVS_CONFIG_NVSINTERNAL_SIZE = 0x4000;

	ti_utils_build_GenMap_FLASH0_SIZE = 0x80000;
	ti_utils_build_GenMap_RAM0_SIZE = 0x10000;
	ti_utils_build_GenMap_S2RRAM_BASE = 0x40098000;
	ti_utils_build_GenMap_S2RRAM_SIZE = 0x1000;
	ti_utils_build_GenMap_CCFG_BASE = 0x4e020000;
	ti_utils_build_GenMap_CCFG_SIZE = 0x800;
	ti_utils_build_GenMap_FLASH0_BASE = 0x0;
	ti_utils_build_GenMap_RAM0_BASE = 0x20000000;
	")

	# [2] THEN Create Executable
	add_executable(${PROJECT_NAME} ${PROJECT_SOURCES_COMMON} ${PROJECT_SOURCES_GCC})
	set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME "${PROJECT_NAME}")
	
	#### Kent changes ####
	#set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
	#find_library(STAND_C_LIB libc.a)
	#### Kent changes ####

	# TI libraries
	target_link_libraries(${PROJECT_NAME} PRIVATE 
						"${LOCAL_LIBS_DIR}/drivers_cc23x0r5.a"
						"${LOCAL_LIBS_DIR}/rcl_cc23x0r5.a"
						"${LOCAL_LIBS_DIR}/driverlib.a"
						"${LOCAL_LIBS_DIR}/ecc.a"
						"${LOCAL_LIBS_DIR}/lrf_cc23x0r5.a"
						"${LOCAL_LIBS_DIR}/StackWrapper.a"
						"${LOCAL_LIBS_DIR}/OneLib.a"
						#"${STAND_C_LIB}"
						-Wl,--unresolved-symbols=ignore-in-shared-libs)


	# [3] Add Dependency (AFTER executable)
	add_custom_target(genmap_conversion ALL DEPENDS ${GCC_GENMAP})
	add_dependencies(${PROJECT_NAME} genmap_conversion)

	# [4] FINALLY Set Link Options (AFTER dependency)
	target_link_options(${PROJECT_NAME} PRIVATE
		"SHELL:-T ${LINKER_SCRIPT}"
		"SHELL:-T ${GCC_GENMAP}"
	)
	#-------- Resolving genmap file error ----------


	target_include_directories(${PROJECT_NAME} PRIVATE "source/syscfg")
	 #In your target_link_options, use:
	target_link_options(basic_ble PRIVATE
	#   "SHELL:-T ${LINKER_SCRIPT}"
	 #  "SHELL:-T ${GCC_GENMAP}"  # Use the converted file
	)
endif()

# --- Debugging Configuration (Example - Adapt for your debugger) ---
# You might need to create a separate launch.json in VS Code
# and configure it to use a GDB server that connects to your target.
# This CMakeLists.txt doesn't directly handle the debugging setup in VS Code.
