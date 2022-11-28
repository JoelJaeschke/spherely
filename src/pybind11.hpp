#ifndef S2SHAPELY_PYBIND11_H_
#define S2SHAPELY_PYBIND11_H_

/*
** Pybind11 patches and workarounds for operating on Python wrapped Geography
** objects through Numpy arrays and universal functions (using numpy.object
** dtype).
**
** Somewhat hacky!
*/

#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>

#include "geography.hpp"

namespace py = pybind11;

namespace s2shapely {

// A ``pybind11::object`` that maybe points to a ``Geography`` C++ object.
//
// To main goal of this class is to be used as argument and/or return type of
// s2shapely's vectorized functions that operate on Geography objects via the
// numpy.object dtype.
//
// Instead of relying on Pybind11's implicit conversion mechanisms (copy), we
// require explicit conversion from / to ``pybind11::object``.
//
//
class PyObjectGeography : public py::object {
public:
    // Python -> C++ conversion
    //
    // Raises a ``ValueError`` on the Python side if the cast fails.
    //
    // Note: a raw pointer is used here because Pybind11's
    // `type_caster<std::unique_ptr<wrapped_type>>` doesnt't support Python->C++
    // conversion (no `load` method) as it would imply that Python needs to give
    // up ownership of an object, which is not possible (the object might be
    // referenced elsewhere)
    //
    // Conversion shouldn't involve any copy. The cast is dynamic, though, as
    // needed since numpy.object dtype can refer to any Python object.
    //
    Geography* as_geog_ptr() const {
        try {
            return cast<Geography*>();
        } catch (const py::cast_error& e) {
            throw py::type_error("not a Geography object");
        }
    }

    // C++ -> Python conversion
    //
    // Note: pybind11's `type_caster<std::unique_ptr<wrapped_type>>` implements
    // move semantics (Python takes ownership).
    //
    template <class T, std::enable_if_t<std::is_base_of<Geography, T>::value,
                                        bool> = true>
    static py::object as_py_object(std::unique_ptr<T> geog_ptr) {
        return py::cast(std::move(geog_ptr));
    }

    // Just check whether the object is a Geography
    //
    bool is_geog_ptr() const {
        try {
            cast<Geography*>();
        } catch (const py::cast_error& e) {
            return false;
        }

        return true;
    }
};
}  // namespace s2shapely

namespace pybind11 {
namespace detail {

// Force pybind11 to allow PyObjectGeography as argument of vectorized
// functions.
//
// Pybind11 doesn't support non-POD types as arguments for vectorized
// functions because of its internal conversion mechanisms and also
// because direct memory access requires a standard layout type.
//
// Here it is probably fine to make an exception since we require
// explicit Python object <-> C++ Geography conversion and also because
// with the numpy.object dtype the data are actually references to Python
// objects (not the objects themselves).
//
// Caveat: be careful and use PyObjectGeography cast methods!
//
template <>
struct vectorize_arg<s2shapely::PyObjectGeography> {
    using T = s2shapely::PyObjectGeography;
    // The wrapped function gets called with this type:
    using call_type = T;
    // Is this a vectorized argument?
    static constexpr bool vectorize = true;
    // Accept this type: an array for vectorized types, otherwise the type
    // as-is:
    using type =
        conditional_t<vectorize,
                      array_t<remove_cv_t<call_type>, array::forcecast>, T>;
};

// Register PyObjectGeography as a valid numpy dtype (numpy.object alias)
// from: https://github.com/pybind/pybind11/pull/1152
template <>
struct npy_format_descriptor<s2shapely::PyObjectGeography> {
    static constexpr auto name = _("object");
    enum { value = npy_api::NPY_OBJECT_ };
    static pybind11::dtype dtype() {
        if (auto ptr = npy_api::get().PyArray_DescrFromType_(value)) {
            return reinterpret_borrow<pybind11::dtype>(ptr);
        }
        pybind11_fail("Unsupported buffer format!");
    }
};

}  // namespace detail

// Cast specialization for PyObjectGeography (just a pass through)
//
// Allows using it as return type of vectorized functions.
//
template <typename T,
          typename detail::enable_if_t<
              std::is_same<T, s2shapely::PyObjectGeography>::value, int> = 0>
object cast(T&& value) {
    return value;
}

}  // namespace pybind11

#endif  // S2SHAPELY_PYBIND11_H_