cmake_minimum_required(VERSION 3.5)
project(flex_sync)

add_definitions(-std=c++17 -g -Wall)

find_package(catkin REQUIRED COMPONENTS roscpp)

catkin_python_setup()

catkin_package(
INCLUDE_DIRS include
CATKIN_DEPENDS roscpp)

include_directories(include
  ${catkin_INCLUDE_DIRS})

install(DIRECTORY include/${PROJECT_NAME}/
        DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION})
