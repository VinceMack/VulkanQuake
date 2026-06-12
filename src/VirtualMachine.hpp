#pragma once
#include "ProgsFormat.hpp"
#include <vector>
#include <span>
#include <string>

namespace engine {

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
};

} // namespace engine
