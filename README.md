hypermotion
----------
Animation system using an animation graph.
It's composed of 3 stages:
* edit - add nodes, animations and state machines to the graph. Saved as a json file
* cook - (optional) load a graph and dump it as an optimized binary file
* play - C runtime to load graph JSON or binary and animate character graph.

