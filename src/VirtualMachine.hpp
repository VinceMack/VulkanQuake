#pragma once
#include "ProgsFormat.hpp"
#include <vector>
#include <span>
#include <string>
#include <glm/glm.hpp>
#include <functional> // <--- NEW

namespace engine {

struct StackFrame {
    int32_t functionIndex;
    int32_t returnPC;      // The Program Counter to jump back to
};

// Callback for Built-in C++ functions
using BuiltinCallback = std::function<void(class VirtualMachine&, int32_t)>;

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
    int32_t     AllocateString(const std::string& str);
    std::string GetGlobalName(int32_t offset) const;
    std::string GetFieldName(int32_t offset) const;

    // ---> NEW: Set the C++ handler for built-ins
    void SetBuiltinHandler(BuiltinCallback handler) { m_builtinHandler = handler; }

    // Field Lookups
    int32_t FindFieldOffset(const std::string& name) const;
    
    // Edict Management
    int32_t AllocateEdict();
    void SetEdictFieldFloat(int32_t edictIdx, int32_t offset, float val);
    void SetEdictFieldVector(int32_t edictIdx, int32_t offset, glm::vec3 val);
    void SetEdictFieldFromString(int32_t edictIdx, const std::string& name, const std::string& value);
    
    // Set a global variable to an Entity/Edict index
    void SetGlobalEdict(int32_t offset, int32_t edictIdx);

    // Getters and Setters for Edicts/Globals
    int32_t GetGlobalEdict(int32_t offset) const;
    float GetEdictFieldFloat(int32_t edictIdx, int32_t offset) const;
    glm::vec3 GetEdictFieldVector(int32_t edictIdx, int32_t offset) const;
    int32_t GetEdictFieldFunction(int32_t edictIdx, int32_t offset) const;
    const std::vector<Edict>& GetEdicts() const { return m_edicts; }

    // QuakeC Parameter API (Parms start at offset 4, spaced by 3)
    float       GetParmFloat(int p) const  { return m_globalData[4 + p * 3].f; }
    std::string GetParmString(int p) const { return GetProgsString(m_globalData[4 + p * 3].string); }
    glm::vec3   GetParmVector(int p) const { 
        return glm::vec3(m_globalData[4 + p*3].f, m_globalData[4 + p*3 + 1].f, m_globalData[4 + p*3 + 2].f); 
    }
    int32_t     GetParmEdict(int p) const  { return m_globalData[4 + p * 3].edict; }
    
    // QuakeC Return API (Return value is ALWAYS at offset 1)
    void SetReturnFloat(float v) { m_globalData[1].f = v; }
    void SetReturnVector(glm::vec3 v) { 
        m_globalData[1].f = v.x; m_globalData[2].f = v.y; m_globalData[3].f = v.z; 
    }
    void SetReturnStringOffset(int32_t s) { m_globalData[1].string = s; }
    void SetReturnEdict(int32_t e) { m_globalData[1].edict = e; }
    void SetGlobalVector(int32_t offset, glm::vec3 val);

private:
    std::vector<std::byte> m_rawData;
    const qc::dprograms_t* m_header;

    std::span<const qc::dstatement_t> m_statements;
    std::span<const qc::ddef_t>       m_globalDefs;
    std::span<const qc::ddef_t>       m_fieldDefs;
    std::span<const qc::dfunction_t>  m_functions;
    const char*                       m_strings;

    // ---> NEW: Writable Memory
    std::vector<qc::eval_t>  m_globalData; // The VM's Global RAM
    std::vector<Edict>       m_edicts;     // The VM's Entity RAM
    std::vector<StackFrame>  m_callStack; // <--- NEW: The CPU Call Stack!
    BuiltinCallback          m_builtinHandler; // <--- NEW
    std::vector<std::string> m_dynamicStrings; // <--- NEW: Dynamic string storage

    struct TraceStep {
        int32_t pc = 0;
        uint16_t op = 0;
        int16_t a = 0, b = 0, c = 0;
    };
    std::vector<TraceStep> m_traceHistory;
    size_t m_traceHistoryIndex = 0;

    friend class Engine; // <--- NEW
};

} // namespace engine
