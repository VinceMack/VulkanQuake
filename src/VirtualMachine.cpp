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
    
    // Globals are stored as 32-bit words (we cast to float for now, as QC is mostly floats)
    m_globals = GetProgsLump<float>(m_rawData, m_header->ofs_globals, m_header->numglobals);

    // Strings are a massive null-separated char block
    m_strings = reinterpret_cast<const char*>(m_rawData.data() + m_header->ofs_strings);
}

void VirtualMachine::PrintInfo() const {
    std::cout << "--- QuakeC VM Initialized ---\n";
    std::cout << "Statements: " << m_statements.size() << "\n";
    std::cout << "Functions: " << m_functions.size() << "\n";
    std::cout << "Globals (Words): " << m_globals.size() << "\n";
    std::cout << "Entity Fields (Words per Entity): " << m_header->entityfields << "\n";
    
    // Let's print the names of the first 10 functions!
    std::cout << "First 10 Functions:\n";
    for (size_t i = 0; i < 10 && i < m_functions.size(); ++i) {
        const char* funcName = m_strings + m_functions[i].s_name;
        std::cout << "  " << i << ": " << funcName << "\n";
    }
}

} // namespace engine
