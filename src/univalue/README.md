# BFXN UniValue

## Summary

A universal value class, with JSON encoding and decoding.
UniValue is an abstract data type that may be a null, boolean, string,
number, array container, or a key/value dictionary container, nested to
an arbitrary depth.
This class is aligned with the JSON standard, [RFC
8259](https://tools.ietf.org/html/rfc8259).

UniValue was originally created by [Jeff Garzik](https://github.com/jgarzik/univalue/)
and is used in node software for many bitcoin-based cryptocurrencies.
**BFXN UniValue** is a fork of UniValue designed and maintained for use in [BitFinite Node (BFXN)](https://bitfinitenode.org/).
Unlike the [Bitcoin Core fork](https://github.com/bitcoin-core/univalue/),
BFXN UniValue contains large changes that improve *code quality* and *performance*.
The BFXN UniValue API deviates from the original UniValue API where necessary.

Development of BFXN UniValue is fully integrated with development of BitFinite Node.
The BFXN UniValue library and call sites can be changed simultaneously, allowing rapid iterations.

## License

Like BFXN, BFXN UniValue is released under the terms of the MIT license. See
[COPYING](COPYING) for more information or see
<https://opensource.org/licenses/MIT>.

## Build instructions

### BitFinite Node build

BFXN UniValue is fully integrated in the BitFinite Node build system.
The library is built automatically while building the node.

Command to build and run tests in the BFXN build system:

```
ninja check-univalue
```

### Stand-alone build

UniValue is a standard GNU
[autotools](https://www.gnu.org/software/automake/manual/html_node/Autotools-Introduction.html)
project. Build and install instructions are available in the `INSTALL`
file provided with GNU autotools.

Commands to build the library stand-alone:

```
./autogen.sh
./configure
make
```

BFXN UniValue requires C++17 or later.
