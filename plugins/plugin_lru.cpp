#include <libCacheSim.h>

#include <list>
#include <unordered_map>

class LruCache {
 private:
  std::list<obj_id_t> list_;
  std::unordered_map<obj_id_t, std::list<obj_id_t>::iterator> map_;
  uint64_t cache_size_;

 public:
  LruCache(uint64_t cache_size) : cache_size_(cache_size) {}

  void on_hit(obj_id_t id) {
    auto it = map_.find(id);
    if (it != map_.end()) {
      list_.erase(it->second);
      list_.push_back(id);
      it->second = std::prev(list_.end());
    }
  }

  void on_miss(obj_id_t id, uint64_t size) {
    if (size <= cache_size_) {
      list_.push_back(id);
      map_[id] = std::prev(list_.end());
    }
  }

  obj_id_t evict() {
    if (list_.empty()) {
      return 0;
    }
    obj_id_t victim = list_.front();
    map_.erase(victim);
    list_.pop_front();
    return victim;
  }

  void on_remove(obj_id_t id) {
    auto it = map_.find(id);
    if (it != map_.end()) {
      list_.erase(it->second);
      map_.erase(it);
    }
  }
};

extern "C" {
void *cache_init_hook(const common_cache_params_t params) {
  return new LruCache(params.cache_size);
}

void cache_hit_hook(void *data, const request_t *req) {
  static_cast<LruCache *>(data)->on_hit(req->obj_id);
}

void cache_miss_hook(void *data, const request_t *req) {
  static_cast<LruCache *>(data)->on_miss(req->obj_id, req->obj_size);
}

obj_id_t cache_eviction_hook(void *data, const request_t * /*req*/) {
  return static_cast<LruCache *>(data)->evict();
}

void cache_remove_hook(void *data, obj_id_t obj_id) {
  static_cast<LruCache *>(data)->on_remove(obj_id);
}

void cache_free_hook(void *data) {
  delete static_cast<LruCache *>(data);
}
}  // extern "C"
