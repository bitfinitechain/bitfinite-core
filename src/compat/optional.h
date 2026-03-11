#ifndef COMPAT_OPTIONAL_H
#define COMPAT_OPTIONAL_H

#if __cplusplus >= 201703L
#include "compat/optional.h"
#else
#include <boost/optional.hpp>
namespace std {
template <typename T>
using optional = boost::optional<T>;
using nullopt_t = boost::none_t;
const auto nullopt = boost::none;
using bad_optional_access = boost::bad_optional_access;
} // namespace std
#endif

#endif // COMPAT_OPTIONAL_H
