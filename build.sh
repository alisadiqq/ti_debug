#!/bin/bash

WorkDir="./"
SrcDir="./"
CMAKEDIR="/c/Simple_link_toolchain/cmake-3.26.5-windows-x86_64/bin"
MAKEDIR="/c/Simple_link_toolchain/make"
GCCDIR="/c/Simple_link_toolchain/gcc-arm-none-eabi-10.3-2021.10/bin"
# TIDIR variable needs to be accessible for PATH export
TIDIR="C:/Simple_link_toolchain/ti-cgt-armllvm_4.0.2.LTS/bin"

# --- IMPORTANT ---
# Make sure your TI debug server (e.g., ccs_debugserver.exe) is also in PATH for debugging.
# If UniFlash is used for flashing, its path might also be needed.
export PATH=$PATH:"C:/Simple_link_toolchain/ti_debugserver_bin"

# Export paths for CMake, Make, and TI tools
export PATH=$PATH:$CMAKEDIR:$MAKEDIR:$TIDIR:$GCCDIR

envos="win" # Assuming Windows environment

# --- Fix for CMakeLists.txt handling ---
# The previous diff/cp logic was likely failing because $board was not set or path was wrong.
# If CMakeLists.txt is always in the root, you don't need this block.
# If you need to copy specific CMakeLists.txt files, ensure $board is passed correctly.
# For simplicity, assuming CMakeLists.txt is stable in the root for now.
# If you still want this, pass $board as $2 in your script call.
# Example: ./build.sh -r basic_ble
BOARD_NAME="$2" # Capture the board name if passed as second argument

echo "--- Running build.sh for board: ${BOARD_NAME} ---"

# Original "ostest" line - consider if needed.
# echo.exe "ostest"

if [ "$1" == "-r" ]; then # Build and copy .out
    echo "--- Building project ---"
    # Assuming your CMakeLists.txt now correctly uses a set variable for compiler choice
    # You might need to add -DCOMPILER_CHOICE=TI here if you followed that approach
    cmake -G"MinGW Makefiles" -B "${SrcDir}/build/Release" -S "${SrcDir}" -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=make # Add -DCOMPILER_CHOICE=TI if your CMakeLists.txt uses it
    
    if [ $? -ne 0 ]; then
        echo "CMake configure failed!"
        exit 1
    fi

    cmake --build "${SrcDir}/build/Release" -j8
    if [ $? -ne 0 ]; then
        echo "Build failed!"
        exit 1
    fi

    # Assuming the project name for the .out is 'basic_ble' from your CMakeLists.txt
    # And that 'debugging_img.out' is the desired name for the debugger
    if [ -f "${SrcDir}/build/Release/basic_ble.out" ]; then
        cp "${SrcDir}/build/Release/basic_ble.out" "${SrcDir}/build/Release/debugging_img.out"
        echo "Copied basic_ble.out to debugging_img.out"
    else
        echo "Warning: basic_ble.out not found at ${SrcDir}/build/Release/basic_ble.out"
    fi

elif [ "$1" == "-c" ]; then # Clean
    echo "--- Cleaning build directories ---"
    rm -fr "${WorkDir}/build"
    echo "Clean complete."

else
    echo "Usage: ./build.sh -r <board_name> (to build)"
    echo "       ./build.sh -c (to clean)"
fi