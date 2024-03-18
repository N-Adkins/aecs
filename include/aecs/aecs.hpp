/*  
    AECS - v0.1
    No warranty implied, use at your own risk.


    Header-only Entity Component System, entirely self-contained with
    no boilerplate or macros needed to function.

    The aecs::internal namespace is used to hide, as the name implies,
    internal functionality that is not meant to be part of the public
    interface.
*/

#ifndef AECS_HPP
#define AECS_HPP

#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <queue>
#include <typeindex>
#include <unordered_map>
#include <vector>

// Leaving this on for now since I need the testing and this isn't even
// useable yet.
#define AECS_USE_ASSERTIONS

// User can define AECS_USE_ASSERTIONS to enable debugging assertions.
// Off by default.
#ifdef AECS_USE_ASSERTIONS
    #define AECS_ASSERT(condition) assert((condition))
#else
    #define AECS_ASSERT(...) ((void)0);
#endif

namespace aecs::internal
{

// Constant values and types
using DEFAULT_ENTITY_ID_TYPE = std::uint32_t;
constexpr std::size_t MAX_COMPONENTS = 64;

/**
 * Returns index number from passed entity.
 */
template <typename entity_id>
entity_id get_entity_index(entity_id entity)
{
    constexpr std::size_t index_offset = sizeof(entity_id) * 8 / 2;
    return entity >> index_offset;
}

/**
 * Returns version number from passed entity.
 */
template <typename entity_id>
entity_id get_entity_version(entity_id entity)
{
    constexpr std::size_t version_size = sizeof(entity_id) * 8 / 2;
    return (entity << version_size) >> version_size;
}

/**
 * Returns new entity_id with passed index.
 */
template <typename entity_id>
entity_id set_entity_index(entity_id entity, entity_id index)
{
    constexpr std::size_t index_offset = sizeof(entity_id) * 8 / 2;
    const auto version = get_entity_version(entity);
    entity = index;
    entity <<= index_offset;
    entity |= version;
    return entity;
}

/**
 * Returns new entity_id with passed version.
 */
template <typename entity_id>
entity_id set_entity_version(entity_id entity, entity_id version)
{
    const auto index = get_entity_index(entity);
    entity = version;
    entity |= index;
    return entity;
}

/**
 * Returns whether or not an entity is valid.
 */
template <typename entity_id>
bool entity_is_valid(entity_id entity)
{
    const entity_id invalid_entity = std::numeric_limits<entity_id>().max();
    return entity != invalid_entity;
}

/**
 * Factory function for an entity id that is considered invalid.
 */
template <typename entity_id>
entity_id create_invalid_entity()
{
    return std::numeric_limits<entity_id>().max();
}

/**
 * Returns a number unique to each time starting from 0 and incrementing
 * with each passed type.
 */
static std::size_t current_component_id = 0;
template <typename T>
std::size_t get_component_id()
{
    static std::size_t id = current_component_id++;
    AECS_ASSERT(id < 64);
    return id;
}

/**
 * Interface for component alloctors so they can be held generically.
 */
template <typename entity_id>
struct i_component_allocator
{
    virtual ~i_component_allocator() = default;
};

/**
 * Type-specific component handler. Basically identical to component_manager but
 * doesn't deal with type_indexes and such, just acts directly on templated type.
 */
template <typename entity_id, typename T>
struct component_allocator : public i_component_allocator<entity_id>
{
    std::vector<T> arr;
    
    T& insert(entity_id entity, T component)
    {
        const auto index = get_entity_index(entity);
        if (index >= arr.size()) {
            arr.resize(index == 0 ? 32 : get_entity_index(entity) * 2);
        };
        arr[index] = component;
        return arr[index];
    }

    T& get(entity_id entity)
    {
        const auto index = get_entity_index(entity);
        AECS_ASSERT(arr.size() >= index);
        return arr[index];
    }
};

/**
 * Handles all direct component interaction. Encapsulating class
 * for registry.
 */
template <typename entity_id>
class component_manager
{
public:

