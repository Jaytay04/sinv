#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#if HYPERSINV_HAVE_DIA
#include <dia2.h>
#endif

#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct FileMapping {
    HANDLE File = INVALID_HANDLE_VALUE;
    HANDLE Mapping = nullptr;
    const std::uint8_t *View = nullptr;
    std::size_t Size = 0;

    FileMapping() = default;
    FileMapping(const FileMapping &) = delete;
    FileMapping &operator=(const FileMapping &) = delete;

    FileMapping(FileMapping &&other) noexcept
        : File(other.File), Mapping(other.Mapping), View(other.View),
          Size(other.Size)
    {
        other.File = INVALID_HANDLE_VALUE;
        other.Mapping = nullptr;
        other.View = nullptr;
        other.Size = 0;
    }

    FileMapping &operator=(FileMapping &&other) noexcept
    {
        if (this == &other) return *this;
        if (View != nullptr) UnmapViewOfFile(View);
        if (Mapping != nullptr) CloseHandle(Mapping);
        if (File != INVALID_HANDLE_VALUE) CloseHandle(File);
        File = other.File;
        Mapping = other.Mapping;
        View = other.View;
        Size = other.Size;
        other.File = INVALID_HANDLE_VALUE;
        other.Mapping = nullptr;
        other.View = nullptr;
        other.Size = 0;
        return *this;
    }

    ~FileMapping()
    {
        if (View != nullptr) UnmapViewOfFile(View);
        if (Mapping != nullptr) CloseHandle(Mapping);
        if (File != INVALID_HANDLE_VALUE) CloseHandle(File);
    }
};

struct ImageIdentity {
    std::uint32_t Timestamp = 0;
    std::uint32_t ImageSize = 0;
    GUID PdbGuid{};
    std::uint32_t PdbAge = 0;
};

struct ExtractedProfile {
    ImageIdentity Identity{};
    std::uint32_t EprocessSize = 0;
    std::uint32_t EprocessPcb = 0;
    std::uint32_t EprocessActiveProcessLinks = 0;
    std::uint32_t EprocessUniqueProcessId = 0;
    std::uint32_t EprocessImageFileName = 0;
    std::uint32_t EprocessImageFileNameSize = 0;
    std::uint32_t KprocessDirectoryTableBase = 0;
    std::uint32_t EprocessDirectoryTableBase = 0;
    std::uint32_t PsActiveProcessHeadRva = 0;
};

[[noreturn]] void Fail(const std::string &message)
{
    throw std::runtime_error(message);
}

std::uint32_t ParseU32(const wchar_t *text, const char *name)
{
    wchar_t *end = nullptr;
    const unsigned long value = std::wcstoul(text, &end, 0);
    if (text == end || *end != L'\0' ||
        value > std::numeric_limits<std::uint32_t>::max()) {
        Fail(std::string("invalid ") + name);
    }
    return static_cast<std::uint32_t>(value);
}

FileMapping MapFile(const std::filesystem::path &path)
{
    FileMapping mapped;
    mapped.File = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (mapped.File == INVALID_HANDLE_VALUE) Fail("cannot open kernel image");
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(mapped.File, &size) || size.QuadPart <= 0 ||
        static_cast<unsigned long long>(size.QuadPart) > SIZE_MAX) {
        Fail("invalid kernel image size");
    }
    mapped.Size = static_cast<std::size_t>(size.QuadPart);
    mapped.Mapping = CreateFileMappingW(mapped.File, nullptr, PAGE_READONLY,
                                        0, 0, nullptr);
    if (mapped.Mapping == nullptr) Fail("cannot map kernel image");
    mapped.View = static_cast<const std::uint8_t *>(
        MapViewOfFile(mapped.Mapping, FILE_MAP_READ, 0, 0, 0));
    if (mapped.View == nullptr) Fail("cannot view kernel image");
    return mapped;
}

template <typename T>
const T *At(const FileMapping &file, std::size_t offset)
{
    if (offset > file.Size || sizeof(T) > file.Size - offset) return nullptr;
    return reinterpret_cast<const T *>(file.View + offset);
}

