#define _CRT_SECURE_NO_WARNINGS 1

#include "../math.h"
#include "model.h"
#include "mesh.h"
#include "material.h"
#include "texture.h"
#include "../libraries/stref.h"

#include <stdio.h>

namespace sk {

///////////////////////////////////////////

model_t model_create() {
	model_t result = (_model_t*)assets_allocate(asset_type_model);
	return result;
}

///////////////////////////////////////////

void model_set_id(model_t model, const char *id) {
	assets_set_id(model->header, id);
}

///////////////////////////////////////////

model_t model_find(const char *id) {
	model_t result = (model_t)assets_find(id, asset_type_model);
	if (result != nullptr) {
		assets_addref(result->header);
		return result;
	}
	return nullptr;
}

///////////////////////////////////////////

model_t model_create_mesh(mesh_t mesh, material_t material) {
	model_t result = model_create();

	model_add_subset(result, mesh, material, matrix_identity);

	return result;
}

///////////////////////////////////////////

model_t model_create_file(const char *filename, shader_t shader) {
	model_t result = model_find(filename);
	if (result != nullptr)
		return result;
	result = model_create();
	model_set_id(result, filename);

	const char *model_file = assets_file(filename);
	// Open file
	FILE *fp;
	if (fopen_s(&fp, model_file, "rb") != 0 || fp == nullptr) {
		log_errf("Can't find file %s!", model_file);
		return nullptr;
	}

	// Get length of file
	fseek(fp, 0L, SEEK_END);
	uint64_t length = ftell(fp);
	rewind(fp);

	// Read the data
	uint8_t *data = (uint8_t *)malloc(sizeof(uint8_t) *length+1);
	if (data == nullptr) { fclose(fp); return false; }
	fread(data, 1, length, fp);
	fclose(fp);
	data[length] = '\0';

	if (string_endswith(filename, ".glb", false) || string_endswith(filename, ".gltf", false)) {
		if (!modelfmt_gltf(result, filename, data, length, shader))
			log_errf("Issue loading GLTF file: %s!", filename);
	} else if (string_endswith(filename, ".obj", false)) {
		if (!modelfmt_obj (result, filename, data, length, shader))
			log_errf("Issue loading Wavefront OBJ file: %s!", filename);
	} else if (string_endswith(filename, ".stl", false)) {
		if (!modelfmt_stl (result, filename, data, length, shader))
			log_errf("Issue loading STL file: %s!", filename);
	} else {
		log_errf("Issue loading %s! Can't recognize the file extension.", filename);
	}

	free(data);

	return result;
}

///////////////////////////////////////////

void model_recalculate_bounds(model_t model) {
	if (model->subset_count <= 0) {
		model->bounds = {};
		return;
	}

	// Get an initial size
	vec3 min, max;
	min = max = matrix_mul_point( model->subsets[0].offset, bounds_corner(model->subsets[0].mesh->bounds, 0) );
	
	// Find the corners for each bounding cube, and factor them in!
	for (int32_t m = 0; m < model->subset_count; m += 1) {
		for (int32_t i = 0; i < 8; i += 1) {
			vec3 corner = bounds_corner   (model->subsets[m].mesh->bounds, i);
			vec3 pt     = matrix_mul_point(model->subsets[m].offset, corner);
			min.x = fminf(pt.x, min.x);
			min.y = fminf(pt.y, min.y);
			min.z = fminf(pt.z, min.z);

			max.x = fmaxf(pt.x, max.x);
			max.y = fmaxf(pt.y, max.y);
			max.z = fmaxf(pt.z, max.z);
		}
	}
	
	// Final bounds value
	model->bounds = bounds_t{ min / 2 + max / 2, max - min };
}

///////////////////////////////////////////

material_t model_get_material(model_t model, int32_t subset) {
	assert(subset < model->subset_count);
	assets_addref(model->subsets[subset].material->header);
	return model->subsets[subset].material;
}

///////////////////////////////////////////

mesh_t model_get_mesh(model_t model, int32_t subset) {
	assert(subset < model->subset_count);
	assets_addref(model->subsets[subset].mesh->header);
	return model->subsets[subset].mesh;
}

///////////////////////////////////////////

matrix model_get_transform(model_t model, int32_t subset) {
	assert(subset < model->subset_count);
	return model->subsets[subset].offset;
}

///////////////////////////////////////////

void model_set_material(model_t model, int32_t subset, material_t material) {
	assert(subset < model->subset_count);
	assert(material != nullptr);

	material_release(model->subsets[subset].material);
	model->subsets[subset].material = material;
	assets_addref(model->subsets[subset].material->header);
}

///////////////////////////////////////////

void model_set_mesh(model_t model, int32_t subset, mesh_t mesh) {
	assert(subset < model->subset_count);
	assert(mesh != nullptr);

	mesh_release(model->subsets[subset].mesh);
	model->subsets[subset].mesh = mesh;
	assets_addref(model->subsets[subset].mesh->header);

	model_recalculate_bounds(model);
}

///////////////////////////////////////////

void model_set_transform(model_t model, int32_t subset, const matrix &transform) {
	assert(subset < model->subset_count);
	model->subsets[subset].offset = transform;
}
///////////////////////////////////////////

int32_t model_subset_count(model_t model) {
	return model->subset_count;
}

///////////////////////////////////////////

int32_t model_add_subset(model_t model, mesh_t mesh, material_t material, const matrix &transform) {
	assert(model    != nullptr);
	assert(mesh     != nullptr);
	assert(material != nullptr);

	model->subsets                      = (model_subset_t *)realloc(model->subsets, sizeof(model_subset_t) * (model->subset_count + 1));
	model->subsets[model->subset_count] = model_subset_t{ mesh, material, transform };
	assets_addref(mesh->header);
	assets_addref(material->header);

	model->subset_count += 1;
	model_recalculate_bounds(model);

	return model->subset_count - 1;
}

///////////////////////////////////////////

void model_remove_subset(model_t model, int32_t subset) {
	assert(subset < model->subset_count);

	mesh_release    (model->subsets[subset].mesh);
	material_release(model->subsets[subset].material);
	if (subset < model->subset_count - 1) {
		memmove(
			&model->subsets[subset],
			&model->subsets[subset + 1],
			sizeof(model_subset_t) * (model->subset_count - (subset + 1)));
	}
	model->subset_count -= 1;
}

///////////////////////////////////////////

void model_release(model_t model) {
	if (model == nullptr)
		return;

	assets_releaseref(model->header);
}

///////////////////////////////////////////

void model_set_bounds(model_t model, const bounds_t &bounds) {
	model->bounds = bounds;
}

///////////////////////////////////////////

bounds_t model_get_bounds(model_t model) {
	return model->bounds;
}

///////////////////////////////////////////

void model_destroy(model_t model) {
	for (size_t i = 0; i < model->subset_count; i++) {
		mesh_release    (model->subsets[i].mesh);
		material_release(model->subsets[i].material);
	}
	free(model->subsets);
	*model = {};
}

///////////////////////////////////////////

} // namespace sk