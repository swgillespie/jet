# Jet - a small Lisp written in C++.

[![Build Status](https://travis-ci.org/swgillespie/jet.svg?branch=master)](https://travis-ci.org/swgillespie/jet)

Jet is a tiny lisp that doesn't offer a whole lot yet. It's quite speedy
at doing the small amount of things that it can do, though.

The most interesting part of Jet so far is the garbage collector. Jet has
a semispace copying collector that uses Cheney's algorithm to copy
all pointers between the semispaces during a garbage collection.

There are a number of things on the roadmap, first and foremost of which
are macros.

Many thanks to fitzgen (Nick Fitzgerald), whose well-documented
Scheme implementation "oxischeme" (https://github.com/fitzgen/oxischeme) has served as a continuous source
of ideas and inspiration for me.
