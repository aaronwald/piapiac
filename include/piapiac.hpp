#pragma once

#include <duckdb.h>
#include <string>

namespace eight99bushwick
{
  namespace piapiac
  {
    class DockerMgr
    {
    private:
      duckdb_database _db;
      duckdb_connection _conn;

    public:
      DockerMgr() noexcept;
      virtual ~DockerMgr() noexcept;
      DockerMgr(const DockerMgr &) = delete;
      DockerMgr &operator=(const DockerMgr &) = delete;
      DockerMgr(DockerMgr &&) = delete;
      DockerMgr &operator=(DockerMgr &&) = delete;

      bool Init() noexcept;
      bool Query(const std::string &query) noexcept;
      bool Query(const std::string &query, duckdb_result *result) noexcept;
    };
  } // namespace piapiac
}
