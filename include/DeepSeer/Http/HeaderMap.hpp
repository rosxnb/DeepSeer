#pragma once

#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <string>

namespace DeepSeer
{

/// Case insensitive comparator (e.g. for HTTP header names).
struct CaseInsensitiveLess
{
    bool operator()(std::string_view a, std::string_view b) const
    {
        return std::lexicographical_compare(
            a.begin(), a.end(), b.begin(), b.end(), [](char c1, char c2)
            { return std::tolower(static_cast<unsigned char>(c1)) < std::tolower(static_cast<unsigned char>(c2)); });
    }
};

class HeaderMap
{
public:
    using Storage = std::map<std::string, std::string, CaseInsensitiveLess>;

    void set(std::string name, std::string value)
    { headers_[std::move(name)] = std::move(value); }

    void append(std::string const& name, std::string const& value)
    {
        auto it = headers_.find(name);
        if (it != headers_.end()) {
            it->second += ", ";
            it->second += value;
        } else {
            headers_[name] = value;
        }
    }

    void remove(std::string const& name)
    {
        auto it = headers_.find(name);
        if (it != headers_.end())
            headers_.erase(it);
    }

    bool has(std::string const& name)
    { return headers_.contains(name); }

    std::optional<std::string_view> get(std::string const& name) const
    {
        auto it = headers_.find(name);
        if (it != headers_.end())
            return it->second;
        return std::nullopt;
    }

    std::optional<std::string_view> contentType() const { return get("Content-Type"); }
    std::optional<std::string_view> contentDisposition() const { return get("Content-Disposition"); }
    std::optional<std::string_view> transferEncoding() const { return get("Transfer-Encoding"); }

    std::optional<size_t> contentLength() const
    {
        auto v = get("Content-Length");
        if (!v)
            return std::nullopt;

        try {
            return std::stoull(std::string(*v));
        } catch (...) {
            return std::nullopt;
        }
    }

    size_t size() const { return headers_.size(); }
    bool empty() const { return headers_.size(); }

    auto begin() const { return headers_.begin(); }
    auto end() const { return headers_.end(); }

private:
    Storage headers_;
};

} // DeepSeer
