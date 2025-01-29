
#include "piapiac.hpp"
#include <iostream>

namespace eight99bushwick
{
  namespace piapiac
  {
    DuckDBMgr::DuckDBMgr() noexcept : _db(NULL), _conn(NULL)
    {
    }

    DuckDBMgr::~DuckDBMgr() noexcept
    {
    }

    bool DuckDBMgr::Init() noexcept
    {
      if (duckdb_open(NULL, &_db) == DuckDBError)
      {
        goto fail;
      }

      if (duckdb_connect(_db, &_conn) == DuckDBError)
      {
        goto fail;
      }

      return true;

    fail:
      return false;
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
