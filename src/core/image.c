/* SHA-256 of files, FNV-1a signatures and read-only access to PE files on disk. */
#include "e1400core.h"

#include <bcrypt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int e1400_sha256_file(const char *path, char hex[65])
{
    BCRYPT_ALG_HANDLE algorithm = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    uint8_t digest[32], buffer[65536];
    FILE *file = fopen(path, "rb");
    size_t n;
    int result = -1;
    hex[0] = 0;
    if (!file) return -1;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0) ||
        BCryptCreateHash(algorithm, &hash, NULL, 0, NULL, 0, 0))
        goto done;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0)
        if (BCryptHashData(hash, buffer, (ULONG)n, 0)) goto done;
    if (BCryptFinishHash(hash, digest, sizeof(digest), 0)) goto done;
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    result = 0;
done:
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    fclose(file);
    return result;
}

uint32_t e1400_fnv1a(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < size; i++) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static void load_relocations(E1400Image *image, const IMAGE_NT_HEADERS32 *nt);

static const IMAGE_NT_HEADERS32 *nt_of(const E1400Image *image)
{
    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)image->data;
    const IMAGE_NT_HEADERS32 *nt;
    if (image->size < sizeof(*dos) || dos->e_magic != IMAGE_DOS_SIGNATURE) return NULL;
    if ((size_t)dos->e_lfanew + sizeof(*nt) > image->size) return NULL;
    nt = (const IMAGE_NT_HEADERS32 *)(image->data + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386) return NULL;
    return nt;
}

int e1400_image_load(const char *path, E1400Image *image)
{
    FILE *file = fopen(path, "rb");
    const IMAGE_NT_HEADERS32 *nt;
    const IMAGE_SECTION_HEADER *section;
    long size;
    memset(image, 0, sizeof(*image));
    if (!file) return -1;
    fseek(file, 0, SEEK_END);
    size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size <= 0 || !(image->data = malloc((size_t)size)) || fread(image->data, 1, (size_t)size, file) != (size_t)size) {
        fclose(file);
        e1400_image_free(image);
        return -1;
    }
    fclose(file);
    image->size = (size_t)size;
    if (!(nt = nt_of(image))) {
        e1400_image_free(image);
        return -1;
    }
    image->image_base = nt->OptionalHeader.ImageBase;
    section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++, section++)
        if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE) {
            image->text_rva = section->VirtualAddress;
            image->text_size = section->Misc.VirtualSize < section->SizeOfRawData ? section->Misc.VirtualSize : section->SizeOfRawData;
            break;
        }
    load_relocations(image, nt);
    return 0;
}

void e1400_image_free(E1400Image *image)
{
    free(image->relocated);
    free(image->data);
    memset(image, 0, sizeof(*image));
}

const uint8_t *e1400_image_at(const E1400Image *image, uint32_t rva, uint32_t size)
{
    const IMAGE_NT_HEADERS32 *nt = nt_of(image);
    const IMAGE_SECTION_HEADER *section;
    if (!nt) return NULL;
    section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; i++, section++) {
        uint32_t offset = rva - section->VirtualAddress;
        if (rva >= section->VirtualAddress && offset + size <= section->SizeOfRawData &&
            (size_t)section->PointerToRawData + offset + size <= image->size)
            return image->data + section->PointerToRawData + offset;
    }
    return NULL;
}

/* Marks the bytes of relocated absolute addresses (HIGHLOW) in the relocation bitmap. */
static void load_relocations(E1400Image *image, const IMAGE_NT_HEADERS32 *nt)
{
    const IMAGE_DATA_DIRECTORY *directory = &nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    const uint8_t *block = directory->VirtualAddress ? e1400_image_at(image, directory->VirtualAddress, directory->Size) : NULL;
    const uint8_t *end = block ? block + directory->Size : NULL;
    image->relocated = calloc((nt->OptionalHeader.SizeOfImage + 7) / 8, 1);
    image->image_size = nt->OptionalHeader.SizeOfImage;
    if (!image->relocated || !block) return;
    while (block + sizeof(IMAGE_BASE_RELOCATION) <= end) {
        const IMAGE_BASE_RELOCATION *page = (const IMAGE_BASE_RELOCATION *)block;
        const uint16_t *entry = (const uint16_t *)(page + 1);
        if (page->SizeOfBlock < sizeof(*page)) break;
        for (unsigned i = 0; i < (page->SizeOfBlock - sizeof(*page)) / 2; i++) {
            uint32_t rva = page->VirtualAddress + (entry[i] & 0xfffu);
            if (entry[i] >> 12 != IMAGE_REL_BASED_HIGHLOW) continue;
            for (uint32_t b = rva; b < rva + 4 && b < image->image_size; b++) image->relocated[b >> 3] |= (uint8_t)(1u << (b & 7));
        }
        block += page->SizeOfBlock;
    }
}

uint32_t e1400_image_signature(const E1400Image *image, uint32_t rva)
{
    const uint8_t *bytes = e1400_image_at(image, rva, E1400_SIGNATURE_BYTES);
    uint8_t window[E1400_SIGNATURE_BYTES];
    if (!bytes) return 0;
    /* absolute addresses differ between builds: relocated bytes count as zero */
    for (uint32_t i = 0; i < E1400_SIGNATURE_BYTES; i++) {
        uint32_t b = rva + i;
        int masked = image->relocated && b < image->image_size && (image->relocated[b >> 3] & (1u << (b & 7)));
        window[i] = masked ? 0 : bytes[i];
    }
    return e1400_fnv1a(window, E1400_SIGNATURE_BYTES);
}

unsigned e1400_image_find_signature(const E1400Image *image, uint32_t signature, uint32_t *out, unsigned max)
{
    unsigned found = 0;
    if (image->text_size < E1400_SIGNATURE_BYTES) return 0;
    for (uint32_t rva = image->text_rva; rva <= image->text_rva + image->text_size - E1400_SIGNATURE_BYTES; rva++)
        if (e1400_image_signature(image, rva) == signature) {
            if (found < max) out[found] = rva;
            found++;
        }
    return found;
}