std::size_t RvaToOffset(const FileMapping &file,
                        const IMAGE_NT_HEADERS64 &nt,
                        std::uint32_t rva,
                        std::size_t bytes)
{
    const auto *section = IMAGE_FIRST_SECTION(&nt);
    for (std::uint16_t index = 0; index < nt.FileHeader.NumberOfSections;
         ++index) {
        const std::uint32_t span = std::max(
            section[index].Misc.VirtualSize,
            section[index].SizeOfRawData);
        if (rva >= section[index].VirtualAddress &&
            rva - section[index].VirtualAddress <= span &&
            bytes <= span - (rva - section[index].VirtualAddress)) {
            const std::size_t offset =
                static_cast<std::size_t>(section[index].PointerToRawData) +
                (rva - section[index].VirtualAddress);
            if (offset <= file.Size && bytes <= file.Size - offset)
                return offset;
        }
    }
    if (rva <= nt.OptionalHeader.SizeOfHeaders &&
        bytes <= nt.OptionalHeader.SizeOfHeaders - rva &&
        rva <= file.Size && bytes <= file.Size - rva) {
        return rva;
    }
    Fail("PE RVA is not backed by image data");
}

ImageIdentity ReadImageIdentity(const std::filesystem::path &image)
{
    const FileMapping file = MapFile(image);
    const auto *dos = At<IMAGE_DOS_HEADER>(file, 0);
    if (dos == nullptr || dos->e_magic != IMAGE_DOS_SIGNATURE ||
        dos->e_lfanew <= 0) {
        Fail("invalid DOS header");
    }
    const auto *nt = At<IMAGE_NT_HEADERS64>(
        file, static_cast<std::size_t>(dos->e_lfanew));
    if (nt == nullptr || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
        nt->OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_DEBUG) {
        Fail("invalid AMD64 PE header");
    }
    const IMAGE_DATA_DIRECTORY debug =
        nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DEBUG];
    if (debug.VirtualAddress == 0 ||
        debug.Size < sizeof(IMAGE_DEBUG_DIRECTORY) ||
        debug.Size % sizeof(IMAGE_DEBUG_DIRECTORY) != 0) {
        Fail("kernel image has no valid debug directory");
    }
    const std::size_t debugOffset = RvaToOffset(
        file, *nt, debug.VirtualAddress, debug.Size);
    const auto *entries = reinterpret_cast<const IMAGE_DEBUG_DIRECTORY *>(
        file.View + debugOffset);
    const std::size_t count = debug.Size / sizeof(*entries);
    for (std::size_t index = 0; index < count; ++index) {
        if (entries[index].Type != IMAGE_DEBUG_TYPE_CODEVIEW ||
            entries[index].SizeOfData < 24 ||
            entries[index].PointerToRawData > file.Size ||
            entries[index].SizeOfData >
                file.Size - entries[index].PointerToRawData) {
            continue;
        }
        const std::uint8_t *cv = file.View + entries[index].PointerToRawData;
        if (std::memcmp(cv, "RSDS", 4) != 0) continue;
        ImageIdentity identity{};
        identity.Timestamp = nt->FileHeader.TimeDateStamp;
        identity.ImageSize = nt->OptionalHeader.SizeOfImage;
        std::memcpy(&identity.PdbGuid, cv + 4, sizeof(identity.PdbGuid));
        std::memcpy(&identity.PdbAge, cv + 20, sizeof(identity.PdbAge));
        if (identity.ImageSize == 0 || identity.PdbAge == 0)
            Fail("invalid PE CodeView identity");
        return identity;
    }
    Fail("kernel image has no RSDS CodeView record");
}

bool GuidEqual(const GUID &left, const GUID &right)
{
    return std::memcmp(&left, &right, sizeof(GUID)) == 0;
}

std::uint32_t CheckedU32(ULONG64 value, const char *name)
{
    if (value == 0 || value > std::numeric_limits<std::uint32_t>::max())
        Fail(std::string("invalid or oversized ") + name);
    return static_cast<std::uint32_t>(value);
}

