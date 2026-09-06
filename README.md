hyperanim
----------
Animation system using an animation graph.
It's composed of 3 stages:
* edit - add nodes, animations and state machines to the graph. Saved as a json file
* cook - load a graph and dump it as an optimized binary file
* play - C runtime to load graph JSON or binary and animate character graph.

Conventions
----------
Create - allocate struct and initialize it (can fail)
Init - initialize already existing structure (can fail)
Deinit - free any memory/resources used by a struct, but NOT the struct itself.
Destroy - frees the struct and any memory/resource used by it.

// TODO change to this convention.
Result FooAlloc(Foo **f);      // allocate only
Result FooInit(Foo *f, ...);   // fills in a caller-owned Foo
Result FooDeInit(Foo *f);      // releases what init acquired; f reusable
Result FooFree(Foo *f);        // free() only, no deinit
Result FooNew(Foo **f, ...);   // alloc + init
Result FooDelete(Foo *f);      // deinit + free




