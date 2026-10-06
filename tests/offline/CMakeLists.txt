find_package(Threads REQUIRED)
cxet_add_offline_suite(hftrec_offline_corpus OWNER hft-recorder EXPECTED 31
  SOURCES CorpusCases.cpp LIBRARIES hftrec::corpus_contract
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Runtime/src" LABELS corpus)
cxet_add_offline_suite(hftrec_offline_capture_writer OWNER hft-recorder EXPECTED 2
  SOURCES CaptureWriterCases.cpp LIBRARIES hftrec::corpus_contract
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Runtime/src" LABELS capture)
cxet_add_offline_suite(hftrec_offline_capture_selection OWNER hft-recorder EXPECTED 3
  SOURCES RecorderCaptureSelectionCases.cpp LIBRARIES hftrec::capture_launch
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Runtime/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Contracts/include" LABELS capture)
cxet_add_offline_suite(hftrec_offline_capture_conversion OWNER hft-recorder EXPECTED 7
  SOURCES ParserCaptureConversionCases.cpp LIBRARIES hftrec::capture_client
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Runtime/src" LABELS capture)
cxet_add_offline_suite(hftrec_offline_capture_session OWNER hft-recorder EXPECTED 3
  SOURCES RecorderCaptureSessionCases.cpp LIBRARIES hftrec::capture_launch
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Runtime/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Contracts/include" LABELS capture)
cxet_add_offline_suite(hftrec_offline_capture_client_loss OWNER hft-recorder EXPECTED 3
  SOURCES ParserCaptureClientLossCases.cpp LIBRARIES hftrec::capture_client Threads::Threads
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Runtime/src" LABELS capture)
cxet_add_offline_suite(hftrec_offline_owned_parser OWNER hft-recorder EXPECTED 3
  SOURCES RecorderOwnedParserCases.cpp LIBRARIES Threads::Threads
  INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/../../src/Runtime/src" LABELS capture process)
