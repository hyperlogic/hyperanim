hyperanim
----------
Animation system using an animation graph.
It's composed of 3 stages:
* edit - add nodes, animations and state machines to the graph. Saved as a json file
* cook - load a graph and dump it as an optimized binary file
* play - C runtime to load graph JSON or binary and animate character graph.

Conventions
----------
Result FooAlloc(Foo **f);      // allocate only
Result FooFree(Foo *f);        // deallocate only
Result FooInit(Foo *f, ...);   // fills in a caller-owned Foo
Result FooDeInit(Foo *f);      // clear Foo so it can be re-initialized. (no free)
Result FooNew(Foo **f, ...);   // alloc + init
Result FooDelete(Foo *f);      // deinit + free