std::uint32_t FindMember(HANDLE process, DWORD64 moduleBase,
                         ULONG typeIndex, const wchar_t *memberName,
                         ULONG *memberType)
{
    DWORD childCount = 0;
    if (!SymGetTypeInfo(process, moduleBase, typeIndex,
                        TI_GET_CHILDRENCOUNT, &childCount) || childCount == 0) {
        Fail("type has no children");
    }
    const std::size_t bytes = sizeof(TI_FINDCHILDREN_PARAMS) +
        (static_cast<std::size_t>(childCount) - 1) * sizeof(ULONG);
    std::vector<std::uint8_t> storage(bytes);
    auto *children = reinterpret_cast<TI_FINDCHILDREN_PARAMS *>(storage.data());
    children->Count = childCount;
    children->Start = 0;
    if (!SymGetTypeInfo(process, moduleBase, typeIndex,
                        TI_FINDCHILDREN, children)) {
        Fail("cannot enumerate type children");
    }
    for (DWORD index = 0; index < childCount; ++index) {
        PWSTR rawName = nullptr;
        if (!SymGetTypeInfo(process, moduleBase, children->ChildId[index],
                            TI_GET_SYMNAME, &rawName) || rawName == nullptr) {
            continue;
        }
        const bool matches = std::wcscmp(rawName, memberName) == 0;
        LocalFree(rawName);
        if (!matches) continue;
        DWORD offset = 0;
        if (!SymGetTypeInfo(process, moduleBase, children->ChildId[index],
                            TI_GET_OFFSET, &offset)) {
            Fail("cannot read member offset");
        }
        if (memberType != nullptr &&
            !SymGetTypeInfo(process, moduleBase, children->ChildId[index],
                            TI_GET_TYPEID, memberType)) {
            Fail("cannot read member type");
        }
        return offset;
    }
    std::wcerr << L"required PDB member missing: " << memberName << L"\n";
    Fail("required PDB member missing");
}

ULONG FindType(HANDLE process, DWORD64 moduleBase, const char *name,
               std::uint32_t *length)
{
    std::array<std::uint8_t, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> storage{};
    auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage.data());
    symbol->SizeOfStruct = sizeof(*symbol);
    symbol->MaxNameLen = MAX_SYM_NAME;
    if (!SymGetTypeFromName(process, moduleBase, name, symbol))
        Fail(std::string("required PDB type missing: ") + name);
    ULONG64 typeLength = 0;
    if (!SymGetTypeInfo(process, moduleBase, symbol->TypeIndex,
                        TI_GET_LENGTH, &typeLength)) {
        Fail(std::string("cannot read type size: ") + name);
    }
    *length = CheckedU32(typeLength, name);
    return symbol->TypeIndex;
}

