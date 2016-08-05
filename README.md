
[![Unix and OSX Build Status](https://travis-ci.org/root-mirror/cling.svg?branch=master)](https://travis-ci.org/root-mirror/cling)

Cling - The Interactive C++ Interpreter
=========================================


Overview
--------
Cling is an interactive C++ interpreter, built on top of Clang and LLVM compiler
infrastructure. Cling realizes the [read-eval-print loop
(REPL)](http://en.wikipedia.org/wiki/Read%E2%80%93eval%E2%80%93print_loop)
concept, in order to leverage rapid application development. Implemented as a
small extension to LLVM and Clang, the interpreter reuses their strengths such
as the praised concise and expressive compiler diagnostics.

See also [cling's web page.](https://cdn.rawgit.com/root-mirror/cling/master/www/index.html)

Please note that some of the resources are rather old and most of the stated
limitations are outdated.
  * [talks](www/docs/talks)
  * http://blog.coldflake.com/posts/2012-08-09-On-the-fly-C++.html
  * http://solarianprogrammer.com/2012/08/14/cling-cpp-11-interpreter/
  * https://www.youtube.com/watch?v=f9Xfh8pv3Fs
  * https://www.youtube.com/watch?v=BrjV1ZgYbbA
  * https://www.youtube.com/watch?v=wZZdDhf2wDw
  * https://www.youtube.com/watch?v=eoIuqLNvzFs


Installation
------------
### Release Notes
See our [release notes](docs/ReleaseNotes.md) to find what's new.


### Binaries
Our nightly binary snapshots can be found
[here](for download at https://root.cern.ch/download/cling/)


### Building from Source with Cling Packaging Tool
Cling's tree has a user-friendly, command-line utility written in Python called
Cling Packaging Tool (CPT) which can build Cling from source and generate
installer bundles for a wide range of platforms. CPT requires Python 2.7 or
later.

If you have Cling's source cloned locally, you can find the tool in
`tools/packaging` directory. Alternatively, you can download the script
manually, or by using `wget`:

```sh
wget https://raw.githubusercontent.com/root-mirror/cling/master/tools/packaging/cpt.py
chmod +x cpt.py
./cpt.py --check-requirements && ./cpt.py --create-dev-env Debug --with-workdir=./cling-build/
```

Usage
-----
Full documentation of CPT can be found in [tools/packaging](tools/packaging).


```c++
/some/install/dir/bin/cling '#include <stdio.h>' 'printf("Hello World!\n")'`
```

To get started run:
```bash
/some/install/dir/bin/cling --help`
```
or type
```
/some/install/dir/bin/cling
[cling]$ .help`
```


Jupyter
-------
Cling comes with a [Jupyter](http://jupyter.org) kernel. After building cling,
install Jupyter and run `jupyter kernelspec install cling`. It requires a fairly
new Jupyter. Make sure cling is in your PATH when you start jupyter!

See also the [tools/Jupyter](tools/Jupyter) subdirectory for more information.


Developers' Corner
==================
[Cling's latest doxygen documentation](http://cling.web.cern.ch/cling/doxygen/)


Contributions
-------------
Every contribution is very welcome. It is considered as a donation and its
copyright and any other related rights become exclusive ownership of the person
who merged the code or in any other case the main developers.

In order for a contribution to be accepted it has to obey the previously
established rules for contribution acceptance in cling's work flow and rules.
