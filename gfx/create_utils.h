#pragma once

#include <functional>
#include <type_traits>
#include <utility>

// Allows a factory result to be constructed directly in its destination,
// including emplacement contexts where the produced type is not copyable.
template<class F>
class WithResultOf {
	F function;

public:
	using T = std::invoke_result_t<F &&>;

	explicit WithResultOf(F function)
		: function(std::move(function))
	{}

	operator T() && {
		return std::invoke(std::move(function));
	}
};

template<class F>
auto with_result_of(F &&function) {
	return WithResultOf<std::decay_t<F>>(std::forward<F>(function));
}
