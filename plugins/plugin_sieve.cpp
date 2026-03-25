#include <libCacheSim.h>

#include <list>
#include <unordered_map>

struct SieveEntry {
  obj_id_t id;
  bool visited;
};

class SieveCache {
 private:
  std::list<SieveEntry> list_;
  std::unordered_map<obj_id_t, std::list<SieveEntry>::iterator> map_;
  std::list<SieveEntry>::iterator hand_;
  bool hand_valid_ = false;
  uint64_t cache_size_;

  void advance_hand() {
    if (hand_ == list_.begin()) {
      hand_ = list_.end();
    }
    --hand_;
  }

 public:
  SieveCache(uint64_t cache_size) : cache_size_(cache_size) {}

  void on_hit(obj_id_t id) {
    auto it = map_.find(id);
    if (it != map_.end()) {
      it->second->visited = true;
    }
  }

  void on_miss(obj_id_t id, uint64_t size) {
    if (size <= cache_size_) {
      list_.push_front({id, false});
      map_[id] = list_.begin();
    }
  }

  obj_id_t evict() {
    if (list_.empty()) return 0;

    if (!hand_valid_) {
      hand_ = std::prev(list_.end());
      hand_valid_ = true;
    }

    while (hand_->visited) {
      hand_->visited = false;
      advance_hand();
    }

    obj_id_t victim = hand_->id;
    auto to_erase = hand_;

    if (list_.size() == 1) {
      hand_valid_ = false;
    } else {
      advance_hand();
    }

    map_.erase(victim);
    list_.erase(to_erase);

    return victim;
  }

  void on_remove(obj_id_t id) {
    auto it = map_.find(id);
    if (it != map_.end()) {
      if (hand_valid_ && hand_ == it->second) {
        if (list_.size() == 1) {
          hand_valid_ = false;
        } else {
          advance_hand();
        }
      }
      list_.erase(it->second);
      map_.erase(it);
    }
  }
};

extern "C" {
void *cache_init_hook(const common_cache_params_t params) {
  return new SieveCache(params.cache_size);
}

void cache_hit_hook(void *data, const request_t *req) {
  static_cast<SieveCache *>(data)->on_hit(req->obj_id);
}

void cache_miss_hook(void *data, const request_t *req) {
  static_cast<SieveCache *>(data)->on_miss(req->obj_id, req->obj_size);
}

obj_id_t cache_eviction_hook(void *data, const request_t * /*req*/) {
  return static_cast<SieveCache *>(data)->evict();
}

void cache_remove_hook(void *data, obj_id_t obj_id) {
  static_cast<SieveCache *>(data)->on_remove(obj_id);
}

void cache_free_hook(void *data) {
  delete static_cast<SieveCache *>(data);
}
}  // extern "C"
