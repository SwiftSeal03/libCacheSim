#include <libCacheSim.h>

#include <deque>
#include <unordered_map>
#include <vector>

class S3FifoCache {
 private:
  enum class Queue : uint8_t { NONE, SMALL, MAIN, GHOST };

  struct GhostEntry {
    uint32_t idx;
    uint32_t gen;
  };

  // obj_id_t -> compact index (the only hash container)
  std::unordered_map<obj_id_t, uint32_t> id_map_;

  // per-slot arrays indexed by compact index
  std::vector<obj_id_t> ids_;
  std::vector<Queue> queues_;
  std::vector<int8_t> freqs_;
  std::vector<uint64_t> sizes_;
  std::vector<uint32_t> gens_;

  std::vector<uint32_t> free_ids_;

  std::deque<uint32_t> small_;
  std::deque<uint32_t> main_;
  std::deque<GhostEntry> ghost_;

  uint64_t cache_size_;
  uint64_t small_capacity_;
  uint64_t small_bytes_ = 0;
  uint64_t ghost_capacity_;

  static constexpr int8_t kMaxFreq = 3;

  uint32_t alloc_idx(obj_id_t obj_id) {
    uint32_t idx;
    if (!free_ids_.empty()) {
      idx = free_ids_.back();
      free_ids_.pop_back();
    } else {
      idx = static_cast<uint32_t>(ids_.size());
      ids_.push_back(0);
      queues_.push_back(Queue::NONE);
      freqs_.push_back(0);
      sizes_.push_back(0);
      gens_.push_back(0);
    }
    ids_[idx] = obj_id;
    id_map_[obj_id] = idx;
    gens_[idx]++;
    return idx;
  }

  void free_idx(uint32_t idx) {
    id_map_.erase(ids_[idx]);
    queues_[idx] = Queue::NONE;
    free_ids_.push_back(idx);
  }

  obj_id_t evict_small() {
    while (!small_.empty() && small_bytes_ > small_capacity_) {
      uint32_t idx = small_.front();
      small_.pop_front();
      small_bytes_ -= sizes_[idx];

      if (freqs_[idx] > 0) {
        freqs_[idx] = 0;
        queues_[idx] = Queue::MAIN;
        main_.push_back(idx);
      } else {
        obj_id_t victim = ids_[idx];
        queues_[idx] = Queue::GHOST;
        sizes_[idx] = 0;
        ghost_.push_back({idx, gens_[idx]});
        trim_ghost();
        return victim;
      }
    }
    return 0;
  }

  obj_id_t evict_main() {
    while (!main_.empty()) {
      uint32_t idx = main_.front();
      main_.pop_front();

      if (freqs_[idx] > 0) {
        freqs_[idx]--;
        main_.push_back(idx);
      } else {
        obj_id_t victim = ids_[idx];
        free_idx(idx);
        return victim;
      }
    }
    return 0;
  }

  void trim_ghost() {
    while (ghost_.size() > ghost_capacity_) {
      auto [idx, gen] = ghost_.front();
      ghost_.pop_front();
      if (gens_[idx] == gen && queues_[idx] == Queue::GHOST) {
        free_idx(idx);
      }
    }
  }

  static void remove_from_deque(std::deque<uint32_t> &dq, uint32_t val) {
    for (auto it = dq.begin(); it != dq.end(); ++it) {
      if (*it == val) {
        dq.erase(it);
        return;
      }
    }
  }

 public:
  S3FifoCache(uint64_t cache_size, uint32_t max_objects)
      : cache_size_(cache_size),
        small_capacity_(cache_size / 11),
        ghost_capacity_(cache_size) {
    uint32_t total = max_objects * 2;
    ids_.reserve(total);
    queues_.reserve(total);
    freqs_.reserve(total);
    sizes_.reserve(total);
    gens_.reserve(total);
    free_ids_.reserve(total);
    id_map_.reserve(total);
  }

  void on_hit(obj_id_t id) {
    auto it = id_map_.find(id);
    if (it != id_map_.end() && freqs_[it->second] < kMaxFreq) {
      freqs_[it->second]++;
    }
  }

  void on_miss(obj_id_t id, uint64_t size) {
    if (size > cache_size_) return;

    auto it = id_map_.find(id);
    if (it != id_map_.end() && queues_[it->second] == Queue::GHOST) {
      uint32_t idx = it->second;
      gens_[idx]++;
      queues_[idx] = Queue::MAIN;
      sizes_[idx] = size;
      freqs_[idx] = 0;
      main_.push_back(idx);
    } else {
      uint32_t idx = alloc_idx(id);
      queues_[idx] = Queue::SMALL;
      sizes_[idx] = size;
      freqs_[idx] = 0;
      small_.push_back(idx);
      small_bytes_ += size;
    }
  }

  obj_id_t evict() {
    obj_id_t victim = evict_small();
    if (victim != 0) return victim;
    return evict_main();
  }

  void on_remove(obj_id_t id) {
    auto it = id_map_.find(id);
    if (it == id_map_.end()) return;

    uint32_t idx = it->second;
    if (queues_[idx] == Queue::SMALL) {
      small_bytes_ -= sizes_[idx];
      remove_from_deque(small_, idx);
    } else if (queues_[idx] == Queue::MAIN) {
      remove_from_deque(main_, idx);
    }
    free_idx(idx);
  }
};

extern "C" {
void *cache_init_hook(const common_cache_params_t params) {
  uint32_t max_objects = 1u << 24;
  return new S3FifoCache(params.cache_size, max_objects);
}

void cache_hit_hook(void *data, const request_t *req) {
  static_cast<S3FifoCache *>(data)->on_hit(req->obj_id);
}

void cache_miss_hook(void *data, const request_t *req) {
  static_cast<S3FifoCache *>(data)->on_miss(req->obj_id, req->obj_size);
}

obj_id_t cache_eviction_hook(void *data, const request_t * /*req*/) {
  return static_cast<S3FifoCache *>(data)->evict();
}

void cache_remove_hook(void *data, obj_id_t obj_id) {
  static_cast<S3FifoCache *>(data)->on_remove(obj_id);
}

void cache_free_hook(void *data) { delete static_cast<S3FifoCache *>(data); }
}  // extern "C"
