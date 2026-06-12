#pragma once
#include <cstdint>

namespace engine::qc {

constexpr int PROG_VERSION = 6;

#pragma pack(push, 1)

// The main header of progs.dat
struct dprograms_t {
    int32_t version;
    int32_t crc;           // Checksum to match the map

    int32_t ofs_statements;
    int32_t numstatements;

    int32_t ofs_globaldefs;
    int32_t numglobaldefs;

    int32_t ofs_fielddefs;
    int32_t numfielddefs;

    int32_t ofs_functions;
    int32_t numfunctions;

    int32_t ofs_strings;
    int32_t numstrings;    // Size in bytes, not count!

    int32_t ofs_globals;
    int32_t numglobals;    // Number of 32-bit words

    int32_t entityfields;  // How many 32-bit words make up ONE entity
};

// A single bytecode instruction
struct dstatement_t {
    uint16_t op;      // The opcode (e.g., ADD, SUB, CALL)
    int16_t  a, b, c; // Pointers to the variables in global memory
};

// Variable metadata (Used mostly for debugging and linking)
struct ddef_t {
    uint16_t type;    // float, string, entity, vector, function
    uint16_t offset;  // Where this variable lives in memory
    int32_t  s_name;  // Offset into the string table for its name
};

// Function metadata
struct dfunction_t {
    int32_t  first_statement; // Index into the statement array
    int32_t  parm_start;      // Index into global memory where parameters are placed
    int32_t  locals;          // Number of local variables
    int32_t  profile;         // (Unused profiling data)
    int32_t  s_name;          // Name of the function
    int32_t  s_file;          // Source file name
    int32_t  numparms;
    uint8_t  parm_size[8];    // Size of each parameter
};

#pragma pack(pop)

// QuakeC Opcodes
enum {
    OP_DONE = 0,
    OP_MUL_F, OP_MUL_V, OP_MUL_FV, OP_MUL_VF,
    OP_DIV_F,
    OP_ADD_F, OP_ADD_V,
    OP_SUB_F, OP_SUB_V,
    OP_EQ_F, OP_EQ_V, OP_EQ_S, OP_EQ_E, OP_EQ_FNC,
    OP_NE_F, OP_NE_V, OP_NE_S, OP_NE_E, OP_NE_FNC,
    OP_LE, OP_GE, OP_LT, OP_GT,
    OP_LOAD_F, OP_LOAD_V, OP_LOAD_S, OP_LOAD_ENT, OP_LOAD_FLD, OP_LOAD_FNC,
    OP_ADDRESS,
    OP_STORE_F, OP_STORE_V, OP_STORE_S, OP_STORE_ENT, OP_STORE_FLD, OP_STORE_FNC,
    OP_STOREP_F, OP_STOREP_V, OP_STOREP_S, OP_STOREP_ENT, OP_STOREP_FLD, OP_STOREP_FNC,
    OP_RETURN,
    OP_NOT_F, OP_NOT_V, OP_NOT_S, OP_NOT_ENT, OP_NOT_FNC,
    OP_IF, OP_IFNOT,
    OP_CALL0, OP_CALL1, OP_CALL2, OP_CALL3, OP_CALL4, OP_CALL5, OP_CALL6, OP_CALL7, OP_CALL8,
    OP_STATE,
    OP_GOTO,
    OP_AND, OP_OR,
    OP_BITAND, OP_BITOR
};

} // namespace engine::qc
