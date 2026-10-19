#include <stdio.h>
#include <string.h>

#include "bootnext.h"

static int g_failures;
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++g_failures; } } while (0)

typedef struct {
    int present;
    unsigned char data[512];
    size_t size;
    unsigned long attributes;
} variable_t;
typedef struct {
    variable_t bootnext;
    variable_t entries[4];
    int fail_bootnext_write;
    int fail_entry_delete;
    int fail_bootnext_delete;
    int mismatch_entry_readback;
    int mismatch_bootnext_readback;
    int mismatch_entry_attributes;
    int mismatch_bootnext_attributes;
    int bootorder_writes;
    int write_sequence;
    int entry_write_sequence;
    int bootnext_write_sequence;
    int delete_count;
    int delete_kind[8];
} fake_vars_t;

static int entry_index(const wchar_t *name)
{
    if (wcsncmp(name, L"Boot", 4u) != 0 || wcslen(name) != 8u) return -1;
    if (name[4] == L'0' && name[5] == L'0' && name[6] == L'0' &&
        name[7] >= L'0' && name[7] <= L'3') return name[7] - L'0';
    return -1;
}

static int fake_read(const wchar_t *name, unsigned char *data, size_t *size,
                     unsigned long *attributes, void *context)
{
    fake_vars_t *vars = (fake_vars_t *)context;
    variable_t *variable;
    int index;
    if (wcscmp(name, L"BootNext") == 0) variable = &vars->bootnext;
    else { index = entry_index(name); if (index < 0) { *size = 0u; return 0; } variable = &vars->entries[index]; }
    if (!variable->present) { *size = 0u; return 0; }
    if (*size < variable->size) return -1;
    memcpy(data, variable->data, variable->size); *size = variable->size;
    *attributes = variable->attributes;
    if (wcscmp(name, L"BootNext") == 0) {
        if (vars->mismatch_bootnext_readback && *size != 0u) data[0] ^= 0x80u;
        if (vars->mismatch_bootnext_attributes) *attributes ^= 0x100u;
    } else {
        if (vars->mismatch_entry_readback && *size != 0u) data[0] ^= 0x80u;
        if (vars->mismatch_entry_attributes) *attributes ^= 0x100u;
    }
    return 1;
}

static int fake_write(const wchar_t *name, const unsigned char *data, size_t size,
                      unsigned long attributes, void *context)
{
    fake_vars_t *vars = (fake_vars_t *)context;
    variable_t *variable;
    int index;
    CHECK(attributes == IB_BOOTNEXT_VARIABLE_ATTRIBUTES);
    ++vars->write_sequence;
    if (wcscmp(name, L"BootOrder") == 0) { ++vars->bootorder_writes; return 0; }
    if (wcscmp(name, L"BootNext") == 0) {
        if (vars->fail_bootnext_write && size != 0u) return 0;
        if (size == 0u && vars->fail_bootnext_delete) return 0;
        if (size != 0u) vars->bootnext_write_sequence = vars->write_sequence;
        variable = &vars->bootnext;
    } else { index = entry_index(name); if (index < 0) return 0;
        if (size == 0u && vars->fail_entry_delete) return 0;
        if (size != 0u) vars->entry_write_sequence = vars->write_sequence;
        variable = &vars->entries[index]; }
    if (size == 0u) {
        if (vars->delete_count < (int)(sizeof(vars->delete_kind) / sizeof(vars->delete_kind[0]))) {
            vars->delete_kind[vars->delete_count++] = wcscmp(name, L"BootNext") == 0 ? 2 : 1;
        }
        memset(variable, 0, sizeof(*variable)); return 1;
    }
    if (size > sizeof(variable->data)) return 0;
    variable->present = 1; variable->size = size;
    variable->attributes = attributes;
    memcpy(variable->data, data, size); return 1;
}

static ib_bootnext_partition_t partition(unsigned char seed)
{
    ib_bootnext_partition_t p; size_t i;
    memset(&p, 0, sizeof(p)); p.partition_number = 1u; p.partition_start = 2048u;
    p.partition_size = 409600u;
    for (i = 0; i < 16u; ++i) p.partition_guid[i] = (unsigned char)(seed + i);
    return p;
}

