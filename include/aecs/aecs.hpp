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
#include <iterator>
#include <limits>
#include <memory>
#include <queue>
#include <tuple>
#include <typeindex>
#include <unordered_map>
#include <vector>

// User can define AECS_USE_ASSERTIONS to enable debugging assertions.
// Off by default.
#ifdef AECS_USE_ASSERTIONS
    #define AECS_ASSERT(condition) assert(condition)
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
    virtual void entity_destroyed(entity_id entity) = 0;
};

/**
 * Type-specific component handler. Basically identical to component_manager but
 * doesn't deal with type_indexes and such, just acts directly on templated type.
 */
template <typename entity_id, typename T>
struct component_allocator : public i_component_allocator<entity_id>
{
    std::vector<T> arr;
    
    T& insert(entity_id entity, const T&& component)
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

    void destroy(entity_id entity)
    {
        const auto index = get_entity_index(entity);
        AECS_ASSERT(arr.size() >= index);
        arr[index] = T{};
    }

    void entity_destroyed(entity_id entity) override
    {
        destroy(entity); 
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
    T& insert(entity_id entity, const T&& component)
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

        const auto allocator = static_cast<component_allocator<entity_id, T>*>(allocators[type_index].get());
        return allocator->insert(entity, std::move(component));
    }
   
    /**
     * Returns component associated with passed entity as reference.
     */
    template <typename T>
    T& get(entity_id entity)
    {
        const auto type_index = std::type_index(typeid(T));
        AECS_ASSERT(allocators.find(type_index) != allocators.end());
        const auto allocator = static_cast<component_allocator<entity_id, T>*>(allocators[type_index].get());
        return allocator->get(entity);
    }
    
    /**
     * Destructs the specified component type that is associated with the passed entity.
     */
    template <typename T>
    void destroy(entity_id entity)
    {
        const auto type_index = std::type_index(typeid(T));
        AECS_ASSERT(allocators.find(type_index) != allocators.end());
        const auto allocator = static_cast<component_allocator<entity_id, T>*>(allocators[type_index].get());
        return allocator->destroy(entity);
    }
    
    /**
     * Destructs all components associated with specified entity
     */
    void entity_destroyed(entity_id entity)
    {
        for (auto& [_, allocator] : allocators) {
            allocator->entity_destroyed(entity);
        }
    }

private:
    std::unordered_map<std::type_index, std::unique_ptr<i_component_allocator<entity_id>>> allocators;
};

/**
 * Internal entity representation, contains a mask to easily iterate over them
 */
template <typename entity_id>
struct entity
{
    entity_id id;
    std::uint64_t mask = 0;
};

/**
 * Tuple type concatenation
 */
template <typename... Tuples>
struct tuple_cat_s;

/**
 * Recursive case, combine both tuple types and call again with new one
 * and other types
 */
template <typename... Ts, typename... Us, typename... Tuples>
struct tuple_cat_s<std::tuple<Ts...>, std::tuple<Us...>, Tuples...>
    : tuple_cat_s<std::tuple<Ts..., Us...>, Tuples...> {};

/**
 * Base case, simple tuple without any nested
 */
template <typename... Ts>
struct tuple_cat_s<std::tuple<Ts...>> 
{
    using type = std::tuple<Ts...>;
};

/**
 * Alias for tuple_cat_s::type for ease of use
 */
template <typename... Tuples>
using tuple_cat_t = typename tuple_cat_s<Tuples...>::type;

/**
 * Base case for generating a reference tuple
 */
template <typename... Types>
struct make_view_tuple 
{
    using type = std::tuple<>;
};

/**
 * Recursive case
 */
template <typename T, typename... Types>
struct make_view_tuple<T, Types...> 
{
    using type = tuple_cat_t<std::tuple<T&>, typename make_view_tuple<Types...>::type>;
};

/**
 * Turns the passed parameter pack of types into a tuple type with the same types
 * but as references
 */
template <typename entity_id, typename... Types>
using view_tuple = tuple_cat_t<std::tuple<entity_id>, typename make_view_tuple<Types...>::type>;

} // namespace aecs::internal

