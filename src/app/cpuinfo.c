__asm__(".code16gcc\n");
#include "apps.h"

void cmd_cpuinfo(const char* args) {
    uint32_t has_cpuid = 0;

    // --- Probe CPUID by testing EFLAGS bit 21 (the ID flag) ---
    // On CPUs that support CPUID, bit 21 of EFLAGS is writable.
    // We toggle it, read back, and check if it stuck.
    __asm__ __volatile__ (
        "pushfl\n\t"
        "popl %%eax\n\t"
        "movl %%eax, %%ebx\n\t"
        "xorl %1, %%eax\n\t"
        "pushl %%eax\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %%eax\n\t"
        "cmpl %%ebx, %%eax\n\t"
        "je 1f\n\t"
        "movl $1, %0\n\t"
        "1:\n\t"
        : "=r"(has_cpuid)
        : "ir"(0x200000)
        : "eax", "ebx", "cc"
    );

    if (!has_cpuid) {
        print_str("CPUID not supported\r\n");
        return;
    }

    // Leaf 0: vendor string + maximum leaf
    uint32_t max_leaf, vendor_ebx, vendor_ecx, vendor_edx;

    __asm__ __volatile__ (
        "xorl %%eax, %%eax\n\t"
        "cpuid\n\t"
        : "=a"(max_leaf), "=b"(vendor_ebx), "=c"(vendor_ecx), "=d"(vendor_edx)
        :
        :
    );

    // Unpack the 12-byte vendor string (little-endian in EBX:EDX:ECX)
    char vendor[13];
    vendor[0] = vendor_ebx & 0xFF;
    vendor[1] = (vendor_ebx >> 8) & 0xFF;
    vendor[2] = (vendor_ebx >> 16) & 0xFF;
    vendor[3] = (vendor_ebx >> 24) & 0xFF;
    vendor[4] = vendor_edx & 0xFF;
    vendor[5] = (vendor_edx >> 8) & 0xFF;
    vendor[6] = (vendor_edx >> 16) & 0xFF;
    vendor[7] = (vendor_edx >> 24) & 0xFF;
    vendor[8] = vendor_ecx & 0xFF;
    vendor[9] = (vendor_ecx >> 8) & 0xFF;
    vendor[10] = (vendor_ecx >> 16) & 0xFF;
    vendor[11] = (vendor_ecx >> 24) & 0xFF;
    vendor[12] = '\0';
    print_str(vendor);
    print_str("\r\n");

    if (max_leaf < 1) return;

    // Leaf 1: model info + feature flags
    uint32_t model_eax, features_edx;

    __asm__ __volatile__ (
        "movl $1, %%eax\n\t"
        "cpuid\n\t"
        : "=a"(model_eax), "=d"(features_edx)
        : 
        : "ebx", "ecx"
    );

    uint8_t family   = (model_eax >> 8) & 0x0F;
    uint8_t model    = (model_eax >> 4) & 0x0F;
    uint8_t stepping = model_eax & 0x0F;
    uint8_t ext_fam  = (model_eax >> 20) & 0xFF;
    uint8_t ext_mod  = (model_eax >> 16) & 0x0F;

    // Handle extended family/model (used when family = 0x0F or 0x06)
    uint8_t disp_family = (family == 0x0F) ? (family + ext_fam) : family;
    uint8_t disp_model  = (family == 0x06 || family == 0x0F) ? ((ext_mod << 4) | model) : model;

    print_str("Family ");
    print_int(disp_family);
    print_str(" Model ");
    print_int(disp_model);
    print_str(" Stepping ");
    print_int(stepping);
    print_str("\r\n");

    // Feature flags are in EDX bits
    print_str("Flags:");
    if (features_edx & 0x01)       print_str(" FPU");
    if (features_edx & 0x10)       print_str(" TSC");
    if (features_edx & (1 << 15))  print_str(" CMOV");
    if (features_edx & (1 << 23))  print_str(" MMX");
    if (features_edx & (1 << 24))  print_str(" FXSR");
    if (features_edx & (1 << 25))  print_str(" SSE");
    print_str("\r\n");
}