static ib_bootnext_request_t request_for(fake_vars_t *vars,
                                         ib_bootnext_variable_ops_t *ops,
                                         ib_bootnext_partition_t p)
{
    ib_bootnext_request_t request;
    ops->read_variable = fake_read; ops->write_variable = fake_write; ops->context = vars;
    request.partition = p; request.ops = ops; return request;
}

static int contains_utf16_ascii(const unsigned char *data, size_t size,
                                const char *text)
{
    size_t i, j, length = strlen(text);
    if (length == 0u || size < length * 2u) return 0;
    for (i = 0u; i + length * 2u <= size; ++i) {
        for (j = 0u; j < length; ++j) {
            if (data[i + j * 2u] != (unsigned char)text[j] ||
                data[i + j * 2u + 1u] != 0u) break;
        }
        if (j == length) return 1;
    }
    return 0;
}

static void serialization_bytes(void)
{
    ib_bootnext_partition_t p = partition(0x10u);
    unsigned char bytes[512]; size_t size = sizeof(bytes);
    CHECK(ib_bootnext_serialize(&p, bytes, &size));
    CHECK(bytes[0] == 1u && bytes[1] == 0u && bytes[4] != 0u);
    CHECK(bytes[size - 4u] == 0x7fu && bytes[size - 3u] == 0xffu);
    CHECK(bytes[50] == 0x04u && bytes[51] == 0x01u);
    CHECK((unsigned short)(bytes[4] | ((unsigned short)bytes[5] << 8)) ==
          (unsigned short)(size - 50u));
    CHECK(bytes[54] == 1u && bytes[58] == 0x00u && bytes[59] == 0x08u);
    CHECK(bytes[90] == 0x02u && bytes[91] == 0x02u);
    CHECK(bytes[92] == 0x04u && bytes[93] == 0x04u);
    CHECK(bytes[96] == '\\' && bytes[97] == 0u && bytes[98] == 'E' && bytes[99] == 0u);
    CHECK(ib_bootnext_is_owned(bytes, size, &p));
    bytes[size - 1u] ^= 1u; CHECK(!ib_bootnext_is_owned(bytes, size, &p));

    size = sizeof(bytes);
    CHECK(ib_bootnext_serialize_profile(&p, IB_BOOTNEXT_PROFILE_INVENTORY,
                                        bytes, &size));
    CHECK(contains_utf16_ascii(bytes, size, "Hypersinv EFI Inventory"));
    CHECK(contains_utf16_ascii(bytes, size,
                               "\\EFI\\HypersinvInventory\\loader.efi"));
    CHECK(ib_bootnext_is_owned_profile(bytes, size, &p,
                                       IB_BOOTNEXT_PROFILE_INVENTORY));
    CHECK(!ib_bootnext_is_owned(bytes, size, &p));
}