// Public interface
namespace aecs
{

/**
 * Monolothic ECS registry, handles creating and destroying entities
 * as well as manipulating them with components. The template parameter
 * specifies the underlying type for an entity id. Half of the bits will
 * be used as an index and the other half will be used as versioning.
 *
 * Ex. if you have aecs::registry<std::uint32_t> then the index is 16 bits
 * and the version is 16 bits. This means you can have a maximum of 2^16 entities.
 */
template <typename entity_id = internal::DEFAULT_ENTITY_ID_TYPE>
class registry
{
private:
    internal::component_manager<entity_id> component_manager;
    std::vector<internal::entity<entity_id>> entities;
    std::queue<entity_id> deleted_ids;
    entity_id current_index = 0;
    
    /**
     * Iterator for a registry that takes in types as a template parameter and will
     * return all components for each entity that possesses all parameter types
     * as components. Should only be created via factory function.
     */
    template <typename... Types>
    class view
    {
    public:
        view(registry& reg)
            : reg(reg)
        {
            (add_type_to_mask<Types>(),...);
            find_next_valid();
        }

        using tuple = internal::view_tuple<entity_id, Types...>;
        using iterator_category = std::forward_iterator_tag;
        using value_type = tuple;

        value_type operator*() const 
        {
            value_type tuple = { reg.entities[current_index].id, reg.get<Types>(reg.entities[current_index].id)... };
            return tuple;
        };

        value_type operator->() const 
        { 
            value_type tuple = { reg.entities[current_index].id, reg.get<Types>(reg.entities[current_index].id)... };
            return tuple;
        }

        view<Types...>& operator++()
        {
            find_next_valid();
            return *this;
        }

        view<Types...>& operator+=(int i)
        {
            for (int j = 0; j < i; j++) {
                find_next_valid();
            }
            return *this;
        }

        friend bool operator==(const view& a, const view& b) 
        { 
            return a.current_index == b.current_index; 
        }

        friend bool operator!=(const view& a, const view& b) 
        { 
            return a.current_index != b.current_index; 
        }

        view<Types...> begin() 
        { 
            return view<Types...>(reg);
        }

        view<Types...> end()
        {
            auto v = view<Types...>(reg);
            v.current_index = reg.current_index;
            return v;
        }

    private:
        registry<entity_id>& reg;
        std::size_t current_index = -1;
        std::size_t first_valid;
        std::uint64_t mask = 0;
        
        template <typename T>
        void add_type_to_mask()
        {
            const auto component_id = internal::get_component_id<T>();
            mask |= (1 << component_id);
        }

        void find_next_valid()
        {
            current_index++;
            while (current_index < reg.current_index) {
                if ((reg.entities[current_index].mask & mask) == mask) {
                    return;
                }
                current_index++;
            }
        }
    };

public:
    using entity_id_type = entity_id;

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
        entities[index] = { .id = internal::create_invalid_entity<entity_id>() };
        deleted_ids.push(entity);
        component_manager.entity_destroyed(entity);
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
    T& assign(entity_id entity, const T&& component)
    {
        AECS_ASSERT(!has<T>(entity));
        const auto component_id = internal::get_component_id<T>();
        auto& internal_entity = entities[internal::get_entity_index(entity)];
        internal_entity.mask |= (1 << component_id);
        return component_manager.insert(entity, std::move(component));
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
        component_manager.destroy(entity);
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
    
    /**
     * This is a mapping function that accepts a lambda or function and component types.
     * It functions as creating a view with the passed in types and running the passed function
     * on that view on each match. The function arguments should be entity_id and then following that
     * references to each passed component type.
     *
     * This function could do with some better type-checking as when an invalid function is passed
     * it just spits out a bunch of template garbage.
     */
    template <typename... Types, typename Func>
    void for_each(Func func)
    {
        auto this_view = new_view<Types...>(); 
        for (const auto& tuple : this_view) {
            std::apply(func, tuple);
        }
    }

    /*
     * Factory function for a view on this registry.
     */
    template <typename... Types>
    view<Types...> new_view()
    {
        return view<Types...>(*this);
    }
};

} // namespace aecs

#endif
