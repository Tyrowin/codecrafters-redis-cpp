#include "redis/Storage.h"

namespace redis {

void Storage::set(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    data_[key] = ValueWithExpiry(value);
}

void Storage::setWithExpiry(const std::string& key, const std::string& value, int64_t expiryMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto expiryTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(expiryMs);
    data_[key] = ValueWithExpiry(value, expiryTime);
}

std::optional<std::string> Storage::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = data_.find(key);
    if (it == data_.end()) {
        return std::nullopt;
    }
    
    if (it->second.hasExpiry) {
        auto now = std::chrono::steady_clock::now();
        if (now >= it->second.expiryTime) {
            data_.erase(it);
            return std::nullopt;
        }
    }
    
    return it->second.value;
}

void Storage::removeExpiredKey(const std::string& key) {
    auto it = data_.find(key);
    if (it != data_.end() && it->second.hasExpiry) {
        auto now = std::chrono::steady_clock::now();
        if (now >= it->second.expiryTime) {
            data_.erase(it);
        }
    }
}

std::vector<std::string> Storage::getAllKeys() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> keys;
    
    auto now = std::chrono::steady_clock::now();
    
    for (auto it = data_.begin(); it != data_.end(); ) {
        if (it->second.hasExpiry && now >= it->second.expiryTime) {
            it = data_.erase(it);
        } else {
            keys.push_back(it->first);
            ++it;
        }
    }
    
    return keys;
}

} // namespace redis