include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/ThirdPartyPaths.cmake)

add_executable(mock_algo_service
  main.cpp
  algo_config.cpp
  algo_input_converter.cpp
  algo_online_service.cpp
  algo_offline_replay.cpp
)
target_include_directories(mock_algo_service PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/adapter_algo_shm/include
  ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/device_abstraction/include
  ${VS_NLOHMANN_INCLUDE}
)
target_sources(mock_algo_service PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../libs/adapter_algo_shm/src/algo_shm_codec.cpp
)
if(MSVC)
  target_compile_options(mock_algo_service PRIVATE /utf-8)
endif()
