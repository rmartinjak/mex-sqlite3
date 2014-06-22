#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include <sqlite3.h>

#include "mex.h"

#include "structlist.h"

#define TYPECHECK(ARRAY, ...) \
    do { \
        mxClassID got = mxGetClassID(ARRAY); \
        mxClassID expect[] = { __VA_ARGS__ }; \
        int ok = 0; \
        for (int i = 0, n = sizeof expect / sizeof expect[0]; i < n; i++) { \
            if (got == expect[i]) { \
                ok = 1; \
            } \
        } \
        if (!ok) { \
            mexErrMsgIdAndTxt("sqlite3:typecheck", "BUG: wrong array type"); \
        } \
    } while (0)

int bind_string(sqlite3_stmt *stmt, int index, const mxArray *array)
{
    TYPECHECK(array, mxCHAR_CLASS);
    char *s = mxArrayToString(array);
    int res = sqlite3_bind_text(stmt, index, s,
                                -1,  // ALL the bytes
                                SQLITE_TRANSIENT  // make a copy
                               );
    mxFree(s);
    return res;
}

int bind_double(sqlite3_stmt *stmt, int index, const mxArray *array)
{
    TYPECHECK(array, mxSINGLE_CLASS, mxDOUBLE_CLASS);
    double val = mxGetScalar(array);
    return sqlite3_bind_double(stmt, index, val);
}

int bind_int64(sqlite3_stmt *stmt, int index, const mxArray *array)
{
    int64_t val = 0;
    TYPECHECK(array,
              mxINT16_CLASS, mxUINT16_CLASS,
              mxINT32_CLASS, mxUINT32_CLASS,
              mxINT64_CLASS, mxUINT64_CLASS);

    mxClassID cls = mxGetClassID(array);
#define GET_VALUE(CLASS, TYPE) \
    if (cls == CLASS) { \
        TYPE *p = mxGetData(array); \
        val = *p; \
    }
    GET_VALUE(mxINT16_CLASS, int16_t);
    GET_VALUE(mxUINT16_CLASS, uint16_t);
    GET_VALUE(mxINT16_CLASS, int32_t);
    GET_VALUE(mxUINT16_CLASS, uint32_t);
    GET_VALUE(mxINT16_CLASS, int64_t);
    GET_VALUE(mxUINT16_CLASS, uint64_t);
    return sqlite3_bind_int64(stmt, index, val);
}

int bind_params(sqlite3_stmt* stmt, const mxArray *params, int column)
{
    TYPECHECK(params, mxSTRUCT_CLASS);

    for (int i = 0, n = mxGetNumberOfFields(params); i < n; i++) {
        mxArray *array = mxGetFieldByNumber(params, column, i);
        mxClassID cls = mxGetClassID(array);
        int res;
        switch (cls) {
            case mxFUNCTION_CLASS:
                break;

            case mxCHAR_CLASS:
                res = bind_string(stmt, i + 1, array);
                break;

            case mxSINGLE_CLASS:
            case mxDOUBLE_CLASS:
                res = bind_double(stmt, i + 1, array);
                break;

            default:  // anything else is an integer
                res = bind_int64(stmt, i + 1, array);
        }
        if (res != SQLITE_OK) {
            return res;
        }
    }
    return SQLITE_OK;
}

int execute_many(sqlite3_stmt *stmt, const mxArray *params)
{
    for (int i = 0, n = mxGetN(params); i < n; i++) {
        mexPrintf("binding params %d of %zu\n", i, mxGetM(params));
        if (bind_params(stmt, params, i) != SQLITE_OK) {
            mexErrMsgIdAndTxt("sqlite3:bind",
                              "failed to bind parameters to statement");
        }
        int res = sqlite3_step(stmt);
        if (res != SQLITE_DONE) {
            return res;
        }
        sqlite3_reset(stmt);
        sqlite3_clear_bindings(stmt);
    }
    return SQLITE_DONE;
}

mxArray *get_column(sqlite3_stmt *stmt, int index)
{
    int type = sqlite3_column_type(stmt, index);
    if (type == SQLITE_TEXT) {
        const char *text = sqlite3_column_text(stmt, index);
        return mxCreateCharMatrixFromStrings(1, &text);
    } else if (type == SQLITE_FLOAT) {
        return mxCreateDoubleScalar(sqlite3_column_double(stmt, index));
    } else if (type == SQLITE_INTEGER) {
        mxArray *a = mxCreateNumericMatrix(1, 1, mxINT64_CLASS, mxREAL);
        int64_t val = sqlite3_column_int64(stmt, index);
        memcpy(mxGetData(a), &val, sizeof val);
        return a;
    }
    mexErrMsgIdAndTxt("sqlite3:get_column", "unsupported column type");
    return NULL;
}

int fetch_row(sqlite3_stmt *stmt, int num_columns, mxArray **array)
{
    int res = sqlite3_step(stmt);
    if (res == SQLITE_ROW) {
        for (int i = 0; i < num_columns; i++) {
            mxSetFieldByNumber(*array, 0, i, get_column(stmt, i));
        }
    }
    return res;
}

int fetch_results(sqlite3_stmt *stmt, mxArray **results)
{
    struct structlist result_list;
    structlist_init(&result_list);
    int num_columns = sqlite3_column_count(stmt);
    mxArray *row = mxCreateStructMatrix(1, 1, 0, NULL);
    for (int i = 0; i < num_columns; i++) {
        const char *name = sqlite3_column_name(stmt, i);
        mxAddField(row, name);
    }

    int res;
    while (res = fetch_row(stmt, num_columns, &row),
           res == SQLITE_ROW) {
        structlist_add(&result_list, row);
    }
    *results = structlist_collapse(&result_list);
    return res;
}

void
mexFunction(int nlhs, mxArray *plhs[], int nrhs, const mxArray *prhs[])
{
    enum { RHS_FILENAME, RHS_QUERY, RHS_PARAMS };
    (void) nlhs;

    if (nrhs < 2) {
        mexErrMsgIdAndTxt("sqlite3:sqlite3",
                          "usage: sqlite3(file, query[, params])");
    }

    char *db_path = mxArrayToString(prhs[RHS_FILENAME]);
    char *query = mxArrayToString(prhs[RHS_QUERY]);

    sqlite3 *db;
    sqlite3_stmt *stmt;

    if (sqlite3_open(db_path, &db) != SQLITE_OK) {
        mexErrMsgIdAndTxt("sqlite3:open", "failed to open db");
    }

    if (sqlite3_prepare_v2(db, query, -1, &stmt, NULL) != SQLITE_OK) {
        mexPrintf("invalid query:\n  %s\n", query);
        mexErrMsgIdAndTxt(
            "sqlite3:prepare",
            "Failed to prepare query. Reasons could be invalid syntax or "
            "creating a table that already exists.");
    }

    int res;
    int num_columns = sqlite3_column_count(stmt);

    if (num_columns > 0) {
        // Looks like a SELECT query
        res = fetch_results(stmt, &plhs[0]);
    } else {
        // If the user passes a struct array, use this to execute the
        // statement for each column
        if (nrhs > 2) {
            res = execute_many(stmt, prhs[RHS_PARAMS]);
        } else {
            res = sqlite3_step(stmt);
        }
    }

    if (res == SQLITE_DONE) {
        // success!
    } else {
        mexErrMsgIdAndTxt("sqlite3:step",
                          "proper error handling is hard, eh?");
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
}
