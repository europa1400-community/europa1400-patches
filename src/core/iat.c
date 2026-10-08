/* Import address table lookup in a loaded module. */
#include "e1400core.h"

#include <stdlib.h>
#include <string.h>

void **e1400_iat_slot(HMODULE module, const char *dll, const char *function)
{
    uint8_t *base = (uint8_t *)module;
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)base;
    const IMAGE_NT_HEADERS32 *nt;
    const IMAGE_DATA_DIRECTORY *directory;
    const IMAGE_IMPORT_DESCRIPTOR *import;
    int by_ordinal = function[0] == '#';
    unsigned ordinal = by_ordinal ? (unsigned)atoi(function + 1) : 0;
    if (!base || dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    nt = (const IMAGE_NT_HEADERS32 *)(base + dos->e_lfanew);
    directory = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory->VirtualAddress) return NULL;
    for (import = (const IMAGE_IMPORT_DESCRIPTOR *)(base + directory->VirtualAddress); import->Name; import++) {
        const IMAGE_THUNK_DATA32 *name;
        IMAGE_THUNK_DATA32 *slot;
        if (_stricmp((const char *)(base + import->Name), dll)) continue;
        name = (const IMAGE_THUNK_DATA32 *)(base + (import->OriginalFirstThunk ? import->OriginalFirstThunk : import->FirstThunk));
        slot = (IMAGE_THUNK_DATA32 *)(base + import->FirstThunk);
        for (; name->u1.AddressOfData; name++, slot++) {
            if (IMAGE_SNAP_BY_ORDINAL32(name->u1.Ordinal)) {
                if (!by_ordinal || IMAGE_ORDINAL32(name->u1.Ordinal) != ordinal) continue;
            } else if (by_ordinal) {
                /* "#n" against an import by name: the same export when the slot still holds the ordinal's address */
                HMODULE exporter = GetModuleHandleA(dll);
                FARPROC target = exporter ? GetProcAddress(exporter, (LPCSTR)(uintptr_t)ordinal) : NULL;
                if (!target || (uintptr_t)target != (uintptr_t)slot->u1.Function) continue;
            } else if (strcmp((const char *)((const IMAGE_IMPORT_BY_NAME *)(base + name->u1.AddressOfData))->Name, function)) {
                continue;
            }
            return (void **)&slot->u1.Function;
        }
    }
    return NULL;
}

int e1400_iat_write(void **slot, void *value)
{
    DWORD old;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old)) return -1;
    *slot = value;
    VirtualProtect(slot, sizeof(*slot), old, &old);
    return 0;
}
