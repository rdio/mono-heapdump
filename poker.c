/*
* Copyright 2012 Rdio Inc
*
* Permission is hereby granted, free of charge, to any person obtaining a copy of
* this software and associated documentation files (the "Software"), to deal in
* the Software without restriction, including without limitation the rights to
* use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
* of the Software, and to permit persons to whom the Software is furnished to do
* so, subject to the following conditions:
* 
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
* 
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
* 
*/

#include <stdio.h>
#include <dlfcn.h>
#include <mono/metadata/assembly.h>
#include <mono/metadata/metadata-internals.h>
#include <mono/metadata/class.h>
#include <mono/metadata/class-internals.h>
#include <mono/metadata/object-internals.h>
#include <mono/utils/mono-internal-hash.h>
#include <mono/metadata/object.h>
#include <mono/metadata/sgen-gc.h>
#include <mono/metadata/mono-gc.h>
#if ANDROID
#include <android/log.h>
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "gcpoker", __VA_ARGS__) 
#else
#define LOGW(...) do { fprintf (stderr, "> " __VA_ARGS__); fprintf (stderr, "\n"); } while (0)
#endif


/* taken directly from sgen-gc.c... */
/* Root bitmap descriptors are simpler: the lower three bits describe the type
 * and we either have 30/62 bitmap bits or nibble-based run-length,
 * or a complex descriptor, or a user defined marker function.
 */
enum {
	ROOT_DESC_CONSERVATIVE, /* 0, so matches NULL value */
	ROOT_DESC_BITMAP,
	ROOT_DESC_RUN_LEN, 
	ROOT_DESC_COMPLEX,
	ROOT_DESC_USER,
	ROOT_DESC_TYPE_MASK = 0x7,
	ROOT_DESC_TYPE_SHIFT = 3,
};

/*
 * this is nice for showing arrays as large rectangles with individual fields pointing to the proper objects,
 * but for large heaps it makes things *very* messy.
 */
/*#define USE_SLOTS 1*/

typedef struct {
	FILE *fp;
	int visited;
	int references;
} HeapWalkData;

typedef void (*gc_lock_func) (void);
typedef MonoClass (*get_class_func) (MonoObject*);
typedef char* (*get_name_func) (MonoClass*);
typedef int (*walk_heap_func) (int, void*, void*);

static gc_lock_func _mono_sgen_gc_lock;
static gc_lock_func _mono_sgen_gc_unlock;
static get_class_func _mono_object_get_class;
static get_name_func _mono_class_get_namespace;
static get_name_func _mono_class_get_name;
static walk_heap_func _mono_gc_walk_heap;

#define stringify(x) stringify2(x)
#define stringify2(x) #x

static int
init_mono_bindings ()
{
	static int initialized = 0;
	static int success = 0;
	if (initialized)
		return success;
	initialized = 1;

#if ANDROID
	const char *libmono_path = stringify(LIBMONODROID);
	LOGW ("dlopening %s", libmono_path);

	void* libmono = dlopen (libmono_path, RTLD_LOCAL | RTLD_LAZY);
	if (libmono == NULL) {
		LOGW ("dlopen returned null");
		return success = 0;
	}
#else
	void* libmono = dlopen (NULL, RTLD_LOCAL | RTLD_LAZY);
#endif

#if ANDROID
	char *mono_gc_enable_events = dlsym (libmono, "mono_gc_enable_events");
	/* this is pretty disgusting.  the symbols mono_sgen_gc_lock
	 * and mono_sgen_gc_unlock are private and not exported.  so,
	 * we disassemble libmonodroid.so and find the assembly for
	 * something that *calls* them, and figure out where they live
	 * in the image.  This is of course completely tied to a given
	 * libmono build, and whenever mono revs change this will need to as
	 * well.
	 */
	_mono_sgen_gc_lock = (gc_lock_func)(mono_gc_enable_events + 0x74);
	_mono_sgen_gc_unlock = (gc_lock_func)(mono_gc_enable_events + 0x60);
#else
	_mono_sgen_gc_lock = dlsym (libmono, "mono_sgen_gc_lock");
	_mono_sgen_gc_unlock = dlsym (libmono, "mono_sgen_gc_unlock");
#endif
	_mono_gc_walk_heap = dlsym (libmono, "mono_gc_walk_heap");
	_mono_class_get_namespace = dlsym (libmono, "mono_class_get_namespace");
	_mono_class_get_name = dlsym (libmono, "mono_class_get_name");
	_mono_object_get_class = dlsym (libmono, "mono_object_get_class");

	LOGW ("mono_sgen_gc_lock %s", _mono_sgen_gc_lock ? "FOUND" : "NOT FOUND");
	LOGW ("mono_sgen_gc_unlock %s", _mono_sgen_gc_unlock ? "FOUND" : "NOT FOUND");
	LOGW ("mono_gc_walk_heap %s", _mono_gc_walk_heap ? "FOUND" : "NOT FOUND");
	LOGW ("mono_class_get_namespace %s", _mono_class_get_namespace ? "FOUND" : "NOT FOUND");
	LOGW ("mono_class_get_name %s", _mono_class_get_name ? "FOUND" : "NOT FOUND");
	LOGW ("mono_object_get_class %s", _mono_object_get_class ? "FOUND" : "NOT FOUND");

	if (!_mono_sgen_gc_lock ||
	    !_mono_sgen_gc_unlock ||
	    !_mono_gc_walk_heap ||
	    !_mono_class_get_namespace ||
	    !_mono_class_get_name ||
	    !_mono_object_get_class)
	  return success = 0;

	return success = 1;
}

