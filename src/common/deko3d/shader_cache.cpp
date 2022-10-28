#include "shader_cache.h"
#include "../assert.h"
#include "../file_system.h"
#include "../log.h"
#include "../md5_digest.h"
#include "context.h"
#include "util.h"
#include <uam.h>
Log_SetChannel(Deko3D::ShaderCache);

// TODO: store the driver version and stuff in the shader header

std::unique_ptr<Deko3D::ShaderCache> g_deko3d_shader_cache;

namespace Deko3D {

uam_pipeline_stage TranslatePipelineStage(ShaderCache::ShaderType type)
{
  switch (type)
  {
    case ShaderCache::ShaderType::Vertex:
      return uam_pipeline_stage_vertex;
    case ShaderCache::ShaderType::Geometry:
      return uam_pipeline_stage_geometry;
    case ShaderCache::ShaderType::Fragment:
      return uam_pipeline_stage_fragment;
    case ShaderCache::ShaderType::Compute:
      return uam_pipeline_stage_compute;
  }
  Panic("Unknown shader type. Should be unreachable");
  return uam_pipeline_stage_vertex;
}

std::optional<std::vector<u8>> CompileShader(ShaderCache::ShaderType stage, std::string_view source)
{
  // this is all very inefficient, though duckstation compiles all shaders
  // it needs before actually running the emulation so it's hopefully ok
  std::string shader_source(source);
  u8* output;
  uint32_t size;
  bool result = uam_compileDksh(TranslatePipelineStage(stage), shader_source.c_str(), 3, &output, &size);
  if (result)
  {
    std::vector<u8> final_output;
    final_output.resize(size);
    memcpy(&final_output[0], output, size);
    free(output);
    return std::make_optional(final_output);
  }
  else
  {
    return std::optional<std::vector<u8>>{};
  }
}

#pragma pack(push)
struct CacheIndexEntry
{
  u64 source_hash_low;
  u64 source_hash_high;
  u32 source_length;
  u32 shader_type;
  u32 file_offset;
  u32 blob_size;
};
#pragma pack(pop)

ShaderCache::ShaderCache() = default;

ShaderCache::~ShaderCache()
{
  CloseShaderCache();
}

bool ShaderCache::CacheIndexKey::operator==(const CacheIndexKey& key) const
{
  return (source_hash_low == key.source_hash_low && source_hash_high == key.source_hash_high &&
          source_length == key.source_length && shader_type == key.shader_type);
}

bool ShaderCache::CacheIndexKey::operator!=(const CacheIndexKey& key) const
{
  return (source_hash_low != key.source_hash_low || source_hash_high != key.source_hash_high ||
          source_length != key.source_length || shader_type != key.shader_type);
}

void ShaderCache::Create(std::string_view base_path, u32 version, bool debug)
{
  uam_init();
  Assert(!g_deko3d_shader_cache);
  g_deko3d_shader_cache.reset(new ShaderCache());
  g_deko3d_shader_cache->Open(base_path, version, debug);
}

void ShaderCache::Destroy()
{
  g_deko3d_shader_cache.reset();
  uam_deinit();
}

void ShaderCache::Open(std::string_view base_path, u32 version, bool debug)
{
  m_version = version;
  m_debug = debug;

  if (!base_path.empty())
  {
    const std::string base_filename = GetShaderCacheBaseFileName(base_path, debug);
    const std::string index_filename = base_filename + ".idx";
    const std::string blob_filename = base_filename + ".bin";

    if (!ReadExistingShaderCache(index_filename, blob_filename))
      CreateNewShaderCache(index_filename, blob_filename);
  }
}

bool ShaderCache::CreateNewShaderCache(const std::string& index_filename, const std::string& blob_filename)
{
  if (FileSystem::FileExists(index_filename.c_str()))
  {
    Log_WarningPrintf("Removing existing index file '%s'", index_filename.c_str());
    FileSystem::DeleteFile(index_filename.c_str());
  }
  if (FileSystem::FileExists(blob_filename.c_str()))
  {
    Log_WarningPrintf("Removing existing blob file '%s'", blob_filename.c_str());
    FileSystem::DeleteFile(blob_filename.c_str());
  }

  m_index_file = FileSystem::OpenCFile(index_filename.c_str(), "wb");
  if (!m_index_file)
  {
    Log_ErrorPrintf("Failed to open index file '%s' for writing", index_filename.c_str());
    return false;
  }

  const u32 index_version = FILE_VERSION;

  if (std::fwrite(&index_version, sizeof(index_version), 1, m_index_file) != 1 ||
      std::fwrite(&m_version, sizeof(m_version), 1, m_index_file) != 1)
  {
    Log_ErrorPrintf("Failed to write header to index file '%s'", index_filename.c_str());
    std::fclose(m_index_file);
    m_index_file = nullptr;
    FileSystem::DeleteFile(index_filename.c_str());
    return false;
  }

  m_blob_file = FileSystem::OpenCFile(blob_filename.c_str(), "w+b");
  if (!m_blob_file)
  {
    Log_ErrorPrintf("Failed to open blob file '%s' for writing", blob_filename.c_str());
    std::fclose(m_index_file);
    m_index_file = nullptr;
    FileSystem::DeleteFile(index_filename.c_str());
    return false;
  }

  return true;
}

bool ShaderCache::ReadExistingShaderCache(const std::string& index_filename, const std::string& blob_filename)
{
  m_index_file = FileSystem::OpenCFile(index_filename.c_str(), "r+b");
  if (!m_index_file)
    return false;

  u32 file_version = 0;
  u32 data_version = 0;
  if (std::fread(&file_version, sizeof(file_version), 1, m_index_file) != 1 || file_version != FILE_VERSION ||
      std::fread(&data_version, sizeof(data_version), 1, m_index_file) != 1 || data_version != m_version)
  {
    Log_ErrorPrintf("Bad file/data version in '%s'", index_filename.c_str());
    std::fclose(m_index_file);
    m_index_file = nullptr;
    return false;
  }

  m_blob_file = FileSystem::OpenCFile(blob_filename.c_str(), "a+b");
  if (!m_blob_file)
  {
    Log_ErrorPrintf("Blob file '%s' is missing", blob_filename.c_str());
    std::fclose(m_index_file);
    m_index_file = nullptr;
    return false;
  }

  std::fseek(m_blob_file, 0, SEEK_END);
  const u32 blob_file_size = static_cast<u32>(std::ftell(m_blob_file));

  for (;;)
  {
    CacheIndexEntry entry;
    if (std::fread(&entry, sizeof(entry), 1, m_index_file) != 1 ||
        (entry.file_offset + entry.blob_size) > blob_file_size)
    {
      if (std::feof(m_index_file))
        break;

      Log_ErrorPrintf("Failed to read entry from '%s', corrupt file?", index_filename.c_str());
      m_index.clear();
      std::fclose(m_blob_file);
      m_blob_file = nullptr;
      std::fclose(m_index_file);
      m_index_file = nullptr;
      return false;
    }

    const CacheIndexKey key{entry.source_hash_low, entry.source_hash_high, entry.source_length,
                            static_cast<ShaderType>(entry.shader_type)};
    const CacheIndexData data{entry.file_offset, entry.blob_size};
    m_index.emplace(key, data);
  }

  // ensure we don't write before seeking
  std::fseek(m_index_file, 0, SEEK_END);

  Log_InfoPrintf("Read %zu entries from '%s'", m_index.size(), index_filename.c_str());
  return true;
}

void ShaderCache::CloseShaderCache()
{
  if (m_index_file)
  {
    std::fclose(m_index_file);
    m_index_file = nullptr;
  }
  if (m_blob_file)
  {
    std::fclose(m_blob_file);
    m_blob_file = nullptr;
  }
}

std::string ShaderCache::GetShaderCacheBaseFileName(const std::string_view& base_path, bool debug)
{
  std::string base_filename(base_path);
  base_filename += "/deko3d_shaders";

  if (debug)
    base_filename += "_debug";

  return base_filename;
}

ShaderCache::CacheIndexKey ShaderCache::GetCacheKey(ShaderCache::ShaderType type, const std::string_view& shader_code)
{
  union HashParts
  {
    struct
    {
      u64 hash_low;
      u64 hash_high;
    };
    u8 hash[16];
  };
  HashParts h;

  MD5Digest digest;
  digest.Update(shader_code.data(), static_cast<u32>(shader_code.length()));
  digest.Final(h.hash);

  return CacheIndexKey{h.hash_low, h.hash_high, static_cast<u32>(shader_code.length()), type};
}

std::optional<std::vector<u8>> ShaderCache::GetShaderDKSH(ShaderCache::ShaderType type, std::string_view shader_code)
{
  const auto key = GetCacheKey(type, shader_code);
  auto iter = m_index.find(key);
  if (iter == m_index.end())
    return CompileAndAddShaderDKSH(key, shader_code);

  std::vector<u8> dksh(iter->second.blob_size);
  if (std::fseek(m_blob_file, iter->second.file_offset, SEEK_SET) != 0 ||
      std::fread(dksh.data(), 1, iter->second.blob_size, m_blob_file) != iter->second.blob_size)
  {
    Log_ErrorPrintf("Read blob from file failed, recompiling");
    return CompileShader(type, shader_code);
  }

  return dksh;
}

struct DkshHeader
{
  uint32_t magic;     // DKSH_MAGIC
  uint32_t header_sz; // sizeof(DkshHeader)
  uint32_t control_sz;
  uint32_t code_sz;
  uint32_t programs_off;
  uint32_t num_programs;
};


bool ShaderCache::GetShaderModule(ShaderType type, std::string_view shader_code, dk::Shader& shader,
                                  MemoryHeap::Allocation& shader_memory)
{
  std::optional<std::vector<u8>> dksh = GetShaderDKSH(type, shader_code);
  if (!dksh.has_value())
    return false;

  DkshHeader* header = reinterpret_cast<DkshHeader*>(&(*dksh)[0]);

  shader_memory = g_deko3d_context->GetShaderHeap().Alloc(header->code_sz, DK_SHADER_CODE_ALIGNMENT);
  memcpy(g_deko3d_context->GetShaderHeap().CpuAddr<void>(shader_memory), &(*dksh)[header->control_sz], header->code_sz);

  dk::ShaderMaker{g_deko3d_context->GetShaderHeap().GetMemBlock(), shader_memory.offset}
    .setControl(&(*dksh)[0])
    .initialize(shader);

  return true;
}

bool ShaderCache::GetVertexShader(std::string_view shader_code, dk::Shader& shader,
                                  MemoryHeap::Allocation& shader_memory)
{
  return GetShaderModule(ShaderType::Vertex, std::move(shader_code), shader, shader_memory);
}

bool ShaderCache::GetGeometryShader(std::string_view shader_code, dk::Shader& shader,
                                    MemoryHeap::Allocation& shader_memory)
{
  return GetShaderModule(ShaderType::Geometry, std::move(shader_code), shader, shader_memory);
}

bool ShaderCache::GetFragmentShader(std::string_view shader_code, dk::Shader& shader,
                                    MemoryHeap::Allocation& shader_memory)
{
  return GetShaderModule(ShaderType::Fragment, std::move(shader_code), shader, shader_memory);
}

bool ShaderCache::GetComputeShader(std::string_view shader_code, dk::Shader& shader,
                                   MemoryHeap::Allocation& shader_memory)
{
  return GetShaderModule(ShaderType::Compute, std::move(shader_code), shader, shader_memory);
}

std::optional<std::vector<u8>> ShaderCache::CompileAndAddShaderDKSH(const CacheIndexKey& key,
                                                                    std::string_view shader_code)
{
  std::optional<std::vector<u8>> dksh = CompileShader(key.shader_type, shader_code);
  if (!dksh.has_value())
    return {};

  if (!m_blob_file || std::fseek(m_blob_file, 0, SEEK_END) != 0)
    return dksh;

  CacheIndexData data;
  data.file_offset = static_cast<u32>(std::ftell(m_blob_file));
  data.blob_size = static_cast<u32>(dksh->size());

  CacheIndexEntry entry = {};
  entry.source_hash_low = key.source_hash_low;
  entry.source_hash_high = key.source_hash_high;
  entry.source_length = key.source_length;
  entry.shader_type = static_cast<u32>(key.shader_type);
  entry.blob_size = data.blob_size;
  entry.file_offset = data.file_offset;

  if (std::fwrite(dksh->data(), 1, entry.blob_size, m_blob_file) != entry.blob_size || std::fflush(m_blob_file) != 0 ||
      std::fwrite(&entry, sizeof(entry), 1, m_index_file) != 1 || std::fflush(m_index_file) != 0)
  {
    Log_ErrorPrintf("Failed to write shader blob to file");
    return dksh;
  }

  m_index.emplace(key, data);
  return dksh;
}

} // namespace Deko3D