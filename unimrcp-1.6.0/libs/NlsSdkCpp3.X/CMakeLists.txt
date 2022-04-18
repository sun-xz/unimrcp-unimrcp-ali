cmake_minimum_required(VERSION 3.0)
project(speech-demo)

add_definitions(-D_GLIBCXX_USE_CXX11_ABI=0)

include_directories(${CMAKE_SOURCE_DIR}/include)
include_directories(${CMAKE_SOURCE_DIR}/demo/include)

link_directories(${CMAKE_SOURCE_DIR}/lib)
link_directories(${CMAKE_SOURCE_DIR}/demo/lib)

add_executable(srDemo demo/speechRecognizerDemo.cpp)
target_link_libraries(srDemo alibabacloud-idst-common alibabacloud-idst-speech pthread)

add_executable(syDemo demo/speechSynthesizerDemo.cpp)
target_link_libraries(syDemo alibabacloud-idst-common alibabacloud-idst-speech pthread)

add_executable(syLongDemo demo/speechLongSynthesizerDemo.cpp)
target_link_libraries(syLongDemo alibabacloud-idst-common alibabacloud-idst-speech pthread)

add_executable(stDemo demo/speechTranscriberDemo.cpp)
target_link_libraries(stDemo alibabacloud-idst-common alibabacloud-idst-speech pthread)

SET(EXECUTABLE_OUTPUT_PATH ${CMAKE_SOURCE_DIR}/demo)