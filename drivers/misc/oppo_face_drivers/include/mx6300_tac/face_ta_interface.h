
#ifndef INCLUSION_GUARD_FACE_TA_INTERFACE
#define INCLUSION_GUARD_FACE_TA_INTERFACE

//#include <stdint.h>

//max size for t-base
#define MAX_CHUNK ((1024*1024) - sizeof(face_ta_byte_array_msg_t))


typedef struct {
    int32_t target;
    int32_t command;
} face_ta_cmd_header_t;

typedef struct {
    face_ta_cmd_header_t header;
    int32_t response;
} face_ta_simple_command_t;

typedef struct {
    face_ta_cmd_header_t header;
    int32_t response;
    uint32_t size;
    uint8_t array[];
} face_ta_byte_array_msg_t;

typedef struct {
    face_ta_cmd_header_t header;
    int32_t response;
    uint32_t size;
} face_ta_size_msg_t;

#endif // INCLUSION_GUARD_FACE_TA_INTERFACE
