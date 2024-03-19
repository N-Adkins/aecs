# AECS
A zero-dependency, header-only C++17 entity component system (ECS) library.

# Usage
The goal of this library is to be simple and concise, and as such the functionality is relatively self-explanatory.

The entire public interface is the ```aecs::registry``` class. Through this registry you can assign and unassign components, check if entities have certain components, and iterate over entities with certain components.

A basic usage of this library might look like the following:

```cpp
#include <aecs/aecs.hpp>
#include <iostream>

struct Foo
{
    int foo;
};

struct Bar
{
    int bar;
};

int main()
{
    // You can specify the underlying type for entity ids with the template argument on aecs::registry,
    // ex. aecs::registry<std::uint64_t> registry;
    aecs::registry registry;

    const auto entity_1 = registry.new_entity();
    const auto entity_2 = registry.new_entity();

    registry.assign<Foo>(entity_1, { .foo = 10 });
    registry.assign<Bar>(entity_1, { .bar = 20 });
    registry.assign<Foo>(entity_2, { .foo = 30 });

    // This block prints "10 30"
    auto foo_view = registry.create_view<Foo>();
    for (const auto& [entity, foo] : foo_view) {
        std::cout << foo.foo << " ";
    }
    std::cout << "\n";

    // This is basically the same as what was done with the view but with lambda syntax
    // Both this and view can check for entities with multiple different component types
    // This block prints "10 20"
    registry.for_each<Foo, Bar>([](auto entity, auto& foo, auto& bar) {
        std::cout << foo.foo << " " << bar.bar << " ";
    });
    std::cout << "\n";

    std::cout << registry.has<Foo>(entity_1); // true
    registry.unassign<Foo>(entity_1);
    std::cout << registry.has<Foo>(entity_1); // false

    return 0;
}
```

# Preconditions
1. Components are assumed to be POD (plain old data) without a constructor or destructor.
2. Functions will not be called with types that are not valid component types for that registry, as it will eat up a bit in every entity's bitmask (I intend to do something about this in time).
3. The registry will not be directly mutated during iteration with a view or for_each.
4. Functions will not be called using an entity that has been invalidated.