ExtractedProfile ExtractDbgHelp(const std::filesystem::path &image,
                                const std::filesystem::path &pdb)
{
    ExtractedProfile profile{};
    profile.Identity = ReadImageIdentity(image);
    const HANDLE process = GetCurrentProcess();
    if (!SymInitializeW(process, pdb.parent_path().c_str(), FALSE))
        Fail("SymInitialize failed");
    struct Cleanup {
        HANDLE Process;
        ~Cleanup() { SymCleanup(Process); }
    } cleanup{process};
    SymSetOptions(SYMOPT_EXACT_SYMBOLS | SYMOPT_FAIL_CRITICAL_ERRORS |
                  SYMOPT_UNDNAME);
    constexpr DWORD64 requestedBase = UINT64_C(0x180000000);
    const DWORD64 moduleBase = SymLoadModuleExW(
        process, nullptr, image.c_str(), L"nt", requestedBase,
        profile.Identity.ImageSize, nullptr, 0);
    if (moduleBase == 0) Fail("SymLoadModuleEx failed");

    IMAGEHLP_MODULEW64 module{};
    module.SizeOfStruct = sizeof(module);
    if (!SymGetModuleInfoW64(process, moduleBase, &module) ||
        module.PdbAge != profile.Identity.PdbAge ||
        !GuidEqual(module.PdbSig70, profile.Identity.PdbGuid)) {
        Fail("loaded PDB GUID/age does not match the PE CodeView record");
    }
    std::error_code loadedError;
    const std::filesystem::path loadedPdb =
        std::filesystem::weakly_canonical(module.LoadedPdbName, loadedError);
    std::error_code requestedError;
    const std::filesystem::path requestedPdb =
        std::filesystem::weakly_canonical(pdb, requestedError);
    if (loadedError || requestedError || loadedPdb.empty() ||
        requestedPdb.empty() ||
        _wcsicmp(loadedPdb.c_str(), requestedPdb.c_str()) != 0) {
        Fail("DbgHelp did not load the explicitly requested PDB");
    }

    std::uint32_t ignoredSize = 0;
    const ULONG eprocess = FindType(process, moduleBase, "_EPROCESS",
                                    &profile.EprocessSize);
    const ULONG kprocess = FindType(process, moduleBase, "_KPROCESS",
                                    &ignoredSize);
    profile.EprocessPcb = FindMember(
        process, moduleBase, eprocess, L"Pcb", nullptr);
    profile.EprocessActiveProcessLinks = FindMember(
        process, moduleBase, eprocess, L"ActiveProcessLinks", nullptr);
    profile.EprocessUniqueProcessId = FindMember(
        process, moduleBase, eprocess, L"UniqueProcessId", nullptr);
    ULONG imageNameType = 0;
    profile.EprocessImageFileName = FindMember(
        process, moduleBase, eprocess, L"ImageFileName", &imageNameType);
    ULONG64 imageNameLength = 0;
    if (!SymGetTypeInfo(process, moduleBase, imageNameType,
                        TI_GET_LENGTH, &imageNameLength)) {
        Fail("cannot read ImageFileName array size");
    }
    profile.EprocessImageFileNameSize =
        CheckedU32(imageNameLength, "_EPROCESS.ImageFileName");
    profile.KprocessDirectoryTableBase = FindMember(
        process, moduleBase, kprocess, L"DirectoryTableBase", nullptr);
    const std::uint64_t combined =
        static_cast<std::uint64_t>(profile.EprocessPcb) +
        profile.KprocessDirectoryTableBase;
    if (combined > std::numeric_limits<std::uint32_t>::max())
        Fail("combined DirectoryTableBase offset overflows");
    profile.EprocessDirectoryTableBase = static_cast<std::uint32_t>(combined);

    std::array<std::uint8_t, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> storage{};
    auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage.data());
    symbol->SizeOfStruct = sizeof(*symbol);
    symbol->MaxNameLen = MAX_SYM_NAME;
    if (!SymFromName(process, "nt!PsActiveProcessHead", symbol) ||
        symbol->Address <= symbol->ModBase || symbol->ModBase != moduleBase ||
        symbol->Address - symbol->ModBase > UINT32_MAX) {
        Fail("cannot resolve PsActiveProcessHead RVA");
    }
    profile.PsActiveProcessHeadRva =
        static_cast<std::uint32_t>(symbol->Address - symbol->ModBase);

    if (profile.EprocessImageFileNameSize != 15 ||
        static_cast<std::uint64_t>(profile.EprocessActiveProcessLinks) + 16 >
            profile.EprocessSize ||
        static_cast<std::uint64_t>(profile.EprocessUniqueProcessId) + 8 >
            profile.EprocessSize ||
        static_cast<std::uint64_t>(profile.EprocessImageFileName) + 15 >
            profile.EprocessSize ||
        static_cast<std::uint64_t>(profile.EprocessDirectoryTableBase) + 8 >
            profile.EprocessSize ||
        static_cast<std::uint64_t>(profile.PsActiveProcessHeadRva) + 16 >
            profile.Identity.ImageSize) {
        Fail("extracted kernel layout violates required invariants");
    }
    return profile;
}

