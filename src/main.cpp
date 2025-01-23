#include <stdlib.h>
#include <iostream>
#include <duckdb.h>
#include "piapiac.hpp"
#include "mqtt.hpp"

#include <echidna/event_mgr.hpp>

using eight99bushwick::piapiac::DockerMgr;

// TODO: Next epoll? and send mqtt connect to server

int main()
{
	DockerMgr dm;
	duckdb_result result;
	idx_t row_count = 0;
	idx_t column_count = 0;

	if (!dm.Init())
	{
		exit(EXIT_FAILURE);
	}

	if (!dm.Query("CREATE TABLE integers(i INTEGER, j INTEGER);"))
	{
		goto cleanup;
	}

	if (!dm.Query("INSERT INTO integers VALUES (3, 4), (5, 6), (7, NULL);"))
	{
		goto cleanup;
	}

	if (!dm.Query("SELECT * FROM integers;", &result))
	{
		goto cleanup;
	}

	// print the names of the result
	row_count = duckdb_row_count(&result);
	column_count = duckdb_column_count(&result);
	for (size_t i = 0; i < column_count; i++)
	{
		printf("%s ", duckdb_column_name(&result, i));
	}
	printf("\n");
	// print the data of the result
	for (size_t row_idx = 0; row_idx < row_count; row_idx++)
	{
		for (size_t col_idx = 0; col_idx < column_count; col_idx++)
		{
			char *val = duckdb_value_varchar(&result, col_idx, row_idx);
			printf("%s ", val);
			duckdb_free(val);
		}
		printf("\n");
	}
	// duckdb_print_result(result);
	exit(EXIT_SUCCESS);
cleanup:
	duckdb_destroy_result(&result);
	exit(EXIT_FAILURE);
}