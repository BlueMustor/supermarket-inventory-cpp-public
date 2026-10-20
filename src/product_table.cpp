#include "supermarket/product_table.hpp"

#include <algorithm>
#include <utility>

namespace market {

ProductTable::ProductTable() : buckets_(17) {}

std::size_t ProductTable::hash(std::string_view id) noexcept {
    std::uint64_t value = 14695981039346656037ULL;
    for (unsigned char ch : id) {
        value ^= ch;
        value *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(value);
}

void ProductTable::insert(Product product) {
    if (find(product.productId) != nullptr) {
        throw Error("商品编号重复。");
    }
    if (size_ + 1 > buckets_.size() * 3 / 4) {
        rehash(buckets_.size() * 2 + 1);
    }
    auto& bucket = buckets_[hash(product.productId) % buckets_.size()];
    bucket.push_back(std::move(product));
    ++size_;
}

Product* ProductTable::find(std::string_view id) {
    auto& bucket = buckets_[hash(id) % buckets_.size()];
    const auto found = std::find_if(bucket.begin(), bucket.end(), [&](const Product& product) {
        return product.productId == id;
    });
    return found == bucket.end() ? nullptr : &*found;
}

const Product* ProductTable::find(std::string_view id) const {
    const auto& bucket = buckets_[hash(id) % buckets_.size()];
    const auto found = std::find_if(bucket.begin(), bucket.end(), [&](const Product& product) {
        return product.productId == id;
    });
    return found == bucket.end() ? nullptr : &*found;
}

std::vector<Product> ProductTable::entries() const {
    std::vector<Product> result;
    result.reserve(size_);
    for (const auto& bucket : buckets_) {
        result.insert(result.end(), bucket.begin(), bucket.end());
    }
    std::sort(result.begin(), result.end(), [](const Product& left, const Product& right) {
        return left.productId < right.productId;
    });
    return result;
}

std::size_t ProductTable::size() const noexcept {
    return size_;
}

std::size_t ProductTable::bucketCount() const noexcept {
    return buckets_.size();
}

void ProductTable::rehash(std::size_t bucketCount) {
    std::vector<std::vector<Product>> next(bucketCount);
    for (const auto& bucket : buckets_) {
        for (const auto& product : bucket) {
            next[hash(product.productId) % bucketCount].push_back(product);
        }
    }
    buckets_.swap(next);
}

}
