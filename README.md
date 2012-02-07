
Get mono heap dumps as pretty graphs
------------------------------------

External Dependencies:
======================

You need the graphviz package, available [here](http://graphviz.org/Download.php).

To build:
=========

  `$ make MONO_SOURCE=/path/to/your/mono/source/checkout`


To test:
========

After you've done a build, you can test it by just running:

  `$ make test`

this will generate `heap-graph.dot` and `heap-graph.svg` files in the
current directory.  open the svg file in your favorite web browser to
take a look at the mono heap.