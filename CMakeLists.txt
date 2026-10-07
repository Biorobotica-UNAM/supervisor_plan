cmake_minimum_required(VERSION 3.8)
project(supervisor_plan)

if(CMAKE_COMPILER_IS_GNUCXX OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(ament_cmake REQUIRED)
find_package(rclcpp REQUIRED)
find_package(std_msgs REQUIRED)
find_package(std_srvs REQUIRED)
find_package(clips_interfaces REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(Boost REQUIRED)  # solo headers (boost::algorithm)

add_executable(supervisor_plan_node
  src/supervisor_plan_node.cpp
  src/supervisor_plan_core.cpp
)

target_include_directories(supervisor_plan_node PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

ament_target_dependencies(supervisor_plan_node
  rclcpp
  std_msgs
  std_srvs
  clips_interfaces
  nlohmann_json
)

# Boost headers only -> no target_link_libraries necesario
# (si luego metes boost::system, ahí sí linkeas)

install(TARGETS supervisor_plan_node
  DESTINATION lib/${PROJECT_NAME}
)

install(DIRECTORY include/
  DESTINATION include
)

ament_export_include_directories(include)
ament_export_dependencies(rclcpp std_msgs std_srvs)

ament_package()
