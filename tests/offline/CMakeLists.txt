find_package(Threads REQUIRED)
cxet_add_offline_suite(hftrec_offline_recording_groups OWNER hft-recorder EXPECTED 8
  SOURCES Recordings/RecordingGroupsCases.cpp LIBRARIES hftrec::recording_groups hftrec::corpus_contract
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Runtime/src" LABELS recordings capture)
cxet_add_offline_suite(hftrec_offline_service OWNER hft-recorder EXPECTED 4
  SOURCES Service/RecorderServiceCases.cpp
  LIBRARIES hftrec::service_client hftrec::reference_contract
  LABELS capture service)
cxet_add_offline_suite(hftrec_offline_corpus OWNER hft-recorder EXPECTED 33
  SOURCES Corpus/CorpusCases.cpp LIBRARIES hftrec::corpus_contract
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Runtime/src" LABELS corpus)
cxet_add_offline_suite(hftrec_offline_capture_writer OWNER hft-recorder EXPECTED 2
  SOURCES Capture/CaptureWriterCases.cpp LIBRARIES hftrec::corpus_contract
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Runtime/src" LABELS capture)
cxet_add_offline_suite(hftrec_offline_capture_conversion OWNER hft-recorder EXPECTED 13
  SOURCES ParserClient/ParserCaptureConversionCases.cpp LIBRARIES hftrec::capture_client
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Runtime/src" LABELS capture)
cxet_add_offline_suite(hftrec_offline_capture_client_loss OWNER hft-recorder EXPECTED 3
  SOURCES ParserClient/ParserCaptureClientLossCases.cpp LIBRARIES hftrec::capture_client Threads::Threads
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Runtime/src" LABELS capture)
