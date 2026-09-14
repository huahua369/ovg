#pragma once

#include <vector>
#include <unordered_map>
#include <typeindex>
#include <assert.h>

/*
sparse_[e] → dense index
dense_[i] → Entity
data_[i * element_size_] → Component
*/
namespace tecs {
	using component_id_t = uint32_t;
	//struct Entity {
	//	uint32_t id = 0;
	//	uint32_t generation = 0;
	//};
	//constexpr Entity kNull{};
	using Entity = uint32_t;

	// ─── 位布局（可全局调整） ─────────────────────────────
	inline constexpr uint32_t kEntityIndexBits = 20;
	inline constexpr uint32_t kEntityGenBits = 12;

	inline constexpr uint32_t kEntityIndexMask = (1u << kEntityIndexBits) - 1; // 0xFFFFF
	inline constexpr uint32_t kEntityGenMask = (1u << kEntityGenBits) - 1; // 0xFFF
	inline constexpr uint32_t kEntityGenShift = kEntityIndexBits;
	static constexpr uint32_t kFreeListEnd = 0xFFFFFFFFu;
	// ─── Null / Tombstone（EnTT 风格） ───────────────────
	inline constexpr Entity kNull =
		(kEntityGenMask << kEntityGenShift) | kEntityIndexMask;

	// ─── 访问器（调试 / 内部使用） ───────────────────────
	inline constexpr uint32_t entity_index(Entity e) {
		return e & kEntityIndexMask;
	}

	inline constexpr uint32_t entity_generation(Entity e) {
		return (e >> kEntityGenShift) & kEntityGenMask;
	}

	// ─── 构造（registry 内部用） ─────────────────────────
	inline constexpr Entity make_entity(uint32_t index, uint32_t gen) {
		return (gen << kEntityGenShift) | (index & kEntityIndexMask);
	}

	using destructor_fn = void(*)(void*);
	class SparseSet {
	public:
		SparseSet(size_t element_size, destructor_fn dtor = nullptr) : element_size_(element_size), destructor_(dtor) {
			data_.reserve(1024 * element_size_);
		}

		void emplace(uint32_t e, const void* src) {
			assert(src);
			ensure_sparse(e);

			if (sparse_[e]) {
				std::memcpy(data() + index(e), src, element_size_);
				return;
			}

			dense_.push_back(e);
			sparse_[e] = dense_.size();
			size_t off = data_.size();
			data_.resize(off + element_size_);
			std::memcpy(data_.data() + off, src, element_size_);
		}

		void destroy(uint32_t e) {
			if (!exists(e)) return;
			uint32_t idx = index(e);

			// ✅ 1. 调用析构函数
			if (destructor_) {
				destructor_(data() + idx);
			}

			// ✅ 2. swap-and-pop（保持 dense 紧凑）
			uint32_t last = dense_.back();
			std::memcpy(data() + idx, data() + dense_.size() - 1, element_size_);

			dense_[idx] = last;
			sparse_[last] = idx + 1;

			dense_.pop_back();
			data_.resize(data_.size() - element_size_);
			sparse_[e] = 0;
		}

		void* get(uint32_t e) {
			return exists(e) ? data() + index(e) : nullptr;
		}

		const void* get(uint32_t e) const {
			return exists(e) ? data() + index(e) : nullptr;
		}

		bool exists(uint32_t e) const {
			return e < sparse_.size() && sparse_[e];
		}

		const std::vector<uint32_t>& dense() const { return dense_; }
		size_t size() const { return dense_.size(); }
		size_t capacity() const { return data_.capacity(); }
		size_t element_size() const { return element_size_; }

		void* allocate(uint32_t e) {
			ensure_sparse(e);
			if (sparse_[e]) {
				return data() + index(e);
			}
			dense_.push_back(e);
			sparse_[e] = dense_.size();
			size_t off = data_.size();
			data_.resize(off + element_size_);
			return data_.data() + off;
		}
	private:
		void ensure_sparse(uint32_t e) {
			if (e >= sparse_.size())
				sparse_.resize(e + 1, 0);
		}

