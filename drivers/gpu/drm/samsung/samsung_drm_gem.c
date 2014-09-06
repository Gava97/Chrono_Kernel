/* samsung_drm_gem.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 * Author: Inki Dae <inki.dae@samsung.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include "drm.h"
#include "samsung_drm.h"

#include <plat/samsung_drm.h>

#include "samsung_drm_gem.h"
#include "samsung_drm_buf.h"

static unsigned int convert_to_vm_err_msg(int msg)
{
	unsigned int out_msg;

	switch (msg) {
	case 0:
	case -ERESTARTSYS:
	case -EINTR:
		out_msg = VM_FAULT_NOPAGE;
		break;

	case -ENOMEM:
		out_msg = VM_FAULT_OOM;
		break;

	default:
		out_msg = VM_FAULT_SIGBUS;
		break;
	}

	return out_msg;
}

static unsigned int get_gem_mmap_offset(struct drm_gem_object *obj)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	return (unsigned int)obj->map_list.hash.key << PAGE_SHIFT;
}

/**
 * samsung_drm_gem_create_mmap_offset - create a fake mmap offset for an object
 * @obj: obj in question
 *
 * GEM memory mapping works by handing back to userspace a fake mmap offset
 * it can use in a subsequent mmap(2) call.  The DRM core code then looks
 * up the object based on the offset and sets up the various memory mapping
 * structures.
 *
 * This routine allocates and attaches a fake offset for @obj.
 */
static int
samsung_drm_gem_create_mmap_offset(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct drm_gem_mm *mm = dev->mm_private;
	struct drm_map_list *list;
	struct drm_local_map *map;
	int ret = 0;

	/* Set the object up for mmap'ing */
	list = &obj->map_list;
	list->map = kzalloc(sizeof(struct drm_map_list), GFP_KERNEL);
	if (!list->map)
		return -ENOMEM;

	map = list->map;
	map->type = _DRM_GEM;
	map->size = obj->size;
	map->handle = obj;

	/* Get a DRM GEM mmap offset allocated... */
	list->file_offset_node = drm_mm_search_free(&mm->offset_manager,
						    obj->size / PAGE_SIZE,
						    0, 0);
	if (!list->file_offset_node) {
		DRM_ERROR("failed to allocate offset for bo %d\n",
			  obj->name);
		ret = -ENOSPC;
		goto out_free_list;
	}

	list->file_offset_node = drm_mm_get_block(list->file_offset_node,
						  obj->size / PAGE_SIZE,
						  0);
	if (!list->file_offset_node) {
		ret = -ENOMEM;
		goto out_free_list;
	}

	list->hash.key = list->file_offset_node->start;
	ret = drm_ht_insert_item(&mm->offset_hash, &list->hash);
	if (ret) {
		DRM_ERROR("failed to add to map hash\n");
		goto out_free_mm;
	}

	return 0;

out_free_mm:
	drm_mm_put_block(list->file_offset_node);
out_free_list:
	kfree(list->map);
	list->map = NULL;

	return ret;
}

static void
samsung_drm_gem_free_mmap_offset(struct drm_gem_object *obj)
{
	struct drm_device *dev = obj->dev;
	struct drm_gem_mm *mm = dev->mm_private;
	struct drm_map_list *list = &obj->map_list;

	drm_ht_remove_item(&mm->offset_hash, &list->hash);
	drm_mm_put_block(list->file_offset_node);
	kfree(list->map);
	list->map = NULL;
}

static int samsung_drm_gem_create(struct drm_file *file, struct drm_device *dev,
		unsigned int size, unsigned int *handle_p)
{
	struct samsung_drm_gem_obj *samsung_gem_obj;
	struct drm_gem_object *obj;
	unsigned int handle;
	int ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	size = roundup(size, PAGE_SIZE);

	/* allocate the new buffer object and memory region. */
	samsung_gem_obj = samsung_drm_buf_create(dev, size);
	if (!samsung_gem_obj)
		return -ENOMEM;

	obj = &samsung_gem_obj->base;

	ret = drm_gem_object_init(dev, obj, size);
	if (ret < 0) {
		DRM_ERROR("failed to initailize gem object.\n");
		goto out;
	}

	DRM_DEBUG_KMS("created file object = 0x%x\n", (unsigned int)obj->filp);

	ret = samsung_drm_gem_create_mmap_offset(obj);
	if (ret < 0) {
		DRM_ERROR("failed to allocate mmap offset.\n");
		goto out;
	}

	/**
	 * allocate a id of idr table where the obj is registered
	 * and handle has the id what user can see.
	 */
	ret = drm_gem_handle_create(file, obj, &handle);
	if (ret) {
		drm_gem_object_release(obj);
		samsung_drm_buf_destroy(dev, samsung_gem_obj);
		goto out;
	}

	DRM_DEBUG_KMS("gem handle = 0x%x\n", handle);

	/* drop reference from allocate - handle holds it now. */
	drm_gem_object_unreference_unlocked(obj);

	*handle_p = handle;

	return 0;

out:
	drm_gem_object_unreference_unlocked(obj);

	samsung_drm_buf_destroy(dev, samsung_gem_obj);

	kfree(samsung_gem_obj);

	return ret;
}

int samsung_drm_gem_create_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_samsung_gem_create *args = data;

	DRM_DEBUG_KMS("%s : size = 0x%x\n", __FILE__, args->size);

	return samsung_drm_gem_create(file, dev, args->size, &args->handle);
}

int samsung_drm_gem_map_offset_ioctl(struct drm_device *dev, void *data,
		struct drm_file *file)
{
	struct drm_samsung_gem_map_off *args = data;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	DRM_DEBUG_KMS("handle = 0x%x, size = 0x%x, offset = 0x%x\n",
			args->handle, args->size, (u32)args->offset);

