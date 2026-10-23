add_library(hftrec_service STATIC src/RecorderServiceServer.cpp)
add_library(hftrec::service ALIAS hftrec_service)
target_compile_features(hftrec_service PUBLIC cxx_std_23)
target_link_libraries(hftrec_service
  PUBLIC hftrec::service_client
  PRIVATE hftrec::core hftrec::reference_contract
    hft_parser::current_reference_client Threads::Threads)
target_include_directories(hftrec_service PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/../Runtime/src")

add_executable(hft-recorderd ../Process/src/Service/RecorderServiceMain.cpp)
target_link_libraries(hft-recorderd PRIVATE hftrec::service)
target_compile_features(hft-recorderd PRIVATE cxx_std_23)
