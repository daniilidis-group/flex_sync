cmake_minimum_required(VERSION 2.8)
project(flex_sync)

add_definitions(-std=c++17 -g -Wall)

find_package(catkin REQUIRED COMPONENTS roscpp std_msgs
message_generation message_filters rosbag)

add_message_files(DIRECTORY msg FILES TestMsg1.msg TestMsg2.msg TestMsg3.msg)
catkin_python_setup()

generate_messages(DEPENDENCIES std_msgs)

catkin_package(
INCLUDE_DIRS include
CATKIN_DEPENDS message_runtime roscpp std_msgs message_filters rosbag)


include_directories(include
  ${catkin_INCLUDE_DIRS})

#add_executable(${PROJECT_NAME}_test src/${PROJECT_NAME}_test.cpp)
#add_dependencies(${PROJECT_NAME}_test ${${PROJECT_NAME}_EXPORTED_TARGETS} ${catkin_EXPORTED_TARGETS})
#target_link_libraries(${PROJECT_NAME}_test ${catkin_LIBRARIES})

add_executable(approx_flex_sync_test src/approx_flex_sync_test.cpp)
add_dependencies(approx_flex_sync_test ${${PROJECT_NAME}_EXPORTED_TARGETS} ${catkin_EXPORTED_TARGETS})
target_link_libraries(approx_flex_sync_test ${catkin_LIBRARIES})

add_executable(message_filter_approx_test src/message_filter_approx_test.cpp)
add_dependencies(message_filter_approx_test ${${PROJECT_NAME}_EXPORTED_TARGETS} ${catkin_EXPORTED_TARGETS})
target_link_libraries(message_filter_approx_test ${catkin_LIBRARIES})

install(DIRECTORY include/${PROJECT_NAME}/
        DESTINATION ${CATKIN_PACKAGE_INCLUDE_DESTINATION})