static void profiles_cannot_delete_each_others_entries(void)
{
    fake_vars_t vars; ib_bootnext_variable_ops_t ops; ib_bootnext_request_t request;
    unsigned short number = 0u; wchar_t error[256] = L"";

    memset(&vars, 0, sizeof(vars));
    request = request_for(&vars, &ops, partition(0x31u));
    CHECK(ib_bootnext_set_profile(&request, IB_BOOTNEXT_PROFILE_INVENTORY,
                                  &number, error, 256u));
    CHECK(!ib_bootnext_clear_owned(&request, error, 256u));
    CHECK(vars.bootnext.present && vars.entries[number].present);
    CHECK(ib_bootnext_clear_owned_profile(&request, IB_BOOTNEXT_PROFILE_INVENTORY,
                                          error, 256u));
    CHECK(!vars.bootnext.present && !vars.entries[number].present);

    memset(&vars, 0, sizeof(vars));
    request = request_for(&vars, &ops, partition(0x33u));
    CHECK(ib_bootnext_set_profile(&request, IB_BOOTNEXT_PROFILE_INVENTORY,
                                  &number, error, 256u));
    memset(&vars.bootnext, 0, sizeof(vars.bootnext));
    CHECK(!ib_bootnext_clear_owned(&request, error, 256u));
    CHECK(vars.entries[number].present);
    CHECK(ib_bootnext_clear_owned_profile(&request, IB_BOOTNEXT_PROFILE_INVENTORY,
                                          error, 256u));
    CHECK(!vars.entries[number].present);

    memset(&vars, 0, sizeof(vars));
    request = request_for(&vars, &ops, partition(0x32u));
    CHECK(ib_bootnext_set(&request, &number, error, 256u));
    CHECK(!ib_bootnext_clear_owned_profile(&request, IB_BOOTNEXT_PROFILE_INVENTORY,
                                           error, 256u));
    CHECK(vars.bootnext.present && vars.entries[number].present);
    CHECK(ib_bootnext_clear_owned(&request, error, 256u));
    CHECK(!vars.bootnext.present && !vars.entries[number].present);

    memset(&vars, 0, sizeof(vars));
    request = request_for(&vars, &ops, partition(0x34u));
    CHECK(ib_bootnext_set(&request, &number, error, 256u));
    memset(&vars.bootnext, 0, sizeof(vars.bootnext));
    CHECK(!ib_bootnext_clear_owned_profile(&request, IB_BOOTNEXT_PROFILE_INVENTORY,
                                           error, 256u));
    CHECK(vars.entries[number].present);
    CHECK(ib_bootnext_clear_owned(&request, error, 256u));
    CHECK(!vars.entries[number].present);
}

static void shim_inventory_profile_uses_its_owned_boot_path(void)
{
    fake_vars_t vars; ib_bootnext_variable_ops_t ops; ib_bootnext_request_t request;
    ib_bootnext_partition_t p = partition(0x41u);
    unsigned char bytes[512]; size_t size = sizeof(bytes);
    unsigned short number = 0u; wchar_t error[256] = L"";

    CHECK(ib_bootnext_serialize_profile(&p, IB_BOOTNEXT_PROFILE_SHIM_INVENTORY,
                                        bytes, &size));
    CHECK(contains_utf16_ascii(bytes, size, "Hypersinv Shim Inventory"));
    CHECK(contains_utf16_ascii(bytes, size,
          "\\EFI\\HypersinvShimInventory\\shimx64.efi"));
    CHECK(ib_bootnext_is_owned_profile(bytes, size, &p,
                                       IB_BOOTNEXT_PROFILE_SHIM_INVENTORY));
    CHECK(!ib_bootnext_is_owned_profile(bytes, size, &p,
                                        IB_BOOTNEXT_PROFILE_INVENTORY));
    CHECK(!ib_bootnext_is_owned(bytes, size, &p));

    memset(&vars, 0, sizeof(vars));
    request = request_for(&vars, &ops, p);
    CHECK(ib_bootnext_set_profile(&request, IB_BOOTNEXT_PROFILE_SHIM_INVENTORY,
                                  &number, error, sizeof(error) / sizeof(error[0])));
    CHECK(vars.bootnext.present && vars.entries[number].present);
    CHECK(vars.bootorder_writes == 0);
    CHECK(!ib_bootnext_clear_owned_profile(&request,
          IB_BOOTNEXT_PROFILE_INVENTORY, error, sizeof(error) / sizeof(error[0])));
    CHECK(vars.bootnext.present && vars.entries[number].present);
    CHECK(ib_bootnext_clear_owned_profile(&request,
          IB_BOOTNEXT_PROFILE_SHIM_INVENTORY, error, sizeof(error) / sizeof(error[0])));
    CHECK(!vars.bootnext.present && !vars.entries[number].present);
    CHECK(vars.bootorder_writes == 0);

    memset(&vars, 0, sizeof(vars));
    vars.bootnext.present = 1;
    vars.bootnext.size = 2u;
    vars.bootnext.attributes = IB_BOOTNEXT_VARIABLE_ATTRIBUTES;
    vars.bootnext.data[0] = 7u;
    vars.bootnext.data[1] = 0u;
    request = request_for(&vars, &ops, p);
    CHECK(!ib_bootnext_set_profile(&request, IB_BOOTNEXT_PROFILE_SHIM_INVENTORY,
                                   &number, error, sizeof(error) / sizeof(error[0])));
    CHECK(vars.bootnext.present && vars.bootnext.data[0] == 7u);
    CHECK(!vars.entries[0].present && vars.bootorder_writes == 0);
}