#if HYPERSINV_HAVE_DIA
template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr &) = delete;
    ComPtr &operator=(const ComPtr &) = delete;
    ComPtr(ComPtr &&other) noexcept : value_(other.value_)
    {
        other.value_ = nullptr;
    }
    ComPtr &operator=(ComPtr &&other) noexcept
    {
        if (this == &other) return *this;
        Reset();
        value_ = other.value_;
        other.value_ = nullptr;
        return *this;
    }
    ~ComPtr() { Reset(); }

    T *operator->() const { return value_; }
    T *Get() const { return value_; }
    T **Put()
    {
        Reset();
        return &value_;
    }
    void Reset()
    {
        if (value_ != nullptr) value_->Release();
        value_ = nullptr;
    }

private:
    T *value_ = nullptr;
};

class ComApartment {
public:
    ComApartment()
    {
        result_ = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(result_) && result_ != RPC_E_CHANGED_MODE)
            Fail("DIA COM initialization failed");
    }
    ~ComApartment()
    {
        if (result_ == S_OK || result_ == S_FALSE) CoUninitialize();
    }

private:
    HRESULT result_ = E_FAIL;
};

class DiaLibrary {
public:
    DiaLibrary() = default;
    DiaLibrary(const DiaLibrary &) = delete;
    DiaLibrary &operator=(const DiaLibrary &) = delete;
    ~DiaLibrary()
    {
        if (module_ != nullptr) FreeLibrary(module_);
    }

    void CreateDataSource(ComPtr<IDiaDataSource> *source)
    {
        if (source == nullptr) Fail("invalid DIA data-source output");
        HRESULT result = CoCreateInstance(
            __uuidof(DiaSource), nullptr, CLSCTX_INPROC_SERVER,
            __uuidof(IDiaDataSource),
            reinterpret_cast<void **>(source->Put()));
        if (SUCCEEDED(result) && source->Get() != nullptr) return;

        module_ = LoadLibraryW(HYPERSINV_DIA_DLL_PATH);
        if (module_ == nullptr) Fail("DIA DLL is unavailable");
        using DllGetClassObjectFn = HRESULT (STDAPICALLTYPE *)(
            REFCLSID, REFIID, void **);
        const auto getClassObject = reinterpret_cast<DllGetClassObjectFn>(
            GetProcAddress(module_, "DllGetClassObject"));
        if (getClassObject == nullptr)
            Fail("DIA DLL has no class factory export");
        ComPtr<IClassFactory> factory;
        result = getClassObject(
            __uuidof(DiaSource), __uuidof(IClassFactory),
            reinterpret_cast<void **>(factory.Put()));
        if (FAILED(result) || factory.Get() == nullptr)
            Fail("DIA class factory is unavailable");
        result = factory->CreateInstance(
            nullptr, __uuidof(IDiaDataSource),
            reinterpret_cast<void **>(source->Put()));
        if (FAILED(result) || source->Get() == nullptr)
            Fail("DIA data source is unavailable");
    }

private:
    HMODULE module_ = nullptr;
};

ComPtr<IDiaSymbol> DiaFindChild(IDiaSymbol *parent,
                                enum SymTagEnum tag,
                                const wchar_t *name)
{
    if (parent == nullptr || name == nullptr) Fail("invalid DIA query");
    ComPtr<IDiaEnumSymbols> matches;
    if (FAILED(parent->findChildren(tag, name, nsCaseSensitive,
                                    matches.Put())) ||
        matches.Get() == nullptr) {
        Fail("DIA cannot enumerate requested symbol");
    }
    ComPtr<IDiaSymbol> symbol;
    ULONG fetched = 0;
    if (matches->Next(1, symbol.Put(), &fetched) != S_OK || fetched != 1 ||
        symbol.Get() == nullptr) {
        Fail("required DIA symbol missing");
    }
    return symbol;
}

std::uint32_t DiaTypeLength(IDiaSymbol *symbol, const char *name)
{
    ULONGLONG length = 0;
    if (symbol == nullptr || FAILED(symbol->get_length(&length)))
        Fail(std::string("DIA cannot read type size: ") + name);
    return CheckedU32(length, name);
}

