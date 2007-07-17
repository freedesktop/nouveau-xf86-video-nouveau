#include "nv_include.h"

NVAllocRec *NVAllocateMemory(NVPtr pNv, int type, int size)
{
	struct drm_nouveau_mem_alloc memalloc;
	NVAllocRec *mem;

	mem = malloc(sizeof(NVAllocRec));
	if (!mem)
		return NULL;

	memalloc.flags         = type | NOUVEAU_MEM_MAPPED;
	memalloc.size          = size;
	memalloc.alignment     = 0;
	if (drmCommandWriteRead(pNv->drm_fd, DRM_NOUVEAU_MEM_ALLOC, &memalloc,
				sizeof(memalloc))) {
		ErrorF("NOUVEAU_MEM_ALLOC failed.  "
			"flags=0x%08x, size=%lld (%d)\n",
			memalloc.flags, memalloc.size, errno);
		free(mem);
		return NULL;
	}
	mem->type   = memalloc.flags;
	mem->size   = memalloc.size;
	mem->offset = memalloc.offset;

	if (drmMap(pNv->drm_fd, memalloc.map_handle, mem->size, &mem->map)) {
		ErrorF("drmMap() failed. handle=0x%llx, size=%lld (%d)\n",
				memalloc.map_handle, mem->size, errno);
		mem->map  = NULL;
		NVFreeMemory(pNv, mem);
		return NULL;
	}

	return mem;
}

void NVFreeMemory(NVPtr pNv, NVAllocRec *mem)
{
	struct drm_nouveau_mem_free memfree;

	if (mem) {
		if (mem->map) {
			if (drmUnmap(mem->map, mem->size))
				ErrorF("drmUnmap() failed.  "
					"map=%p, size=%lld\n",
					mem->map, mem->size);
		}

		memfree.flags = mem->type;
		memfree.offset = mem->offset;

		if (drmCommandWriteRead(pNv->drm_fd,
					DRM_NOUVEAU_MEM_FREE, &memfree,
					sizeof(memfree))) {
			ErrorF("NOUVEAU_MEM_FREE failed.  "
				"flags=0x%08x, offset=0x%llx (%d)\n",
				mem->type, mem->size, errno);
		}
		free(mem);
	}
}