static void set_preserves_existing_and_order(void)
{
    fake_vars_t vars; ib_bootnext_variable_ops_t ops; ib_bootnext_request_t request;
    ib_bootnext_partition_t p = partition(1u); unsigned short number = 99u; wchar_t error[256] = L"";
    memset(&vars, 0, sizeof(vars)); vars.entries[0].present = 1; vars.entries[0].size = 1u;
    request = request_for(&vars, &ops, p);
    CHECK(ib_bootnext_set(&request, &number, error, 256u));
    CHECK(number == 1u && vars.entries[0].present && vars.entries[1].present);
    CHECK(vars.bootnext.present && vars.bootnext.size == 2u && vars.bootnext.data[0] == 1u);
    CHECK(vars.entry_write_sequence < vars.bootnext_write_sequence);
    CHECK(vars.bootorder_writes == 0);
    memset(&vars, 0, sizeof(vars)); vars.bootnext.present = 1; vars.bootnext.size = 2u;
    vars.bootnext.data[0] = 3u; request = request_for(&vars, &ops, p);
    CHECK(!ib_bootnext_set(&request, &number, error, 256u));
    CHECK(vars.bootnext.present && !vars.entries[0].present);
}

static void failed_set_cleans_entry(void)
{
    fake_vars_t vars; ib_bootnext_variable_ops_t ops; ib_bootnext_request_t request;
    unsigned short number; wchar_t error[256] = L"";
    memset(&vars, 0, sizeof(vars)); vars.fail_bootnext_write = 1;
    request = request_for(&vars, &ops, partition(2u));
    CHECK(!ib_bootnext_set(&request, &number, error, 256u));
    CHECK(!vars.entries[0].present && !vars.bootnext.present);
}

static void set_readback_mismatch_is_fail_closed(void)
{
    fake_vars_t vars; ib_bootnext_variable_ops_t ops; ib_bootnext_request_t request;
    unsigned short number; wchar_t error[256] = L"";
    memset(&vars, 0, sizeof(vars)); vars.mismatch_entry_readback = 1;
    request = request_for(&vars, &ops, partition(5u));
    CHECK(!ib_bootnext_set(&request, &number, error, 256u));
    CHECK(vars.entries[0].present && !vars.bootnext.present);
    CHECK(vars.delete_count == 0);

    memset(&vars, 0, sizeof(vars)); vars.mismatch_entry_attributes = 1;
    request = request_for(&vars, &ops, partition(6u));
    CHECK(!ib_bootnext_set(&request, &number, error, 256u));
    CHECK(vars.entries[0].present && !vars.bootnext.present);
    CHECK(vars.delete_count == 0);
}

static void bootnext_readback_mismatch_does_not_delete_entry(void)
{
    fake_vars_t vars; ib_bootnext_variable_ops_t ops; ib_bootnext_request_t request;
    unsigned short number; wchar_t error[256] = L"";
    memset(&vars, 0, sizeof(vars)); vars.mismatch_bootnext_readback = 1;
    request = request_for(&vars, &ops, partition(7u));
    CHECK(!ib_bootnext_set(&request, &number, error, 256u));
    CHECK(vars.entries[0].present && vars.bootnext.present);
    CHECK(vars.delete_count == 0);
}

