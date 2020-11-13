//
// Created by Carl on 2020-09-21.
//

#include <deps/android-base/include/android-base/logging.h>
#include <include/unwindstack/MachineArm.h>
#include <include/unwindstack/Memory.h>
#include <vector>
#include <utility>
#include <memory>
#include <QuickenInstructions.h>
#include <MinimalRegs.h>
#include "QuickenTable.h"
#include "../../common/Log.h"

namespace wechat_backtrace {

using namespace std;
using namespace unwindstack;

std::shared_ptr<QutSections> QuickenTableManager::LoadQutSections(std::string soname, std::string build_id) {
    // TODO
}

void QuickenTableManager::SaveQutSections(std::string soname, std::string build_id, std::shared_ptr<QutSections> qut_sections) {
    // TODO
}

#define ReturnIndexOverflow(_i, _amount)  \
        if (_i >= _amount) { \
            return false; \
        }

#define NoReturnIndexOverflow(_i, _amount)

#define IterateNextByteIdx(_j, _i, _amount, _ReturnIndexOverflow)  \
    _j--; \
    if (_j < 0) { \
        _j = QUT_TBL_ROW_SIZE; \
        _i++; \
        _ReturnIndexOverflow(_i, _amount) \
    }

#define ReadByte(_byte, _instructions, _j, _i, _amount) \
    _byte = _instructions[_i] >> (8 * _j) & 0xff; \
    IterateNextByteIdx(_j, _i, _amount, NoReturnIndexOverflow)

#define DecodeSLEB128(_value, _instructions, _amount, _j, _i) \
    { \
        unsigned Shift = 0; \
        uint8_t Byte; \
        do { \
            IterateNextByteIdx(_j, _i, _amount, ReturnIndexOverflow) \
            ReadByte(Byte, _instructions, _j, _i, _amount) \
            _value |= (uint64_t(Byte & 0x7f) << Shift); \
            Shift += 7; \
        } while (Byte >= 0x80); \
        if (Shift < 64 && (Byte & 0x40)) _value |= (-1ULL) << Shift; \
    }

// TODO this is 32 bit
inline bool QuickenTable::Decode32(const uint32_t* instructions, const size_t amount, const size_t start_pos) {

    QUT_DEBUG_LOG("QuickenTable::Decode32 instructions %x, amount %u, start_pos %u", instructions, (uint32_t)amount, (uint32_t)start_pos);

    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wint-to-pointer-cast"

    int32_t j = start_pos;
    size_t i = 0;

#ifdef EnableLOG
    for (size_t m = 0; m < amount; m++) {
        QUT_DEBUG_LOG("QuickenTable::Decode instruction %x", instructions[m]);
//        QUT_TMP_LOG("QuickenTable::Decode instruction %x", instructions[m]);
    }
#endif

    while(i < amount) {
        uint8_t byte;
        ReadByte(byte, instructions, j, i, amount);
        QUT_DEBUG_LOG("Decode byte " BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(byte));
        switch (byte >> 6) {
        case 0:
            // 00nn nnnn : vsp = vsp + (nnnnnn << 2) ; # (nnnnnnn << 2) in [0, 0xfc]
            cfa_ += (byte << 2);
            break;
        case 1:
            // 01nn nnnn : vsp = vsp - (nnnnnn << 2) ; # (nnnnnnn << 2) in [0, 0xfc]
            cfa_ -= (byte << 2);
            break;
        case 2:
            switch (byte) {
                case QUT_INSTRUCTION_VSP_SET_BY_R7_OP:
                    cfa_ = R7(regs_);
                    break;
                case QUT_INSTRUCTION_VSP_SET_BY_R7_PROLOGUE_OP:
                    cfa_ = R7(regs_) + 8;
                    LR(regs_) = *((uint32_t *)(cfa_ - 4));
                    R7(regs_) = *((uint32_t *)(cfa_ - 8));
                    break;
                case QUT_INSTRUCTION_VSP_SET_BY_R11_OP:
                    cfa_ = R11(regs_);
                    break;
                case QUT_INSTRUCTION_VSP_SET_BY_R11_PROLOGUE_OP:
                    cfa_ = R11(regs_) + 8;
                    LR(regs_) = *((uint32_t *)(cfa_ - 4));
                    R11(regs_) = *((uint32_t *)(cfa_ - 8));
                    break;
                case QUT_INSTRUCTION_VSP_SET_BY_SP_OP:
                    cfa_ = SP(regs_);
                    break;
                case QUT_INSTRUCTION_VSP_SET_BY_JNI_SP_OP: {
                    uint8_t byte;
                    ReadByte(byte, instructions, j, i, amount);
                    cfa_ = R10(regs_) + ((byte & 0x7f) << 2);

                    QUT_DEBUG_LOG("Decode QUT_INSTRUCTION_VSP_SET_BY_JNI_SP_OP %x r10 %x, cfa_ %x, offset %x, " BYTE_TO_BINARY_PATTERN,
                            (uint32_t)byte, R10(regs_), cfa_, (uint32_t)((byte & 0x7f) << 2), BYTE_TO_BINARY(byte));
                    break;
                }
                case QUT_INSTRUCTION_VSP_SET_IMM_OP: {
                    int64_t value = 0;// TODO overflow
                    DecodeSLEB128(value, instructions, amount, j, i)
                    cfa_ = value;
                    break;
                }
                case QUT_INSTRUCTION_VSP_SET_BY_R7_IMM_OP: {
                    uint8_t byte;
                    ReadByte(byte, instructions, j, i, amount);
                    cfa_ = R7(regs_) + ((byte & 0x7f) << 2);
                    break;
                }
                case QUT_INSTRUCTION_VSP_SET_BY_R11_IMM_OP: {
                    uint8_t byte;
                    ReadByte(byte, instructions, j, i, amount);
                    cfa_ = R11(regs_) + ((byte & 0x7f) << 2);
                    break;
                }
                case QUT_INSTRUCTION_DEX_PC_SET_OP: {
                    QUT_DEBUG_LOG("Decode byte QUT_INSTRUCTION_DEX_PC_SET_OP");
                    dex_pc_ = R4(regs_);
                    break;
                }
                case QUT_END_OF_INS_OP: {
                    QUT_DEBUG_LOG("Decode byte QUT_END_OF_INS_OP");
                    return true;
                }
                case QUT_FINISH_OP: {
                    QUT_DEBUG_LOG("Decode byte QUT_FINISH_OP");
                    PC(regs_) = 0;
                    LR(regs_) = 0;
                    return true;
                }
                default: {
                    switch (byte & 0xf0) {
                        case QUT_INSTRUCTION_R4_OFFSET_OP_PREFIX: {
                            R4(regs_) = *((uint32_t *)(cfa_ - ((byte & 0xf) << 2)));
                            break;
                        }
                        case QUT_INSTRUCTION_R7_OFFSET_OP_PREFIX: {
                            R7(regs_) = *((uint32_t *)(cfa_ - ((byte & 0xf) << 2)));
                            break;
                        }
                    }
                }
            }
            break;
        default:
            switch (byte & 0xf0) {
                case QUT_INSTRUCTION_R10_OFFSET_OP_PREFIX:
                    R10(regs_) = *((uint32_t *)(cfa_ - ((byte & 0xf) << 2)));
                    break;
                case QUT_INSTRUCTION_R11_OFFSET_OP_PREFIX:
                    R11(regs_) = *((uint32_t *)(cfa_ - ((byte & 0xf) << 2)));
                    break;
                case QUT_INSTRUCTION_LR_OFFSET_OP_PREFIX:
                    LR(regs_) = *((uint32_t *)(cfa_ - ((byte & 0xf) << 2)));
                    break;
                default:
                    int64_t value = 0;
                    switch (byte) {
                        case QUT_INSTRUCTION_R7_OFFSET_SLEB128_OP:
                            DecodeSLEB128(value, instructions, amount, j, i)
                            R7(regs_) = *((uint32_t *)(cfa_ - (int32_t)value));
                            break;
                        case QUT_INSTRUCTION_R10_OFFSET_SLEB128_OP:
                            DecodeSLEB128(value, instructions, amount, j, i)
                            R10(regs_) = *((uint32_t *)(cfa_ - (int32_t)value));
                            break;
                        case QUT_INSTRUCTION_R11_OFFSET_SLEB128_OP:
                            DecodeSLEB128(value, instructions, amount, j, i)
                            R11(regs_) = *((uint32_t *)(cfa_ - (int32_t)value));
                            break;
                        case QUT_INSTRUCTION_SP_OFFSET_SLEB128_OP:
                            DecodeSLEB128(value, instructions, amount, j, i)
                            SP(regs_) = *((uint32_t *)(cfa_ - (int32_t)value));
                            break;
                        case QUT_INSTRUCTION_LR_OFFSET_SLEB128_OP:
                            DecodeSLEB128(value, instructions, amount, j, i)
                            LR(regs_) = *((uint32_t *)(cfa_ - (int32_t)value));
                            break;
                        case QUT_INSTRUCTION_PC_OFFSET_SLEB128_OP:
                            DecodeSLEB128(value, instructions, amount, j, i)
                            PC(regs_) = *((uint32_t *)(cfa_ - (int32_t)value));
                            pc_set_ = true;
                            break;
                        case QUT_INSTRUCTION_VSP_OFFSET_SLEB128_OP:
                            DecodeSLEB128(value, instructions, amount, j, i)
                            cfa_ += (int32_t)value;
                            break;
                        default:
                            QUT_DEBUG_LOG("Decode byte UNKNOWN");
                            return false;
                    }
                    break;
            }
            break;
        }
    }

    #pragma clang diagnostic pop

    return true;
}

inline bool QuickenTable::Decode64(const uint64_t* instructions, const size_t amount, const size_t start_pos) {

    QUT_DEBUG_LOG("QuickenTable::Decode64 instructions %llx, amount %llu, start_pos %llu", instructions, amount, start_pos);

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wint-to-pointer-cast"

    int32_t j = start_pos;
    size_t i = 0;

#ifdef EnableLOG
    for (size_t m = 0; m < amount; m++) {
        QUT_DEBUG_LOG("QuickenTable::Decode64 instruction %llx", instructions[m]);
    }
#endif

    while(i < amount) {
        uint8_t byte;
        ReadByte(byte, instructions, j, i, amount);
        QUT_DEBUG_LOG("Decode byte " BYTE_TO_BINARY_PATTERN, BYTE_TO_BINARY(byte));
        switch (byte >> 6) {
            case 0:
                // 00nn nnnn : vsp = vsp + (nnnnnn << 2) ; # (nnnnnnn << 2) in [0, 0xfc]
                cfa_ += (byte << 2);
                break;
            case 1:
                // 01nn nnnn : vsp = vsp - (nnnnnn << 2) ; # (nnnnnnn << 2) in [0, 0xfc]
                cfa_ -= (byte << 2);
                break;
            case 2:
                switch (byte) {
                    case QUT_INSTRUCTION_VSP_SET_BY_X29_OP:
                        QUT_DEBUG_LOG("Decode byte QUT_INSTRUCTION_VSP_SET_BY_X29_OP");
                        cfa_ = R29(regs_);
                        break;
                    case QUT_INSTRUCTION_VSP_SET_BY_X29_PROLOGUE_OP:
                        QUT_DEBUG_LOG("Decode byte QUT_INSTRUCTION_VSP_SET_BY_X29_PROLOGUE_OP");
                        cfa_ = R29(regs_) + 16;
                        LR(regs_) = *((uint64_t *)(cfa_ - 8));
                        R29(regs_) = *((uint64_t *)(cfa_ - 16));
                        break;
                    case QUT_INSTRUCTION_VSP_SET_BY_SP_OP:
                        QUT_DEBUG_LOG("Decode byte QUT_INSTRUCTION_VSP_SET_BY_SP_OP");
                        cfa_ = SP(regs_);
                        break;
                    case QUT_INSTRUCTION_VSP_SET_BY_JNI_SP_OP: {
                        uint8_t byte;
                        ReadByte(byte, instructions, j, i, amount);
                        cfa_ = R28(regs_) + ((byte & 0x7f) << 2);

                        QUT_DEBUG_LOG("Decode QUT_INSTRUCTION_VSP_SET_BY_JNI_SP_OP %x x28 %llx, cfa_ %x, offset %x, " BYTE_TO_BINARY_PATTERN,
                                      (uint32_t)byte, R28(regs_), cfa_, (uint32_t)((byte & 0x7f) << 2), BYTE_TO_BINARY(byte));
                        break;
                    }
                    case QUT_INSTRUCTION_VSP_SET_IMM_OP: {
                        int64_t value = 0; // TODO overflow
                        DecodeSLEB128(value, instructions, amount, j, i)
                        cfa_ = value;
                        break;
                    }
                    case QUT_INSTRUCTION_VSP_SET_BY_X29_IMM_OP: {
                        uint8_t byte;
                        ReadByte(byte, instructions, j, i, amount);
                        QUT_DEBUG_LOG("Decode before QUT_INSTRUCTION_VSP_SET_BY_X29_IMM_OP cfa_ %llx, R29(regs_) %llx, computed %llx", cfa_, R29(regs_), R29(regs_) + ((byte & 0x7f) << 2));
                        cfa_ = R29(regs_) + ((byte & 0x7f) << 2);
                        break;
                    }
                    case QUT_INSTRUCTION_DEX_PC_SET_OP: {
                        QUT_DEBUG_LOG("Decode byte QUT_INSTRUCTION_DEX_PC_SET_OP");
                        dex_pc_ = R20(regs_);
                        break;
                    }
                    case QUT_END_OF_INS_OP: {
                        QUT_DEBUG_LOG("Decode byte QUT_END_OF_INS_OP");
                        return true;
                    }
                    case QUT_FINISH_OP: {
                        QUT_DEBUG_LOG("Decode byte QUT_FINISH_OP");
                        PC(regs_) = 0;
                        LR(regs_) = 0;
                        return true;
                    }
                    default: {
                        switch (byte & 0xf0) {
                            case QUT_INSTRUCTION_X20_OFFSET_OP_PREFIX: {
                                R20(regs_) = *((uint64_t *)(cfa_ - ((byte & 0xf) << 2)));
                                break;
                            }
                        }
                    }
                }
                break;
            default:
                switch (byte & 0xf0) {
                    case QUT_INSTRUCTION_X28_OFFSET_OP_PREFIX:
                        R28(regs_) = *((uint64_t *)(cfa_ - ((byte & 0xf) << 2)));
                        break;
                    case QUT_INSTRUCTION_X29_OFFSET_OP_PREFIX:
                        QUT_DEBUG_LOG("Decode before QUT_INSTRUCTION_X29_OFFSET_OP_PREFIX cfa_ %llx, computed %llx, R29(regs_) %llx", cfa_, (cfa_ - ((byte & 0xf) << 2)), *((uint64_t *)(cfa_ - ((byte & 0xf) << 2))));
                        R29(regs_) = *((uint64_t *)(cfa_ - ((byte & 0xf) << 2)));
                        QUT_DEBUG_LOG("Decode QUT_INSTRUCTION_X29_OFFSET_OP_PREFIX");
                        if (R29(regs_) == 0) {  // reach end
                            return true;
                        }
                        break;
                    case QUT_INSTRUCTION_LR_OFFSET_OP_PREFIX:
                        LR(regs_) = *((uint64_t *)(cfa_ - ((byte & 0xf) << 2)));
                        break;
                    default:
                        int64_t value = 0;
                        switch (byte) {
                            case QUT_INSTRUCTION_X28_OFFSET_SLEB128_OP:
                                DecodeSLEB128(value, instructions, amount, j, i)
                                R28(regs_) = *((uint64_t *)(cfa_ - (int64_t)value));
                                break;
                            case QUT_INSTRUCTION_X29_OFFSET_SLEB128_OP:
                                DecodeSLEB128(value, instructions, amount, j, i)
                                R29(regs_) = *((uint64_t *)(cfa_ - (int64_t)value));
                                if (R29(regs_) == 0) { // reach end
                                    return true;
                                }
                                break;
                            case QUT_INSTRUCTION_SP_OFFSET_SLEB128_OP:
                                DecodeSLEB128(value, instructions, amount, j, i)
                                SP(regs_) = *((uint64_t *)(cfa_ - (int64_t)value));
                                break;
                            case QUT_INSTRUCTION_LR_OFFSET_SLEB128_OP:
                                DecodeSLEB128(value, instructions, amount, j, i)
                                LR(regs_) = *((uint64_t *)(cfa_ - (int64_t)value));
                                break;
                            case QUT_INSTRUCTION_PC_OFFSET_SLEB128_OP:
                                DecodeSLEB128(value, instructions, amount, j, i)
                                PC(regs_) = *((uint64_t *)(cfa_ - (int64_t)value));
                                pc_set_ = true;
                                break;
                            case QUT_INSTRUCTION_VSP_OFFSET_SLEB128_OP:
                                DecodeSLEB128(value, instructions, amount, j, i)
                                cfa_ += (int64_t)value;
                                break;
                            default:
                                QUT_DEBUG_LOG("Decode byte UNKNOWN");
                                return false;
                        }
                        break;
                }
                break;
        }
    }

#pragma clang diagnostic pop

    return true;
}

inline bool QuickenTable::Decode(const uptr* instructions, const size_t amount, const size_t start_pos) {
#ifdef __arm__
    return Decode32(instructions, amount, start_pos);
#else
    return Decode64(instructions, amount, start_pos);
#endif
}

bool QuickenTable::Eval(size_t entry_offset) {

    uptr command = fut_sections_->quidx[entry_offset + 1];

    QUT_DEBUG_LOG("QuickenTable::Eval entry_offset %u, add %llx, command %llx",
            (uint32_t)entry_offset, (uint64_t)fut_sections_->quidx[entry_offset],
            (uint64_t)fut_sections_->quidx[entry_offset + 1]);

    if (command >> (sizeof(uptr) * 8 - 1)) {
        return Decode(&command, 1, QUT_TBL_ROW_SIZE - 1); // compact
    } else {
        size_t row_count = (command >> ((sizeof(uptr) - 1) * 8)) & 0x7f;
        size_t row_offset = command & 0xffffff;
        QUT_DEBUG_LOG("QuickenTable::Eval row_count %u, row_offset %u", (uint32_t)row_count, (uint32_t)row_offset);
        return Decode(&fut_sections_->qutbl[row_offset], row_count, QUT_TBL_ROW_SIZE);
    }
}

}  // namespace wechat_backtrace
