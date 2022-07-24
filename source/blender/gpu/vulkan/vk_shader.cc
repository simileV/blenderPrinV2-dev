/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2021 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup gpu
 */

#include "vk_shader.hh"

#include <string>

#include "BLI_vector.hh"

#include "shader_compiler.hh"

#include <fstream>
#include <sstream>
#include <string>

namespace blender::gpu {

/* -------------------------------------------------------------------- */
/** \name Shader stages
 * \{ */

constexpr StringRef SHADER_STAGE_VERTEX_SHADER = "vertex";
constexpr StringRef SHADER_STAGE_GEOMETRY_SHADER = "geometry";
constexpr StringRef SHADER_STAGE_FRAGMENT_SHADER = "fragment";
constexpr StringRef SHADER_STAGE_COMPUTE_SHADER = "compute";

std::ostream &operator<<(std::ostream &os, const VKShaderStageType &stage)
{
  switch (stage) {
    case VKShaderStageType::VertexShader:
      os << SHADER_STAGE_VERTEX_SHADER;
      break;
    case VKShaderStageType::GeometryShader:
      os << SHADER_STAGE_GEOMETRY_SHADER;
      break;
    case VKShaderStageType::FragmentShader:
      os << SHADER_STAGE_FRAGMENT_SHADER;
      break;
    case VKShaderStageType::ComputeShader:
      os << SHADER_STAGE_COMPUTE_SHADER;
      break;
  }
  return os;
}

static shader_compiler::SourceType to_source_type(VKShaderStageType stage)
{
  switch (stage) {
    case VKShaderStageType::VertexShader:
      return shader_compiler::SourceType::GlslVertexShader;
      break;
    case VKShaderStageType::GeometryShader:
      return shader_compiler::SourceType::GlslGeometryShader;
      break;
    case VKShaderStageType::FragmentShader:
      return shader_compiler::SourceType::GlslFragmentShader;
      break;
    case VKShaderStageType::ComputeShader:
      return shader_compiler::SourceType::GlslComputeShader;
      break;
  }
  BLI_assert(!"Unknown VKShaderStageType.");
  return shader_compiler::SourceType::GlslVertexShader;
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Construction/Destruction
 * \{ */

VKShader::VKShader(const char *name) : Shader(name)
{
  interface = new VKShaderInterface();
  context_ = VKContext::get();
};

VKShader::~VKShader()
{
  VkDevice device = context_->device_get();
  if (vertex_shader_ != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device, vertex_shader_, nullptr);
    vertex_shader_ = VK_NULL_HANDLE;
  }
  if (geometry_shader_ != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device, geometry_shader_, nullptr);
    geometry_shader_ = VK_NULL_HANDLE;
  }
  if (fragment_shader_ != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device, fragment_shader_, nullptr);
    fragment_shader_ = VK_NULL_HANDLE;
  }
  if (compute_shader_ != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device, compute_shader_, nullptr);
    compute_shader_ = VK_NULL_HANDLE;
  }

  context_ = nullptr;
};

/** \} */

/* -------------------------------------------------------------------- */
/** \name Compilation
 * \{ */

static std::string to_stage_name(VKShaderStageType stage)
{
  std::stringstream ss;
  ss << stage;
  return ss.str();
}

static std::string to_stage_name(StringRef name, VKShaderStageType stage)
{
  std::stringstream ss;
  ss << name << "." << stage;
  return ss.str();
}

static std::string combine_sources(Span<const char *> sources)
{
  std::stringstream combined;
  for (int i = 0; i < sources.size(); i++) {
    combined << sources[i];
  }
  return combined.str();
}
static char *glsl_patch_get()
{
  static char patch[512] = "\0";
  if (patch[0] != '\0') {
    return patch;
  }

  size_t slen = 0;
  /* Version need to go first. */
  STR_CONCAT(patch, slen, "#version 330\n");

  BLI_assert(slen < sizeof(patch));
  return patch;
}

