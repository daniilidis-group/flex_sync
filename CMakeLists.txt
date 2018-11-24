cmake_minimum_required(VERSION 2.8)
project(flex_sync)

find_package(catkin REQUIRED COMPONENTS roscpp)

catkin_package(
INCLUDE_DIRS include
CATKIN_DEPENDS roscpp)


include_directories(include
  ${catkin_INCLUDE_DIRS})


install(DIRECTORY include/${PROJECT_NAME}/
        DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION})