		uint32_t index(uint32_t e) const {
			return sparse_[e] - 1;
		}

		uint8_t* data() { return data_.data(); }
		const uint8_t* data() const { return data_.data(); }


	private:
		std::vector<uint32_t> sparse_;
		std::vector<uint32_t> dense_;
		std::vector<uint8_t> data_;
		size_t element_size_;
		destructor_fn destructor_;
	};



	class reg_world {
	public:
		reg_world();
		~reg_world();

		Entity create();
		void destroy(Entity e);
		bool is_valid(Entity e) const;

		template <typename C, typename... Args>
		C& emplace(Entity e, Args&&... args);

		template <typename C>
		void remove(Entity e);

		template <typename C>
		C* get(Entity e);

		template <typename C>
		const C* get(Entity e) const;

		void deferred_destroy(Entity e) {
			if (e != kNull) deferred_.push_back(e);
		}

		void process_deferred() {
			for (Entity e : deferred_) destroy(e);
			deferred_.clear();
		}
		// ─── Query ────────────────────────────────
		template <typename... Cs, typename F>
		void query(F&& f) {
			static_assert(sizeof...(Cs) > 0);
			SparseSet* sets[] = { &storage<Cs>()... };
			constexpr uint32_t count = static_cast<uint32_t>(sizeof...(Cs));

			uint32_t leader = 0;
			size_t min_size = sets[0]->size();
			for (uint32_t i = 1; i < count; ++i) {
				if (sets[i]->size() < min_size) {
					min_size = sets[i]->size();
					leader = i;
				}
			}
			int cid = 0;
			for (uint32_t raw : sets[leader]->dense()) {
				Entity e = make_entity(raw, entities_[raw].generation);
				bool ok = true;
				for (uint32_t i = 0; i < count; ++i) {
					if (i != leader && !sets[i]->exists(raw)) {
						ok = false;
						break;
					}
				} 
				if (ok) f(e, *static_cast<Cs*>(sets[cid++]->get(raw))...);
			}
		}

		// ─── Group（EnTT group 语义） ─────────────
		template <typename... Cs>
		struct group_view {
			reg_world* world;
			std::vector<uint32_t> leader_dense;

			group_view(reg_world* w, SparseSet* leader)
				: world(w), leader_dense(leader->dense()) {}

			template <typename F>
			void each(F&& f) {
				for (uint32_t raw : leader_dense) {
					Entity e = make_entity(raw, world->entities_[raw].generation);
					f(e, *world->get<Cs>(e)...);
				}
			}
		};

		template <typename... Cs>
		auto group() {
			SparseSet* sets[] = { &storage<Cs>()... };
			constexpr uint32_t count = static_cast<uint32_t>(sizeof...(Cs));
			uint32_t leader = 0;
			size_t min_size = sets[0]->size();
			for (uint32_t i = 1; i < count; ++i) {
				if (sets[i]->size() < min_size) {
					min_size = sets[i]->size();
					leader = i;
				}
			}
			return group_view<Cs...>{this, sets[leader]};
		}

		// ─── Memory ───────────────────────────────
		struct memory_usage {
			size_t entity_count = 0;
			size_t entity_capacity = 0;
			size_t free_list_capacity = 0;
			size_t component_bytes = 0;
			size_t total_bytes = 0;
		};

		memory_usage usage() const {
			memory_usage m;
			for (auto& e : entities_) if (e.alive) m.entity_count++;
			m.entity_capacity = entities_.capacity() * sizeof(EntityMeta);
			m.free_list_capacity = next_free_.capacity() * sizeof(uint32_t);
			for (const auto& s : storages_)
				m.component_bytes += s->capacity() * s->element_size();
			m.total_bytes = m.entity_capacity + m.free_list_capacity + m.component_bytes;
			return m;
		}

