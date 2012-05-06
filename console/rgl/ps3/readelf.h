#ifndef READELF_H
#define READELF_H

#include "elf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _ELF_section_t
{
	Elf32_Shdr header;
	const char* name;
	char* data;
} ELF_section_t;

typedef struct _ELF_segment_t
{
	Elf32_Phdr header;
	unsigned char* pointer;
	unsigned char* data;
} ELF_segment_t;

typedef struct
{
	const char* name;
	unsigned int vma;
	unsigned int size;
	int section;
	unsigned char resolved;
	unsigned char foreign;
} ELF_symbol_t;

typedef struct _ELF_t
{
	unsigned int endian;
	unsigned int relocatable;
	unsigned int sectionCount;
	unsigned int segmentCount;
	unsigned int symbolCount;
	unsigned int entrypoint;
	ELF_section_t* sections;
	ELF_segment_t* segments;
	ELF_symbol_t* symbols;
	unsigned int symbolsSection;
	unsigned int symbolNamesSection;
	unsigned int paramSection;
} ELF_t;

typedef struct
{
	unsigned int relative;
	unsigned int shift;
	unsigned int size;
	unsigned int position;
	unsigned int mask;
} ELF_rel_type_t;

ELF_section_t* findSection(const ELF_t* elf,const char* name);
int lookupSymbol(const ELF_t* elf,const char* name);
const Elf32_Sym* getSymbolByIndex(const ELF_t* elf,int idx);

const char *findSectionInPlace(const char* memory,unsigned int size,const char *name,size_t *sectionSize);
const char *findSymbolSectionInPlace(const char *memory, unsigned int size, size_t *symbolSize, size_t *symbolCount, const char **symbolstrtab);
int lookupSymbolValueInPlace(const char* symbolSection, size_t symbolSize, size_t symbolCount, const char *symbolstrtab, const char *name);
const char *getSymbolByIndexInPlace(const char* symbolSection, size_t symbolSize, size_t symbolCount,  const char *symbolstrtab, int index);

#ifdef __cplusplus
}
#endif

#endif // READELF_H
