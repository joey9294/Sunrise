#include "console_format.h"

#include <cstdio>

namespace sunrise::core::console::output {
namespace {

/** Appends what fits of one run, advancing the length. */
void append(std::span<char> buffer, std::size_t& length, std::string_view text) noexcept {
    for (const char character : text) {
        if (length + 1 >= buffer.size()) {
            return;
        }
        buffer[length] = character;
        ++length;
    }
}

/**
 * Removes the trailing zeros a fixed-point print leaves, and the point when nothing follows it.
 *
 * A fixed print is used because the parser refuses exponent notation, so `%g` could produce a
 * line that will not read back. Trimming is what keeps `20` from printing as `20.000000`.
 */
void trim_fraction(std::span<char> buffer, std::size_t& length) noexcept {
    bool hasPoint = false;
    for (std::size_t index = 0; index < length; ++index) {
        hasPoint = hasPoint || buffer[index] == '.';
    }
    if (!hasPoint) {
        return;
    }
    while (length > 0 && buffer[length - 1] == '0') {
        --length;
    }
    if (length > 0 && buffer[length - 1] == '.') {
        --length;
    }
    for (std::size_t index = length; index < buffer.size(); ++index) {
        buffer[index] = '\0';
    }
}

/** @return True when printed text needs quotes to read back as one token. */
[[nodiscard]] bool needs_quotes(std::string_view text) noexcept {
    if (text.empty()) {
        return true;
    }
    for (const char character : text) {
        if (character == ' ' || character == '"') {
            return true;
        }
    }
    return false;
}

} // namespace

/** Prints one typed value the way a reader types it back. */
void format_value(const Value& value, std::span<char> buffer, std::size_t& length) noexcept {
    length = 0;
    if (buffer.empty()) {
        return;
    }
    for (char& character : buffer) {
        character = '\0';
    }

    switch (value.type) {
    case Type::boolean:
        append(buffer, length, value.boolean ? "true" : "false");
        return;
    case Type::integer: {
        std::array<char, 32> digits{};
        const int written = std::snprintf(
            digits.data(), digits.size(), "%lld", static_cast<long long>(value.integer));
        if (written > 0) {
            append(
                buffer, length, std::string_view{digits.data(), static_cast<std::size_t>(written)});
        }
        return;
    }
    case Type::real: {
        std::array<char, 64> digits{};
        const int written = std::snprintf(digits.data(), digits.size(), "%.6f", value.real);
        if (written > 0) {
            append(
                buffer, length, std::string_view{digits.data(), static_cast<std::size_t>(written)});
            trim_fraction(buffer, length);
        }
        return;
    }
    case Type::text: {
        const std::string_view text{value.text.data(), value.textLength};
        if (!needs_quotes(text)) {
            append(buffer, length, text);
            return;
        }
        append(buffer, length, "\"");
        append(buffer, length, text);
        append(buffer, length, "\"");
        return;
    }
    case Type::count:
        break;
    }
}

/** Prints the one-line usage of an entry. */
void format_usage(const registry::Descriptor& entry,
                  std::span<char> buffer,
                  std::size_t& length) noexcept {
    length = 0;
    if (buffer.empty()) {
        return;
    }
    for (char& character : buffer) {
        character = '\0';
    }

    append(buffer, length, entry.name);
    if (entry.kind == registry::Kind::variable) {
        // A variable is shown with the value it would take, since typing the name alone reads it.
        append(buffer, length, " [");
        append(buffer, length, type_name(entry.type));
        append(buffer, length, "]");
        if (has_bounds(entry)) {
            std::array<char, 64> range{};
            const int written =
                std::snprintf(range.data(), range.size(), " %g..%g", entry.minimum, entry.maximum);
            if (written > 0) {
                append(buffer,
                       length,
                       std::string_view{range.data(), static_cast<std::size_t>(written)});
            }
        }
        return;
    }
    for (const registry::Argument& argument : entry.arguments) {
        append(buffer, length, " <");
        append(buffer, length, argument.name);
        append(buffer, length, ":");
        append(buffer, length, type_name(argument.type));
        append(buffer, length, ">");
    }
}

} // namespace sunrise::core::console::output