std::uint32_t DiaMember(IDiaSymbol *type,
                        const wchar_t *memberName,
                        ComPtr<IDiaSymbol> *memberType)
{
    ComPtr<IDiaSymbol> member = DiaFindChild(type, SymTagData, memberName);
    LONG offset = -1;
    if (FAILED(member->get_offset(&offset)) || offset < 0)
        Fail("DIA cannot read member offset");
    if (memberType != nullptr) {
        if (FAILED(member->get_type(memberType->Put())) ||
            memberType->Get() == nullptr) {
            Fail("DIA cannot read member type");
        }
    }
    return static_cast<std::uint32_t>(offset);
}

void ValidateExtractedProfile(const ExtractedProfile &profile)
{
    if (profile.EprocessImageFileNameSize != 15 ||
        static_cast<std::uint64_t>(profile.EprocessActiveProcessLinks) + 16 >
            profile.EprocessSize ||
        static_cast<std::uint64_t>(profile.EprocessUniqueProcessId) + 8 >
            profile.EprocessSize ||
        static_cast<std::uint64_t>(profile.EprocessImageFileName) + 15 >
            profile.EprocessSize ||
        static_cast<std::uint64_t>(profile.EprocessDirectoryTableBase) + 8 >
            profile.EprocessSize ||
        static_cast<std::uint64_t>(profile.PsActiveProcessHeadRva) + 16 >
            profile.Identity.ImageSize) {
        Fail("extracted kernel layout violates required invariants");
    }
}

ExtractedProfile ExtractDia(const std::filesystem::path &image,
                            const std::filesystem::path &pdb)
{
    ExtractedProfile profile{};
    profile.Identity = ReadImageIdentity(image);
    ComApartment apartment;
    DiaLibrary diaLibrary;
    ComPtr<IDiaDataSource> source;
    diaLibrary.CreateDataSource(&source);
    if (FAILED(source->loadDataFromPdb(pdb.c_str())))
        Fail("DIA cannot load the requested PDB");
    ComPtr<IDiaSession> session;
    if (FAILED(source->openSession(session.Put())) || session.Get() == nullptr)
        Fail("DIA cannot open a PDB session");
    ComPtr<IDiaSymbol> global;
    if (FAILED(session->get_globalScope(global.Put())) ||
        global.Get() == nullptr) {
        Fail("DIA cannot read the PDB global scope");
    }
    GUID pdbGuid{};
    DWORD pdbAge = 0;
    if (FAILED(global->get_guid(&pdbGuid)) ||
        FAILED(global->get_age(&pdbAge)) ||
        !GuidEqual(pdbGuid, profile.Identity.PdbGuid) ||
        pdbAge != profile.Identity.PdbAge) {
        Fail("DIA PDB GUID/age does not match the PE CodeView record");
    }

    ComPtr<IDiaSymbol> eprocess =
        DiaFindChild(global.Get(), SymTagUDT, L"_EPROCESS");
    ComPtr<IDiaSymbol> kprocess =
        DiaFindChild(global.Get(), SymTagUDT, L"_KPROCESS");
    profile.EprocessSize = DiaTypeLength(eprocess.Get(), "_EPROCESS");
    profile.EprocessPcb = DiaMember(eprocess.Get(), L"Pcb", nullptr);
    profile.EprocessActiveProcessLinks = DiaMember(
        eprocess.Get(), L"ActiveProcessLinks", nullptr);
    profile.EprocessUniqueProcessId = DiaMember(
        eprocess.Get(), L"UniqueProcessId", nullptr);
    ComPtr<IDiaSymbol> imageNameType;
    profile.EprocessImageFileName = DiaMember(
        eprocess.Get(), L"ImageFileName", &imageNameType);
    profile.EprocessImageFileNameSize = DiaTypeLength(
        imageNameType.Get(), "_EPROCESS.ImageFileName");
    profile.KprocessDirectoryTableBase = DiaMember(
        kprocess.Get(), L"DirectoryTableBase", nullptr);
    const std::uint64_t combined =
        static_cast<std::uint64_t>(profile.EprocessPcb) +
        profile.KprocessDirectoryTableBase;
    if (combined > std::numeric_limits<std::uint32_t>::max())
        Fail("combined DirectoryTableBase offset overflows");
    profile.EprocessDirectoryTableBase = static_cast<std::uint32_t>(combined);

    ComPtr<IDiaSymbol> activeHead;
    try {
        activeHead = DiaFindChild(
            global.Get(), SymTagData, L"PsActiveProcessHead");
    } catch (const std::exception &) {
        activeHead = DiaFindChild(
            global.Get(), SymTagPublicSymbol, L"PsActiveProcessHead");
    }
    DWORD rva = 0;
    if (FAILED(activeHead->get_relativeVirtualAddress(&rva)) || rva == 0)
        Fail("DIA cannot resolve PsActiveProcessHead RVA");
    profile.PsActiveProcessHeadRva = rva;
    ValidateExtractedProfile(profile);
    return profile;
}
#endif