std::string VKShader::read_back(const char *filename)
{
  std::ifstream file(filename, std::ios_base::binary);
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::unique_ptr<std::vector<uint32_t>> VKShader::compile_source(Span<const char *> sources,
                                                                VKShaderStageType stage)
{
  //char testName = new char(name);
  
  std::string myShaderName = read_back(name);
  //myShaderName = myShaderName;

  //if (myShaderName != "abjDebug")
  //  return;

  std::string stage_name = to_stage_name(name, stage);

  std::string source = combine_sources(sources);

  shader_compiler::Compiler *compiler = shader_compiler::Compiler::create_default();
  shader_compiler::Job job;
  job.name = stage_name;
  job.source = source;
  job.compilation_target = shader_compiler::TargetType::SpirV;
  job.source_type = to_source_type(stage);

  shader_compiler::Result *result = compiler->compile(job);

  if (!result) {
    return std::make_unique<std::vector<uint32_t>>();
  }

  /* Log compilation errors/warnings. */
  if (!result->error_log.empty()) {
    const char *error_log = result->error_log.c_str();
    std::vector<char> error(error_log, error_log + result->error_log.size() + 1);
    VKLogParser parser;

    std::cout << "SHADER that has error : " << myShaderName << std::endl;
    std::cout << "source : " << std::endl;
    std::cout << source << std::endl;


    print_log(sources,
              error.data(),
              to_stage_name(stage).c_str(),
              result->status_code == shader_compiler::StatusCode::CompilationError,
              &parser);
    BLI_assert(!"Failed to compile shader!");
  }

  /* Retrieve compiled code. */
  std::unique_ptr<std::vector<uint32_t>> bin = std::make_unique<std::vector<uint32_t>>();
  switch (result->status_code) {
    case shader_compiler::StatusCode::Ok:
      bin = std::make_unique<std::vector<uint32_t>>(std::move(result->bin));
      break;
    case shader_compiler::StatusCode::CompilationError:
      break;
  }

  delete result;
  delete compiler;
  return bin;
}

VkShaderModule VKShader::create_shader_module(MutableSpan<const char *> sources,
                                              VKShaderStageType stage)
{
  /* Patch the shader code using the first source slot. */
  sources[0] = glsl_patch_get();

  std::unique_ptr<std::vector<uint32_t>> code = compile_source(sources, stage);
  if (!code || code->size() == 0) {
    compilation_failed_ = true;
    return VK_NULL_HANDLE;
  }

  VkDevice device = context_->device_get();
  VkShaderModuleCreateInfo shader_info = {};
  shader_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  shader_info.codeSize = code->size();
  shader_info.pCode = reinterpret_cast<const uint32_t *>(code->data());

  VkShaderModule shader_module;
  if (vkCreateShaderModule(device, &shader_info, nullptr, &shader_module) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }

  return shader_module;
}

void VKShader::vertex_shader_from_glsl(MutableSpan<const char *> sources)
{
#ifdef WITH_VULKAN_SHADER_COMPILATION
  vertex_shader_ = this->create_shader_module(sources, VKShaderStageType::VertexShader);
#endif
}

void VKShader::geometry_shader_from_glsl(MutableSpan<const char *> sources)
{
#ifdef WITH_VULKAN_SHADER_COMPILATION
  geometry_shader_ = this->create_shader_module(sources, VKShaderStageType::GeometryShader);
#endif
}

void VKShader::fragment_shader_from_glsl(MutableSpan<const char *> sources)
{
#ifdef WITH_VULKAN_SHADER_COMPILATION
  fragment_shader_ = this->create_shader_module(sources, VKShaderStageType::FragmentShader);
#endif
}

void VKShader::compute_shader_from_glsl(MutableSpan<const char *> sources)
{
#ifdef WITH_VULKAN_SHADER_COMPILATION
  compute_shader_ = this->create_shader_module(sources, VKShaderStageType::ComputeShader);
#endif
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Linking
 * \{ */

bool VKShader::finalize(void)
{
#ifdef WITH_VULKAN_SHADER_COMPILATION
  if (compilation_failed_) {
    return false;
  }
#endif

  return true;

  //return false;
};

/** \} */

}  // namespace blender::gpu
