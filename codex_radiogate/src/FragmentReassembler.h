#pragma once

#include "MeshCodec.h"

class FragmentReassembler {
public:
    FragmentReassembler();

    bool consume(const MeshHeader& header,
                 const uint8_t* payload,
                 size_t payloadLength,
                 const MeshCodec& codec,
                 MeshHeader& reassembledHeader,
                 uint8_t* output,
                 size_t& outputLength,
                 uint32_t nowMs);
    void cleanup(uint32_t nowMs);

private:
    FragmentAssembly m_assemblies[cfg::kMaxFragments];
};