std::string GuidText(const GUID &guid)
{
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::uppercase
        << std::setw(8) << guid.Data1 << '-'
        << std::setw(4) << guid.Data2 << '-'
        << std::setw(4) << guid.Data3 << '-';
    for (int index = 0; index < 2; ++index)
        out << std::setw(2) << static_cast<unsigned>(guid.Data4[index]);
    out << '-';
    for (int index = 2; index < 8; ++index)
        out << std::setw(2) << static_cast<unsigned>(guid.Data4[index]);
    return out.str();
}

void WriteHeader(const std::filesystem::path &path,
                 const ExtractedProfile &p,
                 std::uint32_t build, std::uint32_t ubr)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) Fail("cannot create generated header");
    const auto *guid = reinterpret_cast<const std::uint8_t *>(&p.Identity.PdbGuid);
    out << "#pragma once\n\n#include \"kernel_profile.h\"\n\n"
           "namespace hypersinv::windows {\n"
           "inline constexpr KernelProfile kKernelProfile{\n"
        << "    1u, " << build << "u, " << ubr << "u, 0x"
        << std::hex << std::uppercase << p.Identity.Timestamp << "u, 0x"
        << p.Identity.ImageSize << "u,\n    {";
    for (std::size_t index = 0; index < 16; ++index) {
        if (index != 0) out << ", ";
        out << "0x" << std::setw(2) << std::setfill('0')
            << static_cast<unsigned>(guid[index]);
    }
    out << "}, " << std::dec << p.Identity.PdbAge << "u,\n"
        << "    0x" << std::hex << p.EprocessSize << "u, 0x"
        << p.EprocessPcb << "u, 0x" << p.EprocessActiveProcessLinks
        << "u, 0x" << p.EprocessUniqueProcessId << "u, 0x"
        << p.EprocessImageFileName << "u, " << std::dec
        << p.EprocessImageFileNameSize << "u,\n"
        << "    0x" << std::hex << p.KprocessDirectoryTableBase
        << "u, 0x" << p.EprocessDirectoryTableBase << "u, 0x"
        << p.PsActiveProcessHeadRva << "u\n};\n"
           "static_assert(kKernelProfile.SchemaVersion == 1u);\n"
           "static_assert(kKernelProfile.EprocessImageFileNameSize == 15u);\n"
           "} // namespace hypersinv::windows\n";
    if (!out) Fail("cannot write generated header");
}

