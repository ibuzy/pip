file(GLOB THEME_FONTS ${CMAKE_CURRENT_LIST_DIR}/../../ui/fonts/font_*_*.c)
idf_component_register(SRCS main.c ble.c debug.c ../../ui/face.c ../../ui/imu.c ../../ui/pip.c ../../ui/pet.c ../../ui/shell.c ../../ui/apps.c ../../ui/voice.c
                            ../../ui/theme.c ../../ui/phone.c ../../ui/mic.c ../../ui/maze.c ../../ui/grime.c ../../ui/sprite.c ../../ui/cosmos.c ../../ui/sprite_art.c ${THEME_FONTS}
                       INCLUDE_DIRS ../../ui)
# the face renderer: its per-pixel loop in IRAM (FACE_HOT), its tables in DRAM (FACE_DATA); no errno from the little libm it still calls
set_source_files_properties(../../ui/face.c PROPERTIES COMPILE_OPTIONS "-fno-math-errno;-O2"
                            COMPILE_DEFINITIONS "FACE_HOT=__attribute__((section(\".iram1.face\")));FACE_DATA=__attribute__((section(\".dram1.facedata\")))")
set_source_files_properties(../../ui/cosmos.c PROPERTIES COMPILE_OPTIONS "-fno-math-errno;-O2") # his birth's sky: full frames
if(DEFINED ENV{PIP_FACE_PROFILE}) # a profiling build (tools/bench.py --prof): cycles per pixel path, in `perf`
    set_property(SOURCE ../../ui/face.c APPEND PROPERTY COMPILE_DEFINITIONS "FACE_PROFILE")
endif()
