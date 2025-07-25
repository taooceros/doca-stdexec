#pragma once

#ifndef DOCA_STDEXEC_OPERATION_HPP
#define DOCA_STDEXEC_OPERATION_HPP

#include <stdexec/concepts.hpp>

namespace doca_stdexec {

struct immovable {
  immovable() = default;
  immovable(immovable&&) = delete;
};

} // namespace doca_stdexec

#endif // DOCA_STDEXEC_OPERATION_HPP