static int
gc_callback (MonoObject *obj, MonoClass *klass, uintptr_t size, uintptr_t num, MonoObject **refs, uintptr_t *offsets, void *data)
{
	if (size == 0)
		return;

	HeapWalkData *heap_walk_data = (HeapWalkData*)data;
	FILE *fp = heap_walk_data->fp;
	int i;

	heap_walk_data->visited++;

	if (!strcmp (_mono_class_get_namespace (klass), "System") &&
	    !strcmp (_mono_class_get_name (klass), "MonoType")) {
		// skip these, they bloat the graph
		return;
	}

	    
#if USE_SLOTS
 	fprintf (fp, "  node_%p [label=\"<f0> %p|<f1> %s.%s", obj, obj, _mono_class_get_namespace (klass), _mono_class_get_name (klass));
	for (i = 0; i < num; i ++)
		fprintf (fp, "|<f%d> %p", i+2, refs[i]);
	fprintf (fp, "\"];\n");
#else
 	fprintf (fp, "  node_%p [label=\"<f0> %p|<f1> %s.%s\"];\n", obj, obj, _mono_class_get_namespace (klass), _mono_class_get_name (klass));
#endif
	for (i = 0; i < num; i ++) {
		heap_walk_data->references ++;
#if USE_SLOTS
	  	fprintf (fp, "  node_%p:f%d -> node_%p:f0;\n", obj, i+2, refs[i]);
#else
	  	fprintf (fp, "  node_%p:f0 -> node_%p:f0;\n", obj, refs[i]);
#endif
	}
}

