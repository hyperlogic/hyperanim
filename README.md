hyperanim
----------
Animation system using an animation graph.
It's composed of 3 stages:
* edit - add nodes, animations and state machines to the graph. Saved as a json file
* cook - (optional) load a graph and dump it as an optimized binary file
* play - C runtime to load graph JSON or binary and animate character graph.

Conventions
----------
Create - allocate struct and initialize it (can fail)
Init - initialize already existing structure (can fail)
Denit - free any memory/resources used by a struct, but NOT the struct itself.
Destroy - frees the struct and any memory/resource used by it.





