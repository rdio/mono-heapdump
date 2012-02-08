# Get mono heap dumps as pretty graphs

## External Dependencies:

You need the graphviz package, available [here](http://graphviz.org/Download.php).

## To build:

  `$ make MONO_SOURCE=/path/to/your/mono/source/checkout`


## To test:

After you've done a build, you can test it by just running:

  `$ make test`

this will generate `heap-graph.dot` and `heap-graph.svg` files in the
current directory.  open the svg file in your favorite web browser to
take a look at the mono heap.  An example svg graph can be found
[here](http://rdio.github.com/mono-heapdump/example-heap-graph.svg).

## To use it:

At the point in your programs execution where you want to capture a heap
graph, just call `MonoGC.Poker.GenerateHeapDump()`.  You'll want to add
MonoGCPoker-*platform*.dll as a reference, and make sure you have libpoker.dylib
(or .so) in your library load path -- on OSX this means setting DYLD_FALLBACK_LIBRARY_PATH
to the path of the directory containing libpoker.dylib.