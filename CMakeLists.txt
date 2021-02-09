cmake_minimum_required(VERSION 3.17)
project(ChallongeSoku)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/pkgs)

find_package(SFML REQUIRED)
find_package(TGUI REQUIRED)

include_directories(
	${TGUI_INCLUDE_DIRS}
	${SFML_INCLUDE_DIRS}
)

add_library(
	ChallongeLib
	ChallongeLib/src/Exceptions.hpp
	ChallongeLib/src/Socket.hpp
	ChallongeLib/src/Socket.cpp
	ChallongeLib/src/SecuredSocket.hpp
	ChallongeLib/src/SecuredSocket.cpp
	ChallongeLib/src/Client.cpp
	ChallongeLib/src/Client.hpp
	ChallongeLib/src/Tournament.cpp
	ChallongeLib/src/Tournament.hpp
	ChallongeLib/src/Time.cpp
	ChallongeLib/src/Time.hpp
	ChallongeLib/src/JsonUtils.cpp
	ChallongeLib/src/JsonUtils.hpp
	ChallongeLib/src/Match.cpp
	ChallongeLib/src/Match.hpp
	ChallongeLib/src/Participant.cpp
	ChallongeLib/src/Participant.hpp
)

target_link_libraries(ChallongeLib crypto ssl ws2_32)

add_executable(
	ChallongeSoku
	src/main.cpp
	src/SecuredWebSocket.cpp
	src/SecuredWebSocket.hpp
	src/Utils.cpp
	src/Utils.hpp
)
target_link_libraries(
	ChallongeSoku
	${SFML_GRAPHICS_LIBRARY}
	${SFML_SYSTEM_LIBRARY}
	${SFML_WINDOW_LIBRARY}
	${TGUI_LIBRARIES}
	ChallongeLib
)
target_compile_definitions(ChallongeSoku PRIVATE USERNAME="${USERNAME}" APIKEY="${APIKEY}")
target_include_directories(ChallongeSoku PRIVATE ChallongeLib/src)