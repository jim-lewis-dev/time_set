// Copyright (c) 2026 Jim Lewis. MIT License.
#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "time_set_io.hpp"
#include "cli.hpp"
#include <memory>

namespace {
struct python_error {};
struct reference {
    PyObject* p;
    explicit reference(PyObject* value) : p(value) { if (!p) throw python_error{}; }
    ~reference() { Py_DECREF(p); }
    reference(const reference&) = delete;
    reference& operator=(const reference&) = delete;
    PyObject* release() noexcept { auto* out = p; p = Py_None; Py_INCREF(p); return out; }
};
template<class F> PyObject* guard(F&& function) noexcept {
    try { return function(); }
    catch (const python_error&) { return nullptr; }
    catch (const std::bad_alloc&) { return PyErr_NoMemory(); }
    catch (const std::overflow_error& e) { PyErr_SetString(PyExc_OverflowError,e.what()); }
    catch (const std::invalid_argument& e) { PyErr_SetString(PyExc_ValueError,e.what()); }
    catch (const std::out_of_range& e) { PyErr_SetString(PyExc_ValueError,e.what()); }
    catch (const std::exception& e) { PyErr_SetString(PyExc_RuntimeError,e.what()); }
    catch (...) { PyErr_SetString(PyExc_RuntimeError,"unknown native exception"); }
    return nullptr;
}
[[noreturn]] void type_error(const char* message) { PyErr_SetString(PyExc_TypeError,message); throw python_error{}; }
std::string string(PyObject* value) {
    if (!PyUnicode_Check(value)) type_error("expected a string");
    Py_ssize_t length = 0;
    const char* data = PyUnicode_AsUTF8AndSize(value,&length);
    if (!data) throw python_error{};
    return {data,static_cast<std::size_t>(length)};
}
ts::tick_type integer(PyObject* value) {
    if (!PyLong_Check(value) || PyBool_Check(value)) type_error("expected an integer, not a float or boolean");
    const auto result = PyLong_AsLongLong(value);
    if (PyErr_Occurred()) throw python_error{};
    return static_cast<ts::tick_type>(result);
}
PyObject* text(const std::string& value) { return PyUnicode_FromStringAndSize(value.data(),static_cast<Py_ssize_t>(value.size())); }
constexpr const char* line_name = "time_set.timeline";
constexpr const char* set_name = "time_set.set";
void delete_line(PyObject* capsule) { delete static_cast<ts::timeline*>(PyCapsule_GetPointer(capsule,line_name)); }
void delete_set(PyObject* capsule) { delete static_cast<ts::time_set*>(PyCapsule_GetPointer(capsule,set_name)); }
ts::timeline& line(PyObject* capsule) {
    if (!PyCapsule_IsValid(capsule,line_name)) type_error("expected a native timeline handle");
    return *static_cast<ts::timeline*>(PyCapsule_GetPointer(capsule,line_name));
}
ts::time_set& set(PyObject* capsule) {
    if (!PyCapsule_IsValid(capsule,set_name)) type_error("expected a native time-set handle");
    return *static_cast<ts::time_set*>(PyCapsule_GetPointer(capsule,set_name));
}
PyObject* wrap(ts::timeline value) {
    auto pointer = std::make_unique<ts::timeline>(std::move(value));
    PyObject* capsule = PyCapsule_New(pointer.get(),line_name,delete_line);
    if (capsule) pointer.release();
    return capsule;
}
PyObject* wrap(ts::time_set value) {
    auto pointer = std::make_unique<ts::time_set>(std::move(value));
    PyObject* capsule = PyCapsule_New(pointer.get(),set_name,delete_set);
    if (capsule) pointer.release();
    return capsule;
}
ts::tick_type position(const ts::timeline& domain, PyObject* value) {
    return PyUnicode_Check(value) ? ts::parse_position(domain,string(value)) : integer(value);
}
ts::ticks duration(const ts::timeline& domain, PyObject* value) {
    return ts::ticks{PyUnicode_Check(value) ? ts::parse_ticks(domain,string(value)) : integer(value)};
}
PyObject* native_timeline(PyObject*, PyObject* args) {
    PyObject *length, *tick, *origin;
    if (!PyArg_ParseTuple(args,"OOO",&length,&tick,&origin)) return nullptr;
    return guard([&] {
        const auto n = PyUnicode_Check(length) ? string(length) : std::to_string(integer(length));
        return wrap(ts::make_timeline(n,string(tick),origin == Py_None ? "none" : string(origin)));
    });
}
PyObject* native_timeline_info(PyObject*, PyObject* args) {
    PyObject* handle;
    if (!PyArg_ParseTuple(args,"O",&handle)) return nullptr;
    return guard([&] {
        const auto& value = line(handle);
        reference origin{Py_NewRef(Py_None)};
        if (value.origin()) {
            PyObject* formatted = nullptr;
            try { formatted = text(ts::format_timestamp(*value.origin())); }
            catch (const std::out_of_range&) {
                formatted = Py_BuildValue("{s:L,s:L,s:L}","seconds",static_cast<long long>(value.origin()->whole_seconds()),
                    "numerator",static_cast<long long>(value.origin()->fraction().numerator()),
                    "denominator",static_cast<long long>(value.origin()->fraction().denominator()));
            }
            if (!formatted) throw python_error{};
            Py_DECREF(origin.p); origin.p = formatted;
        }
        return Py_BuildValue("{s:L,s:L,s:L,s:O}","length",static_cast<long long>(value.length()),
            "numerator",static_cast<long long>(value.tick_size().numerator()),
            "denominator",static_cast<long long>(value.tick_size().denominator()),"origin",origin.p);
    });
}
PyObject* native_line_equal(PyObject*, PyObject* args) {
    PyObject *a,*b;
    if (!PyArg_ParseTuple(args,"OO",&a,&b)) return nullptr;
    return guard([&] { return PyBool_FromLong(line(a) == line(b)); });
}
PyObject* native_make(PyObject*, PyObject* args) {
    PyObject *domain,*intervals;
    if (!PyArg_ParseTuple(args,"OO",&domain,&intervals)) return nullptr;
    return guard([&] {
        std::vector<ts::interval> items;
        reference sequence{PySequence_Fast(intervals,"intervals must be an iterable of pairs")};
        const auto n = PySequence_Fast_GET_SIZE(sequence.p);
        for (Py_ssize_t i = 0; i < n; ++i) {
            reference pair{PySequence_Fast(PySequence_Fast_GET_ITEM(sequence.p,i),"each interval must be a pair")};
            if (PySequence_Fast_GET_SIZE(pair.p) != 2) throw std::invalid_argument("each interval needs two endpoints");
            items.push_back({integer(PySequence_Fast_GET_ITEM(pair.p,0)),integer(PySequence_Fast_GET_ITEM(pair.p,1))});
        }
        return wrap(ts::time_set{line(domain),std::move(items)});
    });
}
PyObject* native_range(PyObject*, PyObject* args) {
    PyObject *domain,*lo,*hi; const char* boundaries;
    if (!PyArg_ParseTuple(args,"OOOs",&domain,&lo,&hi,&boundaries)) return nullptr;
    return guard([&] {
        const auto& value = line(domain);
        return wrap(value.range(position(value,lo),position(value,hi),ts::parse_bounds(boundaries)));
    });
}
PyObject* native_point(PyObject*, PyObject* args) {
    PyObject *domain,*p;
    if (!PyArg_ParseTuple(args,"OO",&domain,&p)) return nullptr;
    return guard([&] { return wrap(line(domain).point(position(line(domain),p))); });
}
PyObject* native_parse(PyObject*, PyObject* args) {
    PyObject *domain,*source;
    if (!PyArg_ParseTuple(args,"OO",&domain,&source)) return nullptr;
    return guard([&] { return wrap(ts::parse_text(line(domain),string(source))); });
}
PyObject* native_clone(PyObject*, PyObject* args) {
    PyObject* handle;
    if (!PyArg_ParseTuple(args,"O",&handle)) return nullptr;
    return guard([&] { return wrap(set(handle)); });
}
PyObject* native_domain(PyObject*, PyObject* args) {
    PyObject* handle;
    if (!PyArg_ParseTuple(args,"O",&handle)) return nullptr;
    return guard([&] { return wrap(set(handle).domain()); });
}
PyObject* native_intervals(PyObject*, PyObject* args) {
    PyObject* handle;
    if (!PyArg_ParseTuple(args,"O",&handle)) return nullptr;
    return guard([&] {
        const auto& value = set(handle);
        if (value.interval_count() > static_cast<std::size_t>(PY_SSIZE_T_MAX)) throw std::overflow_error("too many intervals for Python");
        reference tuple{PyTuple_New(static_cast<Py_ssize_t>(value.interval_count()))};
        Py_ssize_t i = 0;
        for (auto iv : value) {
            reference pair{Py_BuildValue("(LL)",static_cast<long long>(iv.lo),static_cast<long long>(iv.hi))};
            PyTuple_SET_ITEM(tuple.p,i++,pair.release());
        }
        return tuple.release();
    });
}
PyObject* native_binary(PyObject*, PyObject* args) {
    PyObject *a,*b; const char* operation; int inplace;
    if (!PyArg_ParseTuple(args,"OOsp",&a,&b,&operation,&inplace)) return nullptr;
    return guard([&] {
        auto& left = set(a); const auto& right = set(b);
        const std::string op{operation};
        auto result = op == "union" ? left | right : op == "intersection" ? left & right :
                      op == "difference" ? left - right : op == "xor" ? left ^ right :
                      throw std::invalid_argument("unknown set operation");
        if (inplace) { left = std::move(result); return Py_NewRef(Py_None); }
        return wrap(std::move(result));
    });
}
PyObject* native_complement(PyObject*, PyObject* args) {
    PyObject* handle;
    if (!PyArg_ParseTuple(args,"O",&handle)) return nullptr;
    return guard([&] { return wrap(~set(handle)); });
}
PyObject* native_shift(PyObject*, PyObject* args) {
    PyObject *handle,*amount; int reverse,inplace;
    if (!PyArg_ParseTuple(args,"OOpp",&handle,&amount,&reverse,&inplace)) return nullptr;
    return guard([&] {
        auto& value = set(handle);
        const auto n = duration(value.domain(),amount);
        auto result = reverse ? value - n : value + n;
        if (inplace) { value = std::move(result); return Py_NewRef(Py_None); }
        return wrap(std::move(result));
    });
}
PyObject* native_query(PyObject*, PyObject* args) {
    PyObject *handle,*other; const char* operation;
    if (!PyArg_ParseTuple(args,"OsO",&handle,&operation,&other)) return nullptr;
    return guard([&]() -> PyObject* {
        const auto& value = set(handle); const std::string op{operation};
        if (op == "ticks") return PyLong_FromLongLong(static_cast<long long>(value.tick_count()));
        if (op == "interval_count") return PyLong_FromSize_t(value.interval_count());
        if (op == "duration") {
            const auto d = value.covered_duration();
            return Py_BuildValue("(LL)",static_cast<long long>(d.numerator()),static_cast<long long>(d.denominator()));
        }
        bool result;
        if (op == "empty") result = value.empty();
        else if (op == "contains") result = value.contains(position(value.domain(),other));
        else if (op == "equal") result = value == set(other);
        else if (op == "subset") result = value <= set(other);
        else if (op == "proper_subset") result = value < set(other);
        else if (op == "overlaps") result = value.overlaps(set(other));
        else throw std::invalid_argument("unknown query");
        return PyBool_FromLong(result);
    });
}
PyObject* native_edit(PyObject*, PyObject* args) {
    PyObject *handle,*source,*lo,*hi,*to; const char *operation,*mode,*boundary; int clip;
    if (!PyArg_ParseTuple(args,"OsOOOOsps",&handle,&operation,&source,&lo,&hi,&to,&mode,&clip,&boundary)) return nullptr;
    return guard([&] {
        auto& value = set(handle); const auto& domain = value.domain();
        const std::string op{operation};
        if (op == "clear_all") { value.clear(); return Py_NewRef(Py_None); }
        const auto start = position(domain,lo), end = position(domain,hi);
        const auto ends = ts::parse_bounds(boundary);
        if (op == "fill") value.fill(start,end,ends);
        else if (op == "clear") value.clear(start,end,ends);
        else {
            const auto paste = std::string(mode) == "replace" ? ts::paste_mode::replace :
                std::string(mode) == "overlay" ? ts::paste_mode::overlay : throw std::invalid_argument("mode must be replace or overlay");
            const auto target = position(domain,to);
            if (op == "copy") {
                if (source == Py_None) value.copy_region(start,end,target,paste,clip != 0);
                else value.copy_from(set(source),start,end,target,paste,clip != 0);
            } else if (op == "move") {
                if (source == Py_None) value.move_region(start,end,target,paste,clip != 0);
                else value.move_from(set(source),start,end,target,paste,clip != 0);
            } else throw std::invalid_argument("unknown edit operation");
        }
        return Py_NewRef(Py_None);
    });
}
PyObject* native_repeat(PyObject*, PyObject* args) {
    PyObject *handle,*every,*count,*until;
    if (!PyArg_ParseTuple(args,"OOOO",&handle,&every,&count,&until)) return nullptr;
    return guard([&] {
        const auto& value = set(handle);
        if ((count == Py_None) == (until == Py_None)) throw std::invalid_argument("provide exactly one of count or until");
        const auto step = duration(value.domain(),every);
        return wrap(count != Py_None ? value.repeated(step,integer(count)) : value.repeated_until(step,position(value.domain(),until)));
    });
}
PyObject* native_starts(PyObject*, PyObject* args) {
    PyObject *handle,*amount;
    if (!PyArg_ParseTuple(args,"OO",&handle,&amount)) return nullptr;
    return guard([&] { return wrap(set(handle).valid_starts(duration(set(handle).domain(),amount))); });
}
PyObject* native_text(PyObject*, PyObject* args) {
    PyObject* handle; int dates;
    if (!PyArg_ParseTuple(args,"Op",&handle,&dates)) return nullptr;
    return guard([&] { return text(ts::to_text(set(handle),dates != 0)); });
}
PyObject* native_dump(PyObject*, PyObject* args) {
    PyObject* handle;
    if (!PyArg_ParseTuple(args,"O",&handle)) return nullptr;
    return guard([&] { return text(ts::to_json(set(handle))); });
}
PyObject* native_load(PyObject*, PyObject* args) {
    PyObject* encoded;
    if (!PyArg_ParseTuple(args,"O",&encoded)) return nullptr;
    return guard([&] { return wrap(ts::from_json(string(encoded))); });
}
PyObject* native_position(PyObject*, PyObject* args) {
    PyObject *handle,*value; const char* operation;
    if (!PyArg_ParseTuple(args,"OOs",&handle,&value,&operation)) return nullptr;
    return guard([&]() -> PyObject* {
        const auto& domain = line(handle); const std::string op{operation};
        if (op == "format") return text(ts::format_position(domain,integer(value)));
        const auto n = op == "position" ? position(domain,value) : op == "duration" ? duration(domain,value).count :
            throw std::invalid_argument("unknown position conversion");
        return PyLong_FromLongLong(static_cast<long long>(n));
    });
}
PyObject* native_event_dump(PyObject*, PyObject* args) {
    PyObject *name,*description,*times;
    if (!PyArg_ParseTuple(args,"OOO",&name,&description,&times)) return nullptr;
    return guard([&] { return text(ts::to_json(ts::event{string(name),string(description),set(times)})); });
}
PyObject* native_event_load(PyObject*, PyObject* args) {
    PyObject* encoded;
    if (!PyArg_ParseTuple(args,"O",&encoded)) return nullptr;
    return guard([&] {
        auto value = ts::event_from_json(string(encoded));
        reference name{text(value.name)}, description{text(value.description)}, times{wrap(std::move(value.times))};
        reference out{PyTuple_New(3)};
        PyTuple_SET_ITEM(out.p,0,name.release());
        PyTuple_SET_ITEM(out.p,1,description.release());
        PyTuple_SET_ITEM(out.p,2,times.release());
        return out.release();
    });
}
PyObject* native_cli(PyObject*, PyObject* args) {
    PyObject* arguments;
    if (!PyArg_ParseTuple(args,"O",&arguments)) return nullptr;
    return guard([&] {
        reference sequence{PySequence_Fast(arguments,"arguments must be an iterable of strings")};
        std::vector<std::string> items;
        for (Py_ssize_t i = 0; i < PySequence_Fast_GET_SIZE(sequence.p); ++i)
            items.push_back(string(PySequence_Fast_GET_ITEM(sequence.p,i)));
        return PyLong_FromLong(ts::cli::run(items));
    });
}
PyMethodDef methods[] = {
    {"timeline",native_timeline,METH_VARARGS,"Construct a validated timeline."},
    {"timeline_info",native_timeline_info,METH_VARARGS,"Read timeline metadata."},
    {"timeline_equal",native_line_equal,METH_VARARGS,"Compare timelines."},
    {"make",native_make,METH_VARARGS,"Normalize half-open intervals."},
    {"range",native_range,METH_VARARGS,"Construct an interval with explicit boundaries."},
    {"point",native_point,METH_VARARGS,"Construct one tick."},
    {"parse",native_parse,METH_VARARGS,"Read interval text on an explicit timeline."},
    {"clone",native_clone,METH_VARARGS,"Copy a time set."},
    {"domain",native_domain,METH_VARARGS,"Copy a set's timeline."},
    {"intervals",native_intervals,METH_VARARGS,"Read canonical intervals."},
    {"binary",native_binary,METH_VARARGS,"Apply set algebra."},
    {"complement",native_complement,METH_VARARGS,"Complement within the timeline."},
    {"shift",native_shift,METH_VARARGS,"Shift with clipping."},
    {"query",native_query,METH_VARARGS,"Inspect a set."},
    {"edit",native_edit,METH_VARARGS,"Fill, clear, copy, or move a sector."},
    {"repeat",native_repeat,METH_VARARGS,"Repeat a finite pattern."},
    {"starts",native_starts,METH_VARARGS,"Find valid activity starts."},
    {"text",native_text,METH_VARARGS,"Write readable intervals."},
    {"dump",native_dump,METH_VARARGS,"Serialize an exact JSON set."},
    {"load",native_load,METH_VARARGS,"Read and validate a JSON set."},
    {"position",native_position,METH_VARARGS,"Convert exact positions or durations."},
    {"event_dump",native_event_dump,METH_VARARGS,"Serialize an event."},
    {"event_load",native_event_load,METH_VARARGS,"Read an event."},
    {"cli",native_cli,METH_VARARGS,"Run the same C++ command-line implementation."},
    {nullptr,nullptr,0,nullptr}
};
PyModuleDef module = {PyModuleDef_HEAD_INIT,"_native","Shared C++ time_set engine.",-1,methods,nullptr,nullptr,nullptr,nullptr};
} // namespace
PyMODINIT_FUNC PyInit__native() { return PyModule_Create(&module); }