static void
walk_class_static_data (MonoClass *klass, HeapWalkData *data)
{
	if (!klass->has_static_refs)
		return;

	FILE *fp = data->fp;

	int need_class_node = 0;

	MonoVTable *vtable = mono_class_try_get_vtable (mono_domain_get(), klass);
	if (vtable == NULL)
		return;

	int desc = (int)vtable->gc_descr; // 32 bit specific here
	void** static_data = vtable->data;

	void** start_root = static_data;

	switch (desc & ROOT_DESC_TYPE_MASK) {
	case ROOT_DESC_BITMAP:
		desc >>= ROOT_DESC_TYPE_SHIFT;
		while (desc) {
			if ((desc & 1) && *start_root)
				need_class_node = 1;
			desc >>= 1;
			start_root++;
		}
		break;
	case ROOT_DESC_COMPLEX: {
#if false
		gsize *bitmap_data = complex_descriptors + (desc >> ROOT_DESC_TYPE_SHIFT);
		int bwords = (*bitmap_data) - 1;
		void **start_run = start_root;
		bitmap_data++;
		while (bwords-- > 0) {
			gsize bmap = *bitmap_data++;
			void **objptr = start_run;
			while (bmap) {
				if ((bmap & 1) && *objptr)
					need_class_node = 1;
				bmap >>= 1;
				++objptr;
			}
			start_run += GC_BITS_PER_WORD;
		}
#endif
		break;
	}
	case ROOT_DESC_USER: {
		break;
	}
	case ROOT_DESC_RUN_LEN:
		break;
	default:
		g_assert_not_reached ();
	}

	if (need_class_node) {
		LOGW ("adding static class for %s to the graph", mono_class_get_name (klass));
		fprintf (fp, "  node_%p [label=\"<f0> static data|<f1> %s.%s\", color=\"red\"];\n", klass, _mono_class_get_namespace (klass), _mono_class_get_name (klass));

		desc = (int)vtable->gc_descr; // 32 bit specific here
		start_root = static_data;

		switch (desc & ROOT_DESC_TYPE_MASK) {
		case ROOT_DESC_BITMAP:
			desc >>= ROOT_DESC_TYPE_SHIFT;
			while (desc) {
				if ((desc & 1) && *start_root) {
					void *obj = *start_root;
					char extra[256];
					extra[0] = 0;

					if (mono_gchandle_is_in_domain ((int)obj, mono_domain_get ())) {
						snprintf (extra, sizeof(extra), "[label=\"gchandle %p\"]", obj);
						obj = mono_gchandle_get_target ((int)obj);
					}
					fprintf (fp, "  node_%p:f0 -> node_%p:f0;\n", klass, obj);
				}
				desc >>= 1;
				start_root++;
			}
			break;
		case ROOT_DESC_COMPLEX: {
#if false
			gsize *bitmap_data = complex_descriptors + (desc >> ROOT_DESC_TYPE_SHIFT);
			int bwords = (*bitmap_data) - 1;
			void **start_run = start_root;
			bitmap_data++;
			while (bwords-- > 0) {
				gsize bmap = *bitmap_data++;
				void **objptr = start_run;
				while (bmap) {
					if ((bmap & 1) && *objptr) {
						void *obj = *objptr;
						char extra[256];
						extra[0] = 0;

						if (mono_gchandle_is_in_domain ((int)obj, mono_domain_get ())) {
							snprintf (extra, sizeof(extra), "[label=\"gchandle %p\"]", obj);
							obj = mono_gchandle_get_target ((int)obj);
						}
						fprintf (fp, "  node_%p:f0 -> node_%p:f0;\n", klass, obj);
					}
					bmap >>= 1;
					++objptr;
				}
				start_run += GC_BITS_PER_WORD;
			}
#endif
			break;
		}
		case ROOT_DESC_USER: {
			break;
		}
		case ROOT_DESC_RUN_LEN:
			break;
		default:
			g_assert_not_reached ();
		}
	}
}

static void
walk_assembly_class_static_data (MonoAssembly* assembly, HeapWalkData *data)
{
	MonoImage *image = mono_assembly_get_image (assembly);
	MonoInternalHashTable *class_cache = &image->class_cache;
	int i;

	LOGW ("%d classes in assembly %p", class_cache->num_entries, assembly);

	for (i = 0; i < class_cache->size; i ++) {
		MonoClass *klass;

		for (klass = class_cache->table [i];
		     klass != NULL;
		     klass = *(class_cache->next_value (klass))) {

			walk_class_static_data (klass, data);
		}
	}

}

static void
walk_all_class_static_data (HeapWalkData *data)
{
	mono_assembly_foreach ((MonoFunc)walk_assembly_class_static_data, data);
}

void
generate_heap_dump (char *path)
{
	if (!init_mono_bindings ())
		return;

	LOGW ("generating heap dump");

	_mono_sgen_gc_lock ();

	LOGW ("emitting graph");

	FILE* fp = fopen (path, "w");
	if (fp == NULL) {
		LOGW ("couldn't create file...");
		return;
	}

	fprintf (fp, "digraph heapdump {\n");
	fprintf (fp, "  node [shape=record,fontsize=6];\n");
	fprintf (fp, "  rankdir=LR;\n");

	HeapWalkData data;
	data.visited = 0;
	data.references = 0;
	data.fp = fp;

	// walk over all instances in the heap
	_mono_gc_walk_heap (0, gc_callback, &data);

	// now generate nodes for the static data for all loaded classes (only generate the node if we need to, though)
	walk_all_class_static_data (&data);

	fprintf (fp, "}\n");
	fclose(fp);

	LOGW ("visited %d nodes", data.visited);
	LOGW ("recorded %d inter-object references", data.references);

	_mono_sgen_gc_unlock ();
}
