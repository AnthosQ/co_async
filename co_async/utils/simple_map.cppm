export module co_async:utils.simple_map;

import std;

namespace co_async {

export template <class K, class V>
struct SimpleMap {
    SimpleMap() = default;

    SimpleMap(std::initializer_list<std::pair<K, V>> init)
        : mData(init.begin(), init.end()) {}

    template <class Key>
        requires(requires(K k, Key key) {
            k < key;
            key < k;
        })
    V *at(Key &&key) noexcept {
        auto it = mData.find(std::forward<Key>(key));
        if (it == mData.end()) {
            return nullptr;
        }
        return std::addressof(it->second);
    }

    template <class Key>
        requires(requires(K k, Key key) {
            k < key;
            key < k;
        })
    V const *at(Key const &key) const noexcept {
        auto it = mData.find(key);
        if (it == mData.end()) {
            return nullptr;
        }
        return std::addressof(it->second);
    }

    template <class Key, std::invocable<V const &> F = std::identity>
        requires(requires(K k, Key key) {
            k < key;
            key < k;
        })
    decltype(std::optional(std::declval<std::invoke_result_t<F, V const &>>()))
    get(Key const &key, F &&func = {}) const noexcept {
        auto it = mData.find(key);
        if (it == mData.end()) {
            return std::nullopt;
        }
        return std::invoke(func, it->second);
    }

    V &insert_or_assign(K key, V value) {
        return mData.insert_or_assign(std::move(key), std::move(value))
            .first->second;
    }

    V &insert(K key, V value) {
        return mData.emplace(std::move(key), std::move(value)).first->second;
    }

    template <class Key>
        requires(requires(K k, Key key) {
            k < key;
            key < k;
        })
    bool contains(Key &&key) const noexcept {
        return mData.find(std::forward<Key>(key)) != mData.end();
    }

    template <class Key>
        requires(requires(K k, Key key) {
            k < key;
            key < k;
        })
    bool erase(Key &&key) {
        return mData.erase(std::forward<Key>(key)) != 0;
    }

    auto begin() const noexcept {
        return mData.begin();
    }

    auto end() const noexcept {
        return mData.end();
    }

    auto begin() noexcept {
        return mData.begin();
    }

    auto end() noexcept {
        return mData.end();
    }

private:
    std::map<K, V, std::less<>> mData;
};

} // namespace co_async