static void clear_is_ownership_safe(void)
{
    fake_vars_t vars; ib_bootnext_variable_ops_t ops; ib_bootnext_request_t request;
    unsigned short number; wchar_t error[256] = L"";
    memset(&vars, 0, sizeof(vars)); request = request_for(&vars, &ops, partition(3u));
    CHECK(ib_bootnext_set(&request, &number, error, 256u));
    CHECK(ib_bootnext_clear_owned(&request, error, 256u));
    CHECK(!vars.bootnext.present && !vars.entries[number].present);
    memset(&vars, 0, sizeof(vars)); request = request_for(&vars, &ops, partition(4u));
    CHECK(ib_bootnext_set(&request, &number, error, 256u));
    vars.bootnext.data[0] = 2u;
    CHECK(!ib_bootnext_clear_owned(&request, error, 256u));
    CHECK(vars.bootnext.present && vars.entries[number].present);

    vars.entries[2].present = 1; vars.entries[2].size = 1u;
    vars.entries[2].data[0] = 0xa5u; vars.entries[2].attributes = IB_BOOTNEXT_VARIABLE_ATTRIBUTES;
    vars.delete_count = 0;
    CHECK(!ib_bootnext_clear_owned(&request, error, 256u));
    CHECK(vars.bootnext.present && vars.entries[number].present && vars.entries[2].present);
    CHECK(vars.delete_count == 0);

    vars.bootnext.present = 0; vars.entries[2].present = 0;
    vars.entries[number].attributes ^= 0x100u;
    vars.delete_count = 0;
    CHECK(!ib_bootnext_clear_owned(&request, error, 256u));
    CHECK(vars.entries[number].present && vars.delete_count == 0);
}

static void clear_uses_recoverable_entry_first_order(void)
{
    fake_vars_t vars; ib_bootnext_variable_ops_t ops; ib_bootnext_request_t request;
    unsigned short number; wchar_t error[256] = L"";
    memset(&vars, 0, sizeof(vars)); request = request_for(&vars, &ops, partition(8u));
    CHECK(ib_bootnext_set(&request, &number, error, 256u));
    vars.fail_entry_delete = 1;
    CHECK(!ib_bootnext_clear_owned(&request, error, 256u));
    CHECK(vars.bootnext.present && vars.entries[number].present);
    CHECK(vars.delete_count == 0);

    vars.fail_entry_delete = 0; vars.fail_bootnext_delete = 1; vars.delete_count = 0;
    CHECK(!ib_bootnext_clear_owned(&request, error, 256u));
    CHECK(vars.bootnext.present && vars.entries[number].present);
    CHECK(vars.delete_count == 1 && vars.delete_kind[0] == 1);
}

static void clear_leaves_unrelated_entries_alone(void)
{
    fake_vars_t vars; ib_bootnext_variable_ops_t ops; ib_bootnext_request_t request;
    ib_bootnext_partition_t foreign = partition(9u);
    unsigned char foreign_value[512]; size_t foreign_size = sizeof(foreign_value);
    unsigned short number; wchar_t error[256] = L"";
    memset(&vars, 0, sizeof(vars)); request = request_for(&vars, &ops, partition(10u));
    CHECK(ib_bootnext_set(&request, &number, error, 256u));
    CHECK(ib_bootnext_serialize(&foreign, foreign_value, &foreign_size));
    vars.entries[1].present = 1; vars.entries[1].size = foreign_size;
    vars.entries[1].attributes = IB_BOOTNEXT_VARIABLE_ATTRIBUTES;
    memcpy(vars.entries[1].data, foreign_value, foreign_size);
    memset(&vars.bootnext, 0, sizeof(vars.bootnext));
    vars.delete_count = 0;
    CHECK(ib_bootnext_clear_owned(&request, error, 256u));
    CHECK(!vars.entries[number].present && vars.entries[1].present);
    CHECK(vars.bootorder_writes == 0 && vars.delete_count == 1 && vars.delete_kind[0] == 1);
}

int main(void)
{
    serialization_bytes(); set_preserves_existing_and_order();
    failed_set_cleans_entry(); set_readback_mismatch_is_fail_closed();
    bootnext_readback_mismatch_does_not_delete_entry(); clear_is_ownership_safe();
    clear_uses_recoverable_entry_first_order(); clear_leaves_unrelated_entries_alone();
    profiles_cannot_delete_each_others_entries();
    shim_inventory_profile_uses_its_owned_boot_path();
    if (g_failures != 0) { fprintf(stderr, "%d BootNext tests failed\n", g_failures); return 1; }
    puts("install BootNext tests passed"); return 0;
}
