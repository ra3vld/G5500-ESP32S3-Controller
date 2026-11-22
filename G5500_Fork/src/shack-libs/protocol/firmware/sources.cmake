set(SHACK_FIRMWARE_DIR "${CMAKE_CURRENT_LIST_DIR}")
set(SHACK_WIRE_SOURCES
    "${SHACK_FIRMWARE_DIR}/shack_wire.cpp"
    "${SHACK_FIRMWARE_DIR}/wire.pb.c"
    "${SHACK_FIRMWARE_DIR}/nanopb/pb_common.c"
    "${SHACK_FIRMWARE_DIR}/nanopb/pb_decode.c"
    "${SHACK_FIRMWARE_DIR}/nanopb/pb_encode.c")