		void reset() {
			entities_.clear();
			next_free_.clear();
			storages_.clear();
			for (auto p : storages_) { if (p)delete p; }
			storages_.clear();
			free_list_ = 0xFFFFFFFFu;
			deferred_.clear();
		}
		void remove_all(Entity e) {
			assert(is_valid(e));
			uint32_t idx = entity_index(e);

			for (auto& s : storages_) {
				if (s->element_size() != 0 && s->exists(idx)) {
					s->destroy(idx);
				}
			}
		}
		template <typename C>
		const std::vector<Entity>& view() {
			cache.clear();
			for (uint32_t id : storage<C>().dense()) {
				cache.push_back(make_entity(id, entities_[id].generation));
			}
			return cache;
		}
	private:
		template <typename C>
		SparseSet& storage();

		struct EntityMeta {
			uint32_t generation = 0;
			bool alive = false;
		};

		std::vector<EntityMeta> entities_;
		std::vector<uint32_t> next_free_;
		uint32_t free_list_ = kFreeListEnd;

		std::vector<Entity> deferred_;
		std::vector<SparseSet*> storages_;
		std::vector<Entity> cache;
	};
#ifdef TECS_IMPLEMENTATION
	reg_world::reg_world() {}
	reg_world::~reg_world() {
		for (auto p : storages_) { if (p)delete p; }
		storages_.clear();
	}
	static uint32_t next_id() {
		static std::atomic_uint32_t value{};
		return value++;
	}
	template <typename C>
	static uint32_t component_id() {
		static uint32_t id = next_id();
		return id;
	}

	template <typename C>
	SparseSet& reg_world::storage() {
		component_id_t id = component_id<C>();
		if (id >= storages_.size())
			storages_.resize(id + 1);
		if (!storages_[id])
		{
			storages_[id] = new SparseSet(sizeof(C), [](void* p) { static_cast<C*>(p)->~C(); });
		}
		return *storages_[id];
	}

	Entity reg_world::create() {
		if (free_list_ != 0xFFFFFFFFu) {
			uint32_t idx = free_list_;
			free_list_ = next_free_[idx]; // ✅ 安全
			entities_[idx].alive = true;
			return make_entity(idx, entities_[idx].generation);
		}

		uint32_t idx = static_cast<uint32_t>(entities_.size());
		entities_.push_back({ 0, true });
		next_free_.push_back(0xFFFFFFFFu);
		return make_entity(idx, 0);
	}

	void reg_world::destroy(Entity e) {
		assert(is_valid(e));
		uint32_t idx = entity_index(e);

		auto& meta = entities_[idx];
		meta.alive = false;
		meta.generation = (meta.generation + 1) & kEntityGenMask;

		next_free_[idx] = free_list_; // ✅ 不截断
		free_list_ = idx;
	}

	bool reg_world::is_valid(Entity e) const {
		if (e == kNull) return false;
		uint32_t idx = entity_index(e);
		if (idx >= entities_.size()) return false;
		const auto& meta = entities_[idx];
		return meta.alive && (meta.generation == entity_generation(e));
	}

	template <typename C, typename... Args>
	C& reg_world::emplace(Entity e, Args&&... args) {
		assert(is_valid(e));
		auto& s = storage<C>();
		void* mem = s.allocate(entity_index(e));
		C* p = new (mem) C{ std::forward<Args>(args)... }; // ✅ placement new
		return *p;
	}

	template <typename C>
	void reg_world::remove(Entity e) {
		assert(is_valid(e));
		storage<C>().remove(entity_index(e));
	}

	template <typename C>
	C* reg_world::get(Entity e) {
		if (!is_valid(e)) return nullptr;
		return static_cast<C*>(storage<C>().get(entity_index(e)));
	}

	template <typename C>
	const C* reg_world::get(Entity e) const {
		if (!is_valid(e)) return nullptr;
		return static_cast<const C*>(storage<C>().get(entity_index(e)));
	}

#endif
}
//!ecs 