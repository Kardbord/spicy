// Copyright (c) 2020 by the Zeek Project. See LICENSE for details.

#include <cstdlib>

#include <hilti/rt/autogen/version.h>
#include <hilti/rt/types/bytes.h>
#include <hilti/rt/util.h>

#include <spicy/rt/autogen/config.h>
#include <spicy/rt/util.h>

std::string spicy::rt::version() {
    constexpr char spicy_version[] = PROJECT_VERSION_STRING_LONG;

#if HILTI_RT_BUILD_TYPE_DEBUG
    return hilti::rt::fmt("Spicy runtime library version %s [debug build]", spicy_version);
#elif HILTI_RT_BUILD_TYPE_RELEASE
    return hilti::rt::fmt("Spicy runtime library version %s [release build]", spicy_version);
#else
#error "Neither HILTI_RT_BUILD_TYPE_DEBUG nor HILTI_RT_BUILD_TYPE_RELEASE define."
#endif
}

std::string spicy::rt::bytes_to_hexstring(const hilti::rt::Bytes& value) {
    std::string result;

    for ( auto x : value )
        result += hilti::rt::fmt("%02X", x);

    return result;
}

std::optional<std::string> spicy::rt::getenv(const std::string& name) {
    if ( auto x = ::getenv(name.c_str()) )
        return {x};
    else
        return {};
}
