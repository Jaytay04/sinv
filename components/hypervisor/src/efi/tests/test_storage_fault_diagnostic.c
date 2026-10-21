#include "storage_fault_diagnostic.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d\n", __LINE__); return 1; } } while (0)

typedef struct TEST_READER {
    uint64_t Base;
    uint32_t Fsts;
    uint64_t Cap;
    uint32_t FailOffset;
    uint32_t Reads;
    uint32_t RecordReads;
    uint32_t HighReads;
    uint32_t ChangeHigh;
    uint32_t ClearFaultBit;
    uint32_t AllOnesRecord;
    uint32_t FirstWordAllOnes;
} TEST_READER;

static uint32_t Read32(void *opaque, uint64_t address, uint32_t *value)
{
    TEST_READER *reader = (TEST_READER *)opaque;
    uint32_t offset;
    if (address < reader->Base || address >= reader->Base + 4 * 4096 ||
        (address & 3) != 0) return 0;
    offset = (uint32_t)(address - reader->Base);
    ++reader->Reads;
    if (offset == reader->FailOffset) return 0;
    if (offset == 0x34) *value = reader->Fsts;
    else if (offset == 0x1c) *value = 0x80000000;
    else if (offset == 8) *value = (uint32_t)reader->Cap;
    else if (offset == 12) *value = (uint32_t)(reader->Cap >> 32);
    else if (offset >= 0x100) {
        ++reader->RecordReads;
        if (offset == (((reader->Cap >> 24) & 0x3ff) * 16) +
                       (((reader->Fsts >> 8) & 0xff) * 16) + 12) {
            ++reader->HighReads;
            *value = reader->AllOnesRecord ? UINT32_MAX :
                reader->ClearFaultBit ? 0 :
                (reader->ChangeHigh && reader->HighReads == 2) ?
                    0x80000001 : 0x80000000;
        } else *value = (reader->AllOnesRecord ||
                         (reader->FirstWordAllOnes &&
                          offset == (((reader->Cap >> 24) & 0x3ff) * 16))) ?
                            UINT32_MAX : offset;
    } else return 0;
    return 1;
}

int main(void)
{
    HV_STORAGE_DIAGNOSTIC_STATE state;
    TEST_READER reader;
    uint32_t words[4];
    uint64_t root[512] = { 0 }, pdpt[512] = { 0 },
             pd[512] = { 0 }, pt[512] = { 0 };
    const uint64_t base = UINT64_C(0x100000);
    memset(&state, 0, sizeof(state));
    memset(&reader, 0, sizeof(reader));
    reader.Base = UINT64_C(0xfed90000);
    reader.Cap = UINT64_C(0x10000000); /* FRO=0x100, NFR=0 */
    reader.Fsts = 2;
    reader.FailOffset = UINT32_MAX;
    state.UnitCount = 1;
    state.Units[0].RegisterBase = reader.Base;
    state.Units[0].Capability = reader.Cap;
    HvStorageCaptureUnit(&state.Units[0], 0, Read32, &reader);
    CHECK((state.Units[0].Flags & HV_STORAGE_DIAGNOSTIC_UNIT_BASELINE_FRCD) != 0);
    CHECK(reader.RecordReads == 5);
    CHECK(state.Units[0].Baseline.Frcd[3] == 0x80000000);
    HvStorageQuery(&state, 0x103, words);
    CHECK(words[3] == 0x80000000);

    reader.RecordReads = 0;
    reader.Fsts = 2 | (1 << 8); /* FRI beyond NFR */
    HvStorageCaptureUnit(&state.Units[0], 1, Read32, &reader);
    CHECK(reader.RecordReads == 0);
    CHECK((state.Units[0].Flags & HV_STORAGE_DIAGNOSTIC_UNIT_FAILURE_FRCD) == 0);

    reader.Fsts = 2;
    reader.Cap ^= UINT64_C(1);
    HvStorageCaptureUnit(&state.Units[0], 1, Read32, &reader);
    CHECK((state.Units[0].Flags & HV_STORAGE_DIAGNOSTIC_UNIT_CAP_CHANGED) != 0);
    CHECK(reader.RecordReads == 0);

    reader.Fsts = 0;
    reader.Cap = state.Units[0].Capability;
    reader.RecordReads = 0;
    HvStorageCaptureUnit(&state.Units[0], 1, Read32, &reader);
    CHECK(reader.RecordReads == 0);

    reader.Fsts = 2;
    reader.Cap = UINT64_C(0); /* FRO overlaps the fixed register block. */
    state.Units[0].Capability = reader.Cap;
    reader.RecordReads = 0;
    HvStorageCaptureUnit(&state.Units[0], 1, Read32, &reader);
    CHECK(reader.RecordReads == 0);

    reader.Cap = UINT64_C(0x10000000);
    state.Units[0].Capability = reader.Cap;
    reader.FailOffset = 0x104;
    HvStorageCaptureUnit(&state.Units[0], 1, Read32, &reader);
    CHECK((state.Units[0].Flags & HV_STORAGE_DIAGNOSTIC_UNIT_FAILURE_FRCD) == 0);

    reader.FailOffset = UINT32_MAX;
    reader.Fsts = UINT32_MAX;
    reader.RecordReads = 0;
    HvStorageCaptureUnit(&state.Units[0], 1, Read32, &reader);
    CHECK(reader.RecordReads == 0);

    reader.Fsts = 2;
    reader.Cap = UINT64_MAX;
    reader.RecordReads = 0;
    HvStorageCaptureUnit(&state.Units[0], 1, Read32, &reader);
    CHECK(reader.RecordReads == 0);

    reader.Cap = UINT64_C(0x3ff000000); /* last 16 bytes of the window */
    state.Units[0].Capability = reader.Cap;
    reader.HighReads = reader.RecordReads = 0;
    HvStorageCaptureUnit(&state.Units[0], 1, Read32, &reader);
    CHECK(reader.RecordReads == 5);
    CHECK((state.Units[0].Flags & HV_STORAGE_DIAGNOSTIC_UNIT_FAILURE_FRCD) != 0);

    reader.Cap = UINT64_C(0x103ff000000); /* NFR=1, FRI=1 crosses window */
    state.Units[0].Capability = reader.Cap;
    reader.Fsts = 2 | (1 << 8);
    reader.RecordReads = 0;
    HvStorageCaptureUnit(&state.Units[0], 1, Read32, &reader);
    CHECK(reader.RecordReads == 0);

    reader.Cap = UINT64_C(0x10000000);
    state.Units[0].Capability = reader.Cap;
    reader.Fsts = 2;
    reader.HighReads = 0;
    reader.ChangeHigh = 1;
    state.Units[0].Flags &= ~HV_STORAGE_DIAGNOSTIC_UNIT_FAILURE_FRCD;
    HvStorageCaptureUnit(&state.Units[0], 1, Read32, &reader);
    CHECK((state.Units[0].Flags & HV_STORAGE_DIAGNOSTIC_UNIT_FAILURE_FRCD) == 0);

    reader.ChangeHigh = 0;
    reader.ClearFaultBit = 1;
    reader.HighReads = 0;
    HvStorageCaptureUnit(&state.Units[0], 1, Read32, &reader);
    CHECK((state.Units[0].Flags & HV_STORAGE_DIAGNOSTIC_UNIT_FAILURE_FRCD) == 0);

    reader.ClearFaultBit = 0;
    reader.AllOnesRecord = 1;
    reader.HighReads = 0;
    HvStorageCaptureUnit(&state.Units[0], 1, Read32, &reader);
    CHECK((state.Units[0].Flags & HV_STORAGE_DIAGNOSTIC_UNIT_FAILURE_FRCD) == 0);

    reader.AllOnesRecord = 0;
    reader.FirstWordAllOnes = 1;
    reader.HighReads = 0;
    HvStorageCaptureUnit(&state.Units[0], 1, Read32, &reader);
    CHECK((state.Units[0].Flags & HV_STORAGE_DIAGNOSTIC_UNIT_FAILURE_FRCD) != 0);
    CHECK(state.Units[0].Failure.Frcd[0] == UINT32_MAX);

    state.UnitCount = 9;
    words[0] = 7;
    HvStorageQuery(&state, 0x100, words);
    CHECK(words[0] == 0);
    state.UnitCount = 8;
    HvStorageQuery(&state, 0x120, words);
    CHECK(words[0] == 0); /* first selector beyond eight units */
    state.UnitCount = 1;
    HvStorageQuery(&state, 0xff, words);
    CHECK(words[0] == 0);
    state.Units[0].RegisterBase = UINT64_MAX & ~UINT64_C(0xfff);
    reader.Reads = 0;
    HvStorageCaptureUnit(&state.Units[0], 1, Read32, &reader);
    CHECK(reader.Reads == 0);

    root[0] = (base + 4096) | 3;
    pdpt[1] = (base + 6 * 4096) | 3;
    pd[0] = (base + 7 * 4096) | 3;
#define ADMIT(cr3, cr0, efer, pat) \
    HvStorageWindowAdmission(base, cr3, cr0, efer, pat, \
                             root, pdpt, pd, pt)
    CHECK(ADMIT(base, 1 << 16, 1 << 11, 0) == 1);
    pt[511] = 1;
    CHECK(ADMIT(base, 1 << 16, 1 << 11, 0) == 2);
    pt[511] = 0;
    CHECK(ADMIT(base, 0, 1 << 11, 0) == 0);
    CHECK(ADMIT(base, 1 << 16, 0, 0) == 0);
    CHECK(ADMIT(base, 1 << 16, 1 << 11, UINT64_C(6) << 24) == 0);
    CHECK(ADMIT(base + 4096, 1 << 16, 1 << 11, 0) == 0);
    pdpt[1] ^= 2;
    CHECK(ADMIT(base, 1 << 16, 1 << 11, 0) == 0);
#undef ADMIT
    return 0;
}