	if (!(dev->driver->driver_features & DRIVER_GEM)) {
		DRM_ERROR("not support GEM.\n");
		return -ENODEV;
	}

	return samsung_drm_gem_dumb_map_offset(file, dev, args->handle,
			&args->offset);
}

int samsung_drm_gem_init_object(struct drm_gem_object *obj)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	return 0;
}

void samsung_drm_gem_free_object(struct drm_gem_object *gem_obj)
{
	struct samsung_drm_gem_obj *samsung_gem_obj;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	DRM_DEBUG_KMS("handle count = %d\n",
			atomic_read(&gem_obj->handle_count));

	if (gem_obj->map_list.map)
		samsung_drm_gem_free_mmap_offset(gem_obj);

	/* release file pointer to gem object. */
	drm_gem_object_release(gem_obj);

	samsung_gem_obj = to_samsung_gem_obj(gem_obj);

	samsung_drm_buf_destroy(gem_obj->dev, samsung_gem_obj);
}

struct samsung_drm_gem_obj *
		find_samsung_drm_gem_object(struct drm_file *file_priv,
			struct drm_device *dev, unsigned int handle)
{
	struct drm_gem_object *gem_obj;

	gem_obj = drm_gem_object_lookup(dev, file_priv, handle);
	if (!gem_obj) {
		DRM_LOG_KMS("a invalid gem object not registered to lookup.\n");
		return NULL;
	}

	/**
	 * unreference refcount of the gem object.
	 * at drm_gem_object_lookup(), the gem object was referenced.
	 */
	drm_gem_object_unreference(gem_obj);

	return to_samsung_gem_obj(gem_obj);
}

int samsung_drm_gem_dumb_create(struct drm_file *file_priv,
		struct drm_device *dev, struct drm_mode_create_dumb *args)
{
	DRM_DEBUG_KMS("%s\n", __FILE__);

	/**
	 * alocate memory to be used for framebuffer.
	 * - this callback would be called by user application
	 *	with DRM_IOCTL_MODE_CREATE_DUMB command.
	 */

	args->pitch = args->width * args->bpp >> 3;
	args->size = args->pitch * args->height;

	return samsung_drm_gem_create(file_priv, dev, args->size,
			&args->handle);
}

int samsung_drm_gem_dumb_map_offset(struct drm_file *file_priv,
		struct drm_device *dev, uint32_t handle, uint64_t *offset)
{
	struct samsung_drm_gem_obj *samsung_gem_obj;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	mutex_lock(&dev->struct_mutex);

	/**
	 * get offset of memory allocated for drm framebuffer.
	 * - this callback would be called by user application
	 *	with DRM_IOCTL_MODE_MAP_DUMB command.
	 */

	samsung_gem_obj = find_samsung_drm_gem_object(file_priv, dev, handle);
	if (!samsung_gem_obj) {
		DRM_ERROR("failed to get samsung_drm_get_obj.\n");
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	*offset = get_gem_mmap_offset(&samsung_gem_obj->base);

	DRM_DEBUG_KMS("offset = 0x%x\n", (unsigned int)*offset);

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

int samsung_drm_gem_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct drm_gem_object *obj = vma->vm_private_data;
	struct samsung_drm_gem_obj *samsung_gem_obj = to_samsung_gem_obj(obj);
	struct drm_device *dev = obj->dev;
	unsigned long pfn;
	pgoff_t page_offset;
	int ret;

	page_offset = ((unsigned long)vmf->virtual_address -
			vma->vm_start) >> PAGE_SHIFT;

	mutex_lock(&dev->struct_mutex);

	pfn = (samsung_gem_obj->entry->paddr >> PAGE_SHIFT) + page_offset;

	ret = vm_insert_mixed(vma, (unsigned long)vmf->virtual_address, pfn);

	mutex_unlock(&dev->struct_mutex);

	return convert_to_vm_err_msg(ret);
}

int samsung_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	/* set vm_area_struct. */
	ret = drm_gem_mmap(filp, vma);
	if (ret < 0) {
		DRM_ERROR("failed to mmap.\n");
		return ret;
	}

	vma->vm_flags &= ~VM_PFNMAP;
	vma->vm_flags |= VM_MIXEDMAP;

#if 0
	/* we can change cache attribute of this vm area here. */
	vma->vm_page_prot = vm_get_page_prot(vma->vm_flags);
#endif
	return ret;
}


int samsung_drm_gem_dumb_destroy(struct drm_file *file_priv,
		struct drm_device *dev, unsigned int handle)
{
	struct samsung_drm_gem_obj *samsung_gem_obj;
	int ret;

	DRM_DEBUG_KMS("%s\n", __FILE__);

	samsung_gem_obj = find_samsung_drm_gem_object(file_priv, dev, handle);
	if (!samsung_gem_obj) {
		DRM_ERROR("failed to get samsung_drm_get_obj.\n");
		return -EINVAL;
	}

	/**
	 * obj->refcount and obj->handle_count are decreased and
	 * if both them are 0 then samsung_drm_gem_free_object()
	 * would be called by callback to release resources.
	 */
	ret = drm_gem_handle_delete(file_priv, handle);
	if (ret < 0) {
		DRM_ERROR("failed to delete drm_gem_handle.\n");
		return ret;
	}

	return 0;
}

MODULE_AUTHOR("Inki Dae <inki.dae@samsung.com>");
MODULE_DESCRIPTION("Samsung SoC DRM GEM Module");
MODULE_LICENSE("GPL");