    /**
     * Adds component to passed entity and returns a reference to it.
     */
    template <typename T>
    T& insert(entity_id entity, T component)
    {
        const auto type_index = std::type_index(typeid(T));
        
        // If allocator doesn't exist for type, make one
        if (allocators.find(type_index) == allocators.end()) {
            allocators.insert({
                type_index,
                std::make_unique<component_allocator<entity_id, T>>(),
            });
        }

        AECS_ASSERT(allocators.find(type_index) != allocators.end());

        const auto allocator = dynamic_cast<component_allocator<entity_id, T>*>(allocators[type_index].get());
        return allocator->insert(entity, component);
    }
    
    /**
     * Returns component associated with passed entity as reference.
     */
    template <typename T>
    T& get(entity_id entity)
    {
        const auto type_index = std::type_index(typeid(T));
        AECS_ASSERT(allocators.find(type_index) != allocators.end());
        const auto allocator = dynamic_cast<component_allocator<entity_id, T>*>(allocators[type_index].get());
        return allocator->get(entity);
    }

private:
    std::unordered_map<std::type_index, std::unique_ptr<i_component_allocator<entity_id>>> allocators;
};

template <typename entity_id>
struct entity
{
    entity_id id;
    std::uint64_t mask = 0;
};

} // namespace aecs::internal

// Public interface
namespace aecs
{

/**
 * Monolothic ECS registry, handles creating and destroying entities
 * as well as manipulating them with components.
 */
template <typename entity_id = internal::DEFAULT_ENTITY_ID_TYPE>
class registry
{
public:

    /**
     * Constructs new entity_id and handles populating deleted entities,
     * as well as creating brand new ones.
     */
    entity_id new_entity()
    {
        // If there's a deleted id, modify the version and use it instead
        if (deleted_ids.size() > 0) {
            const auto old_id = deleted_ids.front();
            const auto version = internal::get_entity_version(old_id);
            const auto index = internal::get_entity_index(old_id);
            const auto new_id = internal::set_entity_version(old_id, version + 1);
            AECS_ASSERT(entities.size() >= index);
            AECS_ASSERT(!internal::entity_is_valid(entities[index].id));
            entities[index].mask = 0;
            entities[index].id = new_id;
            return new_id;
        }
        
        // Resize entity vector if there's too many.
        if (current_index >= entities.size()) {
            entities.resize(current_index == 0 ? 32 : current_index * 2);
        }
        
        entities[current_index].id = internal::set_entity_index<entity_id>(0, current_index);
        return entities[current_index++].id;
    }
    
    /**
     * Queues entity in deletion queue and invalidates it.
     */
    void delete_entity(entity_id entity)
    {
        const auto index = internal::get_entity_index(entity);
        AECS_ASSERT(entities.size() >= index);
        entities[index] = internal::create_invalid_entity<entity_id>();
        deleted_ids.push(entity);
    }
    
    /**
     * Returns whether or not the entity has the component of type T.
     * T must be a valid component type as passing it here will affect
     * a global component state.
     */
    template <typename T>
    bool has(entity_id entity)
    {
        const auto component_id = internal::get_component_id<T>();
        const auto& internal_entity = entities[internal::get_entity_index(entity)];
        return (internal_entity.mask & (1 << component_id)) != 0;
    }
    
    /**
     * Adds a component to passed entity and returns a reference to the 
     * registry-owned version of it.
     */
    template <typename T>
    T& assign(entity_id entity, T component)
    {
        AECS_ASSERT(!has<T>(entity));
        const auto component_id = internal::get_component_id<T>();
        auto& internal_entity = entities[internal::get_entity_index(entity)];
        internal_entity.mask |= (1 << component_id);
        return component_manager.insert(entity, component);
    }
    
    /**
     * Removes a component from an entity.
     */
    template <typename T>
    void unassign(entity_id entity)
    {
        AECS_ASSERT(has<T>(entity));
        const auto component_id = internal::get_component_id<T>();
        auto& internal_entity = entities[internal::get_entity_index(entity)];
        internal_entity.mask &= ~(1 << component_id);
    }
    
    /**
     * Returns a reference to registry-owned component held by passed entity.
     * Expects that entity does contain component.
     */
    template <typename T>
    T& get(entity_id entity)
    {
        AECS_ASSERT(has<T>(entity));
        return component_manager.template get<T>(entity);
    }

private:
    internal::component_manager<entity_id> component_manager;
    std::vector<internal::entity<entity_id>> entities;
    std::queue<entity_id> deleted_ids;
    entity_id current_index = 0;
};

} // namespace aecs

#endif