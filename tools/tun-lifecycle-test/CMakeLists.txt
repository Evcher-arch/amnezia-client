cmake_minimum_required(VERSION 3.25)
project(TunLifecycleTest LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
find_package(Qt6 REQUIRED COMPONENTS Core Network RemoteObjects Core5Compat)
if(NOT AMNEZIA_BUILD_DIR)
    message(FATAL_ERROR "Set AMNEZIA_BUILD_DIR to an existing client build with generated IPC replicas")
endif()
add_executable(tun-lifecycle-test main.cpp
    "${AMNEZIA_BUILD_DIR}/client/moc_rep_ipc_interface_replica.cpp"
    "${AMNEZIA_BUILD_DIR}/client/moc_rep_ipc_process_interface_replica.cpp")
target_include_directories(tun-lifecycle-test PRIVATE
    "${AMNEZIA_BUILD_DIR}/client" "../../client" "../../client/mozilla/shared" "../../ipc")
target_link_libraries(tun-lifecycle-test PRIVATE Qt6::Core Qt6::Network Qt6::RemoteObjects Qt6::Core5Compat)
