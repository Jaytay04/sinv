/* Signed SBAT metadata for shim revocation checks. */
#if defined(_MSC_VER)
#pragma section(".sbat", read)
#define IB_SBAT_SECTION __declspec(allocate(".sbat"))
#else
#define IB_SBAT_SECTION __attribute__((section(".sbat"), used))
#endif

IB_SBAT_SECTION const char g_hypersinv_inventory_sbat[] =
    "sbat,1,SBAT Version,sbat,1,https://github.com/rhboot/shim/blob/main/SBAT.md\n"
    "hypersinv.inventory,1,Hypersinv,inventory,1.0.0,urn:hypersinv:inventory\n";

/* Keep a DIR64 relocation even when all generated code is RIP-relative. */
void *g_inventory_image_relocation_anchor = &g_inventory_image_relocation_anchor;
