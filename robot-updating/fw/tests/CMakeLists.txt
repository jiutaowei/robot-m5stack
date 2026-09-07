cmake_minimum_required(VERSION 3.16)

project(stackchan_firmware_tests LANGUAGES CXX)

enable_testing()

add_executable(motion_math_test
    motion_math_test.cpp
    ../main/stackchan/motion/motion_math.cpp
)

target_include_directories(motion_math_test PRIVATE
    ../main
)

target_compile_features(motion_math_test PRIVATE cxx_std_17)

add_test(NAME motion_math_test COMMAND $<TARGET_FILE:motion_math_test>)

add_executable(test_meeting
    test_meeting.cpp
    ../main/apps/app_meeting/meeting_manifest.cpp
    ../main/apps/app_meeting/meeting_protocol.cpp
    ../main/apps/app_meeting/meeting_recorder.cpp
    ../main/apps/app_meeting/meeting_ui_model.cpp
    ../main/apps/app_meeting/meeting_uploader.cpp
    ../main/apps/app_meeting/segment_store.cpp
    ../main/apps/app_meeting/wav_writer.cpp
)

target_include_directories(test_meeting PRIVATE
    ../main
)

target_compile_features(test_meeting PRIVATE cxx_std_17)

add_test(NAME test_meeting COMMAND $<TARGET_FILE:test_meeting>)
