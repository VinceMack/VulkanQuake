#pragma once
#include "ProgsFormat.hpp"
#include <vector>
#include <span>
#include <string>

namespace engine {

class VirtualMachine {
public:
    VirtualMachine(std::vector<std::byte> progsData);

    // Dumps info to the console to prove we loaded it
    void PrintInfo() const;

private:
    std::vector<std::byte> m_rawData;
    const qc::dprograms_t* m_header;

    std::span<const qc::dstatement_t> m_statements;
    std::span<const qc::ddef_t>       m_globalDefs;
    std::span<const qc::ddef_t>       m_fieldDefs;
    std::span<const qc::dfunction_t>  m_functions;
    const char*                       m_strings;
    std::span<const float>            m_globals; // Global memory is mostly 32-bit floats/ints
};

} // namespace engine