void WriteJson(const std::filesystem::path &path,
               const ExtractedProfile &p,
               std::uint32_t build, std::uint32_t ubr)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) Fail("cannot create profile manifest");
    out << "{\n"
        << "  \"schemaVersion\": 1,\n"
        << "  \"buildNumber\": " << build << ",\n"
        << "  \"ubr\": " << ubr << ",\n"
        << "  \"imageTimestamp\": " << p.Identity.Timestamp << ",\n"
        << "  \"imageSize\": " << p.Identity.ImageSize << ",\n"
        << "  \"pdbGuid\": \"" << GuidText(p.Identity.PdbGuid) << "\",\n"
        << "  \"pdbAge\": " << p.Identity.PdbAge << ",\n"
        << "  \"offsets\": {\n"
        << "    \"eprocessSize\": " << p.EprocessSize << ",\n"
        << "    \"eprocessPcb\": " << p.EprocessPcb << ",\n"
        << "    \"activeProcessLinks\": "
        << p.EprocessActiveProcessLinks << ",\n"
        << "    \"uniqueProcessId\": " << p.EprocessUniqueProcessId << ",\n"
        << "    \"imageFileName\": " << p.EprocessImageFileName << ",\n"
        << "    \"imageFileNameSize\": "
        << p.EprocessImageFileNameSize << ",\n"
        << "    \"kprocessDirectoryTableBase\": "
        << p.KprocessDirectoryTableBase << ",\n"
        << "    \"eprocessDirectoryTableBase\": "
        << p.EprocessDirectoryTableBase << ",\n"
        << "    \"psActiveProcessHeadRva\": "
        << p.PsActiveProcessHeadRva << "\n"
        << "  }\n}\n";
    if (!out) Fail("cannot write profile manifest");
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    try {
        if ((argc != 11 && argc != 13) ||
            std::wcscmp(argv[1], L"--image") != 0 ||
            std::wcscmp(argv[3], L"--pdb") != 0 ||
            std::wcscmp(argv[5], L"--build") != 0 ||
            std::wcscmp(argv[7], L"--ubr") != 0 ||
            std::wcscmp(argv[9], L"--out-dir") != 0 ||
            (argc == 13 && std::wcscmp(argv[11], L"--backend") != 0)) {
            std::cerr << "usage: kernel_profile_generator --image <ntoskrnl.exe> "
                         "--pdb <ntkrnlmp.pdb> --build <n> --ubr <n> "
                         "--out-dir <directory> "
                         "[--backend auto|dbghelp|dia]\n";
            return 2;
        }
        const std::filesystem::path image = argv[2];
        const std::filesystem::path pdb = argv[4];
        const std::uint32_t build = ParseU32(argv[6], "build number");
        const std::uint32_t ubr = ParseU32(argv[8], "UBR");
        const std::filesystem::path output = argv[10];
        const std::wstring backend = argc == 13 ? argv[12] : L"auto";
        if (backend != L"auto" && backend != L"dbghelp" &&
            backend != L"dia") {
            Fail("invalid symbol backend");
        }
        if (!std::filesystem::is_regular_file(image) ||
            !std::filesystem::is_regular_file(pdb) || build == 0) {
            Fail("kernel image, PDB, or build number is invalid");
        }
        std::filesystem::create_directories(output);
        ExtractedProfile profile{};
        if (backend == L"dbghelp") {
            profile = ExtractDbgHelp(image, pdb);
        } else if (backend == L"dia") {
#if HYPERSINV_HAVE_DIA
            profile = ExtractDia(image, pdb);
#else
            Fail("DIA backend was requested but the DIA SDK is unavailable");
#endif
        } else {
            try {
                profile = ExtractDbgHelp(image, pdb);
            } catch (const std::exception &dbghelpError) {
#if HYPERSINV_HAVE_DIA
                std::cerr << "DbgHelp extraction failed; trying DIA: "
                          << dbghelpError.what() << '\n';
                try {
                    profile = ExtractDia(image, pdb);
                } catch (const std::exception &diaError) {
                    Fail(std::string("DbgHelp failed: ") +
                         dbghelpError.what() + "; DIA failed: " +
                         diaError.what());
                }
#else
                (void)dbghelpError;
                throw;
#endif
            }
        }
        WriteHeader(output / "kernel_profile_gen.h", profile, build, ubr);
        WriteJson(output / "kernel_profile_manifest.json", profile, build, ubr);
        std::cout << "generated pinned kernel profile for " << build << '.'
                  << ubr << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "kernel profile generation failed: " << error.what()
                  << '\n';
        return 1;
    }
}
