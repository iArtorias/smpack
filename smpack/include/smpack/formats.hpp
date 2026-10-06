// Santa Monica 'SM' engine resource tool, format definitions.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace smpack
{
    inline constexpr std::uint64_t NULL_OFFSET = 0xFFFF'FFFF'FFFF'FFFFull;

    /// FNV-1a 64 with the engine's basis ['TexturePackLibrary' hash map bucket].
    [[nodiscard]] constexpr std::uint64_t Fnv1a64( const void * data, std::size_t len ) noexcept
    {
        constexpr std::uint64_t basis = 0xAF63'BD4C'8601'B7DFull;
        constexpr std::uint64_t prime = 0x0000'0100'0000'01B3ull;
        const auto * p = static_cast<const unsigned char *>(data);
        std::uint64_t h = basis;

        for (std::size_t i = 0; i < len; ++i)
        {
            h ^= p[i];
            h *= prime;
        }

        return h;
    }

    // '.texpack', '.texpack.toc'.
    namespace texpack
    {

        inline constexpr std::uint32_t EXPECTED_VERSION = 6u;
        inline constexpr std::uint32_t PAYLOAD_ALIGN = 16u; // 'file_offset16' unit.
        inline constexpr std::uint32_t MAX_BLOCK_BYTES = 104857600; // 'TexLoadingThread' read buffer.

        struct Header
        {
            std::array<char, 32> m_Name; // +0x00, Zero on disk, filled with pack name at load.
            std::uint32_t m_TocSizeBytes; // +0x20, Size of header and tables [16 aligned].
            std::uint32_t m_NumStreamBlocks; // +0x24
            std::uint32_t m_StreamBlocksOff; // +0x28
            std::uint32_t m_NumTexFiles; // +0x2C
            std::uint32_t m_Version; // +0x30, must == 6.
            std::uint32_t m_JsonSizeBytes; // +0x34
        };

        struct TextureFileInfo
        {
            std::uint64_t m_TexIdentifier; // WAD texture GUID[2-3] [fallback lookup key].
            std::uint64_t m_ContentHash; // Content hash in the WAD GNF 'USER' block [primary key].
            std::uint64_t m_SmallestMipOff; // 'StreamBlockFileInfo', relative to file start.
        };

        struct StreamBlockFileInfo
        {
            std::uint32_t m_FileOffset16; // +0x00 payload offset in 16 byte units.
            std::uint32_t m_MemSizeBytes; // +0x04, Texel bytes carried by the block.
            std::uint32_t m_DiskSizeBytes; // +0x08, Command stream and texel bytes.
            std::uint32_t m_Pad; // +0x0C
            std::uint8_t m_LargestMipIndex; // +0x10, Mip index counted from the 1x1 level.
            std::uint8_t m_SmallestMipIndex; // +0x11
            std::uint16_t m_TexPackIndex; // +0x12, Assigned at load.
            std::uint16_t m_LargestMipWidth; // +0x14, Texels of this block's largest mip
            std::uint16_t m_LargestMipHeight; // +0x16
            std::uint64_t m_NextMipOff; // +0x18, File relative, or null offset.
        };

        [[nodiscard]] constexpr std::uint64_t PayloadOffset( 
            const StreamBlockFileInfo & b
        ) noexcept
        {
            return static_cast<std::uint64_t>(b.m_FileOffset16) * PAYLOAD_ALIGN;
        }

        // Commands at the start of each payload block.
        enum class BlockCmd : std::uint32_t
        {
            HEADER = 1, // {1, data_offset, total_size}, 12 bytes.
            END = 2, // {2}, 4 bytes
            MEMCPY = 3, // {3, src_off, low_mip_count:u8, 0:u8, block_mip_count:u16, size}, 16 bytes
            UNKNOWN4 = 4, // 12 bytes
            GNF = 5, // {5, GNF header [256 bytes]}, 260 bytes
        };
    }

    // WAD [WTOC].
    namespace wad
    {
        inline constexpr std::uint32_t MAGIC = 0x434F5457; // 'WTOC'
        inline constexpr std::uint32_t VERSION = 2;
        inline constexpr std::uint32_t NUM_MEM_TYPES = 9;

        struct TocHeader
        {
            std::uint32_t m_Magic; // 'WTOC'
            std::uint32_t m_Version; // 2
            std::uint32_t m_NumEntries;
            std::uint32_t m_HeapSize;
            std::uint32_t m_ParmCount;
            std::uint32_t m_TotalMemByType[9];
            std::uint32_t m_Bits0; // NumExternalFiles:8, NumGameObjects:17, HasUnoptimized:1.
            std::uint32_t m_Bits1;
        };

        struct TocEntry
        {
            // 'IFF::Header' [0x60].
            std::uint16_t m_Id; // Chunk id.
            std::uint16_t m_Version;
            std::uint32_t m_Length;
            std::uint32_t m_Guid[4];
            char m_Name[56];
            std::uint32_t m_DcVersionHash;
            std::uint8_t m_AlignPad[12];
            std::uint32_t m_DebugSize;
            std::uint32_t m_TempSize;
            std::uint32_t m_ChunkAlign;
            std::uint8_t m_ClientId;
            std::uint8_t m_ServerId;
            std::uint8_t m_CtFlags;
            std::uint8_t m_MemType; // 'WAD_MEM_TYPE'.
            std::uint8_t m_GroupStartType;
            std::uint8_t m_GroupEndCount;
            std::uint8_t m_Bits;
            std::uint8_t m_Pad;
            std::uint32_t m_Pad2;
            std::uint64_t m_MemOff; // Offset within the 'm_MemType' arena.
            std::uint64_t m_DebugOff;
            std::uint64_t m_TempOff;
        };
    }

    // '.lodpack', '.lodpack.toc'.
    namespace lodpack
    {

        struct Header
        {
            std::uint32_t m_GroupCount;
            std::uint32_t m_BlockCount;
            std::uint32_t m_PrimParmCount;
            std::uint32_t m_HeaderFlags; // use LOD pack = 1
        };
        struct GroupEntry
        {
            std::uint64_t m_FileOffset; // Into '.lodpack'
            std::uint64_t m_GroupDataHash;
            std::uint32_t m_GroupDataSize;
            std::uint32_t m_GroupFlags; // Manually streamed = 1
        };

        struct BlockEntry
        {
            std::uint32_t m_GroupIdx;
            std::uint32_t m_GroupOffset; // Offset inside the group.
            std::uint64_t m_BlockDataHash; // Lookup key, entries are sorted by it.
            std::uint32_t m_BlockDataSize;
            std::uint32_t m_BlockFlags;
        };
    }

    // '.shaderpack'.
    namespace shaderpack
    {
        inline constexpr std::uint64_t MAGIC = 0x4B43'4150'5244'4853ull;  // 'SHDRPACK'
        inline constexpr std::uint64_t EXPECTED_VERSION = 11ull;

        struct Header
        {
            std::uint64_t m_Magic;
            std::uint64_t m_Version;
            std::uint64_t m_NumUniqueShaders;
            std::uint64_t m_NumRootSignatures;
            std::uint64_t m_NumShaderCombos;
            std::uint64_t m_NumPsos;
            std::uint64_t m_UniqueShaderRecordsOff;
            std::uint64_t m_RootSignatureRecordsOff;
            std::uint64_t m_ShaderComboRecordsOff;
            std::uint64_t m_PsoRecordsOff;
            std::uint64_t m_UniqueShaderBinsOff;
            std::uint64_t m_RootSignatureBinsOff;
            std::uint64_t m_NumWadShaders;
            std::uint64_t m_NumWadShaderBytes;
            std::uint64_t m_NumUniqueShaderBytes;
            std::uint64_t m_NumUniqueShaderCompressed;
        };

        struct ShaderRecord
        {
            std::uint64_t m_BytecodeHash;
            std::uint32_t m_Profile;
            std::uint32_t m_BinSize;
            std::uint32_t m_BinOffset; // Relative to 'm_UniqueShaderBinsOff'.
            std::uint32_t m_CsGroupX, m_CsGroupY, m_CsGroupZ;
            std::uint8_t m_VsNumOutputs;
            std::uint8_t m_Pad[3];
            char m_DebugName[64];
        };

        inline constexpr std::size_t COMBO_RECORD_SIZE = 0xEA0;
        inline constexpr std::size_t PSO_RECORD_SIZE = 0x388;
    }

    // .as, 'anmFileSet' ['anmStreamable' and header], one per 64 KB aligned slot.
    namespace animset
    {
        inline constexpr std::uint32_t SLOT_ALIGN = 0x10000;

        struct FileSetHeader
        {
            std::uint8_t m_Streamable[0x38]; // Runtime list links etc [zero on disk].
            std::uint64_t m_StreamFlags; // +0x38, 3 in .as, 5 in WAD stubs.
            std::uint32_t m_FileOffset; // +0x40 '_fileInf'. Offset of this set inside the .as.
            std::uint32_t m_FileSize; // +0x44 '_fileInf'. Size in bytes.
            std::uint64_t m_NameHash; // +0x48
            char m_Name[56]; // +0x50
            std::uint64_t m_Flags; // +0x88
            std::uint32_t m_TypeHash; // +0x90
            std::uint16_t m_Id; // +0x94
            std::uint16_t m_NumFiles; // +0x96
            // +0x98 int64 file_offsets[num_files]
        };
    }
}