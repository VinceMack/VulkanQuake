#pragma once
#include "ProgsFormat.hpp"
#include <vector>
#include <span>
#include <string>

namespace engine {

struct StackFrame {
    int32_t functionIndex;
    int32_t returnPC;      // The Program Counter to jump back to
};

// An Entity in QuakeC memory
struct Edict {
    bool isFree = true;
    std::vector<qc::eval_t> v; // The 199 words of entity variables
};

class VirtualMachine {
public:
    VirtualMachine(std::vector<std::byte> progsData);

    void PrintInfo() const;

    // ---> NEW: Engine-to-VM Memory Bridge
    // Finds the memory offset of a global variable by its string name
    int32_t FindGlobalOffset(const std::string& name) const;
    
    // Getters and Setters for Global Memory
    float GetGlobalFloat(int32_t offset) const;
    void  SetGlobalFloat(int32_t offset, float value);

    // Finds the index of a function by its string name
    int32_t FindFunction(const std::string& name) const;

    // The Virtual CPU! Executes bytecode starting at the given function index
    void Execute(int32_t funcIndex);
    
    // Helper to execute by name
    void Execute(const std::string& name) {
        int32_t idx = FindFunction(name);
        if (idx != -1) Execute(idx);
    }

    // Helper to read strings out of QuakeC memory
    std::string GetProgsString(int32_t stringOffset) const;

private:
    std::vector<std::byte> m_rawData;
    const qc::dprograms_t* m_header;

    std::span<const qc::dstatement_t> m_statements;
    std::span<const qc::ddef_t>       m_globalDefs;
    std::span<const qc::ddef_t>       m_fieldDefs;
    std::span<const qc::dfunction_t>  m_functions;
    const char*                       m_strings;

    // ---> NEW: Writable Memory
    std::vector<qc::eval_t> m_globalData; // The VM's Global RAM
    std::vector<Edict>      m_edicts;     // The VM's Entity RAM
    std::vector<StackFrame> m_callStack; // <--- NEW: The CPU Call Stack!
};

} // namespace engine
