/* Signed SBAT metadata for shim revocation checks. */
#if defined(_MSC_VER)
#pragma section(".sbat", read)
#define IB_SBAT_SECTION __declspec(allocate(".sbat"))
#else
#define IB_SBAT_SECTION __attribute__((section(".sbat"), used))
#endif

IB_SBAT_SECTION const char g_hypersinv_loader_sbat[] =
    "sbat,1,SBAT Version,sbat,1,https://github.com/rhboot/shim/blob/main/SBAT.md\n"
    "hypersinv.loader,1,Hypersinv,loader,0.1.0,urn:hypersinv:loader\n";
