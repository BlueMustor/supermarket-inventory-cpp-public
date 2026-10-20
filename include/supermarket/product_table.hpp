#pragma once

#include "supermarket/models.hpp"

#include <vector>

namespace market {

class ProductTable {
public:
    ProductTable();
    void insert(Product product);
    Product* find(std::string_view id);
    const Product* find(std::string_view id) const;
    std::vector<Product> entries() const;
    std::size_t size() const noexcept;
    std::size_t bucketCount() const noexcept;

private:
    static std::size_t hash(std::string_view id) noexcept;
    void rehash(std::size_t bucketCount);

    std::vector<std::vector<Product>> buckets_;
    std::size_t size_ = 0;
};

}
