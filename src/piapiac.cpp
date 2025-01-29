#include "piapiac.hpp"
#include <iostream>

namespace eight99bushwick
{
  namespace piapiac
  {
    DuckDBMgr::DuckDBMgr(const std::string &path) noexcept : _db(NULL), _conn(NULL), _path(path)
    {
    }

    DuckDBMgr::~DuckDBMgr() noexcept
    {
    }

    bool DuckDBMgr::Init() noexcept
    {
      duckdb_config config;

      if (_path.empty())
      {
        return false;
      }

      // create the configuration object
      if (duckdb_create_config(&config) == DuckDBError)
      {
        return false;
      }

      duckdb_set_config(config, "access_mode", "READ_WRITE"); // or READ_ONLY
      duckdb_set_config(config, "threads", "2");
      duckdb_set_config(config, "max_memory", "8GB");
      duckdb_set_config(config, "default_order", "DESC");

      if (duckdb_open_ext(_path.c_str(), &_db, config, NULL) == DuckDBError)
      {
        duckdb_destroy_config(&config);
        return false;
      }

      if (duckdb_connect(_db, &_conn) == DuckDBError)
      {
        duckdb_destroy_config(&config);
        return false;
      }

      duckdb_destroy_config(&config);
      return true;
    }

    void DuckDBMgr::Destroy() noexcept
    {
      if (_conn)
      {
        duckdb_disconnect(&_conn);
      }

      if (_db)
      {
        duckdb_close(&_db);
      }
    }

    bool DuckDBMgr::Query(const std::string &query, duckdb_result *result) noexcept
    {
      return duckdb_query(_conn, query.c_str(), result) != DuckDBError;
    }

  } // namespace piapiac
} // namespace eight99bushwick
