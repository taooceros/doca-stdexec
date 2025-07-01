#pragma once
#ifndef DOCA_STDEXEC_CONTEXT_HPP
#define DOCA_STDEXEC_CONTEXT_HPP

#include <concepts>
#include <doca_pe.h>

namespace doca_stdexec {

template <typename T>
concept AsContext = requires(T c) {
  { c.as_ctx() } -> std::convertible_to<doca_ctx *>;
};
} // namespace doca_stdexec
#endif // DOCA_STDEXEC_CONTEXT_HPP