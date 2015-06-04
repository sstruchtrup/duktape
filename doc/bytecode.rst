=======================
Duktape bytecode format
=======================

Overview
========

Duktape has API functions to dump a compiled function into bytecode and load
a function from bytecode.  Bytecode dump/load allows code to be compiled
offline, compiled code to be cached and reused, compiled code to be moved
from one Duktape heap to another, etc.

There are a few limitations on what kind of functions can be dumped to
bytecode, and what information is lost in the process.  See separate section
on limitations below.

Duktape bytecode is **version specific** and may change arbitrarily even in
minor releases (but is guaranteed not to change in a patch release).  In other
words, the bytecode format is not part of the ordinary versioning guarantees.
If you compile code into bytecode offline, you must ensure such code is
recompiled whenever Duktape source is updated.  In this sense Duktape
bytecode differs fundamentally from e.g. Java bytecode which is used as a
version neutral distribution format.

Duktape bytecode is **unvalidated** which means that loading untrusted
bytecode may cause a crash or other memory unsafe behavior, which may lead
to exploitable vulnerabilities.  Calling code is responsible for ensuring
that bytecode for a different Duktape version is not loaded, and that the
bytecode input is not truncated or corrupted.  (Validating bytecode is quite
difficult, because one would also need to validate the actual bytecode which
might otherwise refer to non-existent registers or constants, jump out of
bounds, etc.)

The bytecode format is **platform neutral**, so that it's possible to compile
the bytecode on one platform and load it on another, even if the platforms
have different byte order.  This is useful to support offline compilation in
cross compilation.

**FIXME: decide whether bytecode could be config option specific, e.g. if
pc2line is disabled, is it dropped from bytecode format too?**

See the following API test case for concrete examples on usage and
current limitations:

* ``api-testcases/test-dump-load-basic.c``

When to use bytecode dump/load
==============================

There are two main motivations for using bytecode dump/load:

* Performance

* Obfuscation

Performance
-----------

Whenever compilation performance is *not* an issue, it is nearly always
preferable to compile functions from source rather than using bytecode
dump/load.  Compiling from source is memory safe, version compatible,
and has no semantic limitations like bytecode.

There are some applications where compilation is a performance issue.
For example, a certain function may be compiled and executed over and
over again in short lived Duktape global contexts or even separate
Duktape heaps (which prevents reusing a single function object).  Caching
the compiled function bytecode and instantiating the function by loading
the bytecode is much faster.

Obfuscation
-----------

Obfuscation is another common reason to use bytecode: it's more difficult
to reverse engineer source code from bytecode than e.g. minified code.
However, when doing so, you should note the following:

* Some minifiers support obfuscation which may be good enough and avoids
  the bytecode limitations and downsides.

* For some targets source code encryption may be a better option than
  relying on bytecode for obfuscation.

* Although Duktape bytecode doesn't currently store source code, it does
  store all variable names (``_Varmap``) and formal argument names
  (``_Formals``) which are needed in some functions.  It may also be
  possible source code is included in bytecode at some point to support
  debugging.  In other words, obfuscation is not a design goal for the
  bytecode format.

When not to use bytecode dump/load
==================================

Duktape bytecode is **not** a good match for:

* Distributing code

* Minimizing code size

Distributing code
-----------------

It's awkward to use a version specific bytecode format for code distribution.
This is especially true for Ecmascript, because the language itself is
otherwise well suited for writing backwards compatible code, detecting
features at run-time, etc.

It's also awkward for code distribution that the bytecode load operation
relies on calling code to ensure the loaded bytecode is trustworthy and
uncorrupted.  In practice this means e.g. cryptographic signatures are
needed to avoid tampering.

Minimizing code size
--------------------

The bytecode format is designed to be fast to dump and load, while still
being platform neutral.  It is *not* designed to be compact (and indeed
is not).

For example, for a simple Mandelbrot function (``mandel()`` in
``dist-files/mandel.js``):

+---------------------------+----------------+----------------------+
| Format                    | Size (bytes)   | Gzipped size (bytes) |
+===========================+================+======================+
| Original source           | 886            | 374                  |
+---------------------------+----------------+----------------------+
| Bytecode dump             | 823            | 528                  |
+---------------------------+----------------+----------------------+
| UglifyJS2-minified source | 364            | 270                  |
+---------------------------+----------------+----------------------+

For minimizing code size, using a minifier and ordinary compression is
a much better idea than relying on compressed or uncompressed bytecode.

Miscellaneous notes
===================

Eval and program code
---------------------

Ecmascript specification recognizes three different types of code: program
code, eval code, and function code, with slightly different scope and variable
binding semantics.  The serialization mechanism supports all three types.

Bytecode limitations
====================

Function lexical environment is lost
------------------------------------

A function loaded from bytecode always works as if it was defined in the
global environment so that any variable lookups not bound in the function
itself will be resolved through the global object.

If the original function was established using a function declaration,
the declaration itself is not restored when a function is loaded.  This may
be confusing.

No function name binding for function declarations
--------------------------------------------------

Function name binding for function expressions is supported, e.g. the
following function would work::

    // Can dump and load this function, the reference to 'count' will
    // be resolved using the automatic function name lexical binding
    // provided for function expressions.

    var func = function count(n) { print(n); if (n > 0) { count(n - 1); } };

However, for technical reasons functions that are established as global
declarations work a bit differently::

    // Can dump and load this function, but the reference to 'count'
    // will lookup globalObject.count instead of automatically
    // referencing the function itself.

    function count(n) { print(n); if (n > 0) { count(n - 1); } };

(The NAMEBINDING flag controls creation of a lexical environment which
contains the function expression name binding.  In Duktape 1.2 the flag
is only set for function templates, not function instances; this was
changed for Duktape 1.3 so that the NAMEBINDING flag could be detected
when loading bytecode, and a lexical environment can then be created
based on the flag.)

Custom internal prototype is lost
---------------------------------

A custom internal prototype is lost, and ``Function.prototype`` is used
on bytecode load.

Custom external prototype is lost
---------------------------------

A custom external prototype (``.prototype`` property) is lost, and a
default empty prototype is created on bytecode load.

Only specific function object properties are kept
-------------------------------------------------

Only specific function object properties, i.e. those needed to correctly
revive a function, are kept.  These properties have type and value
limitations:

* .length: uint32, non-number values replaced by 0

* .name: string required, non-string values replaced by empty string

* .fileName: string required, non-string values replaced by empty string

* ._Formals: internal property, value is an array of strings

* ._Varmap: internal property, value is an object mapping identifier
  names to register numbers

Bound functions are not supported
---------------------------------

Currently a ``TypeError`` is thrown when trying to serialize a bound function
object.

**FIXME: probably better to follow the bound chain and serialize the final
target function instead, i.e. bound status would be lost during serialization.
This is more in line with serializing with loss of some metadata rather than
throwing.**

CommonJS modules don't work well with bytecode dump/load
--------------------------------------------------------

CommonJS modules cannot be trivially serialized because they're normally
evaluated by embedding the module source code inside a temporary function
wrapper (see ``modules.rst`` for details).

* If you compile and serialize the module source, the module will
  have incorrect scope semantics.

* You could add the function wrapper and compile the wrapped function
  instead.

* Module support for bytecode dump/load will probably need future work.

Bytecode format
===============

A function is serialized into a platform neutral byte stream.  Multibyte
values are in network order (big endian), and don't have any alignment
guarantees.

The exact format is ultimately defined by the source code.  When in doubt,
see:

* ``src/duk_api_bytecode.c``

* ``util/dump_bytecode.py``

Top level format
----------------

The basic bytecode format is:

* Marker byte: 0xff

* Bytecode version byte: 0x00 (for this version)

* Serialized function (may contain inner functions)

Function
--------

A function (or a function template) is serialized as:

* count_inst (uint32): number of bytecode instructions.

* count_const (uint32): number of constants.

* count_funcs (uint32): number of inner functions.

* nregs (uint16): number of arguments (``duk_hcompiledfunction`` ``nregs`` field)

* nargs (uint16): number of arguments (``duk_hcompiledfunction`` ``nargs`` field)

* start_line (uint32): function line number minimum (debugging; 0 if not known)

* end_line (uint32): function line number maximum (debugging; 0 if not known)

* duk_hobject flags (uint32): flags for duk_hobject, very version specific,
  covers e.g. 'strict' and 'create args' flag

* Bytecode as ``count_inst`` unsigned 32-bit integers.

* Constants; ``count_const`` entries with one of the following formats:

  - String:

    + String marker: 0x00

    + String length (uint32): string length in bytes of extended UTF-8 data.

    + String data, extended UTF-8 data used directly, may include NUL bytes.

  - Number:

    + Number marker: 0x01

    + Number constant (uint64): number in IEEE double format

* Inner functions, with each function in the same function format as the top
  level function.  Inner functions may contain further inner functions etc.

* Function .length: uint32

  - Technically .length could be an arbitrary value, but we assume it is a
    32-bit unsigned integer (non-number values are serialized as zero):

* Function .name: uint32 string length followed by string data

* Function .fileName: uint32 string length followed by string data

* Function .pc2line: uint32 buffer length followed by buffer data

  - **FIXME: if pc2line disabled, leave out or zero length?**

* Function _Varmap:

  - Encoded as a series of string/uint32 pairs.  Strings are encoded
    as 32-bit length followed by data.  An empty string terminates the
    list.  This format takes advantage of the fact that there can be
    no valid variables with an empty string name.

* Function _Formals:

  - Encoded as a series of strings.  Strings are encoded as 32-bit
    length followed by data.  An empty string terminates the formals
    list.

**FIXME: important function properties** (duk_js_push_closure):

* _Source: string?

The following are intentionally not serialized:

* Function .prototype: value can be an arbitrary object (belonging to
  an arbitrary object graph) so serializing would be very complicated.
  Instead, default ``.prototype`` is created on load.

Function properties added for function instances are set by the internal
function ``duk_js_push_closure()``.

NOTE: The top level function is a function instance, but all inner functions
are function templates.  There are some difference between the two which must
be taken into account in bytecode serialization code.

Security and memory safety
==========================

Duktape bytecode must only be loaded from a trusted source: loading broken
or maliciously crafted bytecode may lead to memory unsafe behavior.

Because bytecode is version specific, it is generally unsafe to load bytecode
provided by a network peer -- unless you can somehow be certain the bytecode
is specifically compiled for your Duktape version.

Design notes
============

Version specific vs. version neutral
------------------------------------

Duktape bytecode instruction format is already version specific and can change
between even minor releases, so it's quite natural for the serialization
format to also be version specific.

Providing a version neutral format would be possible when Duktape bytecode no
longer changes in minor versions (not easy to see when this would be the case)
or by doing some kind of recompilation for bytecode.

Endianness
----------

Network endian was chosen because it's also used elsewhere in Duktape (e.g.
the debugger protocol) as the default, portable endianness.

Faster bytecode dump/load could be achieved by using native endianness and
(if necessary) padding to achieve proper alignment.  This additional speed
improvement was considered less important than portability.

Platform neutrality
-------------------

Supporting cross compilation is a useful feature so that bytecode generated on
one platform can be loaded on another, as long as they run the same Duktape
version.

The cost of being platform neutral is rather small.  The essential features
are normalizing endianness and avoiding alignment assumptions.  Both can be
quite easily accommodated with relatively little run-time cost.

Bytecode header
---------------

The initial 0xFF byte is used because it can never appear in valid UTF-8
(even extended UTF-8) so that using a random string accidentally as bytecode
input will fail.

Memory safety and bytecode validation
-------------------------------------

The bytecode load primitive is memory unsafe, to the extent that trying to
load corrupted (truncated and/or modified) bytecode may lead to memory unsafe
behavior.  To keep bytecode loading fast and simple, there are even no bounds
checks when parsing the input bytecode.

This might seem strange but is intentional: while it would be easy to do basic
syntax validation for the serialized data when it is loaded, it still wouldn't
guarantee memory safety.  To do so one would also need to validate the bytecode
opcodes, otherwise memory unsafe behavior may happen at run time.

Consider the following example: a function being loaded has ``nregs`` 100, so
that 100 slots are allocated from the value stack for the function.  If the
function bytecode then executed::

    LDREG 1, 999   ; read reg 999, out of bounds
    STREG 1, 999   ; write reg 999, out of bounds

Similar issues exist for constants; if the function has 100 constants::

    LDCONST 1, 999 ; read constant 999, out of bounds

In addition to direct out-of-bounds references there are also "indirect"
opcodes which e.g. load a register index from another register.  Validating
these would be a lot more difficult and would need some basic control flow
algorithm, etc.

Overall it would be quite difficult to implement bytecode validation that
would correctly catch broken and perhaps maliciously crafted bytecode, and
it's not very useful to have a partial solution in place.

Even so there is a very simple header signature for bytecode which ensures
that obviously incorrect values are rejected early.  The signature ensures
that (1) no ordinary string data can accidentally be loaded as byte code
(the initial byte 0xFF is invalid extended UTF-8); and (2) there is a basic
bytecode version check.  Any bytes beyond this signature is unvalidated.

Future work
===========

FIXME.
