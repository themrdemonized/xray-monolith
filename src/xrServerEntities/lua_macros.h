#pragma once

#include <string>
#include <sstream>

/**
 * Convert all std::strings to const char* using constexpr if (C++17)
 */
template<typename T>
static auto convert(T&& t) {
	if constexpr (std::is_same<std::remove_cv_t<std::remove_reference_t<T>>, std::string>::value) {
		return std::forward<T>(t).c_str();
	}
	else {
		return std::forward<T>(t);
	}
}

/**
 * printf like formatting for C++ with std::string
 * Original source: https://stackoverflow.com/a/26221725/11722
 */
template<typename ... Args>
static std::string string_format_internal(const std::string& format, Args&& ... args)
{
	const auto size = snprintf(nullptr, 0, format.c_str(), std::forward<Args>(args) ...) + 1;
	if (size <= 0) { throw std::runtime_error("Error during formatting."); }
	std::unique_ptr<char[]> buf(new char[size]);
	snprintf(buf.get(), size, format.c_str(), args ...);
	return std::string(buf.get(), buf.get() + size - 1);
}

template<typename ... Args>
static std::string string_format(std::string fmt, Args&& ... args) {
	return string_format_internal(fmt, convert(std::forward<Args>(args))...);
}

template< typename ... Args >
std::string lines(Args const& ... args)
{
	std::ostringstream stream;
	using List = int[];
	(void)List {
		0, ((void)(stream << "\n" << args), 0) ...
	};

	return stream.str();
}

static std::string assign_local(const std::string& key, const std::string& value)
{
	return string_format("local %s = %s", key, value);
}

static std::string int_literal(int i)
{
	return std::to_string(i);
}

static std::string scope_to(const std::string& sThis)
{
	return string_format("setfenv(%s, %s)", int_literal(1), sThis);
}

static std::string wua_environment(const std::string& key)
{
	return string_format(
		R"(
			local %s = setmetatable(
				{},
				{
					__index = function(self, key)
						local gv = _G[key]
						if gv ~= nil then
							return gv
						end

						local res, out = pcall(require, key)
						if res then
							return out
						end
					end,
					__newindex = function(self, key, value)
						_G[key] = value
					end
				}
			)
		)",
		key
	);
}