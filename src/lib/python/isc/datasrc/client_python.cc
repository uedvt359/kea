// Copyright (C) 2011  Internet Systems Consortium, Inc. ("ISC")
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
// REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
// AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
// INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
// LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
// OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
// PERFORMANCE OF THIS SOFTWARE.

// Enable this if you use s# variants with PyArg_ParseTuple(), see
// http://docs.python.org/py3k/c-api/arg.html#strings-and-buffers
//#define PY_SSIZE_T_CLEAN

// Python.h needs to be placed at the head of the program file, see:
// http://docs.python.org/py3k/extending/extending.html#a-simple-example
#include <Python.h>

#include <util/python/pycppwrapper_util.h>

#include <datasrc/client.h>
#include <datasrc/database.h>
#include <datasrc/data_source.h>
#include <datasrc/sqlite3_accessor.h>
#include <datasrc/iterator.h>

#include <dns/python/name_python.h>
#include <dns/python/rrset_python.h>
#include <dns/python/pydnspp_common.h>

#include "datasrc.h"
#include "client_python.h"
#include "finder_python.h"
#include "iterator_python.h"
#include "updater_python.h"
#include "client_inc.cc"

using namespace std;
using namespace isc::util::python;
using namespace isc::dns::python;
using namespace isc::datasrc;
using namespace isc::datasrc::python;

namespace {
// The s_* Class simply covers one instantiation of the object
class s_DataSourceClient : public PyObject {
public:
    s_DataSourceClient() : cppobj(NULL) {};
    DataSourceClient* cppobj;
};

// Shortcut type which would be convenient for adding class variables safely.
typedef CPPPyObjectContainer<s_DataSourceClient, DataSourceClient>
    DataSourceClientContainer;

PyObject*
DataSourceClient_findZone(PyObject* po_self, PyObject* args) {
    s_DataSourceClient* const self = static_cast<s_DataSourceClient*>(po_self);
    PyObject *name;
    if (PyArg_ParseTuple(args, "O!", &name_type, &name)) {
        try {
            DataSourceClient::FindResult find_result(
                self->cppobj->findZone(PyName_ToName(name)));

            result::Result r = find_result.code;
            ZoneFinderPtr zfp = find_result.zone_finder;
            // Use N instead of O so refcount isn't increased twice
            return (Py_BuildValue("IN", r, createZoneFinderObject(zfp)));
        } catch (const std::exception& exc) {
            PyErr_SetString(getDataSourceException("Error"), exc.what());
            return (NULL);
        } catch (...) {
            PyErr_SetString(getDataSourceException("Error"),
                            "Unexpected exception");
            return (NULL);
        }
    } else {
        return (NULL);
    }
}

PyObject*
DataSourceClient_getIterator(PyObject* po_self, PyObject* args) {
    s_DataSourceClient* const self = static_cast<s_DataSourceClient*>(po_self);
    PyObject *name_obj;
    if (PyArg_ParseTuple(args, "O!", &name_type, &name_obj)) {
        try {
            return (createZoneIteratorObject(
                        self->cppobj->getIterator(PyName_ToName(name_obj))));
        } catch (const isc::NotImplemented& ne) {
            PyErr_SetString(getDataSourceException("NotImplemented"),
                            ne.what());
            return (NULL);
        } catch (const DataSourceError& dse) {
            PyErr_SetString(getDataSourceException("Error"), dse.what());
            return (NULL);
        } catch (const std::exception& exc) {
            PyErr_SetString(getDataSourceException("Error"), exc.what());
            return (NULL);
        } catch (...) {
            PyErr_SetString(getDataSourceException("Error"),
                            "Unexpected exception");
            return (NULL);
        }
    } else {
        return (NULL);
    }
}

PyObject*
DataSourceClient_getUpdater(PyObject* po_self, PyObject* args) {
    s_DataSourceClient* const self = static_cast<s_DataSourceClient*>(po_self);
    PyObject *name_obj;
    PyObject *replace_obj;
    if (PyArg_ParseTuple(args, "O!O", &name_type, &name_obj, &replace_obj) &&
        PyBool_Check(replace_obj)) {
        bool replace = (replace_obj != Py_False);
        try {
            ZoneUpdaterPtr updater =
                self->cppobj->getUpdater(PyName_ToName(name_obj), replace);
            if (!updater) {
                return (Py_None);
            }
            return (createZoneUpdaterObject(updater));
        } catch (const isc::NotImplemented& ne) {
            PyErr_SetString(getDataSourceException("NotImplemented"),
                            ne.what());
            return (NULL);
        } catch (const DataSourceError& dse) {
            PyErr_SetString(getDataSourceException("Error"), dse.what());
            return (NULL);
        } catch (const std::exception& exc) {
            PyErr_SetString(getDataSourceException("Error"), exc.what());
            return (NULL);
        } catch (...) {
            PyErr_SetString(getDataSourceException("Error"),
                            "Unexpected exception");
            return (NULL);
        }
    } else {
        return (NULL);
    }
}

// This list contains the actual set of functions we have in
// python. Each entry has
// 1. Python method name
// 2. Our static function here
// 3. Argument type
// 4. Documentation
PyMethodDef DataSourceClient_methods[] = {
    { "find_zone", reinterpret_cast<PyCFunction>(DataSourceClient_findZone),
      METH_VARARGS, DataSourceClient_findZone_doc },
    { "get_iterator",
      reinterpret_cast<PyCFunction>(DataSourceClient_getIterator), METH_VARARGS,
      DataSourceClient_getIterator_doc },
    { "get_updater", reinterpret_cast<PyCFunction>(DataSourceClient_getUpdater),
      METH_VARARGS, DataSourceClient_getUpdater_doc },
    { NULL, NULL, 0, NULL }
};

int
DataSourceClient_init(s_DataSourceClient* self, PyObject* args) {
    // TODO: we should use the factory function which hasn't been written
    // yet. For now we hardcode the sqlite3 initialization, and pass it one
    // string for the database file. (similar to how the 'old direct'
    // sqlite3_ds code works).  Of course, we shouldn't hardcode the RR class
    // at that point.
    try {
        char* db_file_name;
        if (PyArg_ParseTuple(args, "s", &db_file_name)) {
            boost::shared_ptr<DatabaseAccessor> sqlite3_accessor(
                new SQLite3Accessor(db_file_name, "IN"));
            self->cppobj = new DatabaseClient(isc::dns::RRClass::IN(),
                                              sqlite3_accessor);
            return (0);
        } else {
            return (-1);
        }

    } catch (const exception& ex) {
        const string ex_what = "Failed to construct DataSourceClient object: " +
            string(ex.what());
        PyErr_SetString(getDataSourceException("Error"), ex_what.c_str());
        return (-1);
    } catch (...) {
        PyErr_SetString(PyExc_RuntimeError,
            "Unexpected exception in constructing DataSourceClient");
        return (-1);
    }
    PyErr_SetString(PyExc_TypeError,
                    "Invalid arguments to DataSourceClient constructor");

    return (-1);
}

void
DataSourceClient_destroy(s_DataSourceClient* const self) {
    delete self->cppobj;
    self->cppobj = NULL;
    Py_TYPE(self)->tp_free(self);
}

} // end anonymous namespace

namespace isc {
namespace datasrc {
namespace python {
// This defines the complete type for reflection in python and
// parsing of PyObject* to s_DataSourceClient
// Most of the functions are not actually implemented and NULL here.
PyTypeObject datasourceclient_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "datasrc.DataSourceClient",
    sizeof(s_DataSourceClient),         // tp_basicsize
    0,                                  // tp_itemsize
    reinterpret_cast<destructor>(DataSourceClient_destroy),// tp_dealloc
    NULL,                               // tp_print
    NULL,                               // tp_getattr
    NULL,                               // tp_setattr
    NULL,                               // tp_reserved
    NULL,                               // tp_repr
    NULL,                               // tp_as_number
    NULL,                               // tp_as_sequence
    NULL,                               // tp_as_mapping
    NULL,                               // tp_hash
    NULL,                               // tp_call
    NULL,                               // tp_str
    NULL,                               // tp_getattro
    NULL,                               // tp_setattro
    NULL,                               // tp_as_buffer
    Py_TPFLAGS_DEFAULT,                 // tp_flags
    DataSourceClient_doc,
    NULL,                               // tp_traverse
    NULL,                               // tp_clear
    NULL,                               // tp_richcompare
    0,                                  // tp_weaklistoffset
    NULL,                               // tp_iter
    NULL,                               // tp_iternext
    DataSourceClient_methods,           // tp_methods
    NULL,                               // tp_members
    NULL,                               // tp_getset
    NULL,                               // tp_base
    NULL,                               // tp_dict
    NULL,                               // tp_descr_get
    NULL,                               // tp_descr_set
    0,                                  // tp_dictoffset
    reinterpret_cast<initproc>(DataSourceClient_init),// tp_init
    NULL,                               // tp_alloc
    PyType_GenericNew,                  // tp_new
    NULL,                               // tp_free
    NULL,                               // tp_is_gc
    NULL,                               // tp_bases
    NULL,                               // tp_mro
    NULL,                               // tp_cache
    NULL,                               // tp_subclasses
    NULL,                               // tp_weaklist
    NULL,                               // tp_del
    0                                   // tp_version_tag
};

} // namespace python
} // namespace datasrc
} // namespace isc
