#include "VirtualMachine.hpp"
#include <stdexcept>
#include <iostream>

namespace engine {

template<typename T>
std::span<const T> GetProgsLump(const std::vector<std::byte>& data, int32_t offset, int32_t count) {
    if (offset < 0 || offset + (count * sizeof(T)) > data.size()) {
        throw std::runtime_error("Progs Lump out of bounds");
    }
    const T* ptr = reinterpret_cast<const T*>(data.data() + offset);
    return std::span<const T>(ptr, count);
}

VirtualMachine::VirtualMachine(std::vector<std::byte> progsData) 
    : m_rawData(std::move(progsData)) {
    
    if (m_rawData.size() < sizeof(qc::dprograms_t)) {
        throw std::runtime_error("progs.dat too small");
    }

    m_header = reinterpret_cast<const qc::dprograms_t*>(m_rawData.data());

    if (m_header->version != qc::PROG_VERSION) {
        throw std::runtime_error("Invalid progs.dat version");
    }

    // Extract all the lumps!
    m_statements = GetProgsLump<qc::dstatement_t>(m_rawData, m_header->ofs_statements, m_header->numstatements);
    m_globalDefs = GetProgsLump<qc::ddef_t>(m_rawData, m_header->ofs_globaldefs, m_header->numglobaldefs);
    m_fieldDefs  = GetProgsLump<qc::ddef_t>(m_rawData, m_header->ofs_fielddefs, m_header->numfielddefs);
    m_functions  = GetProgsLump<qc::dfunction_t>(m_rawData, m_header->ofs_functions, m_header->numfunctions);
    m_strings    = reinterpret_cast<const char*>(m_rawData.data() + m_header->ofs_strings);

    // ---> UPDATED: Copy read-only globals into WRITABLE memory
    auto rawGlobals = GetProgsLump<qc::eval_t>(m_rawData, m_header->ofs_globals, m_header->numglobals);
    m_globalData.assign(rawGlobals.begin(), rawGlobals.end());

    // ---> NEW: Initialize the Entity Dictionary (Edicts)
    m_edicts.resize(1024); // 1024 is the standard Quake engine limit
    for (auto& edict : m_edicts) {
        edict.isFree = true;
        edict.v.resize(m_header->entityfields);
        // Zero out the entity memory
        for (auto& val : edict.v) val.i = 0;
    }
    
    // Edict 0 is always reserved for the "World" (worldspawn)
    m_edicts[0].isFree = false;
    
    // ---> NEW: Edict 1 is ALWAYS reserved for Player 1
    m_edicts[1].isFree = false;
}

void VirtualMachine::PrintInfo() const {
    std::cout << "--- QuakeC VM Initialized ---\n";
    std::cout << "Statements: " << m_statements.size() << "\n";
    std::cout << "Functions: " << m_functions.size() << "\n";
    std::cout << "Globals (Words): " << m_globalData.size() << "\n";
    std::cout << "Entity Fields (Words per Entity): " << m_header->entityfields << "\n";
    
    // Let's print the names of the first 10 functions!
    std::cout << "First 10 Functions:\n";
    for (size_t i = 0; i < 10 && i < m_functions.size(); ++i) {
        const char* funcName = m_strings + m_functions[i].s_name;
        std::cout << "  " << i << ": " << funcName << "\n";
    }
}

int32_t VirtualMachine::FindGlobalOffset(const std::string& name) const {
    for (const auto& def : m_globalDefs) {
        const char* defName = m_strings + def.s_name;
        if (name == defName) {
            return def.offset;
        }
    }
    return -1; // Not found
}

float VirtualMachine::GetGlobalFloat(int32_t offset) const {
    if (offset < 0 || offset >= static_cast<int32_t>(m_globalData.size())) return 0.0f;
    return m_globalData[offset].f;
}

void VirtualMachine::SetGlobalFloat(int32_t offset, float value) {
    if (offset >= 0 && offset < static_cast<int32_t>(m_globalData.size())) {
        m_globalData[offset].f = value;
    }
}

std::string VirtualMachine::GetProgsString(int32_t stringOffset) const {
    // Offset 0 is the null string
    if (stringOffset <= 0) {
        return "";
    }
    if (stringOffset < m_header->numstrings) {
        return std::string(m_strings + stringOffset);
    }
    int32_t dynamicIndex = stringOffset - m_header->numstrings;
    if (dynamicIndex >= 0 && dynamicIndex < static_cast<int32_t>(m_dynamicStrings.size())) {
        return m_dynamicStrings[dynamicIndex];
    }
    return "";
}

int32_t VirtualMachine::AllocateString(const std::string& str) {
    if (str.empty()) return 0;
    // Check dynamic strings first to reuse
    for (size_t i = 0; i < m_dynamicStrings.size(); ++i) {
        if (m_dynamicStrings[i] == str) {
            return static_cast<int32_t>(m_header->numstrings + i);
        }
    }
    m_dynamicStrings.push_back(str);
    return static_cast<int32_t>(m_header->numstrings + m_dynamicStrings.size() - 1);
}

int32_t VirtualMachine::FindFunction(const std::string& name) const {
    for (size_t i = 1; i < m_functions.size(); ++i) { // 0 is a dummy null function
        const char* funcName = m_strings + m_functions[i].s_name;
        if (name == funcName) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

void VirtualMachine::Execute(int32_t funcIndex) {
    if (funcIndex <= 0 || funcIndex >= static_cast<int32_t>(m_functions.size())) return;
    
    std::cout << std::unitbuf; // Force unbuffered stdout so prints appear instantly
    m_callStack.clear(); // Reset call stack for new run

    const auto& startFunc = m_functions[funcIndex];
    const char* funcName = m_strings + startFunc.s_name;
    int32_t ofs_self = FindGlobalOffset("self");
    int32_t selfIdx = GetGlobalEdict(ofs_self);
    std::cout << "=== VM Executing: " << funcName << " (self: " << selfIdx << ") ===\n";

    if (startFunc.first_statement < 0) {
        std::cout << "  -> Cannot execute Built-in function directly.\n";
        return;
    }

    int pc = startFunc.first_statement; // Program Counter
    int runaway = 100000;               // Infinite loop protection

    m_traceHistory.assign(50, {0, 0, 0, 0, 0});
    m_traceHistoryIndex = 0;

    try {
        while (runaway-- > 0) {
            const auto& st = m_statements[pc++];
            
            m_traceHistory[m_traceHistoryIndex] = {pc - 1, st.op, st.a, st.b, st.c};
            m_traceHistoryIndex = (m_traceHistoryIndex + 1) % m_traceHistory.size();



            switch (st.op) {
                case qc::OP_RETURN:
                case qc::OP_DONE:
                    // ---> NEW: Stack Popping!
                    if (m_callStack.empty()) {
                        std::cout << "=== VM Execution Finished ===\n";
                        return; // The root function finished!
                    } else {
                        // We finished a sub-function. Jump back to where we were!
                        pc = m_callStack.back().returnPC;
                        m_callStack.pop_back();
                    }
                    break;

                case qc::OP_STORE_F:
                case qc::OP_STORE_ENT:
                case qc::OP_STORE_S:
                case qc::OP_STORE_FNC:
                    // STORE operations simply copy 32-bits from address A to address B
                    m_globalData[st.b].i = m_globalData[st.a].i;
                    break;

                case qc::OP_STORE_V:
                    // Vectors are 3 consecutive 32-bit floats
                    m_globalData[st.b + 0].i = m_globalData[st.a + 0].i;
                    m_globalData[st.b + 1].i = m_globalData[st.a + 1].i;
                    m_globalData[st.b + 2].i = m_globalData[st.a + 2].i;
                    break;

                case qc::OP_ADD_F:
                    m_globalData[st.c].f = m_globalData[st.a].f + m_globalData[st.b].f;
                    break;

                // =======================================================
                // ---> NEW: Basic Math (Multiply, Divide, Add, Sub)
                // =======================================================
                case qc::OP_MUL_F:
                    m_globalData[st.c].f = m_globalData[st.a].f * m_globalData[st.b].f;
                    break;
                case qc::OP_DIV_F:
                    // Standard Quake allows div by zero (results in INF)
                    m_globalData[st.c].f = m_globalData[st.a].f / m_globalData[st.b].f;
                    break;
                case qc::OP_ADD_V:
                    m_globalData[st.c + 0].f = m_globalData[st.a + 0].f + m_globalData[st.b + 0].f;
                    m_globalData[st.c + 1].f = m_globalData[st.a + 1].f + m_globalData[st.b + 1].f;
                    m_globalData[st.c + 2].f = m_globalData[st.a + 2].f + m_globalData[st.b + 2].f;
                    break;
                case qc::OP_SUB_V:
                    m_globalData[st.c + 0].f = m_globalData[st.a + 0].f - m_globalData[st.b + 0].f;
                    m_globalData[st.c + 1].f = m_globalData[st.a + 1].f - m_globalData[st.b + 1].f;
                    m_globalData[st.c + 2].f = m_globalData[st.a + 2].f - m_globalData[st.b + 2].f;
                    break;

                // =======================================================
                // ---> NEW: QuakeC Vector Multiplication Quirks
                // =======================================================
                case qc::OP_MUL_V: // Dot Product!
                    m_globalData[st.c].f = (m_globalData[st.a + 0].f * m_globalData[st.b + 0].f) +
                                           (m_globalData[st.a + 1].f * m_globalData[st.b + 1].f) +
                                           (m_globalData[st.a + 2].f * m_globalData[st.b + 2].f);
                    break;
                case qc::OP_MUL_FV: // Scalar * Vector
                    m_globalData[st.c + 0].f = m_globalData[st.a].f * m_globalData[st.b + 0].f;
                    m_globalData[st.c + 1].f = m_globalData[st.a].f * m_globalData[st.b + 1].f;
                    m_globalData[st.c + 2].f = m_globalData[st.a].f * m_globalData[st.b + 2].f;
                    break;
                case qc::OP_MUL_VF: // Vector * Scalar
                    m_globalData[st.c + 0].f = m_globalData[st.a + 0].f * m_globalData[st.b].f;
                    m_globalData[st.c + 1].f = m_globalData[st.a + 1].f * m_globalData[st.b].f;
                    m_globalData[st.c + 2].f = m_globalData[st.a + 2].f * m_globalData[st.b].f;
                    break;

                // =======================================================
                // ---> NEW: Greater/Less Than Comparisons
                // =======================================================
                case qc::OP_LE:
                    m_globalData[st.c].f = (m_globalData[st.a].f <= m_globalData[st.b].f) ? 1.0f : 0.0f;
                    break;
                case qc::OP_GE:
                    m_globalData[st.c].f = (m_globalData[st.a].f >= m_globalData[st.b].f) ? 1.0f : 0.0f;
                    break;
                case qc::OP_LT:
                    m_globalData[st.c].f = (m_globalData[st.a].f < m_globalData[st.b].f) ? 1.0f : 0.0f;
                    break;
                case qc::OP_GT:
                    m_globalData[st.c].f = (m_globalData[st.a].f > m_globalData[st.b].f) ? 1.0f : 0.0f;
                    break;
                
                // ---> NEW: Vector Comparisons
                case qc::OP_EQ_V:
                    m_globalData[st.c].f = (m_globalData[st.a+0].f == m_globalData[st.b+0].f &&
                                            m_globalData[st.a+1].f == m_globalData[st.b+1].f &&
                                            m_globalData[st.a+2].f == m_globalData[st.b+2].f) ? 1.0f : 0.0f;
                    break;
                case qc::OP_NE_V:
                    m_globalData[st.c].f = (m_globalData[st.a+0].f != m_globalData[st.b+0].f ||
                                            m_globalData[st.a+1].f != m_globalData[st.b+1].f ||
                                            m_globalData[st.a+2].f != m_globalData[st.b+2].f) ? 1.0f : 0.0f;
                    break;

                // =======================================================
                // ---> NEW: Logical and Bitwise Operations
                // =======================================================
                case qc::OP_NOT_F:
                case qc::OP_NOT_ENT:
                case qc::OP_NOT_FNC:
                    m_globalData[st.c].f = (!m_globalData[st.a].i) ? 1.0f : 0.0f;
                    break;
                case qc::OP_NOT_S:
                    m_globalData[st.c].f = (!m_globalData[st.a].string) ? 1.0f : 0.0f;
                    break;
                case qc::OP_NOT_V:
                    m_globalData[st.c].f = (m_globalData[st.a+0].f == 0.0f && 
                                            m_globalData[st.a+1].f == 0.0f && 
                                            m_globalData[st.a+2].f == 0.0f) ? 1.0f : 0.0f;
                    break;
                    
                case qc::OP_AND:
                    m_globalData[st.c].f = (m_globalData[st.a].f && m_globalData[st.b].f) ? 1.0f : 0.0f;
                    break;
                case qc::OP_OR:
                    m_globalData[st.c].f = (m_globalData[st.a].f || m_globalData[st.b].f) ? 1.0f : 0.0f;
                    break;

                // QuakeC bitwise operations cast the floats to ints!
                case qc::OP_BITAND:
                    m_globalData[st.c].f = static_cast<float>(static_cast<int>(m_globalData[st.a].f) & static_cast<int>(m_globalData[st.b].f));
                    break;
                case qc::OP_BITOR:
                    m_globalData[st.c].f = static_cast<float>(static_cast<int>(m_globalData[st.a].f) | static_cast<int>(m_globalData[st.b].f));
                    break;

                // =======================================================
                // ---> NEW: Entity Field Loads (e.g., self.health)
                // =======================================================
                case qc::OP_LOAD_F:
                case qc::OP_LOAD_S:
                case qc::OP_LOAD_ENT:
                case qc::OP_LOAD_FLD:
                case qc::OP_LOAD_FNC: {
                    int32_t edictIdx = m_globalData[st.a].edict;
                    int32_t fieldOffset = m_globalData[st.b].i;
                    if (edictIdx < 0 || edictIdx >= static_cast<int32_t>(m_edicts.size())) {
                        throw std::runtime_error("VM Error: edictIdx " + std::to_string(edictIdx) + " out of bounds in LOAD at PC " + std::to_string(pc - 1));
                    }
                    if (fieldOffset < 0 || fieldOffset >= m_header->entityfields) {
                        throw std::runtime_error("VM Error: fieldOffset " + std::to_string(fieldOffset) + " out of bounds in LOAD at PC " + std::to_string(pc - 1));
                    }
                    m_globalData[st.c].i = m_edicts[edictIdx].v[fieldOffset].i;
                    break;
                }
                case qc::OP_LOAD_V: {
                    int32_t edictIdx = m_globalData[st.a].edict;
                    int32_t fieldOffset = m_globalData[st.b].i;
                    if (edictIdx < 0 || edictIdx >= static_cast<int32_t>(m_edicts.size())) {
                        throw std::runtime_error("VM Error: edictIdx " + std::to_string(edictIdx) + " out of bounds in LOAD_V at PC " + std::to_string(pc - 1));
                    }
                    if (fieldOffset < 0 || fieldOffset + 2 >= m_header->entityfields) {
                        throw std::runtime_error("VM Error: fieldOffset " + std::to_string(fieldOffset) + " out of bounds in LOAD_V at PC " + std::to_string(pc - 1));
                    }
                    m_globalData[st.c + 0].i = m_edicts[edictIdx].v[fieldOffset + 0].i;
                    m_globalData[st.c + 1].i = m_edicts[edictIdx].v[fieldOffset + 1].i;
                    m_globalData[st.c + 2].i = m_edicts[edictIdx].v[fieldOffset + 2].i;
                    break;
                }

                // =======================================================
                // ---> NEW: Pointers and Entity Field Stores
                // =======================================================
                case qc::OP_ADDRESS: {
                    int32_t edictIdx = m_globalData[st.a].edict;
                    int32_t fieldOffset = m_globalData[st.b].i;
                    if (edictIdx < 0 || edictIdx >= static_cast<int32_t>(m_edicts.size())) {
                        throw std::runtime_error("VM Error: edictIdx " + std::to_string(edictIdx) + " out of bounds in ADDRESS at PC " + std::to_string(pc - 1));
                    }
                    if (fieldOffset < 0 || fieldOffset >= m_header->entityfields) {
                        throw std::runtime_error("VM Error: fieldOffset " + std::to_string(fieldOffset) + " out of bounds in ADDRESS at PC " + std::to_string(pc - 1));
                    }
                    // Encode the pointer securely: Top 16 bits = Entity, Bottom 16 bits = Field
                    m_globalData[st.c].i = (edictIdx << 16) | (fieldOffset & 0xFFFF);
                    break;
                }
                case qc::OP_STOREP_F:
                case qc::OP_STOREP_S:
                case qc::OP_STOREP_ENT:
                case qc::OP_STOREP_FLD:
                case qc::OP_STOREP_FNC: {
                    int32_t ptr = m_globalData[st.b].i;
                    int32_t edictIdx = (ptr >> 16) & 0xFFFF;
                    int32_t fieldOffset = ptr & 0xFFFF;
                    if (edictIdx < 0 || edictIdx >= static_cast<int32_t>(m_edicts.size())) {
                        throw std::runtime_error("VM Error: edictIdx " + std::to_string(edictIdx) + " out of bounds in STOREP at PC " + std::to_string(pc - 1));
                    }
                    if (fieldOffset < 0 || fieldOffset >= m_header->entityfields) {
                        throw std::runtime_error("VM Error: fieldOffset " + std::to_string(fieldOffset) + " out of bounds in STOREP at PC " + std::to_string(pc - 1));
                    }
                    m_edicts[edictIdx].v[fieldOffset].i = m_globalData[st.a].i;
                    break;
                }
                case qc::OP_STOREP_V: {
                    int32_t ptr = m_globalData[st.b].i;
                    int32_t edictIdx = (ptr >> 16) & 0xFFFF;
                    int32_t fieldOffset = ptr & 0xFFFF;
                    if (edictIdx < 0 || edictIdx >= static_cast<int32_t>(m_edicts.size())) {
                        throw std::runtime_error("VM Error: edictIdx " + std::to_string(edictIdx) + " out of bounds in STOREP_V at PC " + std::to_string(pc - 1));
                    }
                    if (fieldOffset < 0 || fieldOffset + 2 >= m_header->entityfields) {
                        throw std::runtime_error("VM Error: fieldOffset " + std::to_string(fieldOffset) + " out of bounds in STOREP_V at PC " + std::to_string(pc - 1));
                    }
                    m_edicts[edictIdx].v[fieldOffset + 0].i = m_globalData[st.a + 0].i;
                    m_edicts[edictIdx].v[fieldOffset + 1].i = m_globalData[st.a + 1].i;
                    m_edicts[edictIdx].v[fieldOffset + 2].i = m_globalData[st.a + 2].i;
                    break;
                }

                // =======================================================
                // ---> NEW: Control Flow (If / Else / Goto)
                // =======================================================
                case qc::OP_IF:
                    // Branch offsets are relative to the branch instruction itself, not the next instruction
                    if (m_globalData[st.a].i != 0) pc = (pc - 1) + st.b; 
                    break;
                case qc::OP_IFNOT:
                    if (m_globalData[st.a].i == 0) pc = (pc - 1) + st.b;
                    break;
                case qc::OP_GOTO:
                    pc = (pc - 1) + st.a;
                    break;
                case qc::OP_STATE: {
                    int32_t ofs_self = FindGlobalOffset("self");
                    int32_t ofs_time = FindGlobalOffset("time");
                    int32_t ofs_frame = FindFieldOffset("frame");
                    int32_t ofs_nextthink = FindFieldOffset("nextthink");
                    int32_t ofs_think = FindFieldOffset("think");
                    
                    int32_t selfIdx = GetGlobalEdict(ofs_self);
                    if (selfIdx >= 0 && selfIdx < static_cast<int32_t>(m_edicts.size())) {
                        float timeVal = GetGlobalFloat(ofs_time);
                        SetEdictFieldFloat(selfIdx, ofs_frame, GetGlobalFloat(st.a));
                        SetEdictFieldFloat(selfIdx, ofs_nextthink, timeVal + 0.1f);
                        if (ofs_think != -1) {
                            int32_t nextFunc = 0;
                            if (st.b > 0 && st.b < static_cast<int32_t>(m_globalData.size())) {
                                nextFunc = m_globalData[st.b].function;
                            }
                            m_edicts[selfIdx].v[ofs_think].function = nextFunc;
                        }
                    }
                    break;
                }

                // =======================================================
                // ---> NEW: Equality and Comparisons
                // =======================================================
                case qc::OP_SUB_F:
                    m_globalData[st.c].f = m_globalData[st.a].f - m_globalData[st.b].f;
                    break;
                case qc::OP_EQ_F:
                    m_globalData[st.c].f = (m_globalData[st.a].f == m_globalData[st.b].f) ? 1.0f : 0.0f;
                    break;
                case qc::OP_NE_F:
                    m_globalData[st.c].f = (m_globalData[st.a].f != m_globalData[st.b].f) ? 1.0f : 0.0f;
                    break;
                case qc::OP_EQ_E:
                case qc::OP_EQ_FNC:
                    m_globalData[st.c].f = (m_globalData[st.a].i == m_globalData[st.b].i) ? 1.0f : 0.0f;
                    break;
                case qc::OP_NE_E:
                case qc::OP_NE_FNC:
                    m_globalData[st.c].f = (m_globalData[st.a].i != m_globalData[st.b].i) ? 1.0f : 0.0f;
                    break;
                case qc::OP_EQ_S: {
                    if (m_globalData[st.a].string == m_globalData[st.b].string) {
                        m_globalData[st.c].f = 1.0f; // Fast pointer check
                    } else {
                        // Deep string content check
                        std::string s1 = GetProgsString(m_globalData[st.a].string);
                        std::string s2 = GetProgsString(m_globalData[st.b].string);
                        m_globalData[st.c].f = (s1 == s2) ? 1.0f : 0.0f;
                    }
                    break;
                }
                case qc::OP_NE_S: {
                    if (m_globalData[st.a].string == m_globalData[st.b].string) {
                        m_globalData[st.c].f = 0.0f;
                    } else {
                        std::string s1 = GetProgsString(m_globalData[st.a].string);
                        std::string s2 = GetProgsString(m_globalData[st.b].string);
                        m_globalData[st.c].f = (s1 != s2) ? 1.0f : 0.0f;
                    }
                    break;
                }

                case qc::OP_CALL0:
                case qc::OP_CALL1:
                case qc::OP_CALL2:
                case qc::OP_CALL3:
                case qc::OP_CALL4:
                case qc::OP_CALL5:
                case qc::OP_CALL6:
                case qc::OP_CALL7:
                case qc::OP_CALL8: {
                    int destFunc = m_globalData[st.a].function;
                    if (destFunc <= 0 || destFunc >= static_cast<int32_t>(m_functions.size())) {
                        throw std::runtime_error("VM Error: Invalid function call");
                    }
                    
                    const auto& newFunc = m_functions[destFunc];
                    if (newFunc.first_statement < 0) {
                        int builtinIndex = -newFunc.first_statement;
                        
                        // ---> NEW: Fire the callback if the C++ engine attached one!
                        if (m_builtinHandler) {
                            m_builtinHandler(*this, builtinIndex);
                        } else if (builtinIndex == 25) { 
                            // Fallback dprint if no handler is attached
                            std::cout << "[QuakeC] " << GetParmString(0);
                        }
                    } else {
                        // =======================================================
                        // ---> NEW: Stack Pushing for Sub-Functions
                        // =======================================================
                        // Copy parameters from global OFS_PARM to the function's local variables!
                        int numArgs = st.op - qc::OP_CALL0;
                        int currentParmOffset = 0;
                        for (int i = 0; i < numArgs; ++i) {
                            int size = newFunc.parm_size[i];
                            for (int j = 0; j < size; ++j) {
                                m_globalData[newFunc.parm_start + currentParmOffset + j].i = m_globalData[4 + i * 3 + j].i;
                            }
                            currentParmOffset += size;
                        }

                        m_callStack.push_back({destFunc, pc}); // Save where we are
                        pc = newFunc.first_statement;          // Jump to the new function!
                    }
                    break;
                }

                default:
                    std::cout << "=== VM Halted: Unhandled Opcode " << st.op << " at PC " << (pc - 1) << " ===\n";
                    return;
            }
        }
        std::cout << "=== VM Halted: Runaway loop detected ===\n";
    } catch (const std::exception& e) {
        std::cout << "--- VM EXCEPTION CAUGHT: " << e.what() << " ---\n";
        std::cout << "--- VM TRACE HISTORY (Last 50 instructions) ---\n";
        for (size_t i = 0; i < m_traceHistory.size(); ++i) {
            size_t idx = (m_traceHistoryIndex + i) % m_traceHistory.size();
            const auto& step = m_traceHistory[idx];
            if (step.pc == 0 && step.op == 0) continue;
            std::cout << "  PC: " << step.pc << " Op: " << step.op 
                      << " A: " << step.a << " (" << GetGlobalName(step.a) << ")"
                      << " B: " << step.b << " (" << GetGlobalName(step.b) << ")"
                      << " C: " << step.c << " (" << GetGlobalName(step.c) << ")\n";
        }
        std::cout << "----------------------------------------------\n";
        std::cout.flush();
        throw;
    }
}

int32_t VirtualMachine::FindFieldOffset(const std::string& name) const {
    for (const auto& def : m_fieldDefs) {
        const char* defName = m_strings + def.s_name;
        if (name == defName) {
            return def.offset;
        }
    }
    return -1;
}

int32_t VirtualMachine::AllocateEdict() {
    // Edict 0 is the World. We start searching at 1.
    for (size_t i = 1; i < m_edicts.size(); ++i) {
        if (m_edicts[i].isFree) {
            m_edicts[i].isFree = false;
            // Clear memory
            for (auto& val : m_edicts[i].v) val.i = 0;
            return static_cast<int32_t>(i);
        }
    }
    throw std::runtime_error("VM Error: Out of Edicts (Max 1024)!");
}

void VirtualMachine::SetEdictFieldFloat(int32_t edictIdx, int32_t offset, float val) {
    if (edictIdx >= 0 && edictIdx < static_cast<int32_t>(m_edicts.size())) {
        if (offset >= 0 && offset < static_cast<int32_t>(m_edicts[edictIdx].v.size())) {
            m_edicts[edictIdx].v[offset].f = val;
        }
    }
}

void VirtualMachine::SetEdictFieldVector(int32_t edictIdx, int32_t offset, glm::vec3 val) {
    if (edictIdx >= 0 && edictIdx < static_cast<int32_t>(m_edicts.size())) {
        if (offset >= 0 && offset + 2 < static_cast<int32_t>(m_edicts[edictIdx].v.size())) {
            m_edicts[edictIdx].v[offset + 0].f = val.x;
            m_edicts[edictIdx].v[offset + 1].f = val.y;
            m_edicts[edictIdx].v[offset + 2].f = val.z;
        }
    }
}

void VirtualMachine::SetGlobalEdict(int32_t offset, int32_t edictIdx) {
    if (offset >= 0 && offset < static_cast<int32_t>(m_globalData.size())) {
        m_globalData[offset].edict = edictIdx;
    }
}

int32_t VirtualMachine::GetGlobalEdict(int32_t offset) const {
    if (offset < 0 || offset >= static_cast<int32_t>(m_globalData.size())) return 0;
    return m_globalData[offset].edict;
}

float VirtualMachine::GetEdictFieldFloat(int32_t edictIdx, int32_t offset) const {
    if (edictIdx >= 0 && edictIdx < static_cast<int32_t>(m_edicts.size())) {
        if (offset >= 0 && offset < static_cast<int32_t>(m_edicts[edictIdx].v.size())) {
            return m_edicts[edictIdx].v[offset].f;
        }
    }
    return 0.0f;
}

glm::vec3 VirtualMachine::GetEdictFieldVector(int32_t edictIdx, int32_t offset) const {
    if (edictIdx >= 0 && edictIdx < static_cast<int32_t>(m_edicts.size())) {
        if (offset >= 0 && offset + 2 < static_cast<int32_t>(m_edicts[edictIdx].v.size())) {
            return glm::vec3(m_edicts[edictIdx].v[offset].f, 
                             m_edicts[edictIdx].v[offset + 1].f, 
                             m_edicts[edictIdx].v[offset + 2].f);
        }
    }
    return glm::vec3(0.0f);
}

int32_t VirtualMachine::GetEdictFieldFunction(int32_t edictIdx, int32_t offset) const {
    if (edictIdx < 0 || edictIdx >= static_cast<int32_t>(m_edicts.size())) return 0;
    if (offset < 0 || offset >= static_cast<int32_t>(m_edicts[edictIdx].v.size())) return 0;
    return m_edicts[edictIdx].v[offset].function;
}

void VirtualMachine::SetEdictFieldFromString(int32_t edictIdx, const std::string& name, const std::string& value) {
    if (edictIdx < 0 || edictIdx >= static_cast<int32_t>(m_edicts.size())) return;

    // 1. Find the field definition
    const qc::ddef_t* fieldDef = nullptr;
    for (const auto& def : m_fieldDefs) {
        const char* defName = m_strings + def.s_name;
        if (name == defName) {
            fieldDef = &def;
            break;
        }
    }
    if (!fieldDef) return; // Not a valid field in progs.dat

    int32_t offset = fieldDef->offset;
    uint16_t type = fieldDef->type & 0x7FFF; // Mask out any compiler flags

    if (offset < 0 || offset >= static_cast<int32_t>(m_edicts[edictIdx].v.size())) return;

    switch (type) {
        case 1: { // ev_string
            int32_t strOffset = AllocateString(value);
            m_edicts[edictIdx].v[offset].string = strOffset;
            break;
        }
        case 2: { // ev_float
            try {
                m_edicts[edictIdx].v[offset].f = std::stof(value);
            } catch (...) {
                m_edicts[edictIdx].v[offset].f = 0.0f;
            }
            break;
        }
        case 3: { // ev_vector
            if (offset + 2 >= static_cast<int32_t>(m_edicts[edictIdx].v.size())) return;
            glm::vec3 vec(0.0f);
            if (std::sscanf(value.c_str(), "%f %f %f", &vec.x, &vec.y, &vec.z) >= 1) {
                m_edicts[edictIdx].v[offset + 0].f = vec.x;
                m_edicts[edictIdx].v[offset + 1].f = vec.y;
                m_edicts[edictIdx].v[offset + 2].f = vec.z;
            }
            break;
        }
        case 4: { // ev_entity
            try {
                m_edicts[edictIdx].v[offset].edict = std::stoi(value);
            } catch (...) {
                m_edicts[edictIdx].v[offset].edict = 0;
            }
            break;
        }
        default:
            break;
    }
}

std::string VirtualMachine::GetGlobalName(int32_t offset) const {
    for (const auto& def : m_globalDefs) {
        if (def.offset == offset) {
            return m_strings + def.s_name;
        }
    }
    return "temp_" + std::to_string(offset);
}

std::string VirtualMachine::GetFieldName(int32_t offset) const {
    for (const auto& def : m_fieldDefs) {
        if (def.offset == offset) {
            return m_strings + def.s_name;
        }
    }
    return "field_" + std::to_string(offset);
}

void VirtualMachine::SetGlobalVector(int32_t offset, glm::vec3 val) {
    if (offset >= 0 && offset + 2 < static_cast<int32_t>(m_globalData.size())) {
        m_globalData[offset + 0].f = val.x;
        m_globalData[offset + 1].f = val.y;
        m_globalData[offset + 2].f = val.z;
    }
}

} // namespace engine
