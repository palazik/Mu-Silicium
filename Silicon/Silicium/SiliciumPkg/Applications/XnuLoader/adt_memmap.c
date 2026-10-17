/* Flattened Apple DeviceTree (ADT) editing for the loader: fill /chosen/memory-map
 * entries at runtime. The device tree ships N "MemoryMapReserved-<n>" placeholder
 * properties (each a 16-byte {paddr,size} DTMemoryMapRange); iBoot renames some to
 * named entries (TrustCache, RamDisk, …) and writes their phys addr+size. We do the
 * same so the kernel finds the trust cache and ramdisk we placed in RAM.
 *
 * ADT node layout (little-endian): u32 nprops; u32 nchildren;
 *   prop := char name[32]; u32 len (bit31 = "template" flag, ignored on read);
 *           u8 value[len]; pad to 4 bytes.
 *   then nchildren child nodes (recursive).
 */
#include "lt.h"

typedef struct { uint64_t paddr; uint64_t size; } dt_memmap_range;  /* DTMemoryMapRange */

static uint32_t rd32(const void *p){ const uint8_t*b=p; return b[0]|b[1]<<8|b[2]<<16|(uint32_t)b[3]<<24; }
static int streq(const char*a,const char*b){ while(*a&&*a==*b){a++;b++;} return *a==*b; }
static int nprefix(const char*s,const char*pfx){ while(*pfx){ if(*s++!=*pfx++) return 0;} return 1; }

/* advance past one node, returning pointer just after it; *out set if name matches at this level */
static uint8_t *adt_skip_node(uint8_t *p, uint8_t *end);

static uint8_t *adt_node_name(uint8_t *node, char *outname, int *nprops, int *nchild)
{
    *nprops = (int)rd32(node); *nchild = (int)rd32(node+4);
    uint8_t *p = node + 8;
    /* find the "name" property to identify the node */
    outname[0]=0;
    for (int i=0;i<*nprops;i++){
        char *nm=(char*)p; uint32_t len=rd32(p+32)&0x7fffffff;
        if (streq(nm,"name")){ int j=0; char*v=(char*)(p+36); while(j<31 && v[j]) { outname[j]=v[j]; j++; } outname[j]=0; }
        p += 36 + ((len+3)&~3u);
    }
    return p; /* points at first child */
}

/* Find the /chosen/memory-map node. Returns its start, or 0. */
static uint8_t *find_child(uint8_t *node, uint8_t *end, const char *want);

static uint8_t *adt_skip_node(uint8_t *node, uint8_t *end)
{
    int np=(int)rd32(node), nc=(int)rd32(node+4);
    uint8_t *p=node+8;
    for(int i=0;i<np;i++){ uint32_t len=rd32(p+32)&0x7fffffff; p+=36+((len+3)&~3u); if(p>end) return end; }
    for(int i=0;i<nc;i++){ p=adt_skip_node(p,end); if(p>=end) return end; }
    return p;
}

static uint8_t *find_child(uint8_t *node, uint8_t *end, const char *want)
{
    int np,nc; char nm[32];
    uint8_t *child = adt_node_name(node,nm,&np,&nc);
    for(int i=0;i<nc;i++){
        int cnp,cnc; char cnm[32]; adt_node_name(child,cnm,&cnp,&cnc);
        if(streq(cnm,want)) return child;
        child = adt_skip_node(child,end);
        if(child>=end) break;
    }
    return 0;
}

/* Public: fill a memory-map entry. adt=flattened tree, len=its size.
 * Renames the first free "MemoryMapReserved-*" prop in /chosen/memory-map to `name`
 * and writes {paddr,size}. Returns 0 on success, <0 on failure. */
int adt_memmap_set(void *adt, uint64_t len, const char *name, uint64_t paddr, uint64_t size)
{
    uint8_t *base=adt, *end=base+len;
    uint8_t *chosen = find_child(base,end,"chosen"); if(!chosen) return -1;
    uint8_t *mm = find_child(chosen,end,"memory-map"); if(!mm) return -2;
    int np=(int)rd32(mm); uint8_t *p=mm+8;
    for(int i=0;i<np;i++){
        char *nm=(char*)p; uint32_t vlen=rd32(p+32)&0x7fffffff;
        if(nprefix(nm,"MemoryMapReserved-") && vlen>=sizeof(dt_memmap_range)){
            /* rename: overwrite 32-byte name field */
            int j=0; for(; name[j] && j<31; j++) nm[j]=name[j];
            for(; j<32; j++) nm[j]=0;
            /* write {paddr,size} into the value */
            dt_memmap_range *r=(dt_memmap_range*)(p+36);
            r->paddr=paddr; r->size=size;
            return 0;
        }
        p += 36 + ((vlen+3)&~3u);
    }
    return -3; /* no free reserved slot */
}
