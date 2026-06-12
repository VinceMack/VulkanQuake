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
    if (stringOffset <= 0 || stringOffset >= m_header->numstrings) {
        return "";
    }
    return std::string(m_strings + stringOffset);
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
    
    m_callStack.clear(); // Reset call stack for new run

    const auto& startFunc = m_functions[funcIndex];
    const char* funcName = m_strings + startFunc.s_name;
    std::cout << "=== VM Executing: " << funcName << " ===\n";

    if (startFunc.first_statement < 0) {
        std::cout << "  -> Cannot execute Built-in function directly.\n";
        return;
    }

    int pc = startFunc.first_statement; // Program Counter
    int runaway = 100000;               // Infinite loop protection

    while (runaway-- > 0) {
        const auto& st = m_statements[pc++];
        
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
                    // =======================================================
                    // ---> NEW: Execute C++ Built-in Functions
                    // =======================================================
                    int builtinIndex = -newFunc.first_statement;
                    
                    if (builtinIndex == 25) { // Built-in 25 is 'dprint'
                        // QuakeC always passes the first argument via Global Offset 4 (OFS_PARM0)
                        int32_t strOffset = m_globalData[4].string; 
                        std::cout << "[QuakeC] " << GetProgsString(strOffset);
                    } 
                    else {
                        // Unimplemented built-in. Just ignore and continue!
                        // std::cout << "[VM] Ignored Built-in #" << builtinIndex << " (" << (m_strings + newFunc.s_name) << ")\n";
                    }
                } else {
                    // =======================================================
                    // ---> NEW: Stack Pushing for Sub-Functions
                    // =======================================================
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
}

} // namespace engine